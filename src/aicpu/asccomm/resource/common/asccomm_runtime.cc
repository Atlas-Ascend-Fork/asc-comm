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
#include "asccomm_runtime.h"
#include "asccomm_launch.h"
#include "../stream/sqe_build_a5.h"
#include "../stream/sqe.h"
#include "securec.h"
#include "ascend_hal.h"
#include "asccomm_log.h"

namespace aicpu {
void __attribute__((weak)) __attribute__((visibility("default"))) GetSqeId(
    uint32_t batchNum, uint32_t& taskId, uint32_t& taskIdEnd);
}

namespace {

AsccommResult acquire_task_id(HcommRtsqContext& context, uint32_t& taskId)
{
    CHK_PRT_RET(
        context.localSqeAddr == 0, ASCCOMM_ERROR("[%s] local SQE buffer address is 0.", __func__), ASCCOMM_E_PTR);
    CHK_PRT_RET(
        context.pendingSqeCnt >= HCOMM_RESOURCE_RTSQ_BATCH_SIZE,
        ASCCOMM_ERROR("[%s] pending SQE count[%u] exceeds buffer capacity.", __func__, context.pendingSqeCnt),
        ASCCOMM_E_INTERNAL);
    if (context.taskId >= context.taskIdEnd) {
        CHK_PRT_RET(
            aicpu::GetSqeId == nullptr, ASCCOMM_ERROR("[%s] GetSqeId is unavailable.", __func__), ASCCOMM_E_INTERNAL);
        aicpu::GetSqeId(HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE, context.taskId, context.taskIdEnd);
        CHK_PRT_RET(
            context.taskId >= context.taskIdEnd ||
                context.taskIdEnd - context.taskId > HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE,
            ASCCOMM_ERROR("[%s] invalid Task ID range[%u, %u).", __func__, context.taskId, context.taskIdEnd),
            ASCCOMM_E_INTERNAL);
    }
    taskId = context.taskId;
    ++context.taskId;
    return ASCCOMM_SUCCESS;
}

AsccommResult query_sq_value(const HcommRtsqContext& context, drvSqCqPropType_t property, uint32_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = property;
    drvError_t ret = halSqCqQuery(context.localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], property[%u], ret[%d].", __func__, context.localDevId,
            context.sqId, property, ret),
        ASCCOMM_E_INTERNAL);
    value = queryInfo.value[0];
    return ASCCOMM_SUCCESS;
}

AsccommResult query_sq_base(const HcommRtsqContext& context, uint64_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = DRV_SQCQ_PROP_SQ_BASE;
    drvError_t ret = halSqCqQuery(context.localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], ret[%d].", __func__, context.localDevId, context.sqId,
            ret),
        ASCCOMM_E_INTERNAL);
    value = (static_cast<uint64_t>(queryInfo.value[1]) << 32U) | queryInfo.value[0];
    return ASCCOMM_SUCCESS;
}

