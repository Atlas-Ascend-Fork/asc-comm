/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
#include "multi_jetty_batch_perf_common.h"

namespace multi_jetty_batch_perf {
constexpr uint32_t kJettyIdentityShift = 32U;
constexpr uint64_t kIdentityMultiplier = 0xD6E8FEB86659FD93ULL;
constexpr uint64_t kRemoteIdentityMask = 0xA5A5A5A5A5A5A5A5ULL;

uint64_t IdentityWord(bool remote, uint32_t jetty, uint32_t operation, uint32_t word)
{
    return ((static_cast<uint64_t>(jetty) << kJettyIdentityShift) | operation) ^ (kIdentityMultiplier * (word + 1U)) ^
           (remote ? kRemoteIdentityMask : 0ULL);
}

uint8_t IdentityByte(bool remote, uint32_t jetty, uint32_t operation, uint32_t byte)
{
    return static_cast<uint8_t>(IdentityWord(remote, jetty, operation, byte / 8U) >> ((byte % 8U) * 8U));
}

bool CheckCompletion(
    const std::vector<CompletionSummary>& summaries, uint32_t warmup, uint32_t operationCount, uint32_t batchSize,
    uint32_t api = 0U, uint32_t jettysPerKernel = 1024U)
{
    bool valid = !summaries.empty() && batchSize != 0U && jettysPerKernel != 0U;
    if (!valid)
        return false;
    for (uint32_t index = 0U; index < summaries.size(); ++index) {
        const auto& summary = summaries[index];
        const uint32_t first = index / jettysPerKernel * jettysPerKernel;
        const uint32_t end = std::min(static_cast<uint32_t>(summaries.size()), first + jettysPerKernel);
        uint32_t priorMembers = 0U, members = 0U;
        for (uint32_t other = 0U; other < end; ++other) {
            if (summaries[other].cqBase != summary.cqBase || summaries[other].jfcId != summary.jfcId)
                continue;
            if (other < first)
                ++priorMembers;
            else
                ++members;
        }
        const uint32_t bbEnd = api == 2U ? 2U * operationCount : operationCount + operationCount / batchSize;
        const uint32_t warmupBb = api == 2U ? 2U * warmup : warmup + warmup / batchSize;
        valid = valid && summary.magic == kCompletionSummaryMagic && summary.completionVersion == 1U &&
                summary.status == 0U && summary.completionFault == 0U && summary.warmupCompleted == warmup &&
                summary.warmupCq == priorMembers * operationCount + members * warmup && summary.warmupSq == warmupBb &&
                summary.completed == operationCount && summary.target == operationCount && summary.targetBb == bbEnd &&
                summary.sqTail == bbEnd &&
                summary.publishedHead == ((static_cast<uint64_t>(operationCount) << 32U) | bbEnd) &&
                summary.cqTail == (priorMembers + members) * operationCount && summary.acknowledged == summary.cqTail;
    }
    std::cout << "COMPLETION_CHECK | Status=" << (valid ? "PASS" : "FAIL") << std::endl;
    return valid;
}

// Check every byte and every Notify flag, including warmup requests.
inline bool CheckIdentityBuffer(
    const std::vector<uint8_t>& actual, bool remote, uint32_t jettyCount, uint32_t operationCount,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t batchSize, uint32_t api)
{
    uint64_t badRequests = 0, badNotifies = 0;
    for (uint32_t jetty = 0; jetty < jettyCount; ++jetty) {
        for (uint32_t op = 0; op < operationCount; ++op) {
            const size_t offset = static_cast<size_t>(jetty) * regionBytes + static_cast<size_t>(op) * slotBytes;
            bool bad = false;
            for (uint32_t byte = 0; byte < payloadBytes; ++byte)
                bad |= actual[offset + byte] != IdentityByte(remote, jetty, op, byte);
            if (bad && badRequests++ < 8)
                std::cerr << "data mismatch: jetty=" << jetty << ", operation=" << op << ", batch=" << op / batchSize
                          << std::endl;
            if (api == kApiNotify) {
                uint64_t value;
                std::memcpy(&value, actual.data() + offset + slotBytes - sizeof(value), sizeof(value));
                if (value != NotifyValue(jetty, op) && badNotifies++ < 8)
                    std::cerr << "notify mismatch: jetty=" << jetty << ", operation=" << op << std::endl;
            }
        }
    }
    std::cout << "DATA_AUDIT_SUMMARY | CheckedRequests=" << static_cast<uint64_t>(jettyCount) * operationCount
              << " | BadRequests=" << badRequests << std::endl;
    if (api == kApiNotify)
        std::cout << "NOTIFY_AUDIT_SUMMARY | BadNotifies=" << badNotifies << std::endl;
    return badRequests == 0 && badNotifies == 0;
}

inline void PrintCompletionState(const std::vector<CompletionSummary>& summaries)
{
    for (uint32_t jetty = 0; jetty < summaries.size(); ++jetty) {
        const auto& s = summaries[jetty];
        std::cout << "COMPLETION_STATE | Jetty=" << jetty << " | Phase=" << s.phase << " | Completed=" << s.completed
                  << " | Target=" << s.target << " | TargetBb=" << s.targetBb << " | SqTail=" << s.sqTail
                  << " | CqConsumed=" << s.cqTail << " | CqAcknowledged=" << s.acknowledged << std::endl;
        if (s.completionFault || s.status)
            std::cerr << "COMPLETION_ERROR | Jetty=" << jetty << " | Status=" << s.status
                      << " | Kind=" << s.completionFault << " | ErrorJetty=" << s.errorJetty << " | Cq=" << s.errorCq
                      << " | Sequence=" << s.errorSequence << " | Word0=" << s.errorWord0 << " | Word1=" << s.errorWord1
                      << " | Word2=" << s.errorWord2 << std::endl;
    }
}
} // namespace multi_jetty_batch_perf
