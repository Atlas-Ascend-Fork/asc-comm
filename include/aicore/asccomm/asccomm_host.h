/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_HOST_H
#define INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_HOST_H

#include <stdint.h>

#include "hccl/hccl_res.h"

namespace asc {

/** Device address of the shared-Jetty multi-channel entity. */
typedef uint64_t multi_channel_handle;

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_H
#endif

#include "../../../../impl/comm_api/aicore/asccomm/host/asccomm_host_impl.h"

/*!
 * @brief Create a group of communicator-owned shared-Jetty channels and the multi-channel handle used on the Device.
 * @param [in] comm: The initialized HCCL communicator that owns the channels and Device context.
 * @param [in] shared_queue_tag: The tag identifying the shared queue used by this channel group.
 * @param [in] channel_descs: The channel descriptor array. Its order defines the channel_index used by asccomm_get_handle_ref.
 * @param [in] channel_num: The number of elements in channel_descs. It must be greater than 0.
 * @param [out] multi_channel: The created Device-side multi-channel handle.
 * @return HCCL_SUCCESS indicates success. Other values indicate an HCCL error.
 * @note The returned handle is managed by comm. The communicator must remain valid while a Kernel uses the handle.
 */
static inline HcclResult asccomm_make_multi_channel_handle(
    HcclComm comm, const char* shared_queue_tag, const HcclChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    return asccomm_host_make_multi_channel_handle(comm, shared_queue_tag, channel_descs, channel_num, multi_channel);
}

/*!
 * @brief Create a group of shared-Jetty channels from an Hcomm Endpoint and the multi-channel handle used on the
 * Device.
 * @param [in] endpoint_handle: The initialized Hcomm Endpoint that owns the channels.
 * @param [in] channel_descs: The channel descriptor array. Its order defines the channel_index used by asccomm_get_handle_ref.
 * @param [in] channel_num: The number of elements in channel_descs. It must be greater than 0.
 * @param [out] multi_channel: The created Device-side multi-channel handle.
 * @return HCCL_SUCCESS indicates success. Other values indicate an Hcomm error.
 * @note Destroy the returned handle before destroying endpoint_handle.
 */
static inline HcommResult asccomm_make_multi_channel_handle(
    EndpointHandle endpoint_handle, HcommChannelDesc* channel_descs, uint32_t channel_num,
    multi_channel_handle* multi_channel)
{
    return asccomm_host_make_multi_channel_handle(endpoint_handle, channel_descs, channel_num, multi_channel);
}

/*!
 * @brief Destroy channels and Device context owned by a handle created by asccomm_make_multi_channel_handle.
 * @param [in] multi_channel: The multi-channel handle to destroy.
 * @return HCCL_SUCCESS indicates success. Other values indicate an Hcomm error.
 * @note This is the destroy API corresponding to the asccomm_make_multi_channel_handle overload without an HCCL communicator (the
 * EndpointHandle overload). Do not use it for a handle created by the HcclComm overload.
 */
static inline HcommResult asccomm_destroy_multi_channel_handle(multi_channel_handle multi_channel)
{
    return asccomm_host_destroy_multi_channel_handle(multi_channel);
}

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_HOST_H
#endif

} // namespace asc

#endif // INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_HOST_H
