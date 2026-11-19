/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_IMPL_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_HOST_ASCCOMM_HOST_IMPL_H
#define IMPL_COMM_API_AICORE_ASCCOMM_HOST_ASCCOMM_HOST_IMPL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "acl/acl_rt.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_channel.h"
#include "hcomm/hcomm_res.h"
#include "hcomm/hcomm_res_entity_defs.h"

#include "../common/asccomm_host_utils.h"

typedef struct {
    uint64_t remote_buffer_addr;
    uint32_t remote_buffer_num;
    uint32_t tp_id;
    uint64_t remote_eid_low;
    uint64_t remote_eid_high;
} multi_channel_remote_info;

typedef struct {
    ChannelHandle channel_handle;
    uint32_t channel_num;
    uint32_t reserved;
    uint64_t remote_info_addr;
} multi_channel_entity;

typedef struct {
    SqContext sq_context;
    multi_channel_remote_info remote_info;
} channel_metadata;

static const uint32_t ASCCOMM_CHANNEL_READY_RETRY_COUNT = 120000U;
static const uint32_t ASCCOMM_CHANNEL_READY_RETRY_INTERVAL_US = 1000U;

template <typename config_type, typename result_type>
static inline result_type destroy_channel_config(
    config_type config, result_type (*destroy_func)(config_type), const char* config_name)
{
    result_type destroy_ret = destroy_func(config);
    if (destroy_ret != static_cast<result_type>(HCCL_SUCCESS)) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] %s failed, ret[%d].", __func__, config_name, destroy_ret);
    }
    return destroy_ret;
}

static inline HcclResult hccl_acquire_shared_channels_with_comm(
    HcclComm comm, const char* shared_queue_tag, const HcclChannelDesc* channel_descs, uint32_t channel_num,
    ChannelHandle* channels)
{
    HcclChannelConfig config = NULL;
    HcclResult ret = dl_hcomm_api::hccl_channel_config_create(&config);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hccl_channel_config_create failed, ret[%d].", __func__, ret);
        return ret;
    }

    ret = dl_hcomm_api::hccl_channel_config_set_int(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue config failed, ret[%d].", __func__, ret);
        (void)destroy_channel_config(config, dl_hcomm_api::hccl_channel_config_destroy, "HcclChannelConfigDestroy");
        return ret;
    }

    ret =
        dl_hcomm_api::hccl_channel_config_set_str(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, shared_queue_tag);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue tag failed, ret[%d].", __func__, ret);
        (void)destroy_channel_config(config, dl_hcomm_api::hccl_channel_config_destroy, "HcclChannelConfigDestroy");
        return ret;
    }

    ret = dl_hcomm_api::hccl_channel_acquire_with_config(
        comm, COMM_ENGINE_AIV, channel_descs, channel_num, config, channels);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hccl_channel_acquire_with_config failed, ret[%d].", __func__, ret);
        (void)destroy_channel_config(config, dl_hcomm_api::hccl_channel_config_destroy, "HcclChannelConfigDestroy");
        return ret;
    }

    return destroy_channel_config(config, dl_hcomm_api::hccl_channel_config_destroy, "HcclChannelConfigDestroy");
}

static inline HcommResult asccomm_create_shared_channels(
    EndpointHandle endpoint_handle, HcommChannelDesc* channel_descs, uint32_t channel_num, ChannelHandle* channels)
{
    HcommChannelConfig config = NULL;
    HcommResult ret = dl_hcomm_api::hcomm_channel_config_create(&config);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hcomm_channel_config_create failed, ret[%d].", __func__, ret);
        return ret;
    }

    ret = dl_hcomm_api::hcomm_channel_config_set_int(config, HCOMM_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1U);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] set shared queue config failed, ret[%d].", __func__, ret);
        (void)destroy_channel_config(config, dl_hcomm_api::hcomm_channel_config_destroy, "HcommChannelConfigDestroy");
        return ret;
    }

    ret = dl_hcomm_api::hcomm_channel_create_with_config(
        endpoint_handle, COMM_ENGINE_AIV, channel_descs, channel_num, config, channels);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] hcomm_channel_create_with_config failed, ret[%d], channel_num[%u]. "
            "Check peer endpoint state, network connectivity and firewall rules.",
            __func__, ret, channel_num);
        (void)destroy_channel_config(config, dl_hcomm_api::hcomm_channel_config_destroy, "HcommChannelConfigDestroy");
        return ret;
    }

    HcommResult destroy_ret =
        destroy_channel_config(config, dl_hcomm_api::hcomm_channel_config_destroy, "HcommChannelConfigDestroy");
    if (destroy_ret != HCCL_SUCCESS) {
        HcommResult channel_destroy_ret = dl_hcomm_api::hcomm_channel_destroy(channels, channel_num);
        if (channel_destroy_ret != HCCL_SUCCESS) {
            ASC_CPU_LOG_ERROR(
                "[ERROR] [%s] cleanup hcomm_channel_destroy failed, ret[%d].", __func__, channel_destroy_ret);
        }
        return destroy_ret;
    }
    return HCCL_SUCCESS;
}

