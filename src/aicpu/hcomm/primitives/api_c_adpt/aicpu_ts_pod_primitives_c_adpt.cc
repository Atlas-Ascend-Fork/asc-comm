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
HcclResult validate_pod_entity(const Entity *entity, uint32_t version, uint32_t magicWord)
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

HcclResult get_pod_thread(ThreadHandle thread, bool registerThread, HcommThreadEntity *&threadEntity)
{
    CHK_PRT_RET(thread == 0, HCCL_ERROR("[%s] thread handle is 0.", __func__), HCCL_E_PTR);
    CHK_RET(static_cast<HcclResult>(HcommGetPodThreadResource(thread, &threadEntity)));
    CHK_RET(validate_pod_entity(threadEntity, HCOMM_POD_THREAD_VERSION, HCOMM_POD_THREAD_MAGIC_WORD));
    CHK_PTR_NULL(threadEntity->sqContextAddr);
    if (registerThread) {
        hcomm_pod_register_thread(thread);
    }
    return HCCL_SUCCESS;
}

HcclResult get_pod_context(ThreadHandle thread, ChannelHandle channel, bool registerThread, PodContext &context)
{
    CHK_PRT_RET(channel == 0, HCCL_ERROR("[%s] channel handle is 0.", __func__), HCCL_E_PTR);
    CHK_RET(get_pod_thread(thread, registerThread, context.thread));
    CHK_RET(static_cast<HcclResult>(HcommGetPodChannelResource(channel, &context.channel)));
    CHK_RET(validate_pod_entity(context.channel, HCOMM_POD_CHANNEL_VERSION, HCOMM_POD_CHANNEL_MAGIC_WORD));
    return HCCL_SUCCESS;
}

HcclResult get_thread_notify_id(const HcommThreadEntity &thread, uint32_t notifyIdx, uint32_t &notifyId)
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

HcclResult convert_reduce_in(HcommDataType dataType, HcommReduceOp reduceOp, Asc::PodDataType &innerDataType,
                           Asc::PodReduceOp &innerReduceOp, uint32_t &typeSize)
{
    switch (dataType) {
        case HCOMM_DATA_TYPE_INT8: innerDataType = Asc::PodDataType::INT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_INT16: innerDataType = Asc::PodDataType::INT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT32: innerDataType = Asc::PodDataType::INT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP16: innerDataType = Asc::PodDataType::FP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_FP32: innerDataType = Asc::PodDataType::FP32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_INT64: innerDataType = Asc::PodDataType::INT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT64: innerDataType = Asc::PodDataType::UINT64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_UINT8: innerDataType = Asc::PodDataType::UINT8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_UINT16: innerDataType = Asc::PodDataType::UINT16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_UINT32: innerDataType = Asc::PodDataType::UINT32; typeSize = 4; break;
        case HCOMM_DATA_TYPE_FP64: innerDataType = Asc::PodDataType::FP64; typeSize = 8; break;
        case HCOMM_DATA_TYPE_BFP16: innerDataType = Asc::PodDataType::BFP16; typeSize = 2; break;
        case HCOMM_DATA_TYPE_INT128: innerDataType = Asc::PodDataType::INT128; typeSize = 16; break;
#ifndef OPEN_BUILD_PROJECT
        case HCOMM_DATA_TYPE_HIF8: innerDataType = Asc::PodDataType::HIF8; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E4M3: innerDataType = Asc::PodDataType::FP8E4M3; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E5M2: innerDataType = Asc::PodDataType::FP8E5M2; typeSize = 1; break;
        case HCOMM_DATA_TYPE_FP8E8M0: innerDataType = Asc::PodDataType::FP8E8M0; typeSize = 1; break;
#endif
        default:
            HCCL_ERROR("[%s] dataType[%u] is not supported.", __func__, dataType);
            return HCCL_E_PARA;
    }
    switch (reduceOp) {
        case HCOMM_REDUCE_SUM: innerReduceOp = Asc::PodReduceOp::SUM; break;
        case HCOMM_REDUCE_PROD: innerReduceOp = Asc::PodReduceOp::PROD; break;
        case HCOMM_REDUCE_MAX: innerReduceOp = Asc::PodReduceOp::MAX; break;
        case HCOMM_REDUCE_MIN: innerReduceOp = Asc::PodReduceOp::MIN; break;
        default:
            HCCL_ERROR("[%s] reduceOp[%u] is not supported.", __func__, reduceOp);
            return HCCL_E_PARA;
    }
    return HCCL_SUCCESS;
}

