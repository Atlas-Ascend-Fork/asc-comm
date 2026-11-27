/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include "asccomm_runtime.h"
#include "asccomm_launch.h"
#include "asccomm_diag_internal.h"
#include "../stream/sqe_build_a5.h"
#include "../stream/sqe.h"
#include "securec.h"
#include "ascend_hal.h"
#include "asccomm_log.h"

// 局部打开宏以包含 internal 声明，与下方导出定义做编译期交叉校验；符号契约不变。
#define ASCCOMM_ENABLE_INTERNAL_APIS
#include "asccomm_primitives_internal.h"

namespace aicpu {
void __attribute__((weak)) __attribute__((visibility("default"))) GetSqeId(
    uint32_t batchNum, uint32_t& taskId, uint32_t& taskIdEnd);
}

namespace {
using HcommRtsqContext = decltype(((HcommThreadSqContext*)nullptr)->contextInfo.rtsqContext);

// [Head] 解码行位序锚定（小端），编译断言防布局漂移。
static_assert(offsetof(Asc::Rt91095StarsSqeHeader, rtStreamId) == 4U, "word1 low16 must be rtStreamId");
static_assert(offsetof(Asc::Rt91095StarsSqeHeader, taskId) == 6U, "word1 high16 must be taskId");

asccomm_result acquire_task_id(HcommThreadSqContext& contextOuter, uint32_t& taskId)
{
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    CHK_PRT_RET(
        contextOuter.type != HCOMM_THREAD_SQ_CONTEXT_TYPE_RTSQ || rtsq.localSqAddr == 0,
        ASCCOMM_ERROR("[%s] invalid local SQE buffer.", __func__), ASCCOMM_E_PTR);
    CHK_PRT_RET(
        rtsq.pendingSqeCnt >= HCOMM_RESOURCE_RTSQ_BATCH_SIZE,
        ASCCOMM_ERROR("[%s] pending SQE count[%u] exceeds buffer capacity.", __func__, rtsq.pendingSqeCnt),
        ASCCOMM_E_INTERNAL);
    if (rtsq.taskId >= rtsq.taskIdEnd) {
        CHK_PRT_RET(
            aicpu::GetSqeId == nullptr, ASCCOMM_ERROR("[%s] GetSqeId is unavailable.", __func__), ASCCOMM_E_INTERNAL);
        aicpu::GetSqeId(HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE, rtsq.taskId, rtsq.taskIdEnd);
        CHK_PRT_RET(
            rtsq.taskId >= rtsq.taskIdEnd || rtsq.taskIdEnd - rtsq.taskId > HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE,
            ASCCOMM_ERROR("[%s] invalid Task ID range[%u, %u).", __func__, rtsq.taskId, rtsq.taskIdEnd),
            ASCCOMM_E_INTERNAL);
    }
    taskId = rtsq.taskId;
    ++rtsq.taskId;
    return ASCCOMM_SUCCESS;
}

asccomm_result query_sq_value(
    uint32_t localDevId, const HcommRtsqContext& context, drvSqCqPropType_t property, uint32_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = property;
    drvError_t ret = halSqCqQuery(localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], property[%u], ret[%d].", __func__, localDevId,
            context.sqId, property, ret),
        ASCCOMM_E_INTERNAL);
    value = queryInfo.value[0];
    return ASCCOMM_SUCCESS;
}

asccomm_result query_sq_base(uint32_t localDevId, const HcommRtsqContext& context, uint64_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = DRV_SQCQ_PROP_SQ_BASE;
    drvError_t ret = halSqCqQuery(localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], ret[%d].", __func__, localDevId, context.sqId, ret),
        ASCCOMM_E_INTERNAL);
    value = (static_cast<uint64_t>(queryInfo.value[1]) << 32U) | queryInfo.value[0];
    return ASCCOMM_SUCCESS;
}

