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
    CHK_PRT_RET(thread.localNotifyAddr[notifyIdx].type != HCOMM_NOTIFY_TYPE_LOCAL,
        HCCL_ERROR("[%s] local notify type[%u] is not LOCAL.", __func__,
            thread.localNotifyAddr[notifyIdx].type), HCCL_E_PARA);
    const int32_t resourceId = thread.localNotifyAddr[notifyIdx].notify.local.notifyId;
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

HcommPodDfxEvent CreatePodDfxEvent(HcommPodDfxApi api)
{
    HcommPodDfxEvent event{};
    event.abiHeader = {HCOMM_POD_DFX_VERSION, HCOMM_POD_DFX_MAGIC_WORD, sizeof(HcommPodDfxEvent), 0};
    event.api = api;
    event.stage = HCOMM_POD_DFX_STAGE_INVALID;
    return event;
}

void CopyPodDfxTag(const char *tag, char (&dst)[HCOMM_POD_DFX_TAG_MAX_LEN])
{
    if (tag == nullptr) {
        return;
    }
    size_t index = 0;
    while (index + 1 < HCOMM_POD_DFX_TAG_MAX_LEN && tag[index] != '\0') {
        dst[index] = tag[index];
        ++index;
    }
    dst[index] = '\0';
}

HcommPodDfxCallback GetPodDfxCallback()
{
    static HcommPodDfxCallback callback = []() {
        HcommPodDfxCallback result = nullptr;
        return HcommGetPodDfxCallback(&result) == HCCL_SUCCESS ? result : nullptr;
    }();
    return callback;
}

void ReportPodDfxEvent(HcommPodDfxEvent &event, HcommPodDfxStage stage, HcclResult result)
{
    HcommPodDfxCallback callback = GetPodDfxCallback();
    if (callback == nullptr) {
        return;
    }
    event.stage = stage;
    event.result = result;
    callback(&event);
}

template <typename Operation>
HcclResult ExecutePodApi(HcommPodDfxEvent &event, Operation operation)
{
    ReportPodDfxEvent(event, HCOMM_POD_DFX_STAGE_START, HCCL_SUCCESS);
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = HCCL_E_INTERNAL);
    const HcommPodDfxStage stage = ret == HCCL_SUCCESS ? HCOMM_POD_DFX_STAGE_SUCCESS : HCOMM_POD_DFX_STAGE_FAIL;
    ReportPodDfxEvent(event, stage, ret);
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
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_BATCH_MODE_START);
    CopyPodDfxTag(batchTag, event.params.batchModeStart.batchTag);
    return ExecutePodApi(event, [&]() {
        HcommPodSetBatchLaunchMode(true);
        return HCCL_SUCCESS;
    });
}

int32_t HcommBatchModeEndPod(const char *batchTag)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_BATCH_MODE_END);
    CopyPodDfxTag(batchTag, event.params.batchModeEnd.batchTag);
    return ExecutePodApi(event, [&]() {
        HcommPodSetBatchLaunchMode(false);
        const HcclResult ret = HcommPodLaunchRegisteredThreads();
        HcommPodClearRegisteredThreads();
        return ret;
    });
}

int32_t HcommLocalCopyOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_LOCAL_COPY);
    event.params.localCopy.thread = thread;
    event.params.localCopy.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.localCopy.src = reinterpret_cast<uintptr_t>(src);
    event.params.localCopy.length = len;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(HcommPodRtsqSdmaCopy(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize)));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t HcommLocalReduceOnThreadPod(
    ThreadHandle thread, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_LOCAL_REDUCE);
    event.params.localReduce.thread = thread;
    event.params.localReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.localReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.localReduce.count = count;
    event.params.localReduce.dataType = dataType;
    event.params.localReduce.reduceOp = reduceOp;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(HcommPodRtsqSdmaReduce(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize), reduceIn));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t HcommThreadNotifyRecordOnThreadPod(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_THREAD_NOTIFY_RECORD);
    event.params.threadNotifyRecord.thread = thread;
    event.params.threadNotifyRecord.dstThread = dstThread;
    event.params.threadNotifyRecord.dstNotifyIdx = dstNotifyIdx;
    return ExecutePodApi(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        HcommThreadEntity *dstThreadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        CHK_RET(GetPodThread(dstThread, false, dstThreadEntity));
        uint32_t notifyId = 0;
        CHK_RET(GetThreadNotifyId(*dstThreadEntity, dstNotifyIdx, notifyId));
        return HcommPodRtsqNotifyRecord(*threadEntity->sqContextAddr, notifyId);
    });
}

