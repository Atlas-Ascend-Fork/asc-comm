/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_BATCH_PERF_COMPARE_COMMON_H
#define EXAMPLES_BATCH_PERF_COMPARE_COMMON_H

#include <cstdint>

namespace batch_perf_compare {

constexpr uint32_t kRootRank = 0U;
constexpr uint32_t kSlotCount = 64U;
constexpr uint32_t kMaxBatchSize = 256U;
constexpr uint32_t kMaxJettyCount = 8U;
constexpr uint32_t kSimtBatchBufferBytes = 256U;
constexpr uint32_t kSimdBatchBufferBytes = kMaxBatchSize * 2U * 64U;

constexpr uint8_t kPayloadByte = 0x5AU;
constexpr uint8_t kRemoteInitialByte = 0x3CU;
constexpr uint64_t kNotifyValue = 0x2222222222222222ULL;

constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiRead = 1U;
constexpr uint32_t kApiNotify = 2U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kPublishCountIndex = 4U;
constexpr uint32_t kDiagBaseIndex = 5U;
constexpr uint32_t kDiagWordsPerJetty = 32U;
constexpr uint32_t kTimingWords = kDiagBaseIndex + kMaxJettyCount * kDiagWordsPerJetty;

constexpr uint32_t kDiagPhaseNone = 0U;
constexpr uint32_t kDiagPhaseMakeHandle = 1U;
constexpr uint32_t kDiagPhaseWarmupAppend = 2U;
constexpr uint32_t kDiagPhaseWarmupCommit = 3U;
constexpr uint32_t kDiagPhaseWarmupDrain = 4U;
constexpr uint32_t kDiagPhaseTimedAppend = 5U;
constexpr uint32_t kDiagPhaseTimedCommit = 6U;
constexpr uint32_t kDiagPhaseTimedDrain = 7U;

} // namespace batch_perf_compare

#endif // EXAMPLES_BATCH_PERF_COMPARE_COMMON_H
