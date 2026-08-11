/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "log.h"
#include "exception_util.h"
#include "thread.h"
#include "stream_lite.h"
#include "ub_transport_lite_impl.h"
#include "aicpu_ts_channel_helper.h"
#include "hcomm_pod_resource.h"
#include "hcomm_pod_resource_converter.h"

using namespace hccl;

namespace {

// POD entity内的数组成员是裸指针，转换结果必须由库内持有，否则entity指向已释放内存。
struct PodThreadStorage {
    HcommThreadEntity entity{};
    std::vector<HcommNotify> notifies;
    HcommRtsqContext rtsq{};
};

struct PodChannelStorage {
    HcommChannelEntity entity{};
    std::vector<HcommNotify> localNotifies;
    std::vector<HcommNotify> remoteNotifies;
    std::vector<HcommProtectionInfo> localBuffers;
    std::vector<HcommProtectionInfo> remoteBuffers;
    std::vector<HcommSqContext> sqContexts;
};

std::mutex g_converterMutex;
std::unordered_map<ThreadHandle, std::unique_ptr<PodThreadStorage>> g_threadStorage;
std::unordered_map<ChannelHandle, std::unique_ptr<PodChannelStorage>> g_channelStorage;

bool IsPodThread(ThreadHandle thread)
{
    const auto *entity = reinterpret_cast<const HcommThreadEntity *>(static_cast<uintptr_t>(thread));
    return entity != nullptr && entity->abiHeader.version == HCOMM_POD_THREAD_VERSION &&
        entity->abiHeader.magicWord == HCOMM_POD_THREAD_MAGIC_WORD &&
        entity->abiHeader.size >= sizeof(HcommThreadEntity);
}

bool IsPodChannel(ChannelHandle channel)
{
    const auto *entity = reinterpret_cast<const HcommChannelEntity *>(static_cast<uintptr_t>(channel));
    return entity != nullptr && entity->abiHeader.version == HCOMM_POD_CHANNEL_VERSION &&
        entity->abiHeader.magicWord == HCOMM_POD_CHANNEL_MAGIC_WORD &&
        entity->abiHeader.size >= sizeof(HcommChannelEntity);
}

HcclResult BuildThreadStorage(ThreadHandle legacyThread, PodThreadStorage &storage)
{
    auto *thread = reinterpret_cast<Thread *>(static_cast<uintptr_t>(legacyThread));
    CHK_PTR_NULL(thread);
    CHK_PRT_RET(!thread->IsDeviceA5(),
        HCCL_ERROR("[%s] only A5 AICPU-TS threads can be converted to POD.", __func__), HCCL_E_NOT_SUPPORT);
    auto *stream = static_cast<Hccl::StreamLite *>(thread->GetStreamLitePtr());
    CHK_PTR_NULL(stream);
    Hccl::RtsqBase *rtsq = stream->GetRtsq();
    CHK_PTR_NULL(rtsq);

    storage.notifies.resize(thread->GetNotifyNum());
    for (uint32_t index = 0; index < storage.notifies.size(); ++index) {
        LocalNotify *notify = thread->GetNotify(index);
        CHK_PTR_NULL(notify);
        storage.notifies[index] = {};
        storage.notifies[index].notify.rma.notifyId = static_cast<int32_t>(notify->notifyId_);
        storage.notifies[index].notify.rma.size = sizeof(uint32_t);
    }

    CHK_RET(rtsq->ExportPodResource(storage.rtsq));

    storage.entity = {};
    storage.entity.abiHeader = {
        HCOMM_POD_THREAD_VERSION, HCOMM_POD_THREAD_MAGIC_WORD, sizeof(HcommThreadEntity), 0};
    storage.entity.devInfo.devId = storage.rtsq.devPhyId;
    storage.entity.localNotifyNum = static_cast<uint32_t>(storage.notifies.size());
    storage.entity.localNotifyAddr = storage.notifies.empty() ? nullptr : storage.notifies.data();
    storage.entity.sqContextAddr = &storage.rtsq;
    return HCCL_SUCCESS;
}

HcclResult BuildChannelStorage(ChannelHandle legacyChannel, PodChannelStorage &storage)
{
    const auto *ctx = reinterpret_cast<const HcommAicpuChannelCtx *>(static_cast<uintptr_t>(legacyChannel));
    CHK_PTR_NULL(ctx);
    CHK_PRT_RET(ctx->abiHeader.version != HCOMM_AICPU_CHANNEL_CTX_VERSION ||
        ctx->abiHeader.magicWord != HCOMM_AICPU_CHANNEL_CTX_MAGIC_WORD || ctx->deviceChannel == nullptr,
        HCCL_ERROR("[%s] invalid legacy AICPU channel context.", __func__), HCCL_E_PARA);
    CHK_PRT_RET(ctx->engine != COMM_ENGINE_AICPU_TS,
        HCCL_ERROR("[%s] channel engine[%d] is not AICPU-TS.", __func__, ctx->engine), HCCL_E_NOT_SUPPORT);
    CHK_PRT_RET(ctx->protocol != COMM_PROTOCOL_UBC_CTP && ctx->protocol != COMM_PROTOCOL_UBC_TP &&
        ctx->protocol != COMM_PROTOCOL_UB_MEM && ctx->protocol != COMM_PROTOCOL_UBOE &&
        ctx->protocol != COMM_PROTOCOL_UBG,
        HCCL_ERROR("[%s] channel protocol[%d] is not supported by the UB POD converter.", __func__, ctx->protocol),
        HCCL_E_NOT_SUPPORT);

    auto *transport = static_cast<Hccl::UbTransportLiteImpl *>(ctx->deviceChannel);
    CHK_PTR_NULL(transport);
    CHK_RET(transport->ExportPodResource(storage.entity, storage.localNotifies, storage.remoteNotifies,
        storage.localBuffers, storage.remoteBuffers, storage.sqContexts));
    CHK_PRT_RET(storage.sqContexts.empty(),
        HCCL_ERROR("[%s] exported sq context is empty.", __func__), HCCL_E_INTERNAL);
    storage.entity.abiHeader = {
        HCOMM_POD_CHANNEL_VERSION, HCOMM_POD_CHANNEL_MAGIC_WORD, sizeof(HcommChannelEntity), 0};
    storage.entity.engine = ctx->engine;
    storage.entity.protocol = ctx->protocol;
    storage.entity.devInfo.devId = storage.sqContexts[0].context.jfs.dieId;
    storage.entity.devInfo.dieId = static_cast<uint8_t>(storage.sqContexts[0].context.jfs.dieId);
    storage.entity.devInfo.funcId = static_cast<uint8_t>(storage.sqContexts[0].context.jfs.funcId);
    return HCCL_SUCCESS;
}

} // namespace

