/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_HOST_HCOMM_HOST_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_HOST_HCOMM_HOST_IMPL_H

#include <dlfcn.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "securec.h"

typedef HcclResult (*HcommHcclChannelConfigCreateFunc)(HcclChannelConfig* config);
typedef HcclResult (*HcommHcclChannelConfigDestroyFunc)(HcclChannelConfig config);
typedef HcclResult (*HcommHcclChannelConfigSetIntFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, uint32_t value);
typedef HcclResult (*HcommHcclChannelConfigSetStrFunc)(
    HcclChannelConfig config, HcclChannelConfigType type, const char* value);
typedef HcclResult (*HcommHcclChannelAcquireWithConfigFunc)(
    HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    HcclChannelConfig config, ChannelHandle* channels);
typedef HcclResult (*HcommHcclEngineCtxCreateFunc)(
    HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx);
typedef HcclResult (*HcommHcclEngineCtxCopyFunc)(
    HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset);
typedef HcclResult (*HcommHcclEngineCtxDestroyFunc)(HcclComm comm, const char* ctxTag, CommEngine engine);

class DlHcclApi {
public:
    static HcclResult LoadLibrary()
    {
        HcclApi& api = GetApi();
        if (api.referenceCount != 0U) {
            ++api.referenceCount;
            return HCCL_SUCCESS;
        }

        api.handle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        if (api.handle == nullptr) {
            const char* error = dlerror();
            fprintf(
                stderr, "[ERROR] [%s] open libhcomm.so failed: %s.\n", __func__,
                error == nullptr ? "unknown error" : error);
            return HCCL_E_NOT_SUPPORT;
        }

#define HCOMM_LOAD_HCCL_SYMBOL(member, type, symbol)                            \
    do {                                                                        \
        (void)dlerror();                                                        \
        api.member = reinterpret_cast<type>(dlsym(api.handle, symbol));         \
        const char* hcommDlError = dlerror();                                   \
        if (api.member == nullptr || hcommDlError != nullptr) {                 \
            fprintf(                                                            \
                stderr, "[ERROR] [%s] load %s failed: %s.\n", __func__, symbol, \
                hcommDlError == nullptr ? "symbol not found" : hcommDlError);   \
            CloseLibrary(api);                                                  \
            return HCCL_E_NOT_SUPPORT;                                          \
        }                                                                       \
    } while (0)

        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigCreate, HcommHcclChannelConfigCreateFunc, "HcclChannelConfigCreate");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigDestroy, HcommHcclChannelConfigDestroyFunc, "HcclChannelConfigDestroy");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigSetInt, HcommHcclChannelConfigSetIntFunc, "HcclChannelConfigSetInt");
        HCOMM_LOAD_HCCL_SYMBOL(HcclChannelConfigSetStr, HcommHcclChannelConfigSetStrFunc, "HcclChannelConfigSetStr");
        HCOMM_LOAD_HCCL_SYMBOL(
            HcclChannelAcquireWithConfig, HcommHcclChannelAcquireWithConfigFunc, "HcclChannelAcquireWithConfig");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxCreate, HcommHcclEngineCtxCreateFunc, "HcclEngineCtxCreate");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxCopy, HcommHcclEngineCtxCopyFunc, "HcclEngineCtxCopy");
        HCOMM_LOAD_HCCL_SYMBOL(HcclEngineCtxDestroy, HcommHcclEngineCtxDestroyFunc, "HcclEngineCtxDestroy");

