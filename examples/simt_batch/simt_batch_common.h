/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_BATCH_COMMON_H
#define EXAMPLES_SIMT_BATCH_COMMON_H

#include <cstddef>
#include <cstdint>

namespace simt_batch {

constexpr uint32_t kRootRank = 0U;

constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiRead = 1U;
constexpr uint32_t kApiNotify = 2U;
constexpr uint32_t kApiWriteValue = 3U;
constexpr uint32_t kApiFaa = 4U;
constexpr uint32_t kApiCas = 5U;
constexpr uint32_t kCaseMixed = 6U;
constexpr uint32_t kCaseSingle = 7U;
constexpr uint32_t kCaseCount = 8U;

// The common batch path submits two full batches and drains once. This covers each API as
// the publishing tail, more than one BatchCommit, and completion of outstanding batches.
constexpr uint32_t kOperationsPerBatch = 8U;
constexpr uint32_t kBatchCount = 2U;
constexpr uint32_t kOperationCount = kOperationsPerBatch * kBatchCount;
constexpr uint32_t kTargetCount = kOperationsPerBatch;
constexpr uint32_t kPayloadBytes = 64U;
constexpr size_t kBufferBytes = 4096U;

// The runnable basic mode intentionally stays linear: two WriteNbi appends, one commit and one
// drain. It shares the full suite's buffers and Host setup without exposing that test machinery
// in the basic Kernel.
constexpr uint32_t kBasicOperationCount = 2U;
constexpr int32_t kBasicStatusNotRun = -2;
constexpr uint32_t kBasicStatusInit = 0U;
constexpr uint32_t kBasicStatusHandle = 1U;
constexpr uint32_t kBasicStatusCapacity = 2U;
constexpr uint32_t kBasicStatusAppendFirst = 3U;
constexpr uint32_t kBasicStatusAppendSecond = 4U;
constexpr uint32_t kBasicStatusCommit = 5U;
constexpr uint32_t kBasicStatusDrain = 6U;
constexpr uint32_t kBasicFunctionalStatusCount = 7U;

constexpr size_t kWriteOffset = 0U;
constexpr size_t kReadOffset = kWriteOffset + kOperationCount * kPayloadBytes;
constexpr size_t kNotifyDataOffset = kReadOffset + kOperationCount * kPayloadBytes;
constexpr size_t kNotifySignalOffset = kNotifyDataOffset + kOperationCount * kPayloadBytes;
constexpr size_t kWriteValueOffset = kNotifySignalOffset + kOperationCount * sizeof(uint64_t);
constexpr size_t kFaaTargetOffset = kWriteValueOffset + kOperationCount * sizeof(uint64_t);
constexpr size_t kCasTargetOffset = kFaaTargetOffset + kTargetCount * sizeof(uint64_t);
constexpr size_t kFaaFetchOffset = kCasTargetOffset + kTargetCount * sizeof(uint64_t);
constexpr size_t kCasFetchOffset = kFaaFetchOffset + kOperationCount * sizeof(uint64_t);

constexpr size_t kMixedWriteOffset = kCasFetchOffset + kOperationCount * sizeof(uint64_t);
constexpr size_t kMixedReadOffset = kMixedWriteOffset + sizeof(uint64_t);
constexpr size_t kMixedNotifyDataOffset = kMixedReadOffset + sizeof(uint64_t);
constexpr size_t kMixedNotifySignalOffset = kMixedNotifyDataOffset + sizeof(uint64_t);
constexpr size_t kMixedWriteValueOffset = kMixedNotifySignalOffset + sizeof(uint64_t);
constexpr size_t kMixedFaaTargetOffset = kMixedWriteValueOffset + sizeof(uint64_t);
constexpr size_t kMixedCasTargetOffset = kMixedFaaTargetOffset + sizeof(uint64_t);
constexpr size_t kMixedFaaFetchOffset = kMixedCasTargetOffset + sizeof(uint64_t);
constexpr size_t kMixedCasFetchOffset = kMixedFaaFetchOffset + sizeof(uint64_t);
constexpr size_t kSingleWriteValueOffset = kMixedCasFetchOffset + sizeof(uint64_t);
static_assert(kSingleWriteValueOffset + sizeof(uint64_t) <= kBufferBytes, "test buffer is too small");

constexpr uint8_t kWriteByteBase = 0x10U;
constexpr uint8_t kReadByteBase = 0x40U;
constexpr uint8_t kNotifyByteBase = 0x70U;
constexpr uint64_t kNotifyValueBase = 0x2200U;
constexpr uint64_t kWriteValueBase = 0x3300U;
constexpr uint64_t kFaaInitial = 60U;
constexpr uint64_t kFaaAdd = 6U;
constexpr uint64_t kCasInitial = 70U;
constexpr uint64_t kCasSwap = 77U;

constexpr uint64_t kMixedWriteValue = 101U;
constexpr uint64_t kMixedReadValue = 102U;
constexpr uint64_t kMixedNotifyData = 103U;
constexpr uint64_t kMixedNotifyValue = 104U;
constexpr uint64_t kMixedInlineValue = 105U;
constexpr uint64_t kMixedFaaInitial = 106U;
constexpr uint64_t kMixedFaaAdd = 7U;
constexpr uint64_t kMixedCasInitial = 107U;
constexpr uint64_t kMixedCasSwap = 108U;
constexpr uint64_t kSingleWriteValue = 109U;

constexpr uint32_t kStatusFieldsPerCase = 4U;
constexpr uint32_t kStatusInit = 0U;
constexpr uint32_t kStatusHandle = 1U;
constexpr uint32_t kStatusHandleCapacity = 2U;
constexpr uint32_t kStatusSqDepth = 3U;
constexpr uint32_t kStatusCaseBase = 4U;
constexpr int32_t kStatusNotRun = -2;
constexpr uint32_t kStatusAppend = 0U;
constexpr uint32_t kStatusCommit = 1U;
constexpr uint32_t kStatusDrain = 2U;
constexpr uint32_t kStatusCommitCount = 3U;

constexpr uint32_t kFunctionalStatusCount = kStatusCaseBase + kCaseCount * kStatusFieldsPerCase;
constexpr uint32_t kStatusCount = kFunctionalStatusCount;
constexpr uint32_t kBasicStatusCount = kBasicFunctionalStatusCount;

} // namespace simt_batch

#endif // EXAMPLES_SIMT_BATCH_COMMON_H
