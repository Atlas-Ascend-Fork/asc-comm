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

HcclResult QuerySqValue(const HcommRtsqContext &context, drvSqCqPropType_t property, uint32_t &value)
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

HcclResult ConfigSqTail(const HcommRtsqContext &context, uint32_t value)
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

uint32_t GetTailToHeadDistance(const HcommRtsqContext &context)
{
    if (context.sqHead == context.sqTail) {
        return context.sqDepth;
    }
    return context.sqTail < context.sqHead ? context.sqHead - context.sqTail :
        context.sqDepth - (context.sqTail - context.sqHead);
}

HcclResult EnsureQueueSpace(HcommRtsqContext &context)
{
    auto startTime = std::chrono::steady_clock::now();
    const uint32_t sqFullTimeout = HcommPodGetSqFullTimeout(context.sqFullTimeout);
    uint32_t availableSpace = GetTailToHeadDistance(context);
    while (availableSpace <= context.pendingSqeCnt) {
        CHK_RET(QuerySqValue(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
        availableSpace = GetTailToHeadDistance(context);
        if (availableSpace > context.pendingSqeCnt) {
            break;
        }
        const bool timeout = sqFullTimeout != 0 &&
            std::chrono::steady_clock::now() - startTime >= std::chrono::seconds(sqFullTimeout);
        CHK_PRT_RET(timeout,
            HCCL_ERROR("[%s] RTSQ full timeout, sqId[%u], head[%u], tail[%u], pending[%u].", __func__,
                context.sqId, context.sqHead, context.sqTail, context.pendingSqeCnt), HCCL_E_TIMEOUT);
        CHK_RET(HcommPodTryLaunchRegisteredThreads());
    }
    return HCCL_SUCCESS;
}

HcclResult CopyPendingSqes(HcommRtsqContext &context)
{
    const uint32_t byteCount = context.pendingSqeCnt * HCOMM_POD_RTSQ_SQE_SIZE;
    uint8_t *sqCurrent = reinterpret_cast<uint8_t *>(context.sqBaseAddr) +
        context.sqTail * HCOMM_POD_RTSQ_SQE_SIZE;
    if (context.sqTail >= context.sqHead) {
        const uint32_t depthLeft = context.sqDepth - context.sqTail;
        const uint32_t firstCount = context.pendingSqeCnt < depthLeft ? context.pendingSqeCnt : depthLeft;
        const uint32_t firstBytes = firstCount * HCOMM_POD_RTSQ_SQE_SIZE;
        CHK_PRT_RET(memcpy_sp(sqCurrent, firstBytes, context.localSqeBuffer, firstBytes) != EOK,
            HCCL_ERROR("[%s] first RTSQ memcpy failed.", __func__), HCCL_E_INTERNAL);
        if (firstCount != context.pendingSqeCnt) {
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

HcclResult RefreshRtsq(HcommRtsqContext &context)
{
    ++context.taskId;
    if (context.taskId >= context.taskIdEnd) {
        constexpr uint32_t TASK_ID_BATCH = 1024;
        aicpu::GetSqeId(TASK_ID_BATCH, context.taskId, context.taskIdEnd);
    }
    ++context.pendingSqeCnt;
    if ((context.launchFlag != 0 && !HcommPodIsBatchLaunchMode()) ||
        context.pendingSqeCnt == HCOMM_POD_RTSQ_BATCH_SIZE) {
        return HcommPodRtsqLaunch(context);
    }
    return HCCL_SUCCESS;
}

uint8_t *GetCurrentSqe(HcommRtsqContext &context)
{
    return context.localSqeBuffer + context.pendingSqeCnt * HCOMM_POD_RTSQ_SQE_SIZE;
}

HcclResult ValidateThreadEntity(HcommThreadEntity *thread)
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

HcclResult HcommPodRtsqLaunch(HcommRtsqContext &context)
{
    if (context.pendingSqeCnt == 0) {
        return HCCL_SUCCESS;
    }
    CHK_PRT_RET(context.sqDepth == 0 || context.sqBaseAddr == 0,
        HCCL_ERROR("[%s] invalid RTSQ context, depth[%u], base[0x%llx].", __func__,
            context.sqDepth, context.sqBaseAddr), HCCL_E_PARA);
    CHK_RET(EnsureQueueSpace(context));
    CHK_RET(CopyPendingSqes(context));
    const uint32_t newTail = (context.sqTail + context.pendingSqeCnt) % context.sqDepth;
    CHK_RET(ConfigSqTail(context, newTail));
    context.sqTail = newTail;
    context.pendingSqeCnt = 0;
    CHK_PRT_RET(memset_s(context.localSqeBuffer, sizeof(context.localSqeBuffer), 0,
        sizeof(context.localSqeBuffer)) != EOK, HCCL_ERROR("[%s] clear SQE buffer failed.", __func__),
        HCCL_E_INTERNAL);
    return HCCL_SUCCESS;
}

HcclResult HcommPodRtsqTryLaunch(HcommRtsqContext &context)
{
    if (context.pendingSqeCnt == 0) {
        return HCCL_SUCCESS;
    }
    CHK_RET(QuerySqValue(context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
    if (GetTailToHeadDistance(context) <= context.pendingSqeCnt) {
        return HCCL_SUCCESS;
    }
    return HcommPodRtsqLaunch(context);
}

HcclResult HcommPodRtsqNotifyWait(HcommRtsqContext &context, uint32_t notifyId, uint32_t timeout)
{
    Hccl::BuildA5PodSqeNotifyWait(context.streamId, context.taskId, notifyId, timeout, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodRtsqNotifyRecord(HcommRtsqContext &context, uint32_t notifyId)
{
    Hccl::BuildA5PodSqeNotifyRecord(context.streamId, context.taskId, notifyId, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodRtsqSdmaCopy(
    HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    constexpr uint32_t RTSQ_PART_ID = 0;
    constexpr uint32_t COPY_OPCODE = 0;
    Hccl::BuildA5PodSqeSdmaCopy(context.streamId, context.taskId, dstAddr, srcAddr, size,
        RTSQ_PART_ID, COPY_OPCODE, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodRtsqSdmaReduce(HcommRtsqContext &context, uint64_t dstAddr, uint64_t srcAddr,
                                  uint32_t size, const Hccl::PodReduceIn &reduceIn)
{
    uint32_t operation = 0;
    switch (reduceIn.reduceOp) {
        case Hccl::PodReduceOp::SUM:
            operation = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_ADD);
            break;
        case Hccl::PodReduceOp::MAX:
            operation = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MAX);
            break;
        case Hccl::PodReduceOp::MIN:
            operation = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MIN);
            break;
        default:
            HCCL_ERROR("[%s] unsupported SDMA reduce operation[%u].", __func__, reduceIn.reduceOp);
            return HCCL_E_NOT_SUPPORT;
    }

    uint32_t dataType = 0;
    switch (reduceIn.dataType) {
        case Hccl::PodDataType::INT8:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT8);
            break;
        case Hccl::PodDataType::INT16:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT16);
            break;
        case Hccl::PodDataType::INT32:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT32);
            break;
        case Hccl::PodDataType::FP16:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP16);
            break;
        case Hccl::PodDataType::FP32:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP32);
            break;
        case Hccl::PodDataType::BFP16:
            dataType = static_cast<uint32_t>(Hccl::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_BFP16);
            break;
        default:
            HCCL_ERROR("[%s] unsupported SDMA data type[%u].", __func__, reduceIn.dataType);
            return HCCL_E_NOT_SUPPORT;
    }

    constexpr uint32_t RTSQ_PART_ID = 0;
    Hccl::BuildA5PodSqeSdmaCopy(context.streamId, context.taskId, dstAddr, srcAddr, size,
        RTSQ_PART_ID, operation | dataType, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodRtsqUbDbSend(
    HcommRtsqContext &context, const Hccl::PodUbJettyLiteId &jettyId, uint16_t piValue)
{
    Hccl::BuildA5PodSqeUbDbSend(context.streamId, context.taskId, jettyId, piValue, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodRtsqRdmaDbSend(HcommRtsqContext &context, uint64_t dbAddr, uint64_t dbValue)
{
    Hccl::BuildA5PodSqeRdmaDbSend(context.streamId, context.taskId, dbAddr, dbValue, GetCurrentSqe(context));
    return RefreshRtsq(context);
}

HcclResult HcommPodTaskLaunch(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        auto *thread = reinterpret_cast<HcommThreadEntity *>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(ValidateThreadEntity(thread));
        CHK_RET(HcommPodRtsqLaunch(*thread->sqContextAddr));
    }
    return HCCL_SUCCESS;
}

HcclResult HcommPodTryLaunch(ThreadHandle *threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    for (uint32_t index = 0; index < threadNum; ++index) {
        auto *thread = reinterpret_cast<HcommThreadEntity *>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(ValidateThreadEntity(thread));
        CHK_RET(HcommPodRtsqTryLaunch(*thread->sqContextAddr));
    }
    return HCCL_SUCCESS;
}
