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
#endif

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
    if (comm == NULL || sharedQueueTag == NULL || channelDescs == NULL ||
        multiChannel == NULL || channelNum == 0U) {
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

static inline HcclResult AscCommCopyChannelEntities(
    const ChannelHandle* channels, uint32_t channelNum, void** deviceMemory)
{
    uint32_t i;
    aclError aclRet;
    size_t totalSize;
    if ((size_t)channelNum > SIZE_MAX / sizeof(ChannelEntity)) {
        fprintf(stderr, "[ERROR] [%s] channel entity size overflow, channelNum[%u].\n", __func__, channelNum);
        return HCCL_E_MEMORY;
    }
    totalSize = (size_t)channelNum * sizeof(ChannelEntity);
    aclRet = aclrtMalloc(deviceMemory, totalSize, ACL_MEM_MALLOC_HUGE_FIRST);
    if (aclRet != ACL_SUCCESS) {
        fprintf(stderr, "[ERROR] [%s] aclrtMalloc failed, ret[%d].\n", __func__, aclRet);
        return HCCL_E_RUNTIME;
    }
    if (*deviceMemory == NULL) {
        fprintf(stderr, "[ERROR] [%s] aclrtMalloc returned null device memory.\n", __func__);
        return HCCL_E_MEMORY;
    }

    for (i = 0U; i < channelNum; ++i) {
        void* dst;
        const void* src;
        if (channels[i] == 0U) {
            fprintf(stderr, "[ERROR] [%s] channel[%u] handle is null.\n", __func__, i);
            (void)aclrtFree(*deviceMemory);
            *deviceMemory = NULL;
            return HCCL_E_INTERNAL;
        }

        dst = (uint8_t*)(*deviceMemory) + (size_t)i * sizeof(ChannelEntity);
        src = (const void*)(uintptr_t)channels[i];
        aclRet = aclrtMemcpy(dst, sizeof(ChannelEntity), src, sizeof(ChannelEntity), ACL_MEMCPY_DEVICE_TO_DEVICE);
        if (aclRet != ACL_SUCCESS) {
            fprintf(stderr, "[ERROR] [%s] copy channel[%u] entity failed, ret[%d].\n", __func__, i, aclRet);
            (void)aclrtFree(*deviceMemory);
            *deviceMemory = NULL;
            return HCCL_E_RUNTIME;
        }
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
        ret = AscCommCopyChannelEntities(channels, channelNum, &deviceMemory);
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
