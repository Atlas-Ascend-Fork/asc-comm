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

constexpr uint32_t ROOT_RANK = 0U;
constexpr uint32_t SLOT_COUNT = 64U;
constexpr uint32_t MAX_BATCH_SIZE = 1024U;
constexpr uint32_t MAX_GROUP_LANES = 1024U;
// One 128-byte block holds the shared context and one holds the publisher DWQE image.
constexpr uint32_t BATCH_ITEM_BYTES = 128U;
constexpr uint32_t BATCH_BUFFER_BYTES = 2U * BATCH_ITEM_BYTES;

constexpr uint8_t PAYLOAD_BYTE = 0x5AU;
constexpr uint8_t REMOTE_INITIAL_BYTE = 0x3CU;
constexpr uint64_t INLINE_VALUE = 0x1122334455667788ULL;
constexpr uint64_t NOTIFY_VALUE = 0x2222222222222222ULL;
constexpr uint64_t FAA_INITIAL = 10U;
constexpr uint64_t FAA_ADD = 3U;
constexpr uint64_t CAS_INITIAL = 20U;
constexpr uint64_t CAS_SWAP = 30U;

constexpr uint32_t API_WRITE = 0U;
constexpr uint32_t API_READ = 1U;
constexpr uint32_t API_NOTIFY = 2U;
constexpr uint32_t API_WRITE_VALUE = 3U;
constexpr uint32_t API_FAA = 4U;
constexpr uint32_t API_CAS = 5U;

constexpr uint32_t ISSUE_CYCLES_INDEX = 0U;
constexpr uint32_t COMPLETION_CYCLES_INDEX = 1U;
constexpr uint32_t STATUS_INDEX = 2U;
constexpr uint32_t COMPLETED_INDEX = 3U;
constexpr uint32_t PUBLISH_COUNT_INDEX = 4U;
constexpr uint32_t TIMING_WORDS = 5U;

} // namespace simt_batch_perf

#endif // EXAMPLES_SIMT_BATCH_PERF_COMMON_H
