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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "securec.h"

#include "../common/hcomm_host_utils.h"
#include "../common/hcomm_multi_channel_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    SqContext sqContext;
    MultiChannelRemoteInfo remoteInfo;
} ChannelMetadata;

typedef enum {
    HCOMM_CHANNEL_STATUS_READY = 0,
    HCOMM_CHANNEL_STATUS_CONNECTING = 1,
    HCOMM_CHANNEL_STATUS_FAILED = 2,
    HCOMM_CHANNEL_STATUS_TIMEOUT = 3,
    HCOMM_CHANNEL_STATUS_RES_LOC_UNAVAIL = 4,
    HCOMM_CHANNEL_STATUS_RES_RMT_UNAVAIL = 5,
} HcommChannelStatus;

static const uint32_t HCOMM_CHANNEL_READY_RETRY_COUNT = 120000U;
static const uint32_t HCOMM_CHANNEL_READY_RETRY_INTERVAL_US = 1000U;

static inline HcclResult HcommAcquireSharedChannelsWithComm(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    ChannelHandle* channels)
{
    HcommHostChannelConfig config = NULL;
    HcclResult ret = DlHcommApi::HcclChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigCreate failed, ret[%d].\n", __func__, ret);
        return ret;
    }

    ret = DlHcommApi::HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue config failed, ret[%d].\n", __func__, ret);
    }

    if (ret == HCCL_SUCCESS) {
        ret = DlHcommApi::HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedQueueTag);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] set shared queue tag failed, ret[%d].\n", __func__, ret);
        }
    }

    if (ret == HCCL_SUCCESS) {
        ret = DlHcommApi::HcclChannelAcquireWithConfig(comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] HcclChannelAcquireWithConfig failed, ret[%d].\n", __func__, ret);
        }
    }

    HcclResult destroyRet = DlHcommApi::HcclChannelConfigDestroy(config);
    if (destroyRet != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigDestroy failed, ret[%d].\n", __func__, destroyRet);
        if (ret == HCCL_SUCCESS) {
            ret = destroyRet;
        }
    }
    return ret;
}

static inline HcommResult HcommCreateSharedChannels(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum, ChannelHandle* channels,
    int* channelsCreated)
{
    HcommHostChannelConfig config = NULL;
    HcommResult ret = DlHcommApi::HcommChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcommChannelConfigCreate failed, ret[%d].\n", __func__, ret);
        return ret;
    }

    ret = DlHcommApi::HcommChannelConfigSetInt(config, HCOMM_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue config failed, ret[%d].\n", __func__, ret);
    }

    if (ret == HCCL_SUCCESS) {
        ret = DlHcommApi::HcommChannelCreateWithConfig(
            endpointHandle, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] HcommChannelCreateWithConfig failed, ret[%d].\n", __func__, ret);
        } else {
            *channelsCreated = 1;
        }
    }

    HcommResult destroyRet = DlHcommApi::HcommChannelConfigDestroy(config);
    if (destroyRet != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcommChannelConfigDestroy failed, ret[%d].\n", __func__, destroyRet);
        if (ret == HCCL_SUCCESS) {
            ret = destroyRet;
        }
    }
    return ret;
}

