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
#include <string.h>
#include <unistd.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res.h"
#include "hcomm/hcomm_res_entity_defs.h"

#include "../common/hcomm_host_utils.h"

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

template <typename ConfigType, typename ResultType>
static inline ResultType DestroyChannelConfig(
    ConfigType config, ResultType (*destroyFunc)(ConfigType), const char* configName)
{
    ResultType destroyRet = destroyFunc(config);
    if (destroyRet != static_cast<ResultType>(HCCL_SUCCESS)) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] %s failed, ret[%d].", __func__, configName, destroyRet);
    }
    return destroyRet;
}

static inline HcclResult HcclAcquireSharedChannelsWithComm(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    ChannelHandle* channels)
{
    HcclChannelConfig config = NULL;
    HcclResult ret = DlHcommApi::HcclChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcclChannelConfigCreate failed, ret[%d].", __func__, ret);
        return ret;
    }

    ret = DlHcommApi::HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue config failed, ret[%d].", __func__, ret);
        (void)DestroyChannelConfig(config, DlHcommApi::HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
        return ret;
    }

    ret = DlHcommApi::HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedQueueTag);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue tag failed, ret[%d].", __func__, ret);
        (void)DestroyChannelConfig(config, DlHcommApi::HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
        return ret;
    }

    ret = DlHcommApi::HcclChannelAcquireWithConfig(comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcclChannelAcquireWithConfig failed, ret[%d].", __func__, ret);
        (void)DestroyChannelConfig(config, DlHcommApi::HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
        return ret;
    }

    return DestroyChannelConfig(config, DlHcommApi::HcclChannelConfigDestroy, "HcclChannelConfigDestroy");
}

static inline HcommResult HcommCreateSharedChannels(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum, ChannelHandle* channels)
{
    HcommChannelConfig config = NULL;
    HcommResult ret = DlHcommApi::HcommChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcommChannelConfigCreate failed, ret[%d].", __func__, ret);
        return ret;
    }

    ret = DlHcommApi::HcommChannelConfigSetInt(config, HCOMM_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue config failed, ret[%d].", __func__, ret);
        (void)DestroyChannelConfig(config, DlHcommApi::HcommChannelConfigDestroy, "HcommChannelConfigDestroy");
        return ret;
    }

    ret = DlHcommApi::HcommChannelCreateWithConfig(
        endpointHandle, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcommChannelCreateWithConfig failed, ret[%d].", __func__, ret);
        (void)DestroyChannelConfig(config, DlHcommApi::HcommChannelConfigDestroy, "HcommChannelConfigDestroy");
        return ret;
    }

    return DestroyChannelConfig(config, DlHcommApi::HcommChannelConfigDestroy, "HcommChannelConfigDestroy");
}

static inline HcommResult HcommWaitChannelsReady(const ChannelHandle* channels, uint32_t channelNum)
{
    int32_t* statuses = NULL;
    aclError aclRet = aclrtMallocHost((void**)&statuses, (size_t)channelNum * sizeof(int32_t));
    if (aclRet != ACL_SUCCESS || statuses == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel status array failed, channelNum[%u], ret[%d].", __func__, channelNum,
            aclRet);
        if (statuses != NULL) {
            (void)aclrtFreeHost(statuses);
        }
        return HCCL_E_MEMORY;
    }

    for (uint32_t retry = 0U; retry < HCOMM_CHANNEL_READY_RETRY_COUNT; ++retry) {
        HcommResult ret = DlHcommApi::HcommChannelGetStatus(channels, channelNum, statuses);
        if (ret != HCCL_SUCCESS && ret != HCCL_E_AGAIN) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] HcommChannelGetStatus failed, ret[%d].", __func__, ret);
            (void)aclrtFreeHost(statuses);
            return ret;
        }
        if (ret == HCCL_E_AGAIN) {
            (void)usleep(HCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
            continue;
        }

        int allReady = 1;
        for (uint32_t i = 0U; i < channelNum; ++i) {
            if (statuses[i] == HCOMM_CHANNEL_STATUS_FAILED) {
                ASC_CPU_LOG_ERROR("[ERROR] [%s] channel[%u] connection failed.", __func__, i);
                (void)aclrtFreeHost(statuses);
                return HCCL_E_INTERNAL;
            }
            if (statuses[i] == HCOMM_CHANNEL_STATUS_TIMEOUT) {
                ASC_CPU_LOG_ERROR("[ERROR] [%s] channel[%u] connection timed out.", __func__, i);
                (void)aclrtFreeHost(statuses);
                return HCCL_E_TIMEOUT;
            }
            if (statuses[i] == HCOMM_CHANNEL_STATUS_RES_LOC_UNAVAIL ||
                statuses[i] == HCOMM_CHANNEL_STATUS_RES_RMT_UNAVAIL) {
                ASC_CPU_LOG_ERROR(
                    "[ERROR] [%s] channel[%u] resource unavailable, status[%d].", __func__, i, statuses[i]);
                (void)aclrtFreeHost(statuses);
                return HCCL_E_UNAVAIL;
            }
            if (statuses[i] != HCOMM_CHANNEL_STATUS_READY) {
                allReady = 0;
            }
        }
        if (allReady != 0) {
            (void)aclrtFreeHost(statuses);
            return HCCL_SUCCESS;
        }
        (void)usleep(HCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
    }

    ASC_CPU_LOG_ERROR("[ERROR] [%s] waiting for channels timed out.", __func__);
    (void)aclrtFreeHost(statuses);
    return HCCL_E_TIMEOUT;
}

