/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cmath>
#include <limits>
#include "hcomm/hcomm_pod_primitives.h"
#include "hcomm/hcomm_pod_resource.h"
#include "hcomm_pod_launch.h"
#include "hcomm_pod_runtime.h"
#include "hcomm_pod_transport.h"
#include "../../common/utils/exception_util.h"
#include "log/hcomm_log.h"

namespace {

struct PodContext {
    HcommThreadEntity *thread;
    HcommChannelEntity *channel;
};

template <typename Entity>
HcclResult ValidatePodEntity(const Entity *entity, uint32_t version, uint32_t magicWord)
{
    CHK_PTR_NULL(entity);
    CHK_PRT_RET(entity->abiHeader.version != version || entity->abiHeader.magicWord != magicWord,
        HCCL_ERROR("[%s] invalid POD ABI header, version[%u], magicWord[0x%x].", __func__,
            entity->abiHeader.version, entity->abiHeader.magicWord), HCCL_E_PARA);
    CHK_PRT_RET(entity->abiHeader.size < sizeof(Entity),
        HCCL_ERROR("[%s] POD entity size[%u] is less than expected size[%zu].", __func__,
            entity->abiHeader.size, sizeof(Entity)), HCCL_E_PARA);
    return HCCL_SUCCESS;
}

HcclResult GetPodThread(ThreadHandle thread, bool registerThread, HcommThreadEntity *&threadEntity)
{
    CHK_PRT_RET(thread == 0, HCCL_ERROR("[%s] thread handle is 0.", __func__), HCCL_E_PTR);
    CHK_RET(static_cast<HcclResult>(HcommGetPodThreadResource(thread, &threadEntity)));
    CHK_RET(ValidatePodEntity(threadEntity, HCOMM_POD_THREAD_VERSION, HCOMM_POD_THREAD_MAGIC_WORD));
    CHK_PTR_NULL(threadEntity->sqContextAddr);
    if (registerThread) {
        HcommPodRegisterThread(thread);
    }
    return HCCL_SUCCESS;
}

HcclResult GetPodContext(ThreadHandle thread, ChannelHandle channel, bool registerThread, PodContext &context)
{
    CHK_PRT_RET(channel == 0, HCCL_ERROR("[%s] channel handle is 0.", __func__), HCCL_E_PTR);
    CHK_RET(GetPodThread(thread, registerThread, context.thread));
    CHK_RET(static_cast<HcclResult>(HcommGetPodChannelResource(channel, &context.channel)));
    CHK_RET(ValidatePodEntity(context.channel, HCOMM_POD_CHANNEL_VERSION, HCOMM_POD_CHANNEL_MAGIC_WORD));
    return HCCL_SUCCESS;
}

HcclResult GetThreadNotifyId(const HcommThreadEntity &thread, uint32_t notifyIdx, uint32_t &notifyId)
{
    CHK_PRT_RET(notifyIdx >= thread.localNotifyNum || thread.localNotifyAddr == nullptr,
        HCCL_ERROR("[%s] local notify index[%u] out of range[%u].", __func__, notifyIdx,
            thread.localNotifyNum), HCCL_E_PARA);
    const int32_t resourceId = thread.localNotifyAddr[notifyIdx].notify.rma.notifyId;
    CHK_PRT_RET(resourceId < 0, HCCL_ERROR("[%s] invalid notify id[%d].", __func__, resourceId), HCCL_E_PARA);
    notifyId = static_cast<uint32_t>(resourceId);
    return HCCL_SUCCESS;
}

HcclResult ConvertReduceIn(HcommDataType dataType, HcommReduceOp reduceOp, Hccl::PodDataType &innerDataType,
                           Hccl::PodReduceOp &innerReduceOp, uint32_t &typeSize)
{
    switch (dataType) {
        case HCOMM_DATA_TYPE_INT8: innerDataType = Hccl::PodDataType::INT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_INT16: innerDataType = Hccl::PodDataType::INT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT32: innerDataType = Hccl::PodDataType::INT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP16: innerDataType = Hccl::PodDataType::FP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_FP32: innerDataType = Hccl::PodDataType::FP32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_INT64: innerDataType = Hccl::PodDataType::INT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT64: innerDataType = Hccl::PodDataType::UINT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT8: innerDataType = Hccl::PodDataType::UINT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_UINT16: innerDataType = Hccl::PodDataType::UINT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_UINT32: innerDataType = Hccl::PodDataType::UINT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP64: innerDataType = Hccl::PodDataType::FP64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_BFP16: innerDataType = Hccl::PodDataType::BFP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT128: innerDataType = Hccl::PodDataType::INT128; typeSize = 16; break;
#ifndef OPEN_BUILD_PROJECT
        case HCOMM_DATA_TYPE_HIF8: innerDataType = Hccl::PodDataType::HIF8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E4M3: innerDataType = Hccl::PodDataType::FP8E4M3; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E5M2: innerDataType = Hccl::PodDataType::FP8E5M2; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E8M0: innerDataType = Hccl::PodDataType::FP8E8M0; typeSize = 1; break;
#endif
        default:
            HCCL_ERROR("[%s] dataType[%u] is not supported.", __func__, dataType);
            return HCCL_E_PARA;
    }
    switch (reduceOp) {
        case HCOMM_REDUCE_SUM: innerReduceOp = Hccl::PodReduceOp::SUM; break;
        case HCOMM_REDUCE_PROD: innerReduceOp = Hccl::PodReduceOp::PROD; break;
        case HCOMM_REDUCE_MAX: innerReduceOp = Hccl::PodReduceOp::MAX; break;
        case HCOMM_REDUCE_MIN: innerReduceOp = Hccl::PodReduceOp::MIN; break;
        default:
            HCCL_ERROR("[%s] reduceOp[%u] is not supported.", __func__, reduceOp);
            return HCCL_E_PARA;
    }
    return HCCL_SUCCESS;
}

HcclResult GetReduceParams(HcommDataType dataType, HcommReduceOp reduceOp, uint64_t count,
                           uint64_t &len, Hccl::PodReduceIn &reduceIn)
{
    Hccl::PodDataType innerDataType = Hccl::PodDataType::INVALID;
    Hccl::PodReduceOp innerReduceOp = Hccl::PodReduceOp::INVALID;
    uint32_t typeSize = 0;
    CHK_RET(ConvertReduceIn(dataType, reduceOp, innerDataType, innerReduceOp, typeSize));
    CHK_PRT_RET(count > std::numeric_limits<uint64_t>::max() / typeSize,
        HCCL_ERROR("[%s] reduce length overflow.", __func__), HCCL_E_PARA);
    len = count * typeSize;
    reduceIn = Hccl::PodReduceIn(innerDataType, innerReduceOp);
    return HCCL_SUCCESS;
}

template <typename Operation>
HcclResult ExecutePodOperation(Operation operation)
{
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = HCCL_E_INTERNAL);
    return ret;
}