#undef HCOMM_LOAD_HCCL_SYMBOL

        api.referenceCount = 1U;
        return HCCL_SUCCESS;
    }

    static void CleanupLibrary()
    {
        HcclApi& api = GetApi();
        if (api.referenceCount == 0U) {
            return;
        }
        --api.referenceCount;
        if (api.referenceCount == 0U) {
            CloseLibrary(api);
        }
    }

    static inline HcclResult HcclChannelConfigCreate(HcclChannelConfig* config)
    {
        return GetApi().HcclChannelConfigCreate(config);
    }

    static inline HcclResult HcclChannelConfigDestroy(HcclChannelConfig config)
    {
        return GetApi().HcclChannelConfigDestroy(config);
    }

    static inline HcclResult HcclChannelConfigSetInt(
        HcclChannelConfig config, HcclChannelConfigType type, uint32_t value)
    {
        return GetApi().HcclChannelConfigSetInt(config, type, value);
    }

    static inline HcclResult HcclChannelConfigSetStr(
        HcclChannelConfig config, HcclChannelConfigType type, const char* value)
    {
        return GetApi().HcclChannelConfigSetStr(config, type, value);
    }

    static inline HcclResult HcclChannelAcquireWithConfig(
        HcclComm comm, CommEngine engine, const HcclChannelDesc* channelDescs, uint32_t channelNum,
        HcclChannelConfig config, ChannelHandle* channels)
    {
        return GetApi().HcclChannelAcquireWithConfig(comm, engine, channelDescs, channelNum, config, channels);
    }

    static inline HcclResult HcclEngineCtxCreate(
        HcclComm comm, const char* ctxTag, CommEngine engine, uint64_t size, void** ctx)
    {
        return GetApi().HcclEngineCtxCreate(comm, ctxTag, engine, size, ctx);
    }

    static inline HcclResult HcclEngineCtxCopy(
        HcclComm comm, CommEngine engine, const char* ctxTag, const void* srcCtx, uint64_t size, uint64_t dstCtxOffset)
    {
        return GetApi().HcclEngineCtxCopy(comm, engine, ctxTag, srcCtx, size, dstCtxOffset);
    }

    static inline HcclResult HcclEngineCtxDestroy(HcclComm comm, const char* ctxTag, CommEngine engine)
    {
        return GetApi().HcclEngineCtxDestroy(comm, ctxTag, engine);
    }

private:
    struct HcclApi {
        void* handle = nullptr;
        uint32_t referenceCount = 0U;
        HcommHcclChannelConfigCreateFunc HcclChannelConfigCreate = nullptr;
        HcommHcclChannelConfigDestroyFunc HcclChannelConfigDestroy = nullptr;
        HcommHcclChannelConfigSetIntFunc HcclChannelConfigSetInt = nullptr;
        HcommHcclChannelConfigSetStrFunc HcclChannelConfigSetStr = nullptr;
        HcommHcclChannelAcquireWithConfigFunc HcclChannelAcquireWithConfig = nullptr;
        HcommHcclEngineCtxCreateFunc HcclEngineCtxCreate = nullptr;
        HcommHcclEngineCtxCopyFunc HcclEngineCtxCopy = nullptr;
        HcommHcclEngineCtxDestroyFunc HcclEngineCtxDestroy = nullptr;
    };

    static HcclApi& GetApi()
    {
        static thread_local HcclApi api;
        return api;
    }

    static void CloseLibrary(HcclApi& api)
    {
        if (api.handle != nullptr) {
            (void)dlclose(api.handle);
        }
        api = HcclApi{};
    }
};

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpId;
    uint64_t remoteEidLow;
    uint64_t remoteEidHigh;
} MultiChannelRemoteInfo;

typedef struct {
    ChannelHandle channelHandle;
    uint32_t channelNum;
    uint32_t reserved;
    uint64_t remoteInfoAddr;
} MultiChannelEntity;

typedef struct {
    SqContext sqContext;
    MultiChannelRemoteInfo remoteInfo;
} ChannelMetadata;

static inline HcclResult HcommDestroyChannelConfig(HcclChannelConfig config, HcclResult primaryRet)
{
    HcclResult ret = DlHcclApi::HcclChannelConfigDestroy(config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigDestroy failed, ret[%d].\n", __func__, ret);
    }
    return primaryRet == HCCL_SUCCESS ? ret : primaryRet;
}

static inline HcclResult HcommAcquireSharedChannels(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    ChannelHandle* channels)
{
    HcclChannelConfig config = NULL;
    HcclResult ret = DlHcclApi::HcclChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigCreate failed, ret[%d].\n", __func__, ret);
        return ret;
    }

    ret = DlHcclApi::HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue config failed, ret[%d].\n", __func__, ret);
        return HcommDestroyChannelConfig(config, ret);
    }

    ret = DlHcclApi::HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedQueueTag);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue tag failed, ret[%d].\n", __func__, ret);
        return HcommDestroyChannelConfig(config, ret);
    }

    ret = DlHcclApi::HcclChannelAcquireWithConfig(comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelAcquireWithConfig failed, ret[%d].\n", __func__, ret);
        return HcommDestroyChannelConfig(config, ret);
    }
    return HcommDestroyChannelConfig(config, HCCL_SUCCESS);
}

