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

// Number of uint64_t slots the sender writes to the receiver. The multi-lane mode maps one
// deferred post per lane onto the first kSlotCount - 1 slots, so this also sets how many lanes
// contend for SQ space concurrently.
constexpr uint32_t kSlotCount = 32U;
constexpr uint64_t kSlotBytes = sizeof(uint64_t);
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

// Slot i carries the fixed value i + 1, so the buffer reads 1, 2, ... kSlotCount. A plain
// ascending sequence makes a wrong value obvious at a glance: 0 means nothing landed, and any
// other mismatch names the slot it should have come from.
//
// The host fills the send buffer with these values and checks them on the receiving side; the
// WriteValueNbi kernels pass them as inline payload, so this must be callable from device code
// too. HCOMM_SIMT_WRITE_DEVICE_FN is empty in a host translation unit, where __aicore__ and the
// SIMT headers are unavailable.
#if defined(__CCE_KT_TEST__) || defined(__DAV_C310__) || defined(__NPU_ARCH__)
#define HCOMM_SIMT_WRITE_DEVICE_FN __aicore__
#else
#define HCOMM_SIMT_WRITE_DEVICE_FN
#endif

HCOMM_SIMT_WRITE_DEVICE_FN constexpr uint64_t SlotValue(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// Ways of driving the SIMT write interfaces.
// Every Hcomm interface is called by a single lane, so all modes below drive one lane.
constexpr uint32_t kModeSingle = 0U;    // one WriteNbi<commit=true> per slot, single lane
constexpr uint32_t kModeBatchLast = 1U; // N-1 serial WriteNbi<false> + WriteNbi<true>, single lane
// WriteValueNbi carries the payload inline in the WQE instead of pointing at a local buffer,
// so the sender needs no send buffer at all for this mode.
constexpr uint32_t kModeValueSingle = 2U;    // one WriteValueNbi<commit=true> per slot, single lane
constexpr uint32_t kModeValueBatchLast = 3U; // N-1 serial WriteValueNbi<false> + WriteValueNbi<true>

} // namespace simt_write

#endif // EXAMPLES_SIMT_WRITE_SIMT_WRITE_COMMON_H