AsccommResult config_sq_tail(const HcommRtsqContext& context, uint32_t value)
{
    halSqCqConfigInfo configInfo{};
    configInfo.tsId = 0;
    configInfo.sqId = context.sqId;
    configInfo.cqId = 0;
    configInfo.type = DRV_NORMAL_TYPE;
    configInfo.prop = DRV_SQCQ_PROP_SQ_TAIL;
    configInfo.value[0] = value;
    drvError_t ret = halSqCqConfig(context.localDevId, &configInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqConfig failed, localDevId[%u], sqId[%u], tail[%u], ret[%d].", __func__, context.localDevId,
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

AsccommResult ensure_queue_space(HcommRtsqContext& context)
{
    auto startTime = std::chrono::steady_clock::now();
    const uint32_t sqFullTimeout = asccomm_get_sq_full_timeout();
    uint32_t availableSpace = get_tail_to_head_distance(context);
    while (availableSpace <= context.pendingSqeCnt) {
        CHK_RET(query_sq_value(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
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

AsccommResult copy_pending_sqes(HcommRtsqContext& context)
{
    CHK_PRT_RET(
        context.localSqeAddr == 0, ASCCOMM_ERROR("[%s] local SQE buffer address is 0.", __func__), ASCCOMM_E_PTR);
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(context.localSqeAddr);
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

AsccommResult refresh_rtsq(HcommRtsqContext& context)
{
    ++context.pendingSqeCnt;
    if ((context.launchFlag != 0 && !asccomm_is_batch_launch_mode()) ||
        context.pendingSqeCnt == HCOMM_RESOURCE_RTSQ_BATCH_SIZE) {
        return asccomm_rtsq_launch(context);
    }
    return ASCCOMM_SUCCESS;
}

uint8_t* get_current_sqe(HcommRtsqContext& context)
{
    return reinterpret_cast<uint8_t*>(context.localSqeAddr) + context.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
}

AsccommResult validate_thread_entity(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PRT_RET(
        thread->abiHeader.version != HCOMM_RESOURCE_THREAD_VERSION ||
            thread->abiHeader.magicWord != HCOMM_RESOURCE_THREAD_MAGIC_WORD ||
            thread->abiHeader.size < sizeof(ThreadEntity),
        ASCCOMM_ERROR("[%s] invalid resource thread entity.", __func__), ASCCOMM_E_PARA);
    CHK_PTR_NULL(thread->sqContextAddr);
    const HcommRtsqContext& rtsq = *thread->sqContextAddr;
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0 || rtsq.sqHead >= rtsq.sqDepth || rtsq.sqTail >= rtsq.sqDepth,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ parameters, base[0x%llx], depth[%u], head[%u], tail[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth, rtsq.sqHead, rtsq.sqTail),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

} // namespace

AsccommResult asccomm_resolve_thread_rtsq(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PTR_NULL(thread->sqContextAddr);
    HcommRtsqContext& rtsq = *thread->sqContextAddr;
    if (rtsq.sqBaseAddr != 0) {
        return ASCCOMM_SUCCESS;
    }

    drvError_t ret = drvGetLocalDevIDByHostDevID(thread->devInfo.devId, &rtsq.localDevId);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] drvGetLocalDevIDByHostDevID failed, devicePhyId[%u], ret[%d].", __func__, thread->devInfo.devId, ret),
        ASCCOMM_E_INTERNAL);
    CHK_RET(query_sq_base(rtsq, rtsq.sqBaseAddr));
    CHK_RET(query_sq_value(rtsq, DRV_SQCQ_PROP_SQ_DEPTH, rtsq.sqDepth));
    CHK_RET(query_sq_value(rtsq, DRV_SQCQ_PROP_SQ_HEAD, rtsq.sqHead));
    CHK_RET(query_sq_value(rtsq, DRV_SQCQ_PROP_SQ_TAIL, rtsq.sqTail));
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ resource, base[0x%llx], depth[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth),
        ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

AsccommResult asccomm_rtsq_launch(HcommRtsqContext& context)
{
    if (context.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        context.sqDepth == 0 || context.sqBaseAddr == 0,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ context, depth[%u], base[0x%llx].", __func__, context.sqDepth, context.sqBaseAddr),
        ASCCOMM_E_PARA);
    CHK_RET(ensure_queue_space(context));
    CHK_RET(copy_pending_sqes(context));
    const uint32_t newTail = (context.sqTail + context.pendingSqeCnt) % context.sqDepth;
    CHK_RET(config_sq_tail(context, newTail));
    context.sqTail = newTail;
    context.pendingSqeCnt = 0;
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(context.localSqeAddr);
    constexpr size_t LOCAL_SQE_BUFFER_SIZE = HCOMM_RESOURCE_RTSQ_SQE_SIZE * HCOMM_RESOURCE_RTSQ_BATCH_SIZE;
    CHK_PRT_RET(
        memset_s(localSqeBuffer, LOCAL_SQE_BUFFER_SIZE, 0, LOCAL_SQE_BUFFER_SIZE) != EOK,
        ASCCOMM_ERROR("[%s] clear SQE buffer failed.", __func__), ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

AsccommResult asccomm_rtsq_try_launch(HcommRtsqContext& context)
{
    if (context.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_RET(query_sq_value(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
    if (get_tail_to_head_distance(context) <= context.pendingSqeCnt) {
        return ASCCOMM_SUCCESS;
    }
    return asccomm_rtsq_launch(context);
}

AsccommResult asccomm_rtsq_notify_wait(HcommRtsqContext& context, uint32_t notifyId, uint32_t timeout)
{
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_wait(context.streamId, taskId, notifyId, timeout, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_rtsq_notify_record(HcommRtsqContext& context, uint32_t notifyId)
{
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_record(context.streamId, taskId, notifyId, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_rtsq_sdma_copy(HcommRtsqContext& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    constexpr uint32_t RTSQ_PART_ID = 0;
    constexpr uint32_t COPY_OPCODE = 0;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_sdma_copy(
        context.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, COPY_OPCODE, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_rtsq_sdma_reduce(
    HcommRtsqContext& context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn)
{
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
        context.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, operation | dataType, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_rtsq_ub_db_send(HcommRtsqContext& context, const Asc::UbJettyLiteId& jettyId, uint16_t piValue)
{
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_ub_db_send(context.streamId, taskId, jettyId, piValue, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_rtsq_rdma_db_send(HcommRtsqContext& context, uint64_t dbAddr, uint64_t dbValue)
{
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_rdma_db_send(context.streamId, taskId, dbAddr, dbValue, get_current_sqe(context));
    return refresh_rtsq(context);
}

AsccommResult asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_launch(*thread->sqContextAddr));
    }
    return ASCCOMM_SUCCESS;
}

AsccommResult asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_try_launch(*thread->sqContextAddr));
    }
    return ASCCOMM_SUCCESS;
}