static inline HcommResult asccomm_wait_channels_ready(const ChannelHandle* channels, uint32_t channel_num)
{
    int32_t* statuses = NULL;
    aclError acl_ret = aclrtMallocHost((void**)&statuses, (size_t)channel_num * sizeof(int32_t));
    if (acl_ret != ACL_SUCCESS || statuses == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel status array failed, channel_num[%u], ret[%d].", __func__,
            channel_num, acl_ret);
        if (statuses != NULL) {
            (void)aclrtFreeHost(statuses);
        }
        return HCCL_E_MEMORY;
    }

    for (uint32_t retry = 0U; retry < ASCCOMM_CHANNEL_READY_RETRY_COUNT; ++retry) {
        HcommResult ret = dl_hcomm_api::hcomm_channel_get_status(channels, channel_num, statuses);
        if (ret != HCCL_SUCCESS && ret != HCCL_E_AGAIN) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] hcomm_channel_get_status failed, ret[%d].", __func__, ret);
            (void)aclrtFreeHost(statuses);
            return ret;
        }
        if (ret == HCCL_E_AGAIN) {
            (void)usleep(ASCCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
            continue;
        }

        int all_ready = 1;
        for (uint32_t i = 0U; i < channel_num; ++i) {
            if (statuses[i] >= HCOMM_CHANNEL_STATUS_FAILED_INTERNAL) {
                ASC_CPU_LOG_ERROR("[ERROR] [%s] channel[%u] connection failed, status[%d].", __func__, i, statuses[i]);
                (void)aclrtFreeHost(statuses);
                return HCCL_E_INTERNAL;
            }
            if (statuses[i] != HCOMM_CHANNEL_STATUS_READY) {
                all_ready = 0;
            }
        }
        if (all_ready != 0) {
            (void)aclrtFreeHost(statuses);
            return HCCL_SUCCESS;
        }
        (void)usleep(ASCCOMM_CHANNEL_READY_RETRY_INTERVAL_US);
    }

    ASC_CPU_LOG_ERROR("[ERROR] [%s] waiting for channels timed out.", __func__);
    (void)aclrtFreeHost(statuses);
    return HCCL_E_TIMEOUT;
}

static inline int32_t asccomm_copy_from_device(
    void* dst, size_t size, const void* src, uint32_t channel_index, const char* data_name)
{
    aclError acl_ret = aclrtMemcpy(dst, size, src, size, ACL_MEMCPY_DEVICE_TO_HOST);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] copy channel[%u] %s from device failed, ret[%d].", __func__, channel_index, data_name,
            acl_ret);
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline int32_t asccomm_extract_channel_metadata(
    ChannelHandle channel, uint32_t channel_index, channel_metadata* metadata)
{
    ChannelEntity channel_entity;
    int32_t ret;

    ret = asccomm_copy_from_device(
        &channel_entity, sizeof(channel_entity), (const void*)(uintptr_t)channel, channel_index, "ChannelEntity");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (channel_entity.sqContextAddr == NULL || channel_entity.cqContextAddr == NULL ||
        channel_entity.remoteBufferAddr == NULL || channel_entity.remoteBufferNum == 0U) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] channel[%u] has invalid SQ/CQ context or remote MR table.", __func__, channel_index);
        return HCCL_E_NOT_SUPPORT;
    }

    ret = asccomm_copy_from_device(
        &metadata->sq_context, sizeof(metadata->sq_context), channel_entity.sqContextAddr, channel_index, "SqContext");
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    metadata->remote_info.remote_buffer_addr = (uint64_t)(uintptr_t)channel_entity.remoteBufferAddr;
    metadata->remote_info.remote_buffer_num = channel_entity.remoteBufferNum;
    metadata->remote_info.tp_id = metadata->sq_context.contextInfo.ubJfs.tpID;
    uint64_t remote_eid[2];
    aclError copy_ret = aclrtMemcpy(
        remote_eid, sizeof(remote_eid), metadata->sq_context.contextInfo.ubJfs.remoteEID, sizeof(remote_eid),
        ACL_MEMCPY_HOST_TO_HOST);
    if (copy_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] copy channel[%u] remote EID failed, ret[%d].", __func__, channel_index, copy_ret);
        return HCCL_E_INTERNAL;
    }
    metadata->remote_info.remote_eid_low = remote_eid[0];
    metadata->remote_info.remote_eid_high = remote_eid[1];

    return HCCL_SUCCESS;
}

