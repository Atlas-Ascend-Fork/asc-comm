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

// A batch handle is lane-private. The public workspace contract keeps 128 bytes per operation.
// A bound group also records its lane rank so Commit can place its compact WQE by group prefix.
struct UbcCtpBatchHandle {
    ChannelHandle channel = 0U;
    __ubuf__ uint64_t* buffer = nullptr;
    uint32_t capacity = 0U;
    uint32_t bufferBbCapacity = 0U;
    uint32_t prepared = 0U;
    uint32_t preparedBb = 0U;
    uint32_t lastBb = 0U;
    uint32_t preparedCqe = 0U;
    uint32_t lastCqe = 0U;
    uint32_t groupRank = 0U;
    uint32_t groupSize = 1U;
    uint64_t remoteBaseAddr = 0U;
    uint64_t remoteSize = 0U;
    __gm__ uint64_t* headAddr = nullptr;
    __gm__ uint32_t* sqTailAddr = nullptr;
    __gm__ uint8_t* dwqeAddr = nullptr;
    uint64_t sqBaseAddr = 0U;
    uint32_t sqWqeSize = 0U;
    uint32_t sqDepth = 0U;
    uint32_t tpId = 0U;
    uint64_t remoteEidL = 0U;
    uint64_t remoteEidH = 0U;
    uint32_t remoteTokenId = 0U;
    uint32_t remoteTokenValue = 0U;
    uint64_t headSnapshot = 0U;
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    uint64_t profilePlanCycles = 0U;
    uint64_t profileEmitCycles = 0U;
    uint64_t profilePublishCycles = 0U;
#endif
    bool valid = false;
};

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
