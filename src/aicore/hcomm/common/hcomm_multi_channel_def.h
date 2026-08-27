/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_multi_channel_def.h
 * \brief Shared Host and AICore multi-channel definitions
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_MULTI_CHANNEL_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_MULTI_CHANNEL_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_MULTI_CHANNEL_DEF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpId;
    uint64_t remoteEidLow;
    uint64_t remoteEidHigh;
} MultiChannelRemoteInfo;

typedef struct {
    uint64_t channelHandle;
    uint32_t channelNum;
    uint32_t reserved;
    uint64_t remoteInfoAddr;
} MultiChannelEntity;

#ifdef __cplusplus
namespace AscendC {
using ::MultiChannelEntity;
using ::MultiChannelRemoteInfo;
} // namespace AscendC
#endif

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_MULTI_CHANNEL_DEF_H

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_MULTI_CHANNEL_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_MULTI_CHANNEL_DEF_H
#endif