static inline int32_t asccomm_prepare_multi_channel_entity(
    const ChannelHandle* channels, uint32_t channel_num, int owns_channels, void** host_memory, size_t* total_size)
{
    channel_metadata current_metadata;
    multi_channel_entity* entity = NULL;
    multi_channel_remote_info* channel_infos = NULL;
    int32_t ret = HCCL_SUCCESS;

    *total_size = sizeof(multi_channel_entity) + (size_t)channel_num * sizeof(multi_channel_remote_info);
    if (owns_channels != 0) {
        if ((size_t)channel_num > (SIZE_MAX - *total_size) / sizeof(ChannelHandle)) {
            ASC_CPU_LOG_ERROR(
                "[ERROR] [%s] owned channel array size overflow, channel_num[%u].", __func__, channel_num);
            return HCCL_E_MEMORY;
        }
        *total_size += (size_t)channel_num * sizeof(ChannelHandle);
    }

    aclError acl_ret = aclrtMallocHost(host_memory, *total_size);
    if (acl_ret != ACL_SUCCESS || *host_memory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtMallocHost staging memory failed, ret[%d].", __func__, acl_ret);
        if (*host_memory != NULL) {
            (void)aclrtFreeHost(*host_memory);
            *host_memory = NULL;
        }
        return HCCL_E_MEMORY;
    }
    aclError memset_ret = aclrtMemset(*host_memory, *total_size, 0, *total_size);
    if (memset_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] initialize host staging memory failed, ret[%d].", __func__, memset_ret);
        (void)aclrtFreeHost(*host_memory);
        *host_memory = NULL;
        return HCCL_E_INTERNAL;
    }
    entity = (multi_channel_entity*)*host_memory;
    channel_infos = (multi_channel_remote_info*)((uint8_t*)*host_memory + sizeof(multi_channel_entity));
    for (uint32_t i = 0U; i < channel_num; ++i) {
        ret = asccomm_extract_channel_metadata(channels[i], i, &current_metadata);
        if (ret != HCCL_SUCCESS) {
            (void)aclrtFreeHost(*host_memory);
            *host_memory = NULL;
            return ret;
        }
        channel_infos[i] = current_metadata.remote_info;
    }

    entity->channel_handle = channels[0];
    entity->channel_num = channel_num;
    if (owns_channels != 0) {
        void* owned_channels = (uint8_t*)channel_infos + (size_t)channel_num * sizeof(multi_channel_remote_info);
        aclError copy_ret = aclrtMemcpy(
            owned_channels, (size_t)channel_num * sizeof(ChannelHandle), channels,
            (size_t)channel_num * sizeof(ChannelHandle), ACL_MEMCPY_HOST_TO_HOST);
        if (copy_ret != ACL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] copy owned channel handles failed, ret[%d].", __func__, copy_ret);
            (void)aclrtFreeHost(*host_memory);
            *host_memory = NULL;
            return HCCL_E_INTERNAL;
        }
    }
    return HCCL_SUCCESS;
}

static inline HcclResult hccl_build_multi_channel_entity_with_comm(
    HcclComm comm, const ChannelHandle* channels, const char* shared_queue_tag, uint32_t channel_num,
    void** device_memory)
{
    void* host_memory = NULL;
    size_t total_size = 0U;
    int32_t prepare_ret = asccomm_prepare_multi_channel_entity(channels, channel_num, 0, &host_memory, &total_size);
    if (prepare_ret != HCCL_SUCCESS) {
        return static_cast<HcclResult>(prepare_ret);
    }

    HcclResult ret =
        dl_hcomm_api::hccl_engine_ctx_create(comm, shared_queue_tag, COMM_ENGINE_AIV, total_size, device_memory);
    if (ret != HCCL_SUCCESS || *device_memory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hccl_engine_ctx_create failed, ret[%d].", __func__, ret);
        (void)aclrtFreeHost(host_memory);
        return ret == HCCL_SUCCESS ? HCCL_E_MEMORY : ret;
    }
    multi_channel_entity* entity = (multi_channel_entity*)host_memory;
    entity->remote_info_addr = (uint64_t)(uintptr_t)*device_memory + sizeof(multi_channel_entity);
    ret = dl_hcomm_api::hccl_engine_ctx_copy(comm, COMM_ENGINE_AIV, shared_queue_tag, host_memory, total_size, 0U);
    (void)aclrtFreeHost(host_memory);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hccl_engine_ctx_copy failed, ret[%d].", __func__, ret);
        *device_memory = NULL;
        return ret;
    }
    return HCCL_SUCCESS;
}