HcclResult get_reduce_params(HcommDataType dataType, HcommReduceOp reduceOp, uint64_t count,
                           uint64_t &len, Asc::PodReduceIn &reduceIn)
{
    Asc::PodDataType innerDataType = Asc::PodDataType::INVALID;
    Asc::PodReduceOp innerReduceOp = Asc::PodReduceOp::INVALID;
    uint32_t typeSize = 0;
    CHK_RET(convert_reduce_in(dataType, reduceOp, innerDataType, innerReduceOp, typeSize));
    CHK_PRT_RET(count > std::numeric_limits<uint64_t>::max() / typeSize,
        HCCL_ERROR("[%s] reduce length overflow.", __func__), HCCL_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::PodReduceIn(innerDataType, innerReduceOp);
    return HCCL_SUCCESS;
}

HcommPodDfxEvent create_pod_dfx_event(HcommPodDfxApi api)
{
    HcommPodDfxEvent event{};
    event.abiHeader = {HCOMM_POD_DFX_VERSION, HCOMM_POD_DFX_MAGIC_WORD, sizeof(HcommPodDfxEvent), 0};
    event.api = api;
    event.stage = HCOMM_POD_DFX_STAGE_INVALID;
    return event;
}

void copy_pod_dfx_tag(const char *tag, char (&dst)[HCOMM_POD_DFX_TAG_MAX_LEN])
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

HcommPodDfxCallback get_pod_dfx_callback()
{
    static HcommPodDfxCallback callback = []() {
        HcommPodDfxCallback result = nullptr;
        return HcommGetPodDfxCallback(&result) == HCCL_SUCCESS ? result : nullptr;
    }();
    return callback;
}

void report_pod_dfx_event(HcommPodDfxEvent &event, HcommPodDfxStage stage, HcclResult result)
{
    HcommPodDfxCallback callback = get_pod_dfx_callback();
    if (callback == nullptr) {
        return;
    }
    event.stage = stage;
    event.result = result;
    callback(&event);
}

template <typename Operation>
HcclResult execute_pod_api(HcommPodDfxEvent &event, Operation operation)
{
    report_pod_dfx_event(event, HCOMM_POD_DFX_STAGE_START, HCCL_SUCCESS);
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = operation(), ret = HCCL_E_INTERNAL);
    const HcommPodDfxStage stage = ret == HCCL_SUCCESS ? HCOMM_POD_DFX_STAGE_SUCCESS : HCOMM_POD_DFX_STAGE_FAIL;
    report_pod_dfx_event(event, stage, ret);
    return ret;
}

HcclResult convert_timeout(float timeout, uint32_t &timeoutSeconds)
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

int32_t hcomm_batch_mode_start_pod(const char *batchTag)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_BATCH_MODE_START);
    copy_pod_dfx_tag(batchTag, event.params.batchModeStart.batchTag);
    return execute_pod_api(event, [&]() {
        hcomm_pod_set_batch_launch_mode(true);
        return HCCL_SUCCESS;
    });
}

int32_t hcomm_batch_mode_end_pod(const char *batchTag)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_BATCH_MODE_END);
    copy_pod_dfx_tag(batchTag, event.params.batchModeEnd.batchTag);
    return execute_pod_api(event, [&]() {
        hcomm_pod_set_batch_launch_mode(false);
        const HcclResult ret = hcomm_pod_launch_registered_threads();
        hcomm_pod_clear_registered_threads();
        return ret;
    });
}

