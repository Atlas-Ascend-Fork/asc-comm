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
 * \file simt_urma_common.h
 * \brief Constants shared by the SIMT URMA functional example host and device code
 */

#ifndef EXAMPLES_SIMT_URMA_SIMT_URMA_COMMON_H
#define EXAMPLES_SIMT_URMA_SIMT_URMA_COMMON_H

#include <cstdint>

namespace simt_urma {

constexpr uint64_t kSlotBytes = sizeof(uint64_t);

// ---------------------------------------------------------------------------------------------
// Write family layout
//
// Number of uint64_t slots the sender writes to the receiver. Slot i carries the fixed value
// i + 1, so the buffer reads 1, 2, ... kWriteSlotCount. A plain ascending sequence makes a wrong
// value obvious at a glance: 0 means nothing landed, and any other mismatch names the slot it
// should have come from.
constexpr uint32_t kWriteSlotCount = 32U;

// The host fills the send buffer with these values and checks them on the receiving side; the
// WriteValueNbi kernels pass them as inline payload, so this must be callable from device code
// too. HCOMM_SIMT_URMA_DEVICE_FN is empty in a host translation unit, where __aicore__ and the
// SIMT headers are unavailable.
#if defined(__CCE_KT_TEST__) || defined(__DAV_C310__) || defined(__NPU_ARCH__)
#define HCOMM_SIMT_URMA_DEVICE_FN __aicore__
#else
#define HCOMM_SIMT_URMA_DEVICE_FN
#endif

HCOMM_SIMT_URMA_DEVICE_FN constexpr uint64_t SlotValue(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// ---------------------------------------------------------------------------------------------
// Notify/atomic family layout
//
// The two families use disjoint slot assignments rather than a shared one: each was designed
// around what its own operations need to observe, and keeping them separate means a mode's
// expected-value table stays readable. The buffer is sized to hold whichever is larger.
constexpr uint32_t kNotifyDataSlot = 0U;
constexpr uint32_t kNotifySignalSlot = 1U;
constexpr uint32_t kFaaTargetSlot = 2U;
constexpr uint32_t kCasTargetSlot = 3U;
constexpr uint32_t kFaaFetchSlot = 1U;
constexpr uint32_t kCasFetchSlot = 2U;
constexpr uint32_t kNotifyDrainSlot = 3U;
constexpr uint32_t kFaaDrainSlot = 4U;
constexpr uint32_t kCasDrainSlot = 5U;

constexpr uint32_t kNotifySlotCount = kCasDrainSlot + 1U;

constexpr uint64_t kNotifyData = 0x1111111111111111ULL;
constexpr uint64_t kNotifySignal = 0x2222222222222222ULL;
constexpr uint64_t kFaaInitial = 10U;
constexpr uint64_t kFaaAdd = 3U;
constexpr uint64_t kCasInitial = 20U;
constexpr uint64_t kCasSwap = 30U;

// One buffer serves both families, so it must cover the larger layout.
constexpr uint32_t kSlotCount = kWriteSlotCount > kNotifySlotCount ? kWriteSlotCount : kNotifySlotCount;
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

// Number of back-to-back immediate Notify posts the notify_immediate_repeat mode issues.
constexpr uint32_t kNotifyImmediateRepeatCount = 100U;

// ---------------------------------------------------------------------------------------------
// Modes
//
// write_* 与 notify/atomic 的模式名分开：两族的 "single"/"batch_last" 含义不同——前者选择 32 次
// slot 写入如何发布，后者选择 Notify、FAA、CAS 三个操作如何组合。
constexpr uint32_t kModeWriteSingle = 0U;    // one WriteNbi<commit=true> per slot
constexpr uint32_t kModeWriteBatchLast = 1U; // N-1 serial WriteNbi<false> + WriteNbi<true>
// WriteValueNbi carries the payload inline in the WQE instead of pointing at a local buffer,
// so the sender needs no send buffer at all for these two modes.
constexpr uint32_t kModeWriteValueSingle = 2U;
constexpr uint32_t kModeWriteValueBatchLast = 3U;

constexpr uint32_t kModeNotify = 4U;                // one WriteWithNotifyNbi, then Drain
constexpr uint32_t kModeFaa = 5U;                   // one AtomicFAA, checks remote sum and old value
constexpr uint32_t kModeCas = 6U;                   // one AtomicCAS, checks remote swap and old value
constexpr uint32_t kModeNotifyAtomicSingle = 7U;    // Notify, FAA, CAS serially, Drain after each
constexpr uint32_t kModeNotifyAtomicBatchLast = 8U; // Notify and FAA deferred, CAS publishes
constexpr uint32_t kModeNotifyImmediateRepeat = 9U; // kNotifyImmediateRepeatCount immediate posts

// True for the four write_* modes. Selects which buffer layout, expected-value table and kernel
// family a mode belongs to.
HCOMM_SIMT_URMA_DEVICE_FN constexpr bool IsWriteMode(uint32_t mode) { return mode <= kModeWriteValueBatchLast; }

} // namespace simt_urma

#endif // EXAMPLES_SIMT_URMA_SIMT_URMA_COMMON_H