static inline HcclResult HcommCopyFromDevice(
    void* dst, size_t size, const void* src, uint32_t channelIndex, const char* dataName)
{
    aclError aclRet = aclrtMemcpy(dst, size, src, size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        fprintf(
            stderr, "[ERROR] [%s] copy channel[%u] %s from device failed, ret[%d].\n", __func__, channelIndex, dataName,
            aclRet);
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HcommExtractChannelMetadata(
    ChannelHandle channel, uint32_t channelIndex, ChannelMetadata* metadata)
{
    ChannelEntity channelEntity;
    HcclResult ret;

    if (channel == 0U || metadata == NULL) {
        fprintf(stderr, "[ERROR] [%s] invalid channel[%u].\n", __func__, channelIndex);
        return HCCL_E_PTR;
    }

    ret = HcommCopyFromDevice(
        &channelEntity, sizeof(channelEntity), (const void*)(uintptr_t)channel, channelIndex, "ChannelEntity");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (channelEntity.sqContextAddr == NULL || channelEntity.cqContextAddr == NULL ||
        channelEntity.remoteBufferAddr == NULL || channelEntity.remoteBufferNum == 0U) {
        fprintf(
            stderr, "[ERROR] [%s] channel[%u] has invalid SQ/CQ context or remote MR table.\n", __func__, channelIndex);
        return HCCL_E_NOT_SUPPORT;
    }

    ret = HcommCopyFromDevice(
        &metadata->sqContext, sizeof(metadata->sqContext), channelEntity.sqContextAddr, channelIndex, "SqContext");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    metadata->remoteInfo.remoteBufferAddr = (uint64_t)(uintptr_t)channelEntity.remoteBufferAddr;
    metadata->remoteInfo.remoteBufferNum = channelEntity.remoteBufferNum;
    metadata->remoteInfo.tpId = metadata->sqContext.contextInfo.ubJfs.tpID;
    uint64_t remoteEid[2];
    errno_t secureRet =
        memcpy_s(remoteEid, sizeof(remoteEid), metadata->sqContext.contextInfo.ubJfs.remoteEID, sizeof(remoteEid));
    if (secureRet != EOK) {
        fprintf(
            stderr, "[ERROR] [%s] copy channel[%u] remote EID failed, ret[%d].\n", __func__, channelIndex, secureRet);
        return HCCL_E_INTERNAL;
    }
    metadata->remoteInfo.remoteEidLow = remoteEid[0];
    metadata->remoteInfo.remoteEidHigh = remoteEid[1];

    return HCCL_SUCCESS;
}

static inline HcclResult HcommBuildMultiChannelEntity(
    HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag, uint32_t channelNum, void** deviceMemory)
{
    ChannelMetadata currentMetadata;
    MultiChannelEntity* entity = NULL;
    MultiChannelRemoteInfo* channelInfos = NULL;
    void* hostMemory = NULL;
    size_t totalSize;
    HcclResult ret = HCCL_SUCCESS;

    totalSize = sizeof(MultiChannelEntity) + (size_t)channelNum * sizeof(MultiChannelRemoteInfo);

    hostMemory = calloc(1U, totalSize);
    if (hostMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate host staging memory failed.\n", __func__);
        return HCCL_E_MEMORY;
    }
    entity = (MultiChannelEntity*)hostMemory;
    channelInfos = (MultiChannelRemoteInfo*)((uint8_t*)hostMemory + sizeof(MultiChannelEntity));
    for (uint32_t i = 0U; i < channelNum; ++i) {
        ret = HcommExtractChannelMetadata(channels[i], i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            free(hostMemory);
            return ret;
        }
        channelInfos[i] = currentMetadata.remoteInfo;
    }

    entity->channelHandle = channels[0];
    entity->channelNum = channelNum;

    ret = DlHcclApi::HcclEngineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].\n", __func__, ret);
        free(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    ret = DlHcclApi::HcclEngineCtxCopy(comm, COMM_ENGINE_AIV, sharedQueueTag, hostMemory, totalSize, 0U);
    free(hostMemory);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCopy failed, ret[%d].\n", __func__, ret);
        (void)DlHcclApi::HcclEngineCtxDestroy(comm, sharedQueueTag, COMM_ENGINE_AIV);
        *deviceMemory = NULL;
        return ret;
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HcommHostMakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ChannelHandle* channels;
    void* deviceMemory = NULL;
    HcclResult ret;

    if (multiChannel == NULL) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }
    *multiChannel = 0U;
    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL || channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }

    channels = (ChannelHandle*)calloc(channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    ret = DlHcclApi::LoadLibrary();
    if (ret != HCCL_SUCCESS) {
        free(channels);
        return ret;
    }
    ret = HcommAcquireSharedChannels(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntity(comm, channels, sharedQueueTag, channelNum, &deviceMemory);
    }
    DlHcclApi::CleanupLibrary();
    free(channels);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    return HCCL_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif // IMPL_ADV_API_DETAIL_HCOMM_HOST_HCOMM_HOST_IMPL_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H
#endif
