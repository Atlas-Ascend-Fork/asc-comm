/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_MULTI_JETTY_BATCH_PERF_COMMON_H
#define EXAMPLES_MULTI_JETTY_BATCH_PERF_COMMON_H

#include <cstdint>

namespace multi_jetty_batch_perf {

constexpr uint32_t kRootRank = 0U;
constexpr uint32_t kMaxJettyCount = 8U;
constexpr uint32_t kMaxBatchSize = 256U;
constexpr uint32_t kMaxActiveLanes = 1024U;
constexpr uint32_t kLocalSendIndex = 1U;

constexpr uint32_t kSimtBatchWorkspaceBytes = 256U;
constexpr uint32_t kSimdBatchWorkspaceBytes = kMaxBatchSize * 2U * 64U;

constexpr uint32_t kApiWrite = 0U;
constexpr uint32_t kApiRead = 1U;
constexpr uint32_t kApiNotify = 2U;

constexpr uint32_t kModeSimdSerial = 0U;
constexpr uint32_t kModeSimtSerial = 1U;
constexpr uint32_t kModeSimtParallel = 2U;

constexpr uint32_t kIssueCyclesIndex = 0U;
constexpr uint32_t kCompletionCyclesIndex = 1U;
constexpr uint32_t kStatusIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kDoorbellCountIndex = 4U;
constexpr uint32_t kTimingWords = 5U;

constexpr uint8_t kSendPatternBase = 0x21U;
constexpr uint8_t kRemotePatternBase = 0x91U;
constexpr uint64_t kNotifyValueBase = 0xA500000000000000ULL;

#if MULTI_JETTY_SCOPE_BUILD >= 6
// Private example diagnostic buffers; never part of the Hcomm handle or queue layout.
constexpr uint64_t kPublishAuditMagic = 0x5055424155444954ULL;
struct PublishAuditSummary {
    uint64_t magic, initialHead, publishedHead;
    uint64_t channel, sqContext, cqContext, sqBase, cqBase, sqTailAddr, cqTailAddr;
    uint32_t jfsId, jfcId, cqeSize, sampleStart;
    uint32_t cqDepth, sqDepth, cqTail, sqTail;
    uint32_t captured, requested, phase, status;
    uint32_t completionVersion, completed, target, targetBb, acknowledged;
    uint32_t warmupCompleted, warmupCq, warmupSq;
    uint32_t completionFault, errorCq, errorJetty, errorSequence, errorWord0, errorWord1, errorWord2;
};
constexpr uint32_t kCompletionWorkspaceOffset =
    (kMaxJettyCount * sizeof(PublishAuditSummary) + 127U) / 128U * 128U;
constexpr uint32_t kCompletionWorkspaceBytes = 4096U;
// Two diagnostic records in unused example-owned workspace, outside comm's state.
constexpr uint32_t kCompletionProfileOffset = kCompletionWorkspaceBytes - 256U;
struct CompletionProfile {
    uint64_t calls, probes, notReady, processed;
    uint64_t probeTicks, notReadyTicks, accountTicks, ackTicks, progressTicks;
};
static_assert(sizeof(CompletionProfile) <= 128U);
constexpr uint32_t kPublishAuditAllocationBytes = kCompletionWorkspaceOffset + kCompletionWorkspaceBytes;
struct PublishAuditCqe {
    uint32_t word0, word1, word2;
};
static_assert(sizeof(PublishAuditCqe) == 12U);
#endif

inline uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

inline uint32_t SlotBytes(uint32_t api, uint32_t payloadBytes)
{
    const uint32_t dataBytes = AlignUp(payloadBytes, sizeof(uint64_t));
    return api == kApiNotify ? dataBytes + sizeof(uint64_t) : dataBytes;
}

inline uint8_t PayloadPattern(bool remote, uint32_t jetty, uint32_t operation, uint32_t byte)
{
    const uint32_t base = remote ? kRemotePatternBase : kSendPatternBase;
    return static_cast<uint8_t>((base + jetty * 29U + operation * 7U + byte * 3U) & 0xFFU);
}

inline uint64_t NotifyValue(uint32_t jetty, uint32_t operation)
{
    return kNotifyValueBase | (static_cast<uint64_t>(jetty) << 32U) | operation;
}

} // namespace multi_jetty_batch_perf

#endif // EXAMPLES_MULTI_JETTY_BATCH_PERF_COMMON_H
