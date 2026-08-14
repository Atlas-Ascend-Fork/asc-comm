/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_HCOMM_HOST_HCOMM_BATCH_H
#define INCLUDE_HCOMM_HOST_HCOMM_BATCH_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/common/hcomm_batch_entity.h"
#include "hcomm/hcomm_res_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
static_assert(sizeof(ChannelEntity) == 256U, "ChannelEntity ABI must be 256 bytes");
static_assert(sizeof(SqContext) == 128U, "SqContext ABI must be 128 bytes");
static_assert(sizeof(CqContext) == 128U, "CqContext ABI must be 128 bytes");
static_assert(sizeof(RegedBufferEntity) == 64U, "RegedBufferEntity ABI must be 64 bytes");
#endif

enum {
    ASC_COMM_BATCH_WQE_SIZE = 64U,
    ASC_COMM_BATCH_CQE_SIZE = 64U,
};

typedef struct {
    SqContext sqContext;
    CqContext cqContext;
    HcommBatchRemoteInfo remoteInfo;
} AscCommChannelMetadata;

static inline int AscCommIsSameEndpoint(const EndpointDesc* lhs, const EndpointDesc* rhs)
{
    return lhs->protocol == rhs->protocol && lhs->commAddr.type == rhs->commAddr.type &&
           memcmp(lhs->commAddr.raws, rhs->commAddr.raws, sizeof(lhs->commAddr.raws)) == 0 &&
           lhs->loc.locType == rhs->loc.locType && memcmp(lhs->loc.raws, rhs->loc.raws, sizeof(lhs->loc.raws)) == 0;
}

static inline HcclResult AscCommValidateMultiChannelInputs(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    uint32_t i;
    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL || multiChannel == NULL || channelNum == 0U) {
        fprintf(stderr, "[ERROR] [%s] invalid parameter.\n", __func__);
        return HCCL_E_PARA;
    }
    for (i = 1U; i < channelNum; ++i) {
        if (!AscCommIsSameEndpoint(&channelDescs[0].localEndpoint, &channelDescs[i].localEndpoint)) {
            fprintf(stderr, "[ERROR] [%s] localEndpoint of channel[%u] does not match channel[0].\n", __func__, i);
            return HCCL_E_PARA;
        }
    }
    return HCCL_SUCCESS;
}

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