int32_t HcommThreadConvertToPod(ThreadHandle legacyThread, ThreadHandle *podThread)
{
    CHK_PTR_NULL(podThread);
    CHK_PRT_RET(legacyThread == 0, HCCL_ERROR("[%s] legacy thread is 0.", __func__), HCCL_E_PTR);
    if (IsPodThread(legacyThread)) {
        *podThread = legacyThread;
        return HCCL_SUCCESS;
    }

    std::lock_guard<std::mutex> lock(g_converterMutex);
    auto cached = g_threadStorage.find(legacyThread);
    if (cached != g_threadStorage.end()) {
        *podThread = reinterpret_cast<ThreadHandle>(&cached->second->entity);
        return HCCL_SUCCESS;
    }

    auto storage = std::make_unique<PodThreadStorage>();
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = BuildThreadStorage(legacyThread, *storage), ret = HCCL_E_INTERNAL);
    CHK_RET(ret);
    *podThread = reinterpret_cast<ThreadHandle>(&storage->entity);
    g_threadStorage.emplace(legacyThread, std::move(storage));
    return HCCL_SUCCESS;
}

int32_t HcommChannelConvertToPod(ChannelHandle legacyChannel, ChannelHandle *podChannel)
{
    CHK_PTR_NULL(podChannel);
    CHK_PRT_RET(legacyChannel == 0, HCCL_ERROR("[%s] legacy channel is 0.", __func__), HCCL_E_PTR);
    if (IsPodChannel(legacyChannel)) {
        *podChannel = legacyChannel;
        return HCCL_SUCCESS;
    }

    std::lock_guard<std::mutex> lock(g_converterMutex);
    auto cached = g_channelStorage.find(legacyChannel);
    if (cached != g_channelStorage.end()) {
        *podChannel = reinterpret_cast<ChannelHandle>(&cached->second->entity);
        return HCCL_SUCCESS;
    }

    auto storage = std::make_unique<PodChannelStorage>();
    HcclResult ret = HCCL_SUCCESS;
    EXCEPTION_CATCH(ret = BuildChannelStorage(legacyChannel, *storage), ret = HCCL_E_INTERNAL);
    CHK_RET(ret);
    *podChannel = reinterpret_cast<ChannelHandle>(&storage->entity);
    g_channelStorage.emplace(legacyChannel, std::move(storage));
    return HCCL_SUCCESS;
}