int32_t HcommThreadNotifyWaitOnThreadPod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_THREAD_NOTIFY_WAIT);
    event.params.threadNotifyWait.thread = thread;
    event.params.threadNotifyWait.notifyIdx = notifyIdx;
    event.params.threadNotifyWait.timeOut = timeOut;
    return ExecutePodApi(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(GetThreadNotifyId(*threadEntity, notifyIdx, notifyId));
        return HcommPodRtsqNotifyWait(*threadEntity->sqContextAddr, notifyId, timeOut);
    });
}

int32_t HcommAclrtNotifyRecordOnThreadPod(ThreadHandle thread, uint64_t dstNotifyId)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_ACLRT_NOTIFY_RECORD);
    event.params.aclrtNotifyRecord.thread = thread;
    event.params.aclrtNotifyRecord.dstNotifyId = dstNotifyId;
    return ExecutePodApi(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        return HcommPodRtsqNotifyRecord(*threadEntity->sqContextAddr, static_cast<uint32_t>(dstNotifyId));
    });
}

int32_t HcommAclrtNotifyWaitOnThreadPod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_ACLRT_NOTIFY_WAIT);
    event.params.aclrtNotifyWait.thread = thread;
    event.params.aclrtNotifyWait.notifyId = notifyId;
    event.params.aclrtNotifyWait.timeOut = timeOut;
    return ExecutePodApi(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        return HcommPodRtsqNotifyWait(*threadEntity->sqContextAddr, static_cast<uint32_t>(notifyId), timeOut);
    });
}

int32_t HcommWriteOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_WRITE);
    event.params.write.thread = thread;
    event.params.write.channel = channel;
    event.params.write.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.write.src = reinterpret_cast<uintptr_t>(src);
    event.params.write.length = len;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodWrite(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len);
    });
}

int32_t HcommWriteReduceOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_WRITE_REDUCE);
    event.params.writeReduce.thread = thread;
    event.params.writeReduce.channel = channel;
    event.params.writeReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeReduce.count = count;
    event.params.writeReduce.dataType = dataType;
    event.params.writeReduce.reduceOp = reduceOp;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodWriteReduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn);
    });
}

int32_t HcommWriteWithNotifyOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_WRITE_WITH_NOTIFY);
    event.params.writeWithNotify.thread = thread;
    event.params.writeWithNotify.channel = channel;
    event.params.writeWithNotify.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeWithNotify.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeWithNotify.length = len;
    event.params.writeWithNotify.remoteNotifyIdx = remoteNotifyIdx;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodWriteWithNotify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            remoteNotifyIdx);
    });
}

int32_t HcommWriteReduceWithNotifyOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_WRITE_REDUCE_WITH_NOTIFY);
    event.params.writeReduceWithNotify.thread = thread;
    event.params.writeReduceWithNotify.channel = channel;
    event.params.writeReduceWithNotify.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeReduceWithNotify.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeReduceWithNotify.count = count;
    event.params.writeReduceWithNotify.dataType = dataType;
    event.params.writeReduceWithNotify.reduceOp = reduceOp;
    event.params.writeReduceWithNotify.remoteNotifyIdx = remoteNotifyIdx;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodWriteReduceWithNotify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn, remoteNotifyIdx);
    });
}

int32_t HcommReadOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_READ);
    event.params.read.thread = thread;
    event.params.read.channel = channel;
    event.params.read.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.read.src = reinterpret_cast<uintptr_t>(src);
    event.params.read.length = len;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodRead(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t HcommReadReduceOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_READ_REDUCE);
    event.params.readReduce.thread = thread;
    event.params.readReduce.channel = channel;
    event.params.readReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.readReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.readReduce.count = count;
    event.params.readReduce.dataType = dataType;
    event.params.readReduce.reduceOp = reduceOp;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Hccl::PodReduceIn reduceIn{Hccl::PodDataType::INVALID, Hccl::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(GetReduceParams(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodReadReduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len,
            reduceIn);
    });
}