static inline HcommResult asccomm_build_multi_channel_entity(
    const ChannelHandle* channels, uint32_t channel_num, void** device_memory)
{
    void* host_memory = NULL;
    size_t total_size = 0U;
    int32_t prepare_ret = asccomm_prepare_multi_channel_entity(channels, channel_num, 1, &host_memory, &total_size);
    if (prepare_ret != HCCL_SUCCESS) {
        return static_cast<HcommResult>(prepare_ret);
    }

    aclError acl_ret = aclrtMalloc(device_memory, total_size, ACL_MEM_MALLOC_HUGE_FIRST);
    if (acl_ret != ACL_SUCCESS || *device_memory == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtMalloc failed, ret[%d].", __func__, acl_ret);
        (void)aclrtFreeHost(host_memory);
        return acl_ret == ACL_SUCCESS ? HCCL_E_MEMORY : HCCL_E_RUNTIME;
    }
    multi_channel_entity* entity = (multi_channel_entity*)host_memory;
    entity->remote_info_addr = (uint64_t)(uintptr_t)*device_memory + sizeof(multi_channel_entity);
    acl_ret = aclrtMemcpy(*device_memory, total_size, host_memory, total_size, ACL_MEMCPY_HOST_TO_DEVICE);
    (void)aclrtFreeHost(host_memory);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy multi-channel entity to device failed, ret[%d].", __func__, acl_ret);
        aclError free_ret = aclrtFree(*device_memory);
        if (free_ret != ACL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] cleanup aclrtFree failed, ret[%d].", __func__, free_ret);
        }
        *device_memory = NULL;
        return HCCL_E_RUNTIME;
    }
    return HCCL_SUCCESS;
}

static inline HcclResult asccomm_host_make_multi_channel_handle(
    HcclComm comm, const char* shared_queue_tag, const HcclChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    ChannelHandle* channels = NULL;
    void* device_memory = NULL;
    HcclResult ret;
    aclError acl_ret;

    if (multi_channel == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }
    *multi_channel = 0U;

    if (comm == NULL || shared_queue_tag == NULL || channel_descs == NULL || channel_num == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }

    acl_ret = aclrtMallocHost((void**)&channels, (size_t)channel_num * sizeof(ChannelHandle));
    if (acl_ret != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel handles failed, channel_num[%u], ret[%d].", __func__, channel_num,
            acl_ret);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    ret = dl_hcomm_api::load_hccl_api();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = hccl_acquire_shared_channels_with_comm(comm, shared_queue_tag, channel_descs, channel_num, channels);
    if (ret == HCCL_SUCCESS) {
        ret = hccl_build_multi_channel_entity_with_comm(comm, channels, shared_queue_tag, channel_num, &device_memory);
    }
    aclError free_ret = aclrtFreeHost(channels);
    if (free_ret != ACL_SUCCESS && ret == HCCL_SUCCESS) {
        ret = HCCL_E_RUNTIME;
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multi_channel = (multi_channel_handle)(uintptr_t)device_memory;
    return HCCL_SUCCESS;
}

static inline HcommResult asccomm_host_make_multi_channel_handle(
    EndpointHandle endpoint_handle, HcommChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    ChannelHandle* channels = NULL;
    void* device_memory = NULL;
    HcommResult ret;
    aclError acl_ret;

    if (multi_channel == NULL) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }
    *multi_channel = 0U;

    if (endpoint_handle == NULL || channel_descs == NULL || channel_num == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid parameter.", __func__);
        return HCCL_E_PARA;
    }

    acl_ret = aclrtMallocHost((void**)&channels, (size_t)channel_num * sizeof(ChannelHandle));
    if (acl_ret != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost failed, channel_num[%u], ret[%d].", __func__, channel_num, acl_ret);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    ret = dl_hcomm_api::load_hcomm_api();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = asccomm_create_shared_channels(endpoint_handle, channel_descs, channel_num, channels);
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }

    ret = asccomm_wait_channels_ready(channels, channel_num);
    if (ret == HCCL_SUCCESS) {
        ret = asccomm_build_multi_channel_entity(channels, channel_num, &device_memory);
    }
    if (ret != HCCL_SUCCESS) {
        HcommResult destroy_ret = dl_hcomm_api::hcomm_channel_destroy(channels, channel_num);
        if (destroy_ret != HCCL_SUCCESS) {
            ASC_CPU_LOG_ERROR("[ERROR] [%s] cleanup hcomm_channel_destroy failed, ret[%d].", __func__, destroy_ret);
        }
    }
    acl_ret = aclrtFreeHost(channels);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtFreeHost failed, ret[%d].", __func__, acl_ret);
        if (ret == HCCL_SUCCESS) {
            ret = HCCL_E_RUNTIME;
        }
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    *multi_channel = (multi_channel_handle)(uintptr_t)device_memory;
    return HCCL_SUCCESS;
}