static inline int32_t HcommCopyFromDevice(
    void* dst, size_t size, const void* src, uint32_t channelIndex, const char* dataName)
{
    aclError aclRet = aclrtMemcpy(dst, size, src, size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] copy channel[%u] %s from device failed, ret[%d].", __func__, channelIndex, dataName, aclRet);
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline int32_t HcommExtractChannelMetadata(
    ChannelHandle channel, uint32_t channelIndex, ChannelMetadata* metadata)
{
    ChannelEntity channelEntity;
    int32_t ret;

    ret = HcommCopyFromDevice(
        &channelEntity, sizeof(channelEntity), (const void*)(uintptr_t)channel, channelIndex, "ChannelEntity");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (channelEntity.sqContextAddr == NULL || channelEntity.cqContextAddr == NULL ||
        channelEntity.remoteBufferAddr == NULL || channelEntity.remoteBufferNum == 0U) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] channel[%u] has invalid SQ/CQ context or remote MR table.", __func__, channelIndex);
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
    aclError copyRet = aclrtMemcpy(
        remoteEid, sizeof(remoteEid), metadata->sqContext.contextInfo.ubJfs.remoteEID, sizeof(remoteEid),
        ACL_MEMCPY_HOST_TO_HOST);
    if (copyRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy channel[%u] remote EID failed, ret[%d].", __func__, channelIndex, copyRet);
        return HCCL_E_INTERNAL;
    }
    metadata->remoteInfo.remoteEidLow = remoteEid[0];
    metadata->remoteInfo.remoteEidHigh = remoteEid[1];

    return HCCL_SUCCESS;
}