asccomm_result config_sq_tail(uint32_t localDevId, const HcommRtsqContext& context, uint32_t value)
{
    halSqCqConfigInfo configInfo{};
    configInfo.tsId = 0;
    configInfo.sqId = context.sqId;
    configInfo.cqId = 0;
    configInfo.type = DRV_NORMAL_TYPE;
    configInfo.prop = DRV_SQCQ_PROP_SQ_TAIL;
    configInfo.value[0] = value;
    drvError_t ret = halSqCqConfig(localDevId, &configInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqConfig failed, localDevId[%u], sqId[%u], tail[%u], ret[%d].", __func__, localDevId,
            context.sqId, value, ret),
        ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

uint32_t get_tail_to_head_distance(const HcommRtsqContext& context)
{
    if (context.sqHead == context.sqTail) {
        return context.sqDepth;
    }
    return context.sqTail < context.sqHead ? context.sqHead - context.sqTail :
                                             context.sqDepth - (context.sqTail - context.sqHead);
}

asccomm_result ensure_queue_space(uint32_t localDevId, HcommRtsqContext& context)
{
    auto startTime = std::chrono::steady_clock::now();
    const uint32_t sqFullTimeout = asccomm_get_sq_full_timeout();
    uint32_t availableSpace = get_tail_to_head_distance(context);
    while (availableSpace <= context.pendingSqeCnt) {
        CHK_RET(query_sq_value(localDevId, context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
        availableSpace = get_tail_to_head_distance(context);
        if (availableSpace > context.pendingSqeCnt) {
            break;
        }
        const bool timeout =
            sqFullTimeout != 0 && std::chrono::steady_clock::now() - startTime >= std::chrono::seconds(sqFullTimeout);
        CHK_PRT_RET(
            timeout,
            ASCCOMM_ERROR(
                "[%s] RTSQ full timeout, sqId[%u], head[%u], tail[%u], pending[%u].", __func__, context.sqId,
                context.sqHead, context.sqTail, context.pendingSqeCnt),
            ASCCOMM_E_TIMEOUT);
        CHK_RET(asccomm_try_launch_registered_threads());
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result copy_pending_sqes(HcommRtsqContext& context)
{
    CHK_PRT_RET(
        context.localSqAddr == 0, ASCCOMM_ERROR("[%s] local SQE buffer address is 0.", __func__), ASCCOMM_E_PTR);
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(context.localSqAddr));
    const uint32_t byteCount = context.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
    uint8_t* sqCurrent = reinterpret_cast<uint8_t*>(context.sqBaseAddr) + context.sqTail * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
    if (context.sqTail >= context.sqHead) {
        const uint32_t depthLeft = context.sqDepth - context.sqTail;
        const uint32_t firstCount = context.pendingSqeCnt < depthLeft ? context.pendingSqeCnt : depthLeft;
        const uint32_t firstBytes = firstCount * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
        CHK_PRT_RET(
            memcpy_sp(sqCurrent, firstBytes, localSqeBuffer, firstBytes) != EOK,
            ASCCOMM_ERROR("[%s] first RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
        if (firstCount != context.pendingSqeCnt) {
            const uint32_t remainingBytes = byteCount - firstBytes;
            CHK_PRT_RET(
                memcpy_sp(
                    reinterpret_cast<void*>(context.sqBaseAddr), remainingBytes, localSqeBuffer + firstBytes,
                    remainingBytes) != EOK,
                ASCCOMM_ERROR("[%s] wrapped RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
        }
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        memcpy_sp(sqCurrent, byteCount, localSqeBuffer, byteCount) != EOK,
        ASCCOMM_ERROR("[%s] RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

asccomm_result refresh_rtsq(ThreadEntity& thread)
{
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    ++rtsq.pendingSqeCnt;
    if (asccomm_is_automatic_launch_mode() || rtsq.pendingSqeCnt == HCOMM_RESOURCE_RTSQ_BATCH_SIZE) {
        return asccomm_rtsq_launch(thread);
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result launch_thread_sqes(ThreadEntity& thread, const char* caller)
{
    HcommRtsqContext& rtsq = thread.sqContextAddr->contextInfo.rtsqContext;
    uint32_t localDevId = 0;
    CHK_PRT_RET(
        drvGetLocalDevIDByHostDevID(thread.devInfo.devPhyId, &localDevId) != 0,
        ASCCOMM_ERROR("[%s] resolve local device failed.", caller), ASCCOMM_E_INTERNAL);
    CHK_RET(ensure_queue_space(localDevId, rtsq));
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_RET(copy_pending_sqes(rtsq));
    const uint32_t newTail = (rtsq.sqTail + rtsq.pendingSqeCnt) % rtsq.sqDepth;
    CHK_RET(config_sq_tail(localDevId, rtsq, newTail));
    rtsq.sqTail = newTail;
    rtsq.pendingSqeCnt = 0;
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(rtsq.localSqAddr));
    constexpr size_t LOCAL_SQE_BUFFER_SIZE = HCOMM_RESOURCE_RTSQ_SQE_SIZE * HCOMM_RESOURCE_RTSQ_BATCH_SIZE;
    CHK_PRT_RET(
        memset_s(localSqeBuffer, LOCAL_SQE_BUFFER_SIZE, 0, LOCAL_SQE_BUFFER_SIZE) != EOK,
        ASCCOMM_ERROR("[%s] clear SQE buffer failed.", caller), ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

uint8_t* get_current_sqe(HcommThreadSqContext& contextOuter)
{
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(contextOuter.contextInfo.rtsqContext.localSqAddr)) +
           contextOuter.contextInfo.rtsqContext.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
}

asccomm_result validate_thread_entity(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PRT_RET(
        thread->abiHeader.version != HCOMM_THREAD_ENTITY_VERSION ||
            thread->abiHeader.magicWord != HCOMM_THREAD_ENTITY_MAGIC_WORD ||
            thread->abiHeader.size < sizeof(ThreadEntity),
        ASCCOMM_ERROR("[%s] invalid resource thread entity.", __func__), ASCCOMM_E_PARA);
    CHK_PTR_NULL(thread->sqContextAddr);
    const HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0 || rtsq.sqHead >= rtsq.sqDepth || rtsq.sqTail >= rtsq.sqDepth,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ parameters, base[0x%llx], depth[%u], head[%u], tail[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth, rtsq.sqHead, rtsq.sqTail),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

} // namespace

asccomm_result asccomm_resolve_thread_rtsq(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PTR_NULL(thread->sqContextAddr);
    HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;
    if (rtsq.sqBaseAddr != 0) {
        return ASCCOMM_SUCCESS;
    }

    uint32_t localDevId = 0;
    drvError_t ret = drvGetLocalDevIDByHostDevID(thread->devInfo.devPhyId, &localDevId);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] drvGetLocalDevIDByHostDevID failed, devicePhyId[%u], ret[%d].", __func__, thread->devInfo.devPhyId,
            ret),
        ASCCOMM_E_INTERNAL);
    CHK_RET(query_sq_base(localDevId, rtsq, rtsq.sqBaseAddr));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_DEPTH, rtsq.sqDepth));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, rtsq.sqHead));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_TAIL, rtsq.sqTail));
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ resource, base[0x%llx], depth[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth),
        ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_rtsq_launch(ThreadEntity& thread)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        rtsq.sqDepth == 0 || rtsq.sqBaseAddr == 0,
        ASCCOMM_ERROR("[%s] invalid RTSQ context, depth[%u], base[0x%llx].", __func__, rtsq.sqDepth, rtsq.sqBaseAddr),
        ASCCOMM_E_PARA);
    return launch_thread_sqes(thread, __func__);
}

asccomm_result asccomm_rtsq_try_launch(ThreadEntity& thread)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    uint32_t localDevId = 0;
    CHK_PRT_RET(
        drvGetLocalDevIDByHostDevID(thread.devInfo.devPhyId, &localDevId) != 0,
        ASCCOMM_ERROR("[%s] resolve local device failed.", __func__), ASCCOMM_E_INTERNAL);
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, rtsq.sqHead));
    if (get_tail_to_head_distance(rtsq) <= rtsq.pendingSqeCnt) {
        return ASCCOMM_SUCCESS;
    }
    return launch_thread_sqes(thread, __func__);
}

