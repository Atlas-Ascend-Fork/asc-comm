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

constexpr uint32_t kSenderRank = 0U;
constexpr uint32_t kReceiverRank = 1U;

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

// Isolated control path for diagnosing DWQE publication: one deferred Write followed by one
// immediate Write, matching the validated single-thread "batch last" usage without BatchHandle.
constexpr uint32_t kSingleLastStatusInit = 0U;
constexpr uint32_t kSingleLastStatusDeferred = 1U;
constexpr uint32_t kSingleLastStatusImmediate = 2U;
constexpr uint32_t kSingleLastStatusDrain = 3U;
constexpr uint32_t kSingleLastStatusCount = 4U;

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

// Diagnostics for the first WriteNbi append. Keep these in the example output buffer rather
// than in the batch implementation so observing a failure does not change the library path.
constexpr uint32_t kFunctionalStatusCount = kStatusCaseBase + kCaseCount * kStatusFieldsPerCase;
constexpr uint32_t kDiagBase = kFunctionalStatusCount;
constexpr uint32_t kDiagGuardMask = kDiagBase;
constexpr uint32_t kDiagAppendRet = kDiagBase + 1U;
constexpr uint32_t kDiagPreparedBefore = kDiagBase + 2U;
constexpr uint32_t kDiagPreparedAfter = kDiagBase + 3U;
constexpr uint32_t kDiagCapacity = kDiagBase + 4U;
constexpr uint32_t kDiagTargetAddrLow = kDiagBase + 5U;
constexpr uint32_t kDiagTargetAddrHigh = kDiagBase + 6U;
constexpr uint32_t kDiagTargetLenLow = kDiagBase + 7U;
constexpr uint32_t kDiagTargetLenHigh = kDiagBase + 8U;
constexpr uint32_t kDiagRemoteBaseLow = kDiagBase + 9U;
constexpr uint32_t kDiagRemoteBaseHigh = kDiagBase + 10U;
constexpr uint32_t kDiagRemoteSizeLow = kDiagBase + 11U;
constexpr uint32_t kDiagRemoteSizeHigh = kDiagBase + 12U;
constexpr uint32_t kDiagOffsetLow = kDiagBase + 13U;
constexpr uint32_t kDiagOffsetHigh = kDiagBase + 14U;
constexpr uint32_t kAppendDiagEnd = kDiagBase + 15U;

// Two structured commit snapshots for the first homogeneous Write case. Every 64-bit field
// occupies a low/high pair so the same int32_t GM status buffer can be printed by the Host.
constexpr uint32_t kCommitDiagWordCount = 20U;
constexpr uint32_t kCommitDiagHeadSnapshot = 0U;
constexpr uint32_t kCommitDiagHeadBefore = 2U;
constexpr uint32_t kCommitDiagHeadVal = 4U;
constexpr uint32_t kCommitDiagHeadAfter = 6U;
constexpr uint32_t kCommitDiagPrepared = 8U;
constexpr uint32_t kCommitDiagPreparedCqe = 9U;
constexpr uint32_t kCommitDiagSqTailBefore = 10U;
constexpr uint32_t kCommitDiagChunkItems = 11U;
constexpr uint32_t kCommitDiagTailItemHead = 12U;
constexpr uint32_t kCommitDiagTailUbWord0 = 13U;
constexpr uint32_t kCommitDiagTailSqWord0 = 15U;
constexpr uint32_t kCommitDiagRingReached = 17U;
constexpr uint32_t kCommitDiagPublishCount = 18U;
constexpr uint32_t kCommitDiagTailSqMismatchMask = 19U;
constexpr uint32_t kCommitDiagBase = kAppendDiagEnd;

constexpr uint32_t kDrainDiagBase = kCommitDiagBase + kBatchCount * kCommitDiagWordCount;
constexpr uint32_t kDrainDiagExpectedCqTail = kDrainDiagBase;
constexpr uint32_t kDrainDiagInitialCqTail = kDrainDiagBase + 1U;
constexpr uint32_t kDrainDiagFinalCqTail = kDrainDiagBase + 2U;
constexpr uint32_t kDrainDiagInitialSqTail = kDrainDiagBase + 3U;
constexpr uint32_t kDrainDiagFinalSqTail = kDrainDiagBase + 4U;
constexpr uint32_t kDrainDiagPollRet = kDrainDiagBase + 5U;
constexpr uint32_t kDrainDiagFailedCqIndex = kDrainDiagBase + 6U;
constexpr uint32_t kDrainDiagRetryCount = kDrainDiagBase + 7U;
constexpr uint32_t kDrainDiagCqeWord0 = kDrainDiagBase + 8U;
constexpr uint32_t kDrainDiagCqeWord1 = kDrainDiagBase + 9U;
constexpr uint32_t kStatusCount = kDrainDiagBase + 10U;

constexpr uint32_t kBasicCommitDiagBase = kBasicFunctionalStatusCount;
constexpr uint32_t kBasicDrainDiagBase = kBasicCommitDiagBase + kCommitDiagWordCount;
constexpr uint32_t kBasicStatusCount = kBasicDrainDiagBase + 10U;
static_assert(kCommitDiagTailSqMismatchMask + 1U == kCommitDiagWordCount, "commit diagnostic layout mismatch");
static_assert(kDrainDiagCqeWord1 + 1U == kStatusCount, "full status buffer layout mismatch");
static_assert(kBasicDrainDiagBase + 10U == kBasicStatusCount, "basic status buffer layout mismatch");

constexpr uint32_t kDiagInvalidHandle = 1U << 0U;
constexpr uint32_t kDiagCapacityExhausted = 1U << 1U;
constexpr uint32_t kDiagTargetLenTooLarge = 1U << 2U;
constexpr uint32_t kDiagNullTarget = 1U << 3U;
constexpr uint32_t kDiagTargetBelowBase = 1U << 4U;
constexpr uint32_t kDiagOffsetPastEnd = 1U << 5U;
constexpr uint32_t kDiagLenPastEnd = 1U << 6U;

} // namespace simt_batch

#endif // EXAMPLES_SIMT_BATCH_COMMON_H