static inline int32_t HcommPrepareMultiChannelEntity(
    const ChannelHandle* channels, uint32_t channelNum, int ownsChannels, void** hostMemory, size_t* totalSize)
{
    ChannelMetadata currentMetadata;
    MultiChannelEntity* entity = NULL;
    MultiChannelRemoteInfo* channelInfos = NULL;
    int32_t ret = HCCL_SUCCESS;

    *totalSize = sizeof(MultiChannelEntity) + (size_t)channelNum * sizeof(MultiChannelRemoteInfo);
    if (ownsChannels != 0) {
        if ((size_t)channelNum > (SIZE_MAX - *totalSize) / sizeof(ChannelHandle)) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] owned channel array size overflow, channelNum[%u].", __func__, channelNum);
            return HCCL_E_MEMORY;
        }
        *totalSize += (size_t)channelNum * sizeof(ChannelHandle);
    }

    aclError aclRet = aclrtMallocHost(hostMemory, *totalSize);
    if (aclRet != ACL_SUCCESS || *hostMemory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtMallocHost staging memory failed, ret[%d].", __func__, aclRet);
        if (*hostMemory != NULL) {
            (void)aclrtFreeHost(*hostMemory);
            *hostMemory = NULL;
        }
        return HCCL_E_MEMORY;
    }
    aclError memsetRet = aclrtMemset(*hostMemory, *totalSize, 0, *totalSize);
    if (memsetRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] initialize host staging memory failed, ret[%d].", __func__, memsetRet);
        (void)aclrtFreeHost(*hostMemory);
        *hostMemory = NULL;
        return HCCL_E_INTERNAL;
    }
    entity = (MultiChannelEntity*)*hostMemory;
    channelInfos = (MultiChannelRemoteInfo*)((uint8_t*)*hostMemory + sizeof(MultiChannelEntity));
    for (uint32_t i = 0U; i < channelNum; ++i) {
        ret = HcommExtractChannelMetadata(channels[i], i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            (void)aclrtFreeHost(*hostMemory);
            *hostMemory = NULL;
            return ret;
        }
        channelInfos[i] = currentMetadata.remoteInfo;
    }

    entity->channelHandle = channels[0];
    entity->channelNum = channelNum;
    if (ownsChannels != 0) {
        void* ownedChannels = (uint8_t*)channelInfos + (size_t)channelNum * sizeof(MultiChannelRemoteInfo);
        aclError copyRet = aclrtMemcpy(
            ownedChannels, (size_t)channelNum * sizeof(ChannelHandle), channels,
            (size_t)channelNum * sizeof(ChannelHandle), ACL_MEMCPY_HOST_TO_HOST);
        if (copyRet != ACL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] copy owned channel handles failed, ret[%d].", __func__, copyRet);
            (void)aclrtFreeHost(*hostMemory);
            *hostMemory = NULL;
            return HCCL_E_INTERNAL;
        }
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HcclBuildMultiChannelEntityWithComm(
    HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag, uint32_t channelNum, void** deviceMemory)
{
    void* hostMemory = NULL;
    size_t totalSize = 0U;
    int32_t prepareRet = HcommPrepareMultiChannelEntity(channels, channelNum, 0, &hostMemory, &totalSize);
    if (prepareRet != HCCL_SUCCESS) {
        return static_cast<HcclResult>(prepareRet);
    }

    HcclResult ret = DlHcommApi::HcclEngineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].", __func__, ret);
        (void)aclrtFreeHost(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    MultiChannelEntity* entity = (MultiChannelEntity*)hostMemory;
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    ret = DlHcommApi::HcclEngineCtxCopy(comm, COMM_ENGINE_AIV, sharedQueueTag, hostMemory, totalSize, 0U);
    (void)aclrtFreeHost(hostMemory);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcclEngineCtxCopy failed, ret[%d].", __func__, ret);
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
    int32_t prepareRet = HcommPrepareMultiChannelEntity(channels, channelNum, 1, &hostMemory, &totalSize);
    if (prepareRet != HCCL_SUCCESS) {
        return static_cast<HcommResult>(prepareRet);
    }

    aclError aclRet = aclrtMalloc(deviceMemory, totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS || *deviceMemory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtMalloc failed, ret[%d].", __func__, aclRet);
        (void)aclrtFreeHost(hostMemory);
        return aclRet == ACL_SUCCESS ? HCCL_E_MEMORY : HCCL_E_RUNTIME;
    }
    MultiChannelEntity* entity = (MultiChannelEntity*)hostMemory;
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(MultiChannelEntity);
    aclRet = aclrtMemcpy(*deviceMemory, totalSize, hostMemory, totalSize, ACL_MEMCPY_HOST_TO_DEVICE);
    (void)aclrtFreeHost(hostMemory);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy multi-channel entity to device failed, ret[%d].", __func__, aclRet);
        aclError freeRet = aclrtFree(*deviceMemory);
        if (freeRet != ACL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] cleanup aclrtFree failed, ret[%d].", __func__, freeRet);
        }
        *deviceMemory = NULL;
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline HcclResult HostMakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ChannelHandle* channels = NULL;
    void* deviceMemory = NULL;
    HcclResult ret;
    aclError aclRet;

    if (multiChannel == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }
    *multiChannel = 0U;

    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL || channelNum == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }

    aclRet = aclrtMallocHost((void**)&channels, (size_t)channelNum * sizeof(ChannelHandle));
    if (aclRet != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel handles failed, channelNum[%u], ret[%d].", __func__, channelNum,
            aclRet);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    ret = DlHcommApi::LoadHcclApi();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = HcclAcquireSharedChannelsWithComm(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = HcclBuildMultiChannelEntityWithComm(comm, channels, sharedQueueTag, channelNum, &deviceMemory);
    }
    aclError freeRet = aclrtFreeHost(channels);
    if (freeRet != ACL_SUCCESS && ret == HCCL_SUCCESS) {
        ret = HCCL_E_RUNTIME;
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    return HCCL_SUCCESS;
}