static inline HcommResult HcommWaitChannelsReady(const ChannelHandle* channels, uint32_t channelNum)
{
    int32_t* statuses = (int32_t*)calloc(channelNum, sizeof(int32_t));
    if (statuses == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel status array failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }

    for (uint32_t retry = 0U; retry < HCOMM_CHANNEL_READY_RETRY_COUNT; ++retry) {
        HcommResult ret = DlHcommApi::HcommChannelGetStatus(channels, channelNum, statuses);
        if (ret != HCCL_SUCCESS && ret != HCCL_E_AGAIN) {
            fprintf(stderr, "[ERROR] [%s] HcommChannelGetStatus failed, ret[%d].\n", __func__, ret);
            free(statuses);
            return ret;
        }
        if (ret == HCCL_E_AGAIN) {
            (void)usleep(HCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
            continue;
        }

        int allReady = 1;
        for (uint32_t i = 0U; i < channelNum; ++i) {
            if (statuses[i] == HCOMM_CHANNEL_STATUS_FAILED) {
                fprintf(stderr, "[ERROR] [%s] channel[%u] connection failed.\n", __func__, i);
                free(statuses);
                return HCCL_E_INTERNAL;
            }
            if (statuses[i] == HCOMM_CHANNEL_STATUS_TIMEOUT) {
                fprintf(stderr, "[ERROR] [%s] channel[%u] connection timed out.\n", __func__, i);
                free(statuses);
                return HCCL_E_TIMEOUT;
            }
            if (statuses[i] == HCOMM_CHANNEL_STATUS_RES_LOC_UNAVAIL ||
                statuses[i] == HCOMM_CHANNEL_STATUS_RES_RMT_UNAVAIL) {
                fprintf(
                    stderr, "[ERROR] [%s] channel[%u] resource unavailable, status[%d].\n", __func__, i, statuses[i]);
                free(statuses);
                return HCCL_E_UNAVAIL;
            }
            if (statuses[i] != HCOMM_CHANNEL_STATUS_READY) {
                allReady = 0;
            }
        }
        if (allReady != 0) {
            free(statuses);
            return HCCL_SUCCESS;
        }
        (void)usleep(HCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
    }

    fprintf(stderr, "[ERROR] [%s] waiting for channels timed out.\n", __func__);
    free(statuses);
    return HCCL_E_TIMEOUT;
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

static inline HcclResult HcommPrepareMultiChannelEntity(
    const ChannelHandle* channels, uint32_t channelNum, int ownsChannels, void** hostMemory, size_t* totalSize)
{
    ChannelMetadata currentMetadata;
    MultiChannelEntity* entity = NULL;
    MultiChannelRemoteInfo* channelInfos = NULL;
    HcclResult ret = HCCL_SUCCESS;

    if ((size_t)channelNum > (SIZE_MAX - sizeof(MultiChannelEntity)) / sizeof(MultiChannelRemoteInfo)) {
        fprintf(stderr, "[ERROR] [%s] multi-channel entity size overflow, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    *totalSize = sizeof(MultiChannelEntity) + (size_t)channelNum * sizeof(MultiChannelRemoteInfo);
    if (ownsChannels != 0) {
        if ((size_t)channelNum > (SIZE_MAX - *totalSize) / sizeof(ChannelHandle)) {
            fprintf(stderr, "[ERROR] [%s] owned channel array size overflow, channelNum[%u].\n", __func__, channelNum);
            return HCCL_E_MEMORY;
        }
        *totalSize += (size_t)channelNum * sizeof(ChannelHandle);
    }

    *hostMemory = calloc(1U, *totalSize);
    if (*hostMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate host staging memory failed.\n", __func__);
        return HCCL_E_MEMORY;
    }
    entity = (MultiChannelEntity*)*hostMemory;
    channelInfos = (MultiChannelRemoteInfo*)((uint8_t*)*hostMemory + sizeof(MultiChannelEntity));
    for (uint32_t i = 0U; i < channelNum; ++i) {
        ret = HcommExtractChannelMetadata(channels[i], i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            free(*hostMemory);
            *hostMemory = NULL;
            return ret;
        }
        channelInfos[i] = currentMetadata.remoteInfo;
    }

    entity->channelHandle = channels[0];
    entity->channelNum = channelNum;
    if (ownsChannels != 0) {
        void* ownedChannels = (uint8_t*)channelInfos + (size_t)channelNum * sizeof(MultiChannelRemoteInfo);
        errno_t secureRet = memcpy_s(
            ownedChannels, (size_t)channelNum * sizeof(ChannelHandle), channels,
            (size_t)channelNum * sizeof(ChannelHandle));
        if (secureRet != EOK) {
            fprintf(stderr, "[ERROR] [%s] copy owned channel handles failed, ret[%d].\n", __func__, secureRet);
            free(*hostMemory);
            *hostMemory = NULL;
            return HCCL_E_INTERNAL;
        }
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HcommBuildMultiChannelEntityWithComm(
    HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag, uint32_t channelNum, void** deviceMemory)
{
    void* hostMemory = NULL;
    size_t totalSize = 0U;
    HcclResult ret = HcommPrepareMultiChannelEntity(channels, channelNum, 0, &hostMemory, &totalSize);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    ret = DlHcommApi::HcclEngineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].\n", __func__, ret);
        free(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    MultiChannelEntity* entity = (MultiChannelEntity*)hostMemory;
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    ret = DlHcommApi::HcclEngineCtxCopy(comm, COMM_ENGINE_AIV, sharedQueueTag, hostMemory, totalSize, 0U);
    free(hostMemory);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCopy failed, ret[%d].\n", __func__, ret);
        (void)DlHcommApi::HcclEngineCtxDestroy(comm, sharedQueueTag, COMM_ENGINE_AIV);
        *deviceMemory = NULL;
        return ret;
    }
    return HCCL_SUCCESS;
}

static inline HcommResult HcommBuildMultiChannelEntity(
    const ChannelHandle* channels, uint32_t channelNum, void** deviceMemory)
{
    void* hostMemory = NULL;
    size_t totalSize = 0U;
    HcclResult prepareRet = HcommPrepareMultiChannelEntity(channels, channelNum, 1, &hostMemory, &totalSize);
    if (prepareRet != HCCL_SUCCESS) {
        return prepareRet;
    }

    aclError aclRet = aclrtMalloc(deviceMemory, totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] aclrtMalloc failed, ret[%d].\n", __func__, aclRet);
        free(hostMemory);
        return aclRet == ACL_SUCCESS ? HCCL_E_MEMORY : HCCL_E_RUNTIME;
    }
    MultiChannelEntity* entity = (MultiChannelEntity*)hostMemory;
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    aclRet = aclrtMemcpy(*deviceMemory, totalSize, hostMemory, totalSize, ACL_MEMCPY_HOST_TO_DEVICE);
    free(hostMemory);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] copy multi-channel entity to device failed, ret[%d].\n", __func__, aclRet);
        aclError freeRet = aclrtFree(*deviceMemory);
        if (freeRet != ACL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] cleanup aclrtFree failed, ret[%d].\n", __func__, freeRet);
        }
        *deviceMemory = NULL;
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HcommHostMakeMultiChannelHandleWithComm(
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
    ret = DlHcommApi::LoadHcclApi();
    if (ret != HCCL_SUCCESS) {
        free(channels);
        return ret;
    }
    ret = HcommAcquireSharedChannelsWithComm(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntityWithComm(comm, channels, sharedQueueTag, channelNum, &deviceMemory);
    }
    free(channels);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    return HCCL_SUCCESS;
}

static inline HcommResult HcommHostMakeMultiChannelHandle(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ChannelHandle* channels;
    void* deviceMemory = NULL;
    HcommResult ret;
    int channelsCreated = 0;

    if (multiChannel == NULL) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }
    *multiChannel = 0U;
    if (endpointHandle == NULL || channelDescs == NULL || channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }

    channels = (ChannelHandle*)calloc(channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    ret = DlHcommApi::LoadHcommApi();
    if (ret != HCCL_SUCCESS) {
        free(channels);
        return ret;
    }
    ret = HcommCreateSharedChannels(endpointHandle, channelDescs, channelNum, channels, &channelsCreated);
    if (ret == HCCL_SUCCESS) {
        ret = HcommWaitChannelsReady(channels, channelNum);
    }
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntity(channels, channelNum, &deviceMemory);
    }
    if (ret != HCCL_SUCCESS && channelsCreated != 0) {
        HcommResult destroyRet = DlHcommApi::HcommChannelDestroy(channels, channelNum);
        if (destroyRet != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] cleanup HcommChannelDestroy failed, ret[%d].\n", __func__, destroyRet);
        }
    }
    free(channels);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    return HCCL_SUCCESS;
}

static inline HcommResult HcommHostDestroyMultiChannelHandle(MultiChannelHandle multiChannel)
{
    MultiChannelEntity entity;
    ChannelHandle* channels = NULL;
    HcommResult ret;
    aclError aclRet;

    if (multiChannel == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid multi-channel handle.\n", __func__);
        return HCCL_E_PTR;
    }
    aclRet = aclrtMemcpy(
        &entity, sizeof(entity), (const void*)(uintptr_t)multiChannel, sizeof(entity), ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] copy multi-channel entity from device failed, ret[%d].\n", __func__, aclRet);
        return HCCL_E_RUNTIME;
    }
    if (entity.channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid channel count in multi-channel handle.\n", __func__);
        return HCCL_E_PARA;
    }
    if ((size_t)entity.channelNum > (SIZE_MAX - sizeof(MultiChannelEntity)) / sizeof(MultiChannelRemoteInfo)) {
        fprintf(stderr, "[ERROR] [%s] invalid channelNum[%u] in multi-channel entity.\n", __func__, entity.channelNum);
        return HCCL_E_INTERNAL;
    }
    size_t channelOffset = sizeof(MultiChannelEntity) + (size_t)entity.channelNum * sizeof(MultiChannelRemoteInfo);
    if ((uint64_t)multiChannel > (uint64_t)UINTPTR_MAX - channelOffset) {
        fprintf(stderr, "[ERROR] [%s] owned channel address overflow.\n", __func__);
        return HCCL_E_INTERNAL;
    }

    channels = (ChannelHandle*)calloc(entity.channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, entity.channelNum);
        return HCCL_E_MEMORY;
    }
    aclRet = aclrtMemcpy(
        channels, (size_t)entity.channelNum * sizeof(ChannelHandle),
        (const void*)((uintptr_t)multiChannel + channelOffset), (size_t)entity.channelNum * sizeof(ChannelHandle),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] copy owned channel handles from device failed, ret[%d].\n", __func__, aclRet);
        free(channels);
        return HCCL_E_RUNTIME;
    }

    ret = DlHcommApi::LoadHcommApi();
    if (ret != HCCL_SUCCESS) {
        free(channels);
        return ret;
    }
    ret = DlHcommApi::HcommChannelDestroy(channels, entity.channelNum);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcommChannelDestroy failed, ret[%d].\n", __func__, ret);
    }
    aclRet = aclrtFree((void*)(uintptr_t)multiChannel);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] aclrtFree failed, ret[%d].\n", __func__, aclRet);
    }
    free(channels);
    return ret == HCCL_SUCCESS && aclRet != ACL_SUCCESS ? HCCL_E_RUNTIME : ret;
}

#ifdef __cplusplus
}
#endif

#endif // IMPL_ADV_API_DETAIL_HCOMM_HOST_HCOMM_HOST_IMPL_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H
#endif
