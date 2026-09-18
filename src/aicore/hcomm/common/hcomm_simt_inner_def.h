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
 * \file hcomm_simt_inner_def.h
 * \brief Hcomm SIMT inner definition
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H

#include <cstdint>
#include <cstddef>

// SIMT intrinsics used throughout the SIMT implementation: asc_atomic_add (reserve),
// asc_threadfence (ordering before the doorbell).
#include "simt_api/asc_simt.h"
#include "simt_api/device_atomic_functions.h"
#include "simt_api/device_sync_functions.h"
#include "simt_api/device_warp_functions.h"

#include "hcomm/hcomm_common.h"

#include "hcomm_inner_def.h"

namespace AscendC::simt {

// headAddr stores curHead in low 32 bits and wqeCnt in high 32 bits.
// A single lane drives a channel, so the reservation is a plain read-modify-write of both
// counters; Drain is likewise called by a single lane.
constexpr uint64_t HCOMM_SIMT_WQE_CNT_MASK = 0xFFFFFFFFULL;

// SIMT view of ChannelEntity: the shared definition stores typed host pointers, which the SIMT
// path cannot name, so every pointer member is viewed as a uint64_t here. The static_assert keeps
// this view locked to the shared layout.
struct HcommSimtChannelEntity {
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
    // Unused by SIMT, which packs curHead and wqeCnt into the single u64 at
    // SqContext::ubJfs::headAddr so both advance in one store. Listed only to keep this view
    // field-for-field identical to ChannelEntity.
    uint32_t sqHead;
    uint32_t sqTail;
    uint32_t cqHead;
    uint32_t cqTail;
    uint8_t reserve[144];
};

static_assert(sizeof(HcommSimtChannelEntity) == sizeof(ChannelEntity), "SIMT ChannelEntity view size mismatch");

// The handle is lane-private, while context points at the shared reservation state and publisher
// image. Append writes its final SQ slot directly; it does not retain a per-lane request image.
struct UbcCtpBatchHandle {
    ChannelHandle channel = 0U;
    __ubuf__ uint64_t* context = nullptr;
    uint32_t groupRank = 0U;
    uint32_t groupSize = 1U;
};

// Completion state is caller-owned and separate from the HCCL channel ABI. One owner lane
// operates a set; publishing lanes never modify this state. Each channel has its own window.
constexpr uint32_t HCOMM_SIMT_COMPLETION_VERSION = 1U;
constexpr uint32_t HCOMM_SIMT_COMPLETION_NONE = 0xFFFFFFFFU;
enum class HcommSimtCompletionFault : uint32_t {
    NONE = 0U,
    RESOURCE = 1U,
    IDENTITY = 2U,
    CQE = 3U,
    COUNT = 4U,
    TIMEOUT = 5U
};
struct alignas(128) HcommSimtCompletionState {
    uint32_t version, channelCount, cqCount, nextCq;
    uint32_t fault, errorCq, errorChannel, errorSequence;
    uint32_t errorWord0, errorWord1, errorWord2, reserved;
};
struct alignas(128) HcommSimtCompletionChannel {
    uint64_t channel, sqBase, headAddr, tailAddr;
    uint32_t jfsId, sqDepth, cqIndex, completed;
    uint32_t target, targetBb;
};
struct alignas(128) HcommSimtCompletionCq {
    uint64_t base, dbAddr;
    uint32_t jfcId, depth, cqeSize, consumed, acknowledged;
};
static_assert(sizeof(HcommSimtCompletionState) == 128U);
static_assert(sizeof(HcommSimtCompletionChannel) == 128U);
static_assert(sizeof(HcommSimtCompletionCq) == 128U);
static_assert(offsetof(HcommSimtCompletionState, reserved) + sizeof(uint32_t) <= 64U);
static_assert(offsetof(HcommSimtCompletionChannel, targetBb) + sizeof(uint32_t) <= 64U);
static_assert(offsetof(HcommSimtCompletionCq, acknowledged) + sizeof(uint32_t) <= 64U);

struct UbcCtpCompletionSet {
    __gm__ uint8_t* workspace = nullptr;
};

// Reserve enough CQ records for the worst case of one distinct CQ per channel.
inline constexpr uint64_t HcommSimtCompletionBufferBytes(uint32_t channelCount)
{
    return channelCount == 0U || channelCount > 65535U ? 0ULL : 128ULL * (1ULL + 2ULL * channelCount);
}

template <typename T>
struct ChannelTraits;

template <>
struct ChannelTraits<ChannelHandle> {
    using BatchHandleType = UbcCtpBatchHandle;
};

template <typename T>
using BatchHandle = typename ChannelTraits<T>::BatchHandleType;

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H
#endif
