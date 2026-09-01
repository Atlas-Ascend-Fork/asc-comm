/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_BATCH_PERF_COMMON_H
#define EXAMPLES_SIMT_BATCH_PERF_COMMON_H

#include <cstdint>

namespace simt_batch_perf {

constexpr uint32_t kRootRank = 0U;
constexpr uint32_t kSlotCount = 64U;
constexpr uint32_t kMaxBatchSize = 1024U;
constexpr uint32_t kMaxGroupLanes = 1024U;
// One shared 128-byte UB region holds the stable queue and remote Jetty context.
constexpr uint32_t kBatchItemBytes = 128U;
constexpr uint32_t kBatchBufferBytes = kBatchItemBytes;

constexpr uint8_t kPayloadByte = 0x5AU;
constexpr uint8_t kRemoteInitialByte = 0x3CU;
constexpr uint64_t kInlineValue = 0x1122334455667788ULL;
constexpr uint64_t kNotifyValue = 0x2222222222222222ULL;
constexpr uint64_t kFaaInitial = 10U;
constexpr uint64_t kFaaAdd = 3U;
constexpr uint64_t kCasInitial = 20U;
constexpr uint64_t kCasSwap = 30U;

constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiRead = 1U;
constexpr uint32_t kApiNotify = 2U;
constexpr uint32_t kApiWriteValue = 3U;
constexpr uint32_t kApiFaa = 4U;
constexpr uint32_t kApiCas = 5U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kPublishCountIndex = 4U;
constexpr uint32_t kFailurePhaseIndex = 5U;
constexpr uint32_t kFailureOperationIndex = 6U;
constexpr uint32_t kFailureActionIndex = 7U;
constexpr uint32_t kTimingWords = 8U;

constexpr uint32_t kPhaseSuccess = 0U;
constexpr uint32_t kPhaseInit = 1U;
constexpr uint32_t kPhaseWarmupSubmit = 2U;
constexpr uint32_t kPhaseWarmupDrain = 3U;
constexpr uint32_t kPhaseTimedSubmit = 4U;
constexpr uint32_t kPhaseTimedDrain = 5U;

constexpr uint32_t kActionNone = 0U;
constexpr uint32_t kActionImmediate = 1U;
constexpr uint32_t kActionAppend = 2U;
constexpr uint32_t kActionCommit = 3U;
constexpr uint32_t kActionDrain = 4U;

} // namespace simt_batch_perf

#endif // EXAMPLES_SIMT_BATCH_PERF_COMMON_H