int32_t hcomm_local_copy_on_thread_pod(ThreadHandle thread, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_LOCAL_COPY);
    event.params.localCopy.thread = thread;
    event.params.localCopy.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.localCopy.src = reinterpret_cast<uintptr_t>(src);
    event.params.localCopy.length = len;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(hcomm_pod_rtsq_sdma_copy(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize)));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t hcomm_local_reduce_on_thread_pod(
    ThreadHandle thread, void *dst, const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_LOCAL_REDUCE);
    event.params.localReduce.thread = thread;
    event.params.localReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.localReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.localReduce.count = count;
    event.params.localReduce.dataType = dataType;
    event.params.localReduce.reduceOp = reduceOp;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::PodReduceIn reduceIn{Asc::PodDataType::INVALID, Asc::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        constexpr uint64_t SDMA_SEND_MAX_SIZE = 0x100000000ULL;
        uint64_t offset = 0;
        while (offset < len) {
            const uint64_t remaining = len - offset;
            const uint64_t realSize = remaining > SDMA_SEND_MAX_SIZE ? SDMA_SEND_MAX_SIZE : remaining;
            CHK_RET(hcomm_pod_rtsq_sdma_reduce(
                *threadEntity->sqContextAddr, reinterpret_cast<uintptr_t>(dst) + offset,
                reinterpret_cast<uintptr_t>(src) + offset, static_cast<uint32_t>(realSize), reduceIn));
            offset += realSize;
        }
        return HCCL_SUCCESS;
    });
}

int32_t hcomm_thread_notify_record_on_thread_pod(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_THREAD_NOTIFY_RECORD);
    event.params.threadNotifyRecord.thread = thread;
    event.params.threadNotifyRecord.dstThread = dstThread;
    event.params.threadNotifyRecord.dstNotifyIdx = dstNotifyIdx;
    return execute_pod_api(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        HcommThreadEntity *dstThreadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        CHK_RET(get_pod_thread(dstThread, false, dstThreadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*dstThreadEntity, dstNotifyIdx, notifyId));
        return hcomm_pod_rtsq_notify_record(*threadEntity->sqContextAddr, notifyId);
    });
}

int32_t hcomm_thread_notify_wait_on_thread_pod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_THREAD_NOTIFY_WAIT);
    event.params.threadNotifyWait.thread = thread;
    event.params.threadNotifyWait.notifyIdx = notifyIdx;
    event.params.threadNotifyWait.timeOut = timeOut;
    return execute_pod_api(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*threadEntity, notifyIdx, notifyId));
        return hcomm_pod_rtsq_notify_wait(*threadEntity->sqContextAddr, notifyId, timeOut);
    });
}

int32_t hcomm_aclrt_notify_record_on_thread_pod(ThreadHandle thread, uint64_t dstNotifyId)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_ACLRT_NOTIFY_RECORD);
    event.params.aclrtNotifyRecord.thread = thread;
    event.params.aclrtNotifyRecord.dstNotifyId = dstNotifyId;
    return execute_pod_api(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        return hcomm_pod_rtsq_notify_record(*threadEntity->sqContextAddr, static_cast<uint32_t>(dstNotifyId));
    });
}

int32_t hcomm_aclrt_notify_wait_on_thread_pod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_ACLRT_NOTIFY_WAIT);
    event.params.aclrtNotifyWait.thread = thread;
    event.params.aclrtNotifyWait.notifyId = notifyId;
    event.params.aclrtNotifyWait.timeOut = timeOut;
    return execute_pod_api(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        return hcomm_pod_rtsq_notify_wait(*threadEntity->sqContextAddr, static_cast<uint32_t>(notifyId), timeOut);
    });
}