static inline HcclResult AscCommExtractChannelMetadata(
    ChannelHandle channel, uint32_t channelIndex, AscCommChannelMetadata* metadata)
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

    if (channelEntity.sqNum != 1U || channelEntity.cqNum != 1U || channelEntity.remoteBufferNum == 0U ||
        channelEntity.sqContextAddr == NULL || channelEntity.cqContextAddr == NULL ||
        channelEntity.remoteBufferAddr == NULL) {
        fprintf(
            stderr,
            "[ERROR] [%s] channel[%u] has invalid resource counts, sqNum[%u], cqNum[%u], remoteBufferNum[%u].\n",
            __func__, channelIndex, channelEntity.sqNum, channelEntity.cqNum, channelEntity.remoteBufferNum);
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
    if (metadata->sqContext.type != SQ_CONTEXT_TYPE_UB_JFS || metadata->cqContext.type != CQ_CONTEXT_TYPE_UB_JFC) {
        fprintf(stderr, "[ERROR] [%s] channel[%u] is not a UB JFS/JFC channel.\n", __func__, channelIndex);
        return HCCL_E_NOT_SUPPORT;
    }
    if (metadata->sqContext.contextInfo.ubJfs.sqVa == 0U || metadata->sqContext.contextInfo.ubJfs.headAddr == 0U ||
        metadata->sqContext.contextInfo.ubJfs.tailAddr == 0U || metadata->sqContext.contextInfo.ubJfs.dbVa == 0U ||
        metadata->sqContext.contextInfo.ubJfs.wqeSize != ASC_COMM_BATCH_WQE_SIZE ||
        metadata->sqContext.contextInfo.ubJfs.sqDepth == 0U || metadata->cqContext.contextInfo.ubJfc.scqVa == 0U ||
        metadata->cqContext.contextInfo.ubJfc.headAddr == 0U || metadata->cqContext.contextInfo.ubJfc.tailAddr == 0U ||
        metadata->cqContext.contextInfo.ubJfc.dbVa == 0U ||
        metadata->cqContext.contextInfo.ubJfc.cqeSize != ASC_COMM_BATCH_CQE_SIZE ||
        metadata->cqContext.contextInfo.ubJfc.cqDepth == 0U) {
        fprintf(stderr, "[ERROR] [%s] channel[%u] has invalid SQ/CQ metadata.\n", __func__, channelIndex);
        return HCCL_E_INTERNAL;
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

static inline int AscCommIsSameSharedSqContext(const SqContext* lhs, const SqContext* rhs)
{
    return lhs->type == rhs->type && lhs->contextInfo.ubJfs.sqVa == rhs->contextInfo.ubJfs.sqVa &&
           lhs->contextInfo.ubJfs.headAddr == rhs->contextInfo.ubJfs.headAddr &&
           lhs->contextInfo.ubJfs.tailAddr == rhs->contextInfo.ubJfs.tailAddr &&
           lhs->contextInfo.ubJfs.dbVa == rhs->contextInfo.ubJfs.dbVa &&
           lhs->contextInfo.ubJfs.jfsID == rhs->contextInfo.ubJfs.jfsID &&
           lhs->contextInfo.ubJfs.wqeSize == rhs->contextInfo.ubJfs.wqeSize &&
           lhs->contextInfo.ubJfs.sqDepth == rhs->contextInfo.ubJfs.sqDepth;
}

static inline int AscCommIsSameSharedCqContext(const CqContext* lhs, const CqContext* rhs)
{
    return lhs->type == rhs->type && lhs->contextInfo.ubJfc.scqVa == rhs->contextInfo.ubJfc.scqVa &&
           lhs->contextInfo.ubJfc.headAddr == rhs->contextInfo.ubJfc.headAddr &&
           lhs->contextInfo.ubJfc.tailAddr == rhs->contextInfo.ubJfc.tailAddr &&
           lhs->contextInfo.ubJfc.dbVa == rhs->contextInfo.ubJfc.dbVa &&
           lhs->contextInfo.ubJfc.jfcID == rhs->contextInfo.ubJfc.jfcID &&
           lhs->contextInfo.ubJfc.cqeSize == rhs->contextInfo.ubJfc.cqeSize &&
           lhs->contextInfo.ubJfc.cqDepth == rhs->contextInfo.ubJfc.cqDepth;
}

static inline HcclResult AscCommBuildMultiChannelEntity(
    const ChannelHandle* channels, uint32_t channelNum, void** deviceMemory)
{
    AscCommChannelMetadata anchorMetadata;
    AscCommChannelMetadata currentMetadata;
    HcommMultiChannelEntity* entity = NULL;
    HcommBatchRemoteInfo* remoteInfos = NULL;
    void* hostMemory = NULL;
    size_t totalSize;
    uint32_t i;
    HcclResult ret = HCCL_SUCCESS;
    aclError aclRet;

    if ((size_t)channelNum > (SIZE_MAX - sizeof(HcommMultiChannelEntity)) / sizeof(HcommBatchRemoteInfo)) {
        fprintf(stderr, "[ERROR] [%s] multi-channel entity size overflow, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    totalSize = sizeof(HcommMultiChannelEntity) + (size_t)channelNum * sizeof(HcommBatchRemoteInfo);
    if (totalSize > UINT32_MAX) {
        fprintf(stderr, "[ERROR] [%s] multi-channel entity exceeds ABI size, totalSize[%zu].\n", __func__, totalSize);
        return HCCL_E_MEMORY;
    }

    memset(&anchorMetadata, 0, sizeof(anchorMetadata));
    memset(&currentMetadata, 0, sizeof(currentMetadata));
    hostMemory = calloc(1U, totalSize);
    if (hostMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate host staging memory failed.\n", __func__);
        return HCCL_E_MEMORY;
    }
    entity = (HcommMultiChannelEntity*)hostMemory;
    remoteInfos = (HcommBatchRemoteInfo*)((uint8_t*)hostMemory + sizeof(HcommMultiChannelEntity));
    for (i = 0U; i < channelNum; ++i) {
        ret = AscCommExtractChannelMetadata(channels[i], i, &currentMetadata);
        if (ret != HCCL_SUCCESS) {
            free(hostMemory);
            return ret;
        }
        if (i == 0U) {
            anchorMetadata = currentMetadata;
        } else if (
            !AscCommIsSameSharedSqContext(&anchorMetadata.sqContext, &currentMetadata.sqContext) ||
            !AscCommIsSameSharedCqContext(&anchorMetadata.cqContext, &currentMetadata.cqContext)) {
            fprintf(stderr, "[ERROR] [%s] channel[%u] does not use the shared SQ/CQ.\n", __func__, i);
            free(hostMemory);
            return HCCL_E_PARA;
        }
        remoteInfos[i] = currentMetadata.remoteInfo;
    }

    entity->abiHeader.version = HCOMM_MULTI_CHANNEL_VERSION;
    entity->abiHeader.magicWord = HCOMM_MULTI_CHANNEL_MAGIC_WORD;
    entity->abiHeader.size = (uint32_t)totalSize;
    entity->sqContext = anchorMetadata.sqContext;
    entity->cqContext = anchorMetadata.cqContext;
    entity->channelNum = channelNum;

    aclRet = aclrtMalloc(deviceMemory, totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS || *deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] aclrtMalloc failed, ret[%d].\n", __func__, aclRet);
        free(hostMemory);
        return aclRet == ACL_SUCCESS ? HCCL_E_MEMORY : HCCL_E_RUNTIME;
    }
    aclRet = aclrtMemcpy(*deviceMemory, totalSize, hostMemory, totalSize, ACL_MEMCPY_HOST_TO_DEVICE);
    free(hostMemory);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] copy multi-channel entity to device failed, ret[%d].\n", __func__, aclRet);
        (void)aclrtFree(*deviceMemory);
        *deviceMemory = NULL;
        return HCCL_E_RUNTIME;
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
    ret = AscCommValidateMultiChannelInputs(comm, sharedQueueTag, channelDescs, channelNum, multiChannel);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    channels = (ChannelHandle*)calloc(channelNum, sizeof(ChannelHandle));
    if (channels == NULL) {
        fprintf(stderr, "[ERROR] [%s] allocate channel handles failed, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    ret = AscCommAcquireSharedChannels(comm, sharedQueueTag, channelDescs, channelNum, channels);
    if (ret == HCCL_SUCCESS) {
        ret = AscCommBuildMultiChannelEntity(channels, channelNum, &deviceMemory);
    }
    free(channels);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multiChannel = (MultiChannelHandle)(uintptr_t)deviceMemory;
    fprintf(
        stderr, "[INFO] [%s] success, channelNum[%u], multiChannel[0x%llx].\n", __func__, channelNum,
        (unsigned long long)(*multiChannel));
    return HCCL_SUCCESS;
}

/**
 * @brief Destroy device metadata owned by a multi-channel handle.
 * @note The caller must ensure that no kernel is still using the handle.
 */
static inline HcclResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel)
{
    void* deviceMemory;
    aclError aclRet;
    if (multiChannel == 0U) {
        fprintf(stderr, "[ERROR] [%s] multiChannel is null.\n", __func__);
        return HCCL_E_PTR;
    }
    deviceMemory = (void*)(uintptr_t)multiChannel;
    aclRet = aclrtFree(deviceMemory);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] aclrtFree failed, ret[%d].\n", __func__, aclRet);
        return HCCL_E_RUNTIME;
    }
    fprintf(stderr, "[INFO] [%s] success, multiChannel[0x%llx].\n", __func__, (unsigned long long)multiChannel);
    return HCCL_SUCCESS;
}

#ifdef __cplusplus
}
#endif

#endif // INCLUDE_HCOMM_HOST_HCOMM_BATCH_H
