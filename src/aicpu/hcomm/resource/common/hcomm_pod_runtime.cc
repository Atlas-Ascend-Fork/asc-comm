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
#include <unordered_map>
#include "hcomm_pod_runtime.h"
#include "hcomm_pod_launch.h"
#include "../stream/sqe_build_a5.h"
#include "../stream/sqe.h"
#include "securec.h"
#include "ascend_hal.h"
#include "log/hcomm_log.h"

namespace aicpu {
void __attribute__((weak)) __attribute__((visibility("default")))
GetSqeId(uint32_t batchNum, uint32_t &taskId, uint32_t &taskIdEnd);
}

namespace {

struct PodRtsqTaskState {
    uint32_t taskId{0};
    uint32_t taskIdEnd{0};
    uint32_t pendingSqeCnt{0};
};

std::unordered_map<const HcommRtsqContext *, PodRtsqTaskState> g_podTaskStates;

PodRtsqTaskState &get_task_state(const HcommRtsqContext &context)
{
    return g_podTaskStates[&context];
}

uint32_t get_task_id(const HcommRtsqContext &context)
{
    return get_task_state(context).taskId;
}

void refresh_task_id(const HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    ++state.taskId;
    if (state.taskId >= state.taskIdEnd) {
        constexpr uint32_t TASK_ID_BATCH = 1024;
        aicpu::GetSqeId(TASK_ID_BATCH, state.taskId, state.taskIdEnd);
    }
}

uint32_t get_pending_sqe_cnt(const HcommRtsqContext &context)
{
    return get_task_state(context).pendingSqeCnt;
}

HcclResult query_sq_value(const HcommRtsqContext &context, drvSqCqPropType_t property, uint32_t &value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = property;
    drvError_t ret = halSqCqQuery(context.localDevId, &queryInfo);
    CHK_PRT_RET(ret != 0,
        HCCL_ERROR("[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], property[%u], ret[%d].", __func__,
            context.localDevId, context.sqId, property, ret), HCCL_E_INTERNAL);
    value = queryInfo.value[0];
    return HCCL_SUCCESS;
}

HcclResult config_sq_tail(const HcommRtsqContext &context, uint32_t value)
{
    halSqCqConfigInfo configInfo{};
    configInfo.tsId = 0;
    configInfo.sqId = context.sqId;
    configInfo.cqId = 0;
    configInfo.type = DRV_NORMAL_TYPE;
    configInfo.prop = DRV_SQCQ_PROP_SQ_TAIL;
    configInfo.value[0] = value;
    drvError_t ret = halSqCqConfig(context.localDevId, &configInfo);
    CHK_PRT_RET(ret != 0,
        HCCL_ERROR("[%s] halSqCqConfig failed, localDevId[%u], sqId[%u], tail[%u], ret[%d].", __func__,
            context.localDevId, context.sqId, value, ret), HCCL_E_INTERNAL);
    return HCCL_SUCCESS;
}

uint32_t get_tail_to_head_distance(const HcommRtsqContext &context)
{
    if (context.sqHead == context.sqTail) {
        return context.sqDepth;
    }
    return context.sqTail < context.sqHead ? context.sqHead - context.sqTail :
        context.sqDepth - (context.sqTail - context.sqHead);
}

HcclResult ensure_queue_space(HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    auto startTime = std::chrono::steady_clock::now();
    const uint32_t sqFullTimeout = hcomm_pod_get_sq_full_timeout();
    uint32_t availableSpace = get_tail_to_head_distance(context);
    while (availableSpace <= state.pendingSqeCnt) {
        CHK_RET(query_sq_value(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
        availableSpace = get_tail_to_head_distance(context);
        if (availableSpace > state.pendingSqeCnt) {
            break;
        }
        const bool timeout = sqFullTimeout != 0 &&
            std::chrono::steady_clock::now() - startTime >= std::chrono::seconds(sqFullTimeout);
        CHK_PRT_RET(timeout,
            HCCL_ERROR("[%s] RTSQ full timeout, sqId[%u], head[%u], tail[%u], pending[%u].", __func__,
                context.sqId, context.sqHead, context.sqTail, state.pendingSqeCnt), HCCL_E_TIMEOUT);
        CHK_RET(hcomm_pod_try_launch_registered_threads());
    }
    return HCCL_SUCCESS;
}

HcclResult copy_pending_sqes(HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    const uint32_t byteCount = state.pendingSqeCnt * HCOMM_POD_RTSQ_SQE_SIZE;
    uint8_t *sqCurrent = reinterpret_cast<uint8_t *>(context.sqBaseAddr) +
        context.sqTail * HCOMM_POD_RTSQ_SQE_SIZE;
    if (context.sqTail >= context.sqHead) {
        const uint32_t depthLeft = context.sqDepth - context.sqTail;
        const uint32_t firstCount = state.pendingSqeCnt < depthLeft ? state.pendingSqeCnt : depthLeft;
        const uint32_t firstBytes = firstCount * HCOMM_POD_RTSQ_SQE_SIZE;
        CHK_PRT_RET(memcpy_sp(sqCurrent, firstBytes, context.localSqeBuffer, firstBytes) != EOK,
            HCCL_ERROR("[%s] first RTSQ memcpy failed.", __func__), HCCL_E_INTERNAL);
        if (firstCount != state.pendingSqeCnt) {
            const uint32_t remainingBytes = byteCount - firstBytes;
            CHK_PRT_RET(memcpy_sp(reinterpret_cast<void *>(context.sqBaseAddr), remainingBytes,
                context.localSqeBuffer + firstBytes, remainingBytes) != EOK,
                HCCL_ERROR("[%s] wrapped RTSQ memcpy failed.", __func__), HCCL_E_INTERNAL);
        }
        return HCCL_SUCCESS;
    }
    CHK_PRT_RET(memcpy_sp(sqCurrent, byteCount, context.localSqeBuffer, byteCount) != EOK,
        HCCL_ERROR("[%s] RTSQ memcpy failed.", __func__), HCCL_E_INTERNAL);
    return HCCL_SUCCESS;
}

HcclResult refresh_rtsq(HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    refresh_task_id(context);
    ++state.pendingSqeCnt;
    if ((context.launchFlag != 0 && !hcomm_pod_is_batch_launch_mode()) ||
        state.pendingSqeCnt == HCOMM_POD_RTSQ_BATCH_SIZE) {
        return hcomm_pod_rtsq_launch(context);
    }
    return HCCL_SUCCESS;
}

uint8_t *get_current_sqe(HcommRtsqContext &context)
{
    return context.localSqeBuffer + get_pending_sqe_cnt(context) * HCOMM_POD_RTSQ_SQE_SIZE;
}

HcclResult validate_thread_entity(HcommThreadEntity *thread)
{
    CHK_PTR_NULL(thread);
    CHK_PRT_RET(thread->abiHeader.version != HCOMM_POD_THREAD_VERSION ||
        thread->abiHeader.magicWord != HCOMM_POD_THREAD_MAGIC_WORD ||
        thread->abiHeader.size < sizeof(HcommThreadEntity),
        HCCL_ERROR("[%s] invalid POD thread entity.", __func__), HCCL_E_PARA);
    CHK_PTR_NULL(thread->sqContextAddr);
    return HCCL_SUCCESS;
}

} // namespace

HcclResult hcomm_pod_rtsq_launch(HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    if (state.pendingSqeCnt == 0) {
        return HCCL_SUCCESS;
    }
    CHK_PRT_RET(context.sqDepth == 0 || context.sqBaseAddr == 0,
        HCCL_ERROR("[%s] invalid RTSQ context, depth[%u], base[0x%llx].", __func__,
            context.sqDepth, context.sqBaseAddr), HCCL_E_PARA);
    CHK_RET(ensure_queue_space(context));
    CHK_RET(copy_pending_sqes(context));
    const uint32_t newTail = (context.sqTail + state.pendingSqeCnt) % context.sqDepth;
    CHK_RET(config_sq_tail(context, newTail));
    context.sqTail = newTail;
    state.pendingSqeCnt = 0;
    CHK_PRT_RET(memset_s(context.localSqeBuffer, sizeof(context.localSqeBuffer), 0,
        sizeof(context.localSqeBuffer)) != EOK, HCCL_ERROR("[%s] clear SQE buffer failed.", __func__),
        HCCL_E_INTERNAL);
    return HCCL_SUCCESS;
}

HcclResult hcomm_pod_rtsq_try_launch(HcommRtsqContext &context)
{
    PodRtsqTaskState &state = get_task_state(context);
    if (state.pendingSqeCnt == 0) {
        return HCCL_SUCCESS;
    }
    CHK_RET(query_sq_value(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
    if (get_tail_to_head_distance(context) <= state.pendingSqeCnt) {
        return HCCL_SUCCESS;
    }
    return hcomm_pod_rtsq_launch(context);
}

HcclResult hcomm_pod_rtsq_notify_wait(HcommRtsqContext &context, uint32_t notifyId, uint32_t timeout)
{
    Asc::build_a5_pod_sqe_notify_wait(context.streamId, get_task_id(context), notifyId, timeout, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_rtsq_notify_record(HcommRtsqContext &context, uint32_t notifyId)
{
    Asc::build_a5_pod_sqe_notify_record(context.streamId, get_task_id(context), notifyId, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_rtsq_sdma_copy(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    constexpr uint32_t RTSQ_PART_ID = 0;
    constexpr uint32_t COPY_OPCODE = 0;
    Asc::build_a5_pod_sqe_sdma_copy(context.streamId, get_task_id(context), dstAddr, srcAddr, size,
        RTSQ_PART_ID, COPY_OPCODE, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_rtsq_sdma_reduce(HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr,
                                  uint32_t size, const Asc::PodReduceIn &reduceIn)
{
    uint32_t operation = 0;
    switch (reduceIn.reduceOp) {
        case Asc::PodReduceOp::SUM:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_ADD);
            break;
        case Asc::PodReduceOp::MAX:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MAX);
            break;
        case Asc::PodReduceOp::MIN:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MIN);
            break;
        default:
            HCCL_ERROR("[%s] unsupported SDMA reduce operation[%u].", __func__, reduceIn.reduceOp);
            return HCCL_E_NOT_SUPPORT;
    }

    uint32_t dataType = 0;
    switch (reduceIn.dataType) {
        case Asc::PodDataType::INT8:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT8);
            break;
        case Asc::PodDataType::INT16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT16);
            break;
        case Asc::PodDataType::INT32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT32);
            break;
        case Asc::PodDataType::FP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP16);
            break;
        case Asc::PodDataType::FP32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP32);
            break;
        case Asc::PodDataType::BFP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_BFP16);
            break;
        default:
            HCCL_ERROR("[%s] unsupported SDMA data type[%u].", __func__, reduceIn.dataType);
            return HCCL_E_NOT_SUPPORT;
    }

    constexpr uint32_t RTSQ_PART_ID = 0;
    Asc::build_a5_pod_sqe_sdma_copy(context.streamId, get_task_id(context), dstAddr, srcAddr, size,
        RTSQ_PART_ID, operation | dataType, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_rtsq_ub_db_send(
    HcommRtsqContext &context, const Asc::PodUbJettyLiteId &jettyId, uint16_t piValue)
{
    Asc::build_a5_pod_sqe_ub_db_send(context.streamId, get_task_id(context), jettyId, piValue, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_rtsq_rdma_db_send(HcommRtsqContext &context, uint64_t dbAddr, uint64_t dbValue)
{
    Asc::build_a5_pod_sqe_rdma_db_send(context.streamId, get_task_id(context), dbAddr, dbValue, get_current_sqe(context));
    return refresh_rtsq(context);
}

HcclResult hcomm_pod_task_launch(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        HcommThreadEntity *thread = nullptr;
        CHK_RET(static_cast<HcclResult>(HcommGetPodThreadResource(threads[index], &thread)));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(hcomm_pod_rtsq_launch(*thread->sqContextAddr));
    }
    return HCCL_SUCCESS;
}

HcclResult hcomm_pod_try_launch(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        HcommThreadEntity *thread = nullptr;
        CHK_RET(static_cast<HcclResult>(HcommGetPodThreadResource(threads[index], &thread)));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(hcomm_pod_rtsq_try_launch(*thread->sqContextAddr));
    }
    return HCCL_SUCCESS;
}