int32_t hcomm_write_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_WRITE);
    event.params.write.thread = thread;
    event.params.write.channel = channel;
    event.params.write.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.write.src = reinterpret_cast<uintptr_t>(src);
    event.params.write.length = len;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_write(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len);
    });
}

int32_t hcomm_write_reduce_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_WRITE_REDUCE);
    event.params.writeReduce.thread = thread;
    event.params.writeReduce.channel = channel;
    event.params.writeReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeReduce.count = count;
    event.params.writeReduce.dataType = dataType;
    event.params.writeReduce.reduceOp = reduceOp;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::PodReduceIn reduceIn{Asc::PodDataType::INVALID, Asc::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_write_reduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn);
    });
}

int32_t hcomm_write_with_notify_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_WRITE_WITH_NOTIFY);
    event.params.writeWithNotify.thread = thread;
    event.params.writeWithNotify.channel = channel;
    event.params.writeWithNotify.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeWithNotify.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeWithNotify.length = len;
    event.params.writeWithNotify.remoteNotifyIdx = remoteNotifyIdx;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_write_with_notify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            remoteNotifyIdx);
    });
}

int32_t hcomm_write_reduce_with_notify_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_WRITE_REDUCE_WITH_NOTIFY);
    event.params.writeReduceWithNotify.thread = thread;
    event.params.writeReduceWithNotify.channel = channel;
    event.params.writeReduceWithNotify.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.writeReduceWithNotify.src = reinterpret_cast<uintptr_t>(src);
    event.params.writeReduceWithNotify.count = count;
    event.params.writeReduceWithNotify.dataType = dataType;
    event.params.writeReduceWithNotify.reduceOp = reduceOp;
    event.params.writeReduceWithNotify.remoteNotifyIdx = remoteNotifyIdx;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::PodReduceIn reduceIn{Asc::PodDataType::INVALID, Asc::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_write_reduce_with_notify(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), reinterpret_cast<uintptr_t>(dst), len,
            reduceIn, remoteNotifyIdx);
    });
}

int32_t hcomm_read_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_READ);
    event.params.read.thread = thread;
    event.params.read.channel = channel;
    event.params.read.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.read.src = reinterpret_cast<uintptr_t>(src);
    event.params.read.length = len;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_read(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t hcomm_read_reduce_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t count, HcommDataType dataType,
    HcommReduceOp reduceOp)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_READ_REDUCE);
    event.params.readReduce.thread = thread;
    event.params.readReduce.channel = channel;
    event.params.readReduce.dst = reinterpret_cast<uintptr_t>(dst);
    event.params.readReduce.src = reinterpret_cast<uintptr_t>(src);
    event.params.readReduce.count = count;
    event.params.readReduce.dataType = dataType;
    event.params.readReduce.reduceOp = reduceOp;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(dst);
        CHK_PTR_NULL(src);
        Asc::PodReduceIn reduceIn{Asc::PodDataType::INVALID, Asc::PodReduceOp::INVALID};
        uint64_t len = 0;
        CHK_RET(get_reduce_params(dataType, reduceOp, count, len, reduceIn));
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_read_reduce(
            *context.thread, *context.channel, reinterpret_cast<uintptr_t>(dst), reinterpret_cast<uintptr_t>(src), len,
            reduceIn);
    });
}

int32_t hcomm_batch_transfer_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_BATCH_TRANSFER);
    event.params.batchTransfer.thread = thread;
    event.params.batchTransfer.channel = channel;
    event.params.batchTransfer.transferDescNum = transferDescNum;
    return execute_pod_api(event, [&]() {
        CHK_PTR_NULL(transferDescs);
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_batch_transfer(*context.thread, *context.channel, transferDescs, transferDescNum);
    });
}

int32_t hcomm_channel_notify_record_on_thread_pod(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_RECORD);
    event.params.channelNotifyRecord.thread = thread;
    event.params.channelNotifyRecord.channel = channel;
    event.params.channelNotifyRecord.remoteNotifyIdx = remoteNotifyIdx;
    return execute_pod_api(event, [&]() {
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_notify_record(*context.thread, *context.channel, remoteNotifyIdx);
    });
}

