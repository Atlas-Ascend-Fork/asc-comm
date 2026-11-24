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
 * \file asccomm_simt_inner_def.h
 * \brief Hcomm SIMT inner definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_INNER_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_INNER_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_INNER_DEF_H

#include <cstdint>
#include <cstddef>

// SIMT intrinsics used throughout the SIMT implementation: asc_atomic_add (reserve),
// asc_threadfence (ordering before the doorbell).
#include "simt_api/asc_simt.h"
#include "simt_api/device_atomic_functions.h"
#include "simt_api/device_sync_functions.h"
#include "simt_api/device_warp_functions.h"

// The shared communication types are provided by the including public header chain (either
// asccomm/asccomm_common.h or the legacy hcomm_common.h forwarder resolves first). Best-effort
// self-sufficiency for internal-only inclusion paths:
#if __has_include("asccomm/asccomm_common.h")
#include "asccomm/asccomm_common.h"
#elif __has_include("hcomm/hcomm_common.h")
#include "hcomm/hcomm_common.h"
#endif

#include "asccomm_inner_def.h"

namespace asc::simt {
using namespace AscendC;

// head_addr stores cur_head in low 32 bits and wqe_cnt in high 32 bits.
// A single lane drives a channel, so the reservation is a plain read-modify-write of both
// counters; drain is likewise called by a single lane.
constexpr uint64_t ASCCOMM_SIMT_WQE_CNT_MASK = 0xFFFFFFFFULL;

// SIMT view of ChannelEntity: the shared definition stores typed host pointers, which the SIMT
// path cannot name, so every pointer member is viewed as a uint64_t here. The static_assert keeps
// this view locked to the shared layout.
struct asccomm_simt_channel_entity {
    CommAbiHeader abiHeader;
    CommEngine engine;
    int32_t protocol;
    uint32_t localNotifyNum;
    uint32_t remoteNotifyNum;
    uint32_t localBufferNum;
    uint32_t remoteBufferNum;
    uint32_t sqNum;
    uint32_t cqNum;
    uint64_t localNotifyAddr;
    uint64_t remoteNotifyAddr;
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;
    uint64_t sqContextAddr;
    uint64_t cqContextAddr;
    uint8_t resv1[48]; // for other data seg in aicpu
    // Unused by SIMT, which packs cur_head and wqe_cnt into the single u64 at
    // SqContext::ubJfs::headAddr so both advance in one store. RoCE AIV uses each value as
    // a WQEBB index. Listed only to keep this view field-for-field identical to ChannelEntity.
    uint32_t sqHead;
    uint32_t sqTail;
    uint32_t cqHead;
    uint32_t cqTail;
    uint8_t reserve[96];
};

static_assert(sizeof(asccomm_simt_channel_entity) == sizeof(ChannelEntity), "SIMT ChannelEntity view size mismatch");

// The handle is lane-private, while context points at the shared reservation state and publisher
// image. Append writes its final SQ slot directly; it does not retain a per-lane request image.
struct ubc_ctp_batch_handle {
    ChannelHandle channel = 0U;
    __ubuf__ uint64_t* context = nullptr;
    uint32_t group_rank = 0U;
    uint32_t group_size = 1U;
};

template <typename T>
struct channel_traits;

template <>
struct channel_traits<ChannelHandle> {
    using BatchHandleType = ubc_ctp_batch_handle;
};

template <typename T>
using batch_handle = typename channel_traits<T>::BatchHandleType;

} // namespace asc::simt

// Re-export the SIMT-local batch_handle mapping: the legacy AscendC::simt surface must keep
// resolving BatchHandle<ChannelHandle> to UbcCtpBatchHandle (not the SIMD UbcBatchHandle).
namespace AscendC::simt {
template <typename T>
using BatchHandle = asc::simt::batch_handle<T>;
} // namespace AscendC::simt

// Compatibility bridge for the legacy SIMT Hcomm public header: see the AscendC bridge in
// asccomm_inner_def.h. Single source of truth: namespace asc::simt.
namespace AscendC::simt {
using UbcCtpBatchHandle = asc::simt::ubc_ctp_batch_handle;
} // namespace AscendC::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_INNER_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_INNER_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_INNER_DEF_H
#endif