asccomm_result asccomm_rtsq_notify_wait(ThreadEntity& thread, uint32_t notifyId, uint32_t timeout)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_wait(
        context.contextInfo.rtsqContext.streamId, taskId, notifyId, timeout, get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_notify_record(ThreadEntity& thread, uint32_t notifyId)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_record(
        context.contextInfo.rtsqContext.streamId, taskId, notifyId, get_current_sqe(context));
    return refresh_rtsq(thread);
}

// 地址语义编排原语（COND 型），devkit mc2_server 编排面消费。
// 两条地址统一按 49-bit 收口：load_imm 只编码 bit[48:0]，超限静默截断故入口 fail-loud。
constexpr uint64_t RTSQ_ADDR_NOTIFY_ADDR_LIMIT = 1ULL << 49U; // construct_load_imm 可编码的 bit[48:0] 上界

asccomm_result asccomm_rtsq_addr_notify_wait(ThreadEntity& thread, uint64_t waitAddr, uint64_t valueAddr, uint8_t last)
{
    CHK_PRT_RET(
        waitAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT || valueAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT,
        ASCCOMM_ERROR(
            "[%s] addr out of 49-bit range, waitAddr[0x%llx], valueAddr[0x%llx].", __func__,
            static_cast<unsigned long long>(waitAddr), static_cast<unsigned long long>(valueAddr)),
        ASCCOMM_E_PARA);
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_ccore_notify_wait(
        context.contextInfo.rtsqContext.streamId, taskId, waitAddr, valueAddr, last, get_current_sqe(context));
    CHK_RET(refresh_rtsq(thread));
    return asccomm_rtsq_launch(thread);
}