HcclResult ConvertTimeout(float timeout, uint32_t &timeoutSeconds)
{
    if (std::isnan(timeout) || timeout < 0.0f ||
        static_cast<double>(timeout) > static_cast<double>(UINT32_MAX)) {
        HCCL_ERROR("[%s] timeout[%f] is invalid.", __func__, timeout);
        return HCCL_E_PARA;
    }
    timeoutSeconds = static_cast<uint32_t>(timeout);
    return HCCL_SUCCESS;
}

} // namespace

int32_t HcommBatchModeStartPod(const char *batchTag)
{
    (void)batchTag;
    HcommPodSetBatchLaunchMode(true);
    return HCCL_SUCCESS;
}

int32_t HcommBatchModeEndPod(const char *batchTag)
{
    (void)batchTag;
    HcommPodSetBatchLaunchMode(false);
    const HcclResult ret = HcommPodLaunchRegisteredThreads();
    HcommPodClearRegisteredThreads();
    return ret;
}

int32_t HcommLocalCopyOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t len)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    return ExecutePodOperation([&]() {
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(HcommPodRtsqSdmaCopy(*threadEntity->sqContextAddr,
                reinterpret_cast<uintptr_t>(dst) + offset, reinterpret_cast<uintptr_t>(src) + offset,
                static_cast<uint32_t>(realSize)));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t HcommLocalReduceOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t count,
                                    HcommDataType dataType, HcommReduceOp reduceOp)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
    uint64_t len = 0;
    CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    return ExecutePodOperation([&]() {
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(HcommPodRtsqSdmaReduce(*threadEntity->sqContextAddr,
                reinterpret_cast<uintptr_t>(dst) + offset, reinterpret_cast<uintptr_t>(src) + offset,
                static_cast<uint32_t>(realSize), reduceIn));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t HcommThreadNotifyRecordOnThreadPod(
    ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
{
    HcommThreadEntity *threadEntity = nullptr;
    HcommThreadEntity *dstThreadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    CHK_RET(GetPodThread(dstThread, false, dstThreadEntity));
    uint32_t notifyId = 0;
    CHK_RET(GetThreadNotifyId(*dstThreadEntity, dstNotifyIdx, notifyId));
    return ExecutePodOperation([&]() {
        return HcommPodRtsqNotifyRecord(*threadEntity->sqContextAddr, notifyId);
    });
}

int32_t HcommThreadNotifyWaitOnThreadPod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
{
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    uint32_t notifyId = 0;
    CHK_RET(GetThreadNotifyId(*threadEntity, notifyIdx, notifyId));
    return ExecutePodOperation([&]() {
        return HcommPodRtsqNotifyWait(*threadEntity->sqContextAddr, notifyId, timeOut);
    });
}

int32_t HcommAclrtNotifyRecordOnThreadPod(ThreadHandle thread, uint64_t dstNotifyId)
{
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    return ExecutePodOperation([&]() {
        return HcommPodRtsqNotifyRecord(*threadEntity->sqContextAddr, static_cast<uint32_t>(dstNotifyId));
    });
}

int32_t HcommAclrtNotifyWaitOnThreadPod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
{
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    return ExecutePodOperation([&]() {
        return HcommPodRtsqNotifyWait(
            *threadEntity->sqContextAddr, static_cast<uint32_t>(notifyId), timeOut);
    });
}

int32_t HcommWriteOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodWrite(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(src),
            reinterpret_cast<uintptr_t>(dst), len);
    });
}

int32_t HcommWriteReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
    uint64_t len = 0;
    CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodWriteReduce(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(src),
            reinterpret_cast<uintptr_t>(dst), len, reduceIn);
    });
}

