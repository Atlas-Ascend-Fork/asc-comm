/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_HCOMM_HCOMM_HOST_H
#define INCLUDE_HCOMM_HCOMM_HOST_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_res_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Per-channel transport information and a reference to its remote MR table. */
typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpID;
    uint64_t rmtEidL;
    uint64_t rmtEidH;
} HcommBatchRemoteInfo;

typedef struct {
    uint32_t sqHead;
    uint32_t sqTail;
    uint32_t cqHead;
    uint32_t cqTail;
} HcommBatchQueuePersistentCursor;

/** Shared queue state and the device address of HcommBatchRemoteInfo[channelNum]. */
typedef struct {
    SqContext sqContext;
    CqContext cqContext;
    HcommBatchQueuePersistentCursor cursor;
    uint32_t channelNum;
    uint32_t reserved;
    uint64_t remoteInfoAddr;
} HcommMultiChannelEntity;

/** Device address of one HcommMultiChannelEntity allocation. */
typedef uint64_t MultiChannelHandle;

typedef struct {
    ChannelEntity channelEntity;
    SqContext sqContext;
    CqContext cqContext;
    HcommBatchRemoteInfo remoteInfo;
} HcommChannelMetadata;

static inline HcclResult HcommAcquireSharedChannels(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    ChannelHandle* channels)
{
    HcclChannelConfig config = NULL;
    HcclResult configRet;
    HcclResult ret = HcclChannelConfigCreate(&config);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclChannelConfigCreate failed, ret[%d].\n", __func__, ret);
        return ret;
    }

    ret = HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] set shared queue config failed, ret[%d].\n", __func__, ret);
    }
    if (ret == HCCL_SUCCESS) {
        ret = HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, sharedQueueTag);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] set shared queue tag failed, ret[%d].\n", __func__, ret);
        }
    }
    if (ret == HCCL_SUCCESS) {
        ret = HcclChannelAcquireWithConfig(comm, COMM_ENGINE_AIV, channelDescs, channelNum, config, channels);
        if (ret != HCCL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] HcclChannelAcquireWithConfig failed, ret[%d].\n", __func__, ret);
        }
    }

    configRet = HcclChannelConfigDestroy(config);
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
    ChannelHandle channel, uint32_t channelIndex, HcommChannelMetadata* metadata)
{
    ChannelEntity channelEntity;
    HcclResult ret;

    if (channel == 0U || metadata == NULL) {
        fprintf(stderr, "[ERROR] [%s] invalid channel[%u].\n", __func__, channelIndex);
        return HCCL_E_PTR;
    }

    memset(&channelEntity, 0, sizeof(channelEntity));
    memset(metadata, 0, sizeof(*metadata));
    ret = HcommCopyFromDevice(
        &channelEntity, sizeof(channelEntity), (const void*)(uintptr_t)channel, channelIndex, "ChannelEntity");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    metadata->channelEntity = channelEntity;
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
    metadata->remoteInfo.tpID = metadata->sqContext.contextInfo.ubJfs.tpID;
    memcpy(&metadata->remoteInfo.rmtEidL, metadata->sqContext.contextInfo.ubJfs.remoteEID, sizeof(uint64_t));
    memcpy(
        &metadata->remoteInfo.rmtEidH, metadata->sqContext.contextInfo.ubJfs.remoteEID + sizeof(uint64_t),
        sizeof(uint64_t));

    return HCCL_SUCCESS;
}

static inline HcclResult HcommBuildMultiChannelEntity(
    HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag, uint32_t channelNum, void** deviceMemory)
{
    HcommChannelMetadata anchorMetadata;
    HcommChannelMetadata currentMetadata;
    HcommMultiChannelEntity* entity = NULL;
    HcommBatchRemoteInfo* channelInfos = NULL;
    void* hostMemory = NULL;
    size_t totalSize;
    uint32_t i;
    HcclResult ret = HCCL_SUCCESS;

    totalSize = sizeof(HcommMultiChannelEntity) + (size_t)channelNum * sizeof(HcommBatchRemoteInfo);

    memset(&anchorMetadata, 0, sizeof(anchorMetadata));
    memset(&currentMetadata, 0, sizeof(currentMetadata));
    hostMemory = calloc(1U, totalSize);
    if (hostMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate host staging memory failed.\n", __func__);
        return HCCL_E_MEMORY;
    }
    entity = (HcommMultiChannelEntity*)hostMemory;
    channelInfos = (HcommBatchRemoteInfo*)((uint8_t*)hostMemory + sizeof(HcommMultiChannelEntity));
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
    memcpy(&entity->cursor, anchorMetadata.channelEntity.reserve, sizeof(entity->cursor));
    entity->channelNum = channelNum;

    ret = HcclEngineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].\n", __func__, ret);
        free(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    entity->remoteInfoAddr = (uint64_t)(uintptr_t)*deviceMemory + sizeof(HcommMultiChannelEntity);
    ret = HcclEngineCtxCopy(comm, COMM_ENGINE_AIV, sharedQueueTag, hostMemory, totalSize, 0U);
    free(hostMemory);
    if (ret != HCCL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCopy failed, ret[%d].\n", __func__, ret);
        (void)HcclEngineCtxDestroy(comm, sharedQueueTag, COMM_ENGINE_AIV);
        *deviceMemory = NULL;
        return ret;
    }
    return HCCL_SUCCESS;
}

/**
 * @brief Create tagged shared-Jetty channels and a device-resident multi-channel handle.
 */
static inline HcclResult MakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ChannelHandle* channels;
    void* deviceMemory = NULL;
    HcclResult ret;

    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL || multiChannel == NULL || channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }

    channels = (ChannelHandle*)calloc(channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    ret = HcommAcquireSharedChannels(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = HcommBuildMultiChannelEntity(comm, channels, sharedQueueTag, channelNum, &deviceMemory);
    }
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

#endif // INCLUDE_HCOMM_HCOMM_HOST_H