asccomm_result asccomm_rtsq_addr_notify_record(ThreadEntity& thread, uint64_t recordAddr, uint64_t valueAddr)
{
    CHK_PRT_RET(
        recordAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT || valueAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT,
        ASCCOMM_ERROR(
            "[%s] addr out of 49-bit range, recordAddr[0x%llx], valueAddr[0x%llx].", __func__,
            static_cast<unsigned long long>(recordAddr), static_cast<unsigned long long>(valueAddr)),
        ASCCOMM_E_PARA);
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_ccore_notify_record(
        context.contextInfo.rtsqContext.streamId, taskId, recordAddr, valueAddr, get_current_sqe(context));
    CHK_RET(refresh_rtsq(thread));
    return asccomm_rtsq_launch(thread);
}

asccomm_result asccomm_rtsq_sdma_copy(ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    constexpr uint32_t RTSQ_PART_ID = 0;
    constexpr uint32_t COPY_OPCODE = 0;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_sdma_copy(
        context.contextInfo.rtsqContext.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, COPY_OPCODE,
        get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_sdma_reduce(
    ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t operation = 0;
    switch (reduceIn.reduceOp) {
        case Asc::ReduceOp::SUM:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_ADD);
            break;
        case Asc::ReduceOp::MAX:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MAX);
            break;
        case Asc::ReduceOp::MIN:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MIN);
            break;
        default:
            ASCCOMM_ERROR("[%s] unsupported SDMA reduce operation[%u].", __func__, reduceIn.reduceOp);
            return ASCCOMM_E_NOT_SUPPORT;
    }

    uint32_t dataType = 0;
    switch (reduceIn.dataType) {
        case Asc::DataType::INT8:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT8);
            break;
        case Asc::DataType::INT16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT16);
            break;
        case Asc::DataType::INT32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT32);
            break;
        case Asc::DataType::FP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP16);
            break;
        case Asc::DataType::FP32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP32);
            break;
        case Asc::DataType::BFP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_BFP16);
            break;
        default:
            ASCCOMM_ERROR("[%s] unsupported SDMA data type[%u].", __func__, reduceIn.dataType);
            return ASCCOMM_E_NOT_SUPPORT;
    }

    constexpr uint32_t RTSQ_PART_ID = 0;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_sdma_copy(
        context.contextInfo.rtsqContext.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, operation | dataType,
        get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_ub_db_send(ThreadEntity& thread, const Asc::UbJettyLiteId& jettyId, uint16_t piValue)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_ub_db_send(
        context.contextInfo.rtsqContext.streamId, taskId, jettyId, piValue, get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_rdma_db_send(ThreadEntity& thread, uint64_t dbAddr, uint64_t dbValue)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_rdma_db_send(
        context.contextInfo.rtsqContext.streamId, taskId, dbAddr, dbValue, get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    if (threadNum == 0) {
        return ASCCOMM_SUCCESS;
    }
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_launch(*thread));
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    if (threadNum == 0) {
        return ASCCOMM_SUCCESS;
    }
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_try_launch(*thread));
    }
    return ASCCOMM_SUCCESS;
}

