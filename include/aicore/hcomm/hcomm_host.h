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

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H
#endif

#include "../../../impl/comm_api/aicore/hcomm/host/hcomm_host_impl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*!
 * @brief Create a group of shared-Jetty channels and the multi-channel handle used on the Device.
 * @param [in] comm: The initialized HCCL communicator that owns the channels and Device context.
 * @param [in] sharedQueueTag: The tag identifying the shared queue used by this channel group.
 * @param [in] channelDescs: The channel descriptor array. Its order defines the channelIndex used by GetHandleRef.
 * @param [in] channelNum: The number of elements in channelDescs. It must be greater than 0.
 * @param [out] multiChannel: The created Device-side multi-channel handle.
 * @return HCCL_SUCCESS indicates success. Other values indicate an HCCL error.
 * @note The returned handle is managed by comm. The communicator must remain valid while a Kernel uses the handle.
 */
static inline HcclResult MakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    return HcommHostMakeMultiChannelHandle(comm, sharedQueueTag, channelDescs, channelNum, multiChannel);
}

#ifdef __cplusplus
}
#endif

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H
#endif

#endif // INCLUDE_HCOMM_HCOMM_HOST_H
