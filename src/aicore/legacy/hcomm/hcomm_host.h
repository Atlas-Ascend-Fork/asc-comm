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

#include <stdint.h>

#include "hccl/hccl_res.h"

/** Device address of the shared-Jetty multi-channel entity. */
typedef uint64_t MultiChannelHandle;

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H
#endif

#if __has_include("../../../../impl/comm_api/aicore/asccomm/host/asccomm_host_impl.h")
#include "../../../../impl/comm_api/aicore/asccomm/host/asccomm_host_impl.h"
#else
#include "../../../impl/comm_api/aicore/asccomm/host/asccomm_host_impl.h"
#endif

// Deprecation attribute for the legacy Hcomm interfaces. Definition mirrors the devkit
// c_api/defs/macro.h so both headers can coexist in one translation unit.
// hcomm_common.h is deliberately not included here: it reaches the kernel-side basic
// interface headers, which are not available to host translation units.
#ifndef ASC_DEPRECATED
#define ASC_DEPRECATED(version, removal_date, replacement)                                                  \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif

/*!
 * @brief Create a group of communicator-owned shared-Jetty channels and the multi-channel handle used on the Device.
 * @param [in] comm: The initialized HCCL communicator that owns the channels and Device context.
 * @param [in] sharedQueueTag: The tag identifying the shared queue used by this channel group.
 * @param [in] channelDescs: The channel descriptor array. Its order defines the channelIndex used by GetHandleRef.
 * @param [in] channelNum: The number of elements in channelDescs. It must be greater than 0.
 * @param [out] multiChannel: The created Device-side multi-channel handle.
 * @return HCCL_SUCCESS indicates success. Other values indicate an HCCL error.
 * @note The returned handle is managed by comm. The communicator must remain valid while a Kernel uses the handle.
 */
ASC_DEPRECATED(9.3.0, "2027/12/31", asccomm_make_multi_channel_handle)
static inline HcclResult MakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface MakeMultiChannelHandle is deprecated. "
                     "Please use asccomm_make_multi_channel_handle instead.");
    return asccomm_host_make_multi_channel_handle(comm, sharedQueueTag, channelDescs, channelNum, multiChannel);
}

/*!
 * @brief Create a group of shared-Jetty channels from an Hcomm Endpoint and the multi-channel handle used on the
 * Device.
 * @param [in] endpointHandle: The initialized Hcomm Endpoint that owns the channels.
 * @param [in] channelDescs: The channel descriptor array. Its order defines the channelIndex used by GetHandleRef.
 * @param [in] channelNum: The number of elements in channelDescs. It must be greater than 0.
 * @param [out] multiChannel: The created Device-side multi-channel handle.
 * @return HCCL_SUCCESS indicates success. Other values indicate an Hcomm error.
 * @note Destroy the returned handle before destroying endpointHandle.
 */
ASC_DEPRECATED(9.3.0, "2027/12/31", asccomm_make_multi_channel_handle)
static inline HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface MakeMultiChannelHandle is deprecated. "
                     "Please use asccomm_make_multi_channel_handle instead.");
    return asccomm_host_make_multi_channel_handle(endpointHandle, channelDescs, channelNum, multiChannel);
}

/*!
 * @brief Destroy channels and Device context owned by a handle created by MakeMultiChannelHandle.
 * @param [in] multiChannel: The multi-channel handle to destroy.
 * @return HCCL_SUCCESS indicates success. Other values indicate an Hcomm error.
 * @note This is the destroy API corresponding to the MakeMultiChannelHandle overload without an HCCL communicator (the
 * EndpointHandle overload). Do not use it for a handle created by the HcclComm overload.
 */
ASC_DEPRECATED(9.3.0, "2027/12/31", asccomm_destroy_multi_channel_handle)
static inline HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface DestroyMultiChannelHandle is deprecated. "
                     "Please use asccomm_destroy_multi_channel_handle instead.");
    return asccomm_host_destroy_multi_channel_handle(multiChannel);
}

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H
#endif

#endif // INCLUDE_HCOMM_HCOMM_HOST_H
