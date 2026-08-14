/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_NOTIFY_ATOMIC_PERF_COMMON_H
#define EXAMPLES_SIMT_NOTIFY_ATOMIC_PERF_COMMON_H

#include <cstdint>

namespace simt_notify_atomic_perf {

constexpr int kSenderRank = 0;
constexpr int kReceiverRank = 1;

constexpr uint64_t kNotifySignal = 0x2222222222222222ULL;
constexpr uint64_t kFaaInitial = 10U;
constexpr uint64_t kFaaAdd = 3U;
constexpr uint64_t kCasInitial = 20U;
constexpr uint64_t kCasSwap = 30U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kDrainStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kTimingWords = 4U;

} // namespace simt_notify_atomic_perf

#endif // EXAMPLES_SIMT_NOTIFY_ATOMIC_PERF_COMMON_H
