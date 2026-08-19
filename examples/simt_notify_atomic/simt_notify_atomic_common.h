/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_NOTIFY_ATOMIC_COMMON_H
#define EXAMPLES_SIMT_NOTIFY_ATOMIC_COMMON_H

#include <cstdint>

namespace simt_notify_atomic {

constexpr int kSenderRank = 0;
constexpr int kReceiverRank = 1;

constexpr uint32_t kNotifyDataSlot = 0U;
constexpr uint32_t kNotifySignalSlot = 1U;
constexpr uint32_t kFaaTargetSlot = 2U;
constexpr uint32_t kCasTargetSlot = 3U;
constexpr uint32_t kFinalNotifyDataSlot = 4U;
constexpr uint32_t kFinalNotifySignalSlot = 5U;
constexpr uint32_t kFaaFetchSlot = 1U;
constexpr uint32_t kCasFetchSlot = 2U;
constexpr uint32_t kNotifyDrainSlot = 3U;
constexpr uint32_t kFaaDrainSlot = 4U;
constexpr uint32_t kCasDrainSlot = 5U;

constexpr uint32_t kSlotCount = 6U;
constexpr uint64_t kSlotBytes = sizeof(uint64_t);
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

constexpr uint64_t kNotifyData = 0x1111111111111111ULL;
constexpr uint64_t kNotifySignal = 0x2222222222222222ULL;
constexpr uint64_t kFinalNotifySignal = 0x3333333333333333ULL;
constexpr uint64_t kFaaInitial = 10U;
constexpr uint64_t kFaaAdd = 3U;
constexpr uint64_t kCasInitial = 20U;
constexpr uint64_t kCasSwap = 30U;

constexpr uint32_t kModeSingle = 0U;
constexpr uint32_t kModeBatchLast = 1U;
constexpr uint32_t kModeMultiLane = 2U;
constexpr uint32_t kModeNotify = 3U;
constexpr uint32_t kModeFaa = 4U;
constexpr uint32_t kModeCas = 5U;
constexpr uint32_t kModeNotifyImmediateRepeat = 6U;
constexpr uint32_t kNotifyImmediateRepeatCount = 100U;
constexpr uint32_t kMultiLaneCount = 3U;

} // namespace simt_notify_atomic

#endif // EXAMPLES_SIMT_NOTIFY_ATOMIC_COMMON_H