int32_t HcommBatchTransferOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_BATCH_TRANSFER);
    event.params.batchTransfer.thread = thread;
    event.params.batchTransfer.channel = channel;
    event.params.batchTransfer.transferDescNum = transferDescNum;
    return ExecutePodApi(event, [&]() {
        CHK_PTR_NULL(transferDescs);
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodBatchTransfer(*context.thread, *context.channel, transferDescs, transferDescNum);
    });
}

int32_t HcommChannelNotifyRecordOnThreadPod(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_RECORD);
    event.params.channelNotifyRecord.thread = thread;
    event.params.channelNotifyRecord.channel = channel;
    event.params.channelNotifyRecord.remoteNotifyIdx = remoteNotifyIdx;
    return ExecutePodApi(event, [&]() {
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodNotifyRecord(*context.thread, *context.channel, remoteNotifyIdx);
    });
}

int32_t HcommChannelNotifyWaitOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_WAIT);
    event.params.channelNotifyWait.thread = thread;
    event.params.channelNotifyWait.channel = channel;
    event.params.channelNotifyWait.localNotifyIdx = localNotifyIdx;
    event.params.channelNotifyWait.timeOut = timeOut;
    return ExecutePodApi(event, [&]() {
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodNotifyWait(*context.thread, *context.channel, localNotifyIdx, timeOut);
    });
}

int32_t HcommChannelFenceOnThreadPod(ThreadHandle thread, ChannelHandle channel)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_CHANNEL_FENCE);
    event.params.channelFence.thread = thread;
    event.params.channelFence.channel = channel;
    return ExecutePodApi(event, [&]() {
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, false, context));
        return HcommPodFence(*context.channel);
    });
}

int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeoutPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_WAIT_DEFAULT);
    event.params.channelNotifyWaitDefault.thread = thread;
    event.params.channelNotifyWaitDefault.channel = channel;
    event.params.channelNotifyWaitDefault.localNotifyIdx = localNotifyIdx;
    const uint32_t timeout = HcommPodGetNotifyWaitTimeout();
    event.params.channelNotifyWaitDefault.timeOut = timeout;
    return ExecutePodApi(event, [&]() {
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodNotifyWait(*context.thread, *context.channel, localNotifyIdx, timeout);
    });
}

int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeoutPod(ThreadHandle thread, uint32_t notifyIdx)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_THREAD_NOTIFY_WAIT_DEFAULT);
    event.params.threadNotifyWaitDefault.thread = thread;
    event.params.threadNotifyWaitDefault.notifyIdx = notifyIdx;
    const uint32_t timeout = HcommPodGetNotifyWaitTimeout();
    event.params.threadNotifyWaitDefault.timeOut = timeout;
    return ExecutePodApi(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(GetPodThread(thread, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(GetThreadNotifyId(*threadEntity, notifyIdx, notifyId));
        return HcommPodRtsqNotifyWait(*threadEntity->sqContextAddr, notifyId, timeout);
    });
}

int32_t HcommSetNotifyWaitTimeOutPod(float timeOut)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_SET_NOTIFY_WAIT_TIMEOUT);
    event.params.setNotifyWaitTimeOut.timeOut = timeOut;
    return ExecutePodApi(event, [&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(ConvertTimeout(timeOut, timeoutSeconds));
        HcommPodSetNotifyWaitTimeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t HcommThreadResAcquireTimeOutPod(float timeOut)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_THREAD_RES_ACQUIRE_TIMEOUT);
    event.params.threadResAcquireTimeOut.timeOut = timeOut;
    return ExecutePodApi(event, [&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(ConvertTimeout(timeOut, timeoutSeconds));
        HcommPodSetSqFullTimeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t HcommChannelDrainOnThreadPod(ThreadHandle thread, ChannelHandle channel)
{
    HcommPodDfxEvent event = CreatePodDfxEvent(HCOMM_POD_DFX_API_CHANNEL_DRAIN);
    event.params.channelDrain.thread = thread;
    event.params.channelDrain.channel = channel;
    return ExecutePodApi(event, [&]() {
        PodContext context{};
        CHK_RET(GetPodContext(thread, channel, true, context));
        return HcommPodDrain(*context.thread, *context.channel);
    });
}
