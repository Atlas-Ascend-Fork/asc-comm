/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_WRITE_PERF_COMMON_H
#define EXAMPLES_SIMT_WRITE_PERF_COMMON_H

#include <cstdint>

namespace simt_write_perf {

constexpr int kSenderRank = 0;
constexpr int kReceiverRank = 1;

// Byte the sender fills its payload with, and the value WriteValueNbi carries inline. Both are
// distinctive enough that a receive buffer left at its initial 0 is obvious.
constexpr uint8_t kPayloadByte = 0x5AU;
constexpr uint64_t kInlineValue = 0x1122334455667788ULL;

// WriteNbi with commit=true splits its payload across two SGEs so the WQE fills the 128-byte
// DWQE window that publishes it. A one-byte payload would give the first SGE a zero length,
// which URMA does not accept, so the smallest payload this example allows is two bytes.
constexpr uint32_t kMinPayloadBytes = 2U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kDrainStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kTimingWords = 4U;

// Which of the two write interfaces a run exercises. WRITE points the WQE at a local buffer
// through an SGE; WRITE_VALUE carries an 8-byte payload inline in the WQE itself.
constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiWriteValue = 1U;

} // namespace simt_write_perf

#endif // EXAMPLES_SIMT_WRITE_PERF_COMMON_H