int32_t HcommWriteWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                        uint64_t len, uint32_t remoteNotifyIdx)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodWriteWithNotify(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(src),
            reinterpret_cast<uintptr_t>(dst), len, remoteNotifyIdx);
    });
}

int32_t HcommWriteReduceWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
                                              const void *src, uint64_t count, HcommDataType dataType,
                                              HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
    uint64_t len = 0;
    CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodWriteReduceWithNotify(*context.thread, *context.channel,
            reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len, reduceIn, remoteNotifyIdx);
    });
}

int32_t HcommReadOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodRead(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst),
            reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t HcommReadReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
                                   uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    CHK_PTR_NULL(dst);
    CHK_PTR_NULL(src);
    Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
    uint64_t len = 0;
    CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodReadReduce(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst),
            reinterpret_cast<uintptr_t>(src), len, reduceIn);
    });
}

int32_t HcommBatchTransferOnThreadPod(ThreadHandle thread, ChannelHandle channel,
                                      const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodBatchTransfer(*context.thread, *context.channel, transferDescs, transferDescNum);
    });
}

int32_t HcommChannelNotifyRecordOnThreadPod(ThreadHandle thread, ChannelHandle channel,
                                            uint32_t remoteNotifyIdx)
{
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodNotifyRecord(*context.thread, *context.channel, remoteNotifyIdx);
    });
}

int32_t HcommChannelNotifyWaitOnThreadPod(ThreadHandle thread, ChannelHandle channel,
                                          uint32_t localNotifyIdx, uint32_t timeOut)
{
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodNotifyWait(*context.thread, *context.channel, localNotifyIdx, timeOut);
    });
}

int32_t HcommChannelFenceOnThreadPod(ThreadHandle thread, ChannelHandle channel)
{
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, false, context));
    return HcommPodFence(*context.channel);
}

int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeoutPod(ThreadHandle thread, ChannelHandle channel,
                                                            uint32_t localNotifyIdx)
{
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    const uint32_t timeout = HcommPodGetNotifyWaitTimeout();
    return ExecutePodOperation([&]() {
        return HcommPodNotifyWait(*context.thread, *context.channel, localNotifyIdx, timeout);
    });
}

int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeoutPod(ThreadHandle thread, uint32_t notifyIdx)
{
    HcommThreadEntity *threadEntity = nullptr;
    CHK_RET(GetPodThread(thread, true, threadEntity));
    uint32_t notifyId = 0;
    CHK_RET(GetThreadNotifyId(*threadEntity, notifyIdx, notifyId));
    const uint32_t timeout = HcommPodGetNotifyWaitTimeout();
    return ExecutePodOperation([&]() {
        return HcommPodRtsqNotifyWait(*threadEntity->sqContextAddr, notifyId, timeout);
    });
}

int32_t HcommSetNotifyWaitTimeOutPod(float timeOut)
{
    uint32_t timeoutSeconds = 0;
    CHK_RET(ConvertTimeout(timeOut, timeoutSeconds));
    HcommPodSetNotifyWaitTimeout(timeoutSeconds);
    return HCCL_SUCCESS;
}

int32_t HcommThreadResAcquireTimeOutPod(float timeOut)
{
    uint32_t timeoutSeconds = 0;
    CHK_RET(ConvertTimeout(timeOut, timeoutSeconds));
    HcommPodSetSqFullTimeout(timeoutSeconds);
    return HCCL_SUCCESS;
}

int32_t HcommChannelDrainOnThreadPod(ThreadHandle thread, ChannelHandle channel)
{
    PodContext context{};
    CHK_RET(GetPodContext(thread, channel, true, context));
    return ExecutePodOperation([&]() {
        return HcommPodDrain(*context.thread, *context.channel);
    });
}
