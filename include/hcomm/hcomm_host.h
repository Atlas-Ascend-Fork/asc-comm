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

#define HCOMM_MULTI_CHANNEL_VERSION 1U
#define HCOMM_MULTI_CHANNEL_MAGIC_WORD 0x484D4348U

/** Per-channel transport information and a reference to its remote MR table. */
typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpID;
    uint64_t rmtEidL;
    uint64_t rmtEidH;
} HcommBatchRemoteInfo;

/** Shared queue owner, contexts, and the device address of HcommBatchRemoteInfo[channelNum]. */
typedef struct {
    ChannelEntity queueEntity;
    SqContext sqContext;
    CqContext cqContext;
    uint32_t channelNum;
    uint32_t reserved;
    uint64_t remoteInfoAddr;
} HcommMultiChannelEntity;

/** Device address of one HcommMultiChannelEntity allocation. */
typedef uint64_t MultiChannelHandle;

#ifdef __cplusplus
#define HCOMM_HOST_STATIC_ASSERT(condition, message) static_assert(condition, message)
#else
#define HCOMM_HOST_STATIC_ASSERT(condition, message) _Static_assert(condition, message)
#endif

HCOMM_HOST_STATIC_ASSERT(sizeof(CommAbiHeader) == 16U, "CommAbiHeader ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(sizeof(ChannelEntity) == 256U, "ChannelEntity ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(sizeof(SqContext) == 128U, "SqContext ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(sizeof(CqContext) == 128U, "CqContext ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(sizeof(HcommBatchRemoteInfo) == 32U, "HcommBatchRemoteInfo ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommBatchRemoteInfo, remoteBufferAddr) == 0U, "remoteBufferAddr ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommBatchRemoteInfo, remoteBufferNum) == 8U, "remoteBufferNum ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommBatchRemoteInfo, tpID) == 12U, "tpID ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommBatchRemoteInfo, rmtEidL) == 16U, "rmtEidL ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommBatchRemoteInfo, rmtEidH) == 24U, "rmtEidH ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(sizeof(HcommMultiChannelEntity) == 528U, "HcommMultiChannelEntity ABI size mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, queueEntity) == 0U, "queueEntity ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, sqContext) == 256U, "sqContext ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, cqContext) == 384U, "cqContext ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, channelNum) == 512U, "channelNum ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, reserved) == 516U, "reserved ABI mismatch");
HCOMM_HOST_STATIC_ASSERT(offsetof(HcommMultiChannelEntity, remoteInfoAddr) == 520U, "remoteInfoAddr ABI mismatch");

#undef HCOMM_HOST_STATIC_ASSERT

typedef struct {
    ChannelEntity channelEntity;
    SqContext sqContext;
    CqContext cqContext;
    HcommBatchRemoteInfo remoteInfo;
} AscCommChannelMetadata;

