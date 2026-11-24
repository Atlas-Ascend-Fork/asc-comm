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

constexpr uint64_t SLOT_BYTES = sizeof(uint64_t);

// ---------------------------------------------------------------------------------------------
// Write family layout
//
// Number of uint64_t slots the sender writes to the receiver. Slot i carries the fixed value
// i + 1, so the buffer reads 1, 2, ... WRITE_SLOT_COUNT. A plain ascending sequence makes a wrong
// value obvious at a glance: 0 means nothing landed, and any other mismatch names the slot it
// should have come from.
constexpr uint32_t WRITE_SLOT_COUNT = 32U;

// The host fills the send buffer with these values and checks them on the receiving side; the
// write_value_nbi kernels pass them as inline payload, so this must be callable from device code
// too. The ASC driver also sema-checks the .asc host TU with the device arch macros in scope,
// so those macros alone cannot tell the two TUs apart: op_kernel.cpp defines
// SIMT_URMA_KERNEL_TU after pulling in the kernel headers (which provide __aicore__), and only
// that TU gets the device qualification. In the .asc host TU the helpers stay plain host code.
#if defined(SIMT_URMA_KERNEL_TU) && defined(__aicore__)
#define ASCCOMM_SIMT_URMA_DEVICE_FN __aicore__
#else
#define ASCCOMM_SIMT_URMA_DEVICE_FN
#endif

ASCCOMM_SIMT_URMA_DEVICE_FN constexpr // slot内写入的数据模式：按slot序号递增，用于收发双方对齐校验。
    uint64_t
    slot_value(uint32_t slot)
{
    return static_cast<uint64_t>(slot) + 1U;
}

// ---------------------------------------------------------------------------------------------
// Notify/atomic family layout
//
// The two families use disjoint slot assignments rather than a shared one: each was designed
// around what its own operations need to observe, and keeping them separate means a mode's
// expected-value table stays readable. The buffer is sized to hold whichever is larger.
constexpr uint32_t NOTIFY_DATA_SLOT = 0U;
constexpr uint32_t NOTIFY_SIGNAL_SLOT = 1U;
constexpr uint32_t FAA_TARGET_SLOT = 2U;
constexpr uint32_t CAS_TARGET_SLOT = 3U;
constexpr uint32_t FAA_FETCH_SLOT = 1U;
constexpr uint32_t CAS_FETCH_SLOT = 2U;
constexpr uint32_t NOTIFY_DRAIN_SLOT = 3U;
constexpr uint32_t FAA_DRAIN_SLOT = 4U;
constexpr uint32_t CAS_DRAIN_SLOT = 5U;

constexpr uint32_t NOTIFY_SLOT_COUNT = CAS_DRAIN_SLOT + 1U;

constexpr uint64_t NOTIFY_DATA = 0x1111111111111111ULL;
constexpr uint64_t NOTIFY_SIGNAL = 0x2222222222222222ULL;
constexpr uint64_t FAA_INITIAL = 10U;
constexpr uint64_t FAA_ADD = 3U;
constexpr uint64_t CAS_INITIAL = 20U;
constexpr uint64_t CAS_SWAP = 30U;

// One buffer serves both families, so it must cover the larger layout.
constexpr uint32_t SLOT_COUNT = WRITE_SLOT_COUNT > NOTIFY_SLOT_COUNT ? WRITE_SLOT_COUNT : NOTIFY_SLOT_COUNT;
constexpr uint64_t BUFFER_BYTES = SLOT_COUNT * SLOT_BYTES;

// Number of back-to-back immediate Notify posts the notify_immediate_repeat mode issues.
constexpr uint32_t NOTIFY_IMMEDIATE_REPEAT_COUNT = 100U;

// ---------------------------------------------------------------------------------------------
// Modes
//
// write_* 与 notify/atomic 的模式名分开：两族的 "single"/"batch_last" 含义不同——前者选择 32 次
// slot 写入如何发布，后者选择 Notify、FAA、CAS 三个操作如何组合。
constexpr uint32_t MODE_WRITE_SINGLE = 0U;     // one write_nbi<commit=true> per slot
constexpr uint32_t MODE_WRITE_BATCH_LAST = 1U; // N-1 serial write_nbi<false> + write_nbi<true>
// write_value_nbi carries the payload inline in the WQE instead of pointing at a local buffer,
// so the sender needs no send buffer at all for these two modes.
constexpr uint32_t MODE_WRITE_VALUE_SINGLE = 2U;
constexpr uint32_t MODE_WRITE_VALUE_BATCH_LAST = 3U;

constexpr uint32_t MODE_NOTIFY = 4U;                   // one write_with_notify_nbi, then Drain
constexpr uint32_t MODE_FAA = 5U;                      // one atomic_faa_nbi, checks remote sum and old value
constexpr uint32_t MODE_CAS = 6U;                      // one atomic_cas_nbi, checks remote swap and old value
constexpr uint32_t MODE_NOTIFY_ATOMIC_SINGLE = 7U;     // Notify, FAA, CAS serially, Drain after each
constexpr uint32_t MODE_NOTIFY_ATOMIC_BATCH_LAST = 8U; // Notify and FAA deferred, CAS publishes
constexpr uint32_t MODE_NOTIFY_IMMEDIATE_REPEAT = 9U;  // NOTIFY_IMMEDIATE_REPEAT_COUNT immediate posts

// True for the four write_* modes. Selects which buffer layout, expected-value table and kernel
// family a mode belongs to.
ASCCOMM_SIMT_URMA_DEVICE_FN // 是否为write系列模式（其接收侧期望为递增序列）。
    constexpr bool
    is_write_mode(uint32_t mode)
{
    return mode <= MODE_WRITE_VALUE_BATCH_LAST;
}

} // namespace simt_urma

#endif // EXAMPLES_SIMT_URMA_SIMT_URMA_COMMON_H
