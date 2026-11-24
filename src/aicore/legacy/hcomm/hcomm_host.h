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

ASC_DEPRECATED(9.3.0, "2027/12/31", make_multi_channel_handle)
static inline HcclResult MakeMultiChannelHandle(
    HcclComm comm, const char* sharedQueueTag, const HcclChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface MakeMultiChannelHandle is deprecated. "
                     "Please use make_multi_channel_handle instead.");
    return make_multi_channel_handle(comm, sharedQueueTag, channelDescs, channelNum, multiChannel);
}

ASC_DEPRECATED(9.3.0, "2027/12/31", make_multi_channel_handle)
static inline HcommResult MakeMultiChannelHandle(
    EndpointHandle endpointHandle, HcommChannelDesc* channelDescs, uint32_t channelNum,
    MultiChannelHandle* multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface MakeMultiChannelHandle is deprecated. "
                     "Please use make_multi_channel_handle instead.");
    return make_multi_channel_handle(endpointHandle, channelDescs, channelNum, multiChannel);
}

ASC_DEPRECATED(9.3.0, "2027/12/31", destroy_multi_channel_handle)
static inline HcommResult DestroyMultiChannelHandle(MultiChannelHandle multiChannel)
{
    ASC_CPU_LOG_INFO("Hcomm legacy interface DestroyMultiChannelHandle is deprecated. "
                     "Please use destroy_multi_channel_handle instead.");
    return destroy_multi_channel_handle(multiChannel);
}

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_HOST_H
#endif

#endif // INCLUDE_HCOMM_HCOMM_HOST_H