static inline HcclResult AscCommAcquireSharedChannels(
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

static inline HcclResult AscCommCopyFromDevice(
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

static inline void AscCommDumpChannelEntity(
    const char* sharedQueueTag, ChannelHandle channel, uint32_t channelIndex, const ChannelEntity* channelEntity,
    const AscCommChannelMetadata* metadata)
{
    fprintf(
        stderr,
        "[INFO] [ChannelEntity] tag[%s] channel[%u] handle[0x%llx] sqContextAddr[%p] cqContextAddr[%p] "
        "remoteBufferAddr[%p] sqNum[%u] cqNum[%u] remoteBufferNum[%u].\n",
        sharedQueueTag, channelIndex, (unsigned long long)channel, (void*)channelEntity->sqContextAddr,
        (void*)channelEntity->cqContextAddr, (void*)channelEntity->remoteBufferAddr, channelEntity->sqNum,
        channelEntity->cqNum, channelEntity->remoteBufferNum);
    fprintf(
        stderr,
        "[INFO] [ChannelEntity][SQ] tag[%s] channel[%u] sqVa[0x%llx] headAddr[0x%llx] tailAddr[0x%llx] "
        "dbVa[0x%llx] jfsID[%u] tpID[%u] depth[%u] wqeSize[%u].\n",
        sharedQueueTag, channelIndex, (unsigned long long)metadata->sqContext.contextInfo.ubJfs.sqVa,
        (unsigned long long)metadata->sqContext.contextInfo.ubJfs.headAddr,
        (unsigned long long)metadata->sqContext.contextInfo.ubJfs.tailAddr,
        (unsigned long long)metadata->sqContext.contextInfo.ubJfs.dbVa, metadata->sqContext.contextInfo.ubJfs.jfsID,
        metadata->sqContext.contextInfo.ubJfs.tpID, metadata->sqContext.contextInfo.ubJfs.sqDepth,
        metadata->sqContext.contextInfo.ubJfs.wqeSize);
    fprintf(
        stderr,
        "[INFO] [ChannelEntity][CQ] tag[%s] channel[%u] scqVa[0x%llx] headAddr[0x%llx] tailAddr[0x%llx] "
        "dbVa[0x%llx] jfcID[%u] depth[%u] cqeSize[%u].\n",
        sharedQueueTag, channelIndex, (unsigned long long)metadata->cqContext.contextInfo.ubJfc.scqVa,
        (unsigned long long)metadata->cqContext.contextInfo.ubJfc.headAddr,
        (unsigned long long)metadata->cqContext.contextInfo.ubJfc.tailAddr,
        (unsigned long long)metadata->cqContext.contextInfo.ubJfc.dbVa, metadata->cqContext.contextInfo.ubJfc.jfcID,
        metadata->cqContext.contextInfo.ubJfc.cqDepth, metadata->cqContext.contextInfo.ubJfc.cqeSize);
}

static inline HcclResult AscCommExtractChannelMetadata(
    ChannelHandle channel, const char* sharedQueueTag, uint32_t channelIndex, AscCommChannelMetadata* metadata)
{
    ChannelEntity channelEntity;
    HcclResult ret;

    if (channel == 0U || metadata == NULL) {
        fprintf(stderr, "[ERROR] [%s] invalid channel[%u].\n", __func__, channelIndex);
        return HCCL_E_PTR;
    }

    memset(&channelEntity, 0, sizeof(channelEntity));
    memset(metadata, 0, sizeof(*metadata));
    ret = AscCommCopyFromDevice(
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

    ret = AscCommCopyFromDevice(
        &metadata->sqContext, sizeof(metadata->sqContext), channelEntity.sqContextAddr, channelIndex, "SqContext");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    ret = AscCommCopyFromDevice(
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
    AscCommDumpChannelEntity(sharedQueueTag, channel, channelIndex, &channelEntity, metadata);

    return HCCL_SUCCESS;
}

static inline HcclResult AscCommBuildMultiChannelEntity(
    HcclComm comm, const ChannelHandle* channels, const char* sharedQueueTag, uint32_t channelNum, void** deviceMemory)
{
    AscCommChannelMetadata anchorMetadata;
    AscCommChannelMetadata currentMetadata;
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
        ret = AscCommExtractChannelMetadata(channels[i], sharedQueueTag, i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            free(hostMemory);
            return ret;
        }
        if (i == 0U) {
            anchorMetadata = currentMetadata;
        }
        channelInfos[i] = currentMetadata.remoteInfo;
    }

    entity->queueEntity = anchorMetadata.channelEntity;
    entity->queueEntity.abiHeader.version = HCOMM_MULTI_CHANNEL_VERSION;
    entity->queueEntity.abiHeader.magicWord = HCOMM_MULTI_CHANNEL_MAGIC_WORD;
    entity->queueEntity.abiHeader.size = (uint32_t)totalSize;
    entity->sqContext = anchorMetadata.sqContext;
    entity->cqContext = anchorMetadata.cqContext;
    entity->channelNum = channelNum;

    ret = HcclEngineCtxCreate(comm, sharedQueueTag, COMM_ENGINE_AIV, totalSize, deviceMemory);
    if (ret != HCCL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] HcclEngineCtxCreate failed, ret[%d].\n", __func__, ret);
        free(hostMemory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    entity->queueEntity.sqContextAddr =
        (SqContext*)((uint8_t*)*deviceMemory + offsetof(HcommMultiChannelEntity, sqContext));
    entity->queueEntity.cqContextAddr =
        (CqContext*)((uint8_t*)*deviceMemory + offsetof(HcommMultiChannelEntity, cqContext));
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

    if (multiChannel != NULL) {
        *multiChannel = 0U;
    }
    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL || multiChannel == NULL || channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }

    channels = (ChannelHandle*)calloc(channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    ret = AscCommAcquireSharedChannels(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = AscCommBuildMultiChannelEntity(comm, channels, sharedQueueTag, channelNum, &deviceMemory);
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
