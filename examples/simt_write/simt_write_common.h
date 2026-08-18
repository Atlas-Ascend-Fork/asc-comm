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
 * \file simt_write_common.h
 * \brief Constants shared by the SIMT WriteNbi example host and device code
 */

#ifndef EXAMPLES_SIMT_WRITE_SIMT_WRITE_COMMON_H
#define EXAMPLES_SIMT_WRITE_SIMT_WRITE_COMMON_H

#include <cstdint>

namespace simt_write {

// Only one rank drives the traffic. kSenderRank posts every WriteNbi into kRecvRank's
// buffer; kRecvRank launches no kernel and its NIC lands the data passively.
constexpr int kSenderRank = 0;
constexpr int kRecvRank = 1;

// Number of uint64_t slots the sender writes to the receiver.
constexpr uint32_t kSlotCount = 32U;
constexpr uint64_t kSlotBytes = sizeof(uint64_t);
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

// Slot i carries the fixed value i + 1, so the buffer reads 1, 2, ... kSlotCount. A plain
// ascending sequence makes a wrong value obvious at a glance: 0 means nothing landed, and any
// other mismatch names the slot it should have come from.
//
// The host fills the send buffer with these values and checks them on the receiving side.
// HCOMM_SIMT_WRITE_DEVICE_FN is empty in a host translation unit, where __aicore__ and the SIMT
// headers are unavailable.
#if defined(__CCE_KT_TEST__) || defined(__DAV_C310__) || defined(__NPU_ARCH__)
#define HCOMM_SIMT_WRITE_DEVICE_FN __aicore__
#else
#define HCOMM_SIMT_WRITE_DEVICE_FN
#endif

HCOMM_SIMT_WRITE_DEVICE_FN constexpr uint64_t SlotValue(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// Ways of driving the SIMT write interfaces.
constexpr uint32_t kModeSingle = 0U;    // one WriteNbi<commit=true>, single lane
constexpr uint32_t kModeBatchLast = 1U; // N-1 serial WriteNbi<false> + WriteNbi<true>, single lane
constexpr uint32_t kModeMultiLane = 2U; // N-1 concurrent WriteNbi<false>, barrier, then one WriteNbi<true>
constexpr uint32_t kMultiLaneCount = kSlotCount - 1U;

// Device-side diagnostic slots, copied back by the host after the sender kernel completes.
constexpr uint32_t kStatusInit = 0U;
constexpr uint32_t kStatusPost = 1U;
constexpr uint32_t kStatusDrain = 2U;
constexpr uint32_t kStatusCount = 3U;

// Hcomm::Init needs 128 bytes of WQE staging per lane in the block. Supplying 32 more bytes per
// lane plus one aligned 128-byte region also enables the per-lane remote-registration cache and
// the block-shared post context; both are optional and fall back to global-memory lookup.
constexpr uint32_t kWqeStagingBytes = 128U;
constexpr uint32_t kRemoteCacheBytes = 32U;
constexpr uint32_t kPostContextBytes = 128U;
constexpr uint32_t kWorkspaceAlign = 128U;

constexpr uint32_t HcommWorkspaceBytes(uint32_t laneCount)
{
    return (((laneCount * (kWqeStagingBytes + kRemoteCacheBytes)) + kWorkspaceAlign - 1U) & ~(kWorkspaceAlign - 1U)) +
           kPostContextBytes;
}

} // namespace simt_write

#endif // EXAMPLES_SIMT_WRITE_SIMT_WRITE_COMMON_H