int32_t asccomm_diag_dump_sq(uint64_t threadHandle)
{
    // 探针恒开：只在 server 退避路径被调（正常路径零调用零成本），输出量天然有界无需开关。
    // 例外：[NotifyFlag] 段直读有挂死风险（真机实证），默认禁用，仅 ASCCOMM_DIAG_NOTIFY_FLAG=1 时开。
    ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threadHandle));
    if (thread == nullptr || thread->sqContextAddr == nullptr) {
        ASCCOMM_ERROR("[SQ_DUMP] invalid thread handle[%llu].", static_cast<unsigned long long>(threadHandle));
        return static_cast<int32_t>(ASCCOMM_E_PTR);
    }
    // 入口实体 ABI 校验：错句柄直接读 sqContextAddr 会让探针自身崩溃丢诊断现场。
    if (validate_thread_entity(thread) != ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR(
            "[SQ_DUMP] thread entity ABI mismatch, handle[%llu].", static_cast<unsigned long long>(threadHandle));
        return static_cast<int32_t>(ASCCOMM_E_PARA);
    }
    const HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;

    uint32_t localDevId = 0;
    if (drvGetLocalDevIDByHostDevID(thread->devInfo.devPhyId, &localDevId) != 0) {
        ASCCOMM_ERROR("[SQ_DUMP] drvGetLocalDevIDByHostDevID failed, devPhyId[%u].", thread->devInfo.devPhyId);
        return static_cast<int32_t>(ASCCOMM_E_INTERNAL);
    }

    // query HEAD/TAIL/DEPTH；失败回落镜像值，不中断 dump
    uint32_t hwHead = rtsq.sqHead;
    uint32_t hwTail = rtsq.sqTail;
    uint32_t depth = rtsq.sqDepth;
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, hwHead) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query HEAD failed, fallback to mirror[%u].", hwHead);
    }
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_TAIL, hwTail) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query TAIL failed, fallback to mirror[%u].", hwTail);
    }
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_DEPTH, depth) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query DEPTH failed, fallback to mirror[%u].", depth);
    }
    if (depth == 0 || rtsq.sqBaseAddr == 0) {
        ASCCOMM_ERROR(
            "[SQ_DUMP] invalid RTSQ, sqId[%u], depth[%u], base[0x%llx].", rtsq.sqId, depth,
            static_cast<unsigned long long>(rtsq.sqBaseAddr));
        return static_cast<int32_t>(ASCCOMM_E_PARA);
    }

    // 打印 [hwHead-10, hwHead+2) 共 12 条 SQE × 16 words：健康跑 head 越过整批，向后扩 10
    // 才能拍到已消费的 transport SQE；hwHead 处前 8 word 暂存 headSqe 供 [Head] 解码行复用。
    uint32_t headSqe[8] = {0};
    uint32_t pos = static_cast<uint32_t>((static_cast<uint64_t>(hwHead) + depth - 10U) % depth);
    for (uint32_t i = 0; i < 12U; ++i) {
        const char* marker = "";
        if (pos == hwHead && pos == hwTail) {
            marker = "<-HEAD/TAIL";
        } else if (pos == hwHead) {
            marker = "<-HEAD";
        } else if (pos == hwTail) {
            marker = "<-TAIL";
        }
        const uint64_t sqeBase = rtsq.sqBaseAddr + static_cast<uint64_t>(pos) * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
        for (uint32_t w = 0; w < 16U; ++w) {
            const uint64_t wordAddr = sqeBase + static_cast<uint64_t>(w) * sizeof(uint32_t);
            if (wordAddr == 0) {
                ASCCOMM_INFO("[SQ_DUMP] read fail pos=%u w=%u", pos, w);
                continue;
            }
            const uint32_t val = *reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(wordAddr));
            ASCCOMM_INFO("[SQ_DUMP] sqId[%u] pos[%u] %s w%02u=0x%08x", rtsq.sqId, pos, marker, w, val);
            if (pos == hwHead && w < 8U) {
                headSqe[w] = val;
            }
        }
        pos = (pos + 1U) % depth;
    }

    // [Head] 解码行：word0[5:0]=type、word1 低16/高16=rtStreamId/taskId、word2[16:0]=notifyId、
    // word3[23:16]=credit。type=7 打 notifyId；type=20（COND）word4 起为指令流不解码；其余只打 type。
    const uint8_t headType = static_cast<uint8_t>(headSqe[0] & 0x3FU);
    if (headType == static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_NOTIFY_WAIT)) {
        const uint32_t taskId = (headSqe[1] >> 16U) & 0xFFFFU;
        const uint32_t nid = headSqe[2] & 0x1FFFFU;
        const uint32_t credit = (headSqe[3] >> 16U) & 0xFFU;
        ASCCOMM_INFO(
            "[SQ_DUMP][Head] type=%u taskId=%u notifyId=%u credit=0x%02x", static_cast<uint32_t>(headType), taskId, nid,
            credit);

        // [NotifyFlag] 直读 TS notify 表：基址 0x06a0100000，16 组×512 项×4B，组距 0x10000；
        // bit0=flag、bit1=pendingClr。AICPU 核直读 TS 私有寄存器空间有挂死风险（load 指令本身
        // 不返回，无法兜底），故默认禁用，仅 ASCCOMM_DIAG_NOTIFY_FLAG=1 时开。
        constexpr uint64_t NOTIFY_TBL_BASE = 0x06a0100000ULL; // 0x06a0000000 + 0x100000
        constexpr uint32_t NOTIFY_PER_GROUP = 512U;
        constexpr uint32_t NOTIFY_TOTAL = 16U * NOTIFY_PER_GROUP;
        constexpr uint64_t NOTIFY_GROUP_STRIDE = 0x10000ULL;
        // magic static 落 .data（AICPU 加载域 .bss 零初始化不可信）；AICPU 侧 getenv 不继承
        // host env，常态读不到=禁用=安全默认。
        static const bool notifyFlagReadEnabled = []() {
            const char* env = getenv("ASCCOMM_DIAG_NOTIFY_FLAG");
            return env != nullptr && env[0] == '1';
        }();
        if (notifyFlagReadEnabled) {
            // 只读 nid±1 三个 4B 字看邻域污染；边界 nid 夹到合法区间防回绕野地址。
            for (int32_t delta = -1; delta <= 1; ++delta) {
                int64_t curNid = static_cast<int64_t>(nid) + delta;
                if (curNid < 0) {
                    curNid = 0;
                }
                if (curNid >= NOTIFY_TOTAL) {
                    curNid = NOTIFY_TOTAL - 1;
                }
                const uint32_t group = static_cast<uint32_t>(curNid) / NOTIFY_PER_GROUP;
                const uint32_t idx = static_cast<uint32_t>(curNid) % NOTIFY_PER_GROUP;
                const uint64_t flagAddr = NOTIFY_TBL_BASE + static_cast<uint64_t>(group) * NOTIFY_GROUP_STRIDE +
                                          static_cast<uint64_t>(idx) * sizeof(uint32_t);
                const uint32_t raw = *reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(flagAddr));
                ASCCOMM_INFO(
                    "[SQ_DUMP][NotifyFlag] notifyId=%u group=%u flagAddr=0x%llx flag=0x%08x pendingClr=0x%08x "
                    "raw=0x%08x",
                    static_cast<uint32_t>(curNid), group, static_cast<unsigned long long>(flagAddr), raw & 0x1U,
                    (raw >> 1U) & 0x1U, raw);
            }
        } else {
            ASCCOMM_INFO("[SQ_DUMP][NotifyFlag] disabled (107 batch: TS register read hangs AICPU core).");
        }
    } else if (headType == static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_COND)) {
        ASCCOMM_INFO("[SQ_DUMP][Head] type=%u(COND) word4=0x%08x", static_cast<uint32_t>(headType), headSqe[4]);
    } else {
        ASCCOMM_INFO("[SQ_DUMP][Head] type=%u", static_cast<uint32_t>(headType));
    }

    // 汇总行（便于 grep）+ 镜像对账行 + 分隔行
    ASCCOMM_INFO(
        "[SQ_DUMP][Summary] sqId=%u streamId=%u depth=%u hwHead=%u hwTail=%u base=0x%llx", rtsq.sqId, rtsq.streamId,
        depth, hwHead, hwTail, static_cast<unsigned long long>(rtsq.sqBaseAddr));
    // 镜像取自 rtsq 软件账本（query 失败回落时是唯一真相）；Mirror 与 Summary 背离 = 软硬件分叉的直接证据。
    ASCCOMM_INFO(
        "[SQ_DUMP][Mirror] sqId=%u mirrorHead=%u mirrorTail=%u mirrorDepth=%u pendCnt=%u taskIdRange=[%u,%u)",
        rtsq.sqId, rtsq.sqHead, rtsq.sqTail, rtsq.sqDepth, rtsq.pendingSqeCnt, rtsq.taskId, rtsq.taskIdEnd);
    ASCCOMM_INFO("[SQ_DUMP][End]");
    return static_cast<int32_t>(ASCCOMM_SUCCESS);
}
