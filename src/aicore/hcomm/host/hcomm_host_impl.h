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

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
    void* handle;
    HcommHcclChannelConfigCreateFunc channelConfigCreate;
    HcommHcclChannelConfigDestroyFunc channelConfigDestroy;
    HcommHcclChannelConfigSetIntFunc channelConfigSetInt;
    HcommHcclChannelConfigSetStrFunc channelConfigSetStr;
    HcommHcclChannelAcquireWithConfigFunc channelAcquireWithConfig;
    HcommHcclEngineCtxCreateFunc engineCtxCreate;
    HcommHcclEngineCtxCopyFunc engineCtxCopy;
    HcommHcclEngineCtxDestroyFunc engineCtxDestroy;
} HcommHcclApi;

static inline void HcommCloseHcclApi(HcommHcclApi* api)
{
    if (api == NULL) {
        return;
    }
    if (api->handle != NULL) {
        (void)dlclose(api->handle);
    }
    (void)memset(api, 0, sizeof(*api));
}

#define HCOMM_LOAD_HCCL_SYMBOL(api, member, type, symbol)                         \
    do {                                                                          \
        (void)dlerror();                                                          \
        (api)->member = (type)dlsym((api)->handle, (symbol));                     \
        const char* hcommDlError = dlerror();                                     \
        if ((api)->member == NULL || hcommDlError != NULL) {                      \
            fprintf(                                                              \
                stderr, "[ERROR] [%s] load %s failed: %s.\n", __func__, (symbol), \
                hcommDlError == NULL ? "symbol not found" : hcommDlError);        \
            HcommCloseHcclApi(api);                                               \
            return HCCL_E_NOT_SUPPORT;                                            \
        }                                                                         \
    } while (0)

static inline HcclResult HcommOpenHcclApi(HcommHcclApi* api)
{
    static const char* hcommLibraryName = "libhcomm.so";
    if (api == NULL) {
        return HCCL_E_PTR;
    }
    (void)memset(api, 0, sizeof(*api));
    api->handle = dlopen(hcommLibraryName, RTLD_NOW | RTLD_LOCAL);
    if (api->handle == NULL) {
        const char* error = dlerror();
        fprintf(
            stderr, "[ERROR] [%s] open %s failed: %s.\n", __func__, hcommLibraryName,
            error == NULL ? "unknown error" : error);
        return HCCL_E_NOT_SUPPORT;
    }

    HCOMM_LOAD_HCCL_SYMBOL(api, channelConfigCreate, HcommHcclChannelConfigCreateFunc, "HcclChannelConfigCreate");
    HCOMM_LOAD_HCCL_SYMBOL(api, channelConfigDestroy, HcommHcclChannelConfigDestroyFunc, "HcclChannelConfigDestroy");
    HCOMM_LOAD_HCCL_SYMBOL(api, channelConfigSetInt, HcommHcclChannelConfigSetIntFunc, "HcclChannelConfigSetInt");
    HCOMM_LOAD_HCCL_SYMBOL(api, channelConfigSetStr, HcommHcclChannelConfigSetStrFunc, "HcclChannelConfigSetStr");
    HCOMM_LOAD_HCCL_SYMBOL(
        api, channelAcquireWithConfig, HcommHcclChannelAcquireWithConfigFunc, "HcclChannelAcquireWithConfig");
    HCOMM_LOAD_HCCL_SYMBOL(api, engineCtxCreate, HcommHcclEngineCtxCreateFunc, "HcclEngineCtxCreate");
    HCOMM_LOAD_HCCL_SYMBOL(api, engineCtxCopy, HcommHcclEngineCtxCopyFunc, "HcclEngineCtxCopy");
    HCOMM_LOAD_HCCL_SYMBOL(api, engineCtxDestroy, HcommHcclEngineCtxDestroyFunc, "HcclEngineCtxDestroy");
    return HCCL_SUCCESS;
}

#undef HCOMM_LOAD_HCCL_SYMBOL

typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpId;
    uint64_t remoteEidLow;
    uint64_t remoteEidHigh;
} MultiChannelRemoteInfo;

typedef struct {
    SqContext sqContext;
    CqContext cqContext;
    uint32_t channelNum;
    uint64_t remoteInfoAddr;
} MultiChannelEntity;

typedef struct {
    SqContext sqContext;
    CqContext cqContext;
    MultiChannelRemoteInfo remoteInfo;
} ChannelMetadata;

static inline HcclResult HcommAcquireSharedChannels(
    const HcommHcclApi* hcclApi, HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs,
    uint32_t channelNum, ChannelHandle* channels)
{
    HcclChannelConfig config = NULL;
    HcclResult configRet;
    HcclResult ret = hcclApi->channelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigCreate failed, ret[%d].\n", __func__, ret);
        return ret;
    }

    ret = hcclApi->channelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue config failed, ret[%d].\n", __func__, ret);
    }
    if (ret == HCCL_SUCCESS) {
        ret = hcclApi->channelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedQueueTag);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] set shared queue tag failed, ret[%d].\n", __func__, ret);
        }
    }
    if (ret == HCCL_SUCCESS) {
        ret = hcclApi->channelAcquireWithConfig(comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] HcclChannelAcquireWithConfig failed, ret[%d].\n", __func__, ret);
        }
    }

    configRet = hcclApi->channelConfigDestroy(config);
    if (configRet != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigDestroy failed, ret[%d].\n", __func__, configRet);
    }
    return ret == HCCL_SUCCESS ? configRet : ret;
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
    ret = HcommCopyFromDevice(
        &metadata->cqContext, sizeof(metadata->cqContext), channelEntity.cqContextAddr, channelIndex, "CqContext");
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
    const HcommHcclApi* hcclApi, HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag,
    uint32_t channelNum, void** deviceMemory)
{
    ChannelMetadata anchorMetadata;
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
    uint32_t i;
    for (i = 0U; i < channelNum; ++i) {
        ret = HcommExtractChannelMetadata(channels[i], i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            free(hostMemory);
            return ret;
        }
        if (i == 0U) {
            anchorMetadata = currentMetadata;
        }
        channelInfos[i] = currentMetadata.remoteInfo;
    }

    entity->sqContext = anchorMetadata.sqContext;
    entity->cqContext = anchorMetadata.cqContext;
    entity->channelNum = channelNum;

    ret = hcclApi->engineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].\n", __func__, ret);
        free(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    ret = hcclApi->engineCtxCopy(comm, COMM_ENGINE_AIV, sharedQueueTag, hostMemory, totalSize, 0U);
    free(hostMemory);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCopy failed, ret[%d].\n", __func__, ret);
        (void)hcclApi->engineCtxDestroy(comm, sharedQueueTag, COMM_ENGINE_AIV);
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
    HcommHcclApi hcclApi;
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
    ret = HcommOpenHcclApi(&hcclApi);
    if (ret != HCCL_SUCCESS) {
        free(channels);
        return ret;
    }
    ret = HcommAcquireSharedChannels(&hcclApi, comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntity(&hcclApi, comm, channels, sharedQueueTag, channelNum, &deviceMemory);
    }
    HcommCloseHcclApi(&hcclApi);
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