static inline HcommResult asccomm_host_destroy_multi_channel_handle(multi_channel_handle multi_channel)
{
    multi_channel_entity entity;
    ChannelHandle* channels = NULL;
    HcommResult ret;
    aclError acl_ret;

    if (multi_channel == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid multi-channel handle.", __func__);
        return HCCL_E_PTR;
    }
    acl_ret = aclrtMemcpy(
        &entity, sizeof(entity), (const void*)(uintptr_t)multi_channel, sizeof(entity), ACL_MEMCPY_DEVICE_TO_HOST);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy multi-channel entity from device failed, ret[%d].", __func__, acl_ret);
        return HCCL_E_RUNTIME;
    }
    if (entity.channel_num == 0U) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] invalid channel count in multi-channel handle.", __func__);
        return HCCL_E_PARA;
    }
    if ((size_t)entity.channel_num > (SIZE_MAX - sizeof(multi_channel_entity)) / sizeof(multi_channel_remote_info)) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] invalid channel_num[%u] in multi-channel entity.", __func__, entity.channel_num);
        return HCCL_E_INTERNAL;
    }
    size_t channel_offset =
        sizeof(multi_channel_entity) + (size_t)entity.channel_num * sizeof(multi_channel_remote_info);
    if ((uint64_t)multi_channel > (uint64_t)UINTPTR_MAX - channel_offset) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] owned channel address overflow.", __func__);
        return HCCL_E_INTERNAL;
    }

    acl_ret = aclrtMallocHost((void**)&channels, (size_t)entity.channel_num * sizeof(ChannelHandle));
    if (acl_ret != ACL_SUCCESS || channels == NULL) {
        ASC_CPU_LOG_ERROR(
            "[ERROR] [%s] aclrtMallocHost channel handles failed, channel_num[%u], ret[%d].", __func__,
            entity.channel_num, acl_ret);
        if (channels != NULL) {
            (void)aclrtFreeHost(channels);
        }
        return HCCL_E_MEMORY;
    }
    acl_ret = aclrtMemcpy(
        channels, (size_t)entity.channel_num * sizeof(ChannelHandle),
        (const void*)((uintptr_t)multi_channel + channel_offset), (size_t)entity.channel_num * sizeof(ChannelHandle),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] copy owned channel handles from device failed, ret[%d].", __func__, acl_ret);
        (void)aclrtFreeHost(channels);
        return HCCL_E_RUNTIME;
    }

    ret = dl_hcomm_api::load_hcomm_api();
    if (ret != HCCL_SUCCESS) {
        (void)aclrtFreeHost(channels);
        return ret;
    }
    ret = dl_hcomm_api::hcomm_channel_destroy(channels, entity.channel_num);
    if (ret != HCCL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] hcomm_channel_destroy failed, ret[%d].", __func__, ret);
    }
    acl_ret = aclrtFree((void*)(uintptr_t)multi_channel);
    if (acl_ret != ACL_SUCCESS) {
        ASC_CPU_LOG_ERROR("[ERROR] [%s] aclrtFree failed, ret[%d].", __func__, acl_ret);
    }
    (void)aclrtFreeHost(channels);
    return ret == HCCL_SUCCESS && acl_ret != ACL_SUCCESS ? HCCL_E_RUNTIME : ret;
}

#endif // IMPL_COMM_API_AICORE_ASCCOMM_HOST_ASCCOMM_HOST_IMPL_H

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_IMPL_H
#endif