int32_t hcomm_channel_notify_wait_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_WAIT);
    event.params.channelNotifyWait.thread = thread;
    event.params.channelNotifyWait.channel = channel;
    event.params.channelNotifyWait.localNotifyIdx = localNotifyIdx;
    event.params.channelNotifyWait.timeOut = timeOut;
    return execute_pod_api(event, [&]() {
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_notify_wait(*context.thread, *context.channel, localNotifyIdx, timeOut);
    });
}

int32_t hcomm_channel_fence_on_thread_pod(ThreadHandle thread, ChannelHandle channel)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_CHANNEL_FENCE);
    event.params.channelFence.thread = thread;
    event.params.channelFence.channel = channel;
    return execute_pod_api(event, [&]() {
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, false, context));
        return hcomm_pod_fence(*context.channel);
    });
}

int32_t hcomm_channel_notify_wait_on_thread_with_default_timeout_pod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_CHANNEL_NOTIFY_WAIT_DEFAULT);
    event.params.channelNotifyWaitDefault.thread = thread;
    event.params.channelNotifyWaitDefault.channel = channel;
    event.params.channelNotifyWaitDefault.localNotifyIdx = localNotifyIdx;
    const uint32_t timeout = hcomm_pod_get_notify_wait_timeout();
    event.params.channelNotifyWaitDefault.timeOut = timeout;
    return execute_pod_api(event, [&]() {
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_notify_wait(*context.thread, *context.channel, localNotifyIdx, timeout);
    });
}

int32_t hcomm_thread_notify_wait_on_thread_with_default_timeout_pod(ThreadHandle thread, uint32_t notifyIdx)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_THREAD_NOTIFY_WAIT_DEFAULT);
    event.params.threadNotifyWaitDefault.thread = thread;
    event.params.threadNotifyWaitDefault.notifyIdx = notifyIdx;
    const uint32_t timeout = hcomm_pod_get_notify_wait_timeout();
    event.params.threadNotifyWaitDefault.timeOut = timeout;
    return execute_pod_api(event, [&]() {
        HcommThreadEntity *threadEntity = nullptr;
        CHK_RET(get_pod_thread(thread, true, threadEntity));
        uint32_t notifyId = 0;
        CHK_RET(get_thread_notify_id(*threadEntity, notifyIdx, notifyId));
        return hcomm_pod_rtsq_notify_wait(*threadEntity->sqContextAddr, notifyId, timeout);
    });
}

int32_t hcomm_set_notify_wait_timeout_pod(float timeOut)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_SET_NOTIFY_WAIT_TIMEOUT);
    event.params.setNotifyWaitTimeOut.timeOut = timeOut;
    return execute_pod_api(event, [&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        hcomm_pod_set_notify_wait_timeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t hcomm_thread_res_acquire_timeout_pod(float timeOut)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_THREAD_RES_ACQUIRE_TIMEOUT);
    event.params.threadResAcquireTimeOut.timeOut = timeOut;
    return execute_pod_api(event, [&]() {
        uint32_t timeoutSeconds = 0;
        CHK_RET(convert_timeout(timeOut, timeoutSeconds));
        hcomm_pod_set_sq_full_timeout(timeoutSeconds);
        return HCCL_SUCCESS;
    });
}

int32_t hcomm_channel_drain_on_thread_pod(ThreadHandle thread, ChannelHandle channel)
{
    HcommPodDfxEvent event = create_pod_dfx_event(HCOMM_POD_DFX_API_CHANNEL_DRAIN);
    event.params.channelDrain.thread = thread;
    event.params.channelDrain.channel = channel;
    return execute_pod_api(event, [&]() {
        PodContext context{};
        CHK_RET(get_pod_context(thread, channel, true, context));
        return hcomm_pod_drain(*context.thread, *context.channel);
    });
}