static inline HcommResult HostMakeMultiChannelHandle(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ChannelHandle* channels = NULL;
    void* deviceMemory = NULL;
    HcommResult ret;
    aclError aclRet;

    if (multiChannel == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }
    *multiChannel = 0U;

    if (endpointHandle == NULL || channelDescs == NULL || channelNum == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }

    aclRet = aclrtMallocHost((void**)&channels, (size_t)channelNum * sizeof(ChannelHandle));
    if (aclRet != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost failed, channelNum[%u], ret[%d].", __func__, channelNum, aclRet);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    ret = DlHcommApi::LoadHcommApi();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = HcommCreateSharedChannels(endpointHandle, channelDescs, channelNum, channels);
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }

    ret = HcommWaitChannelsReady(channels, channelNum);
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntity(channels, channelNum, &deviceMemory);
    }
    if (ret != HCCL_SUCCESS) {
        HcommResult destroyRet = DlHcommApi::HcommChannelDestroy(channels, channelNum);
        if (destroyRet != HCCL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] cleanup HcommChannelDestroy failed, ret[%d].", __func__, destroyRet);
        }
    }
    aclRet = aclrtFreeHost(channels);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtFreeHost failed, ret[%d].", __func__, aclRet);
        if (ret == HCCL_SUCCESS) {
            ret = HCCL_E_RUNTIME;
        }
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    return HCCL_SUCCESS;
}

static inline HcommResult HostDestroyMultiChannelHandle(MultiChannelHandle multiChannel)
{
    MultiChannelEntity entity;
    ChannelHandle* channels = NULL;
    HcommResult ret;
    aclError aclRet;

    if (multiChannel == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid multi-channel handle.", __func__);
        return HCCL_E_PTR;
    }
    aclRet = aclrtMemcpy(
        &entity, sizeof(entity), (const void*)(uintptr_t)multiChannel, sizeof(entity), ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy multi-channel entity from device failed, ret[%d].", __func__, aclRet);
        return HCCL_E_RUNTIME;
    }
    if (entity.channelNum == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid channel count in multi-channel handle.", __func__);
        return HCCL_E_PARA;
    }
    if ((size_t)entity.channelNum > (SIZE_MAX - sizeof(MultiChannelEntity)) / sizeof(MultiChannelRemoteInfo)) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid channelNum[%u] in multi-channel entity.", __func__, entity.channelNum);
        return HCCL_E_INTERNAL;
    }
    size_t channelOffset = sizeof(MultiChannelEntity) + (size_t)entity.channelNum * sizeof(MultiChannelRemoteInfo);
    if ((uint64_t)multiChannel > (uint64_t)UINTPTR_MAX - channelOffset) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] owned channel address overflow.", __func__);
        return HCCL_E_INTERNAL;
    }

    aclRet = aclrtMallocHost((void**)&channels, (size_t)entity.channelNum * sizeof(ChannelHandle));
    if (aclRet != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel handles failed, channelNum[%u], ret[%d].", __func__,
            entity.channelNum, aclRet);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    aclRet = aclrtMemcpy(
        channels, (size_t)entity.channelNum * sizeof(ChannelHandle),
        (const void*)((uintptr_t)multiChannel + channelOffset), (size_t)entity.channelNum * sizeof(ChannelHandle),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy owned channel handles from device failed, ret[%d].", __func__, aclRet);
        (void)aclrtFreeHost(channels);
        return HCCL_E_RUNTIME;
    }

    ret = DlHcommApi::LoadHcommApi();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = DlHcommApi::HcommChannelDestroy(channels, entity.channelNum);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] HcommChannelDestroy failed, ret[%d].", __func__, ret);
    }
    aclRet = aclrtFree((void*)(uintptr_t)multiChannel);
    if (aclRet != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtFree failed, ret[%d].", __func__, aclRet);
    }
    (void)aclrtFreeHost(channels);
    return ret == HCCL_SUCCESS && aclRet != ACL_SUCCESS ? HCCL_E_RUNTIME : ret;
}

#endif // IMPL_ADV_API_DETAIL_HCOMM_HOST_HCOMM_HOST_IMPL_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_IMPL_H
#endif
