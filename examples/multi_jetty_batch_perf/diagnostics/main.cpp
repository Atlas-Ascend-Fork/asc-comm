/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "multi_jetty_batch_perf_common.h"

#define CHECK_ACL(expression)                                                                    \
    do {                                                                                         \
        const aclError result = (expression);                                                    \
        if (result != ACL_SUCCESS) {                                                             \
            std::cerr << "ACL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result \
                      << std::endl;                                                              \
            return result;                                                                       \
        }                                                                                        \
    } while (0)

#define CHECK_HCCL(expression)                                                                    \
    do {                                                                                          \
        const HcclResult result = (expression);                                                   \
        if (result != HCCL_SUCCESS) {                                                             \
            std::cerr << "HCCL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result \
                      << std::endl;                                                               \
            return result;                                                                        \
        }                                                                                         \
    } while (0)

#if MULTI_JETTY_ENABLE_SIMD
extern void LaunchSimdMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteAddrs, void* local, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount);
#endif
#if MULTI_JETTY_ENABLE_SIMT
extern void LaunchSimtMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize,
    uint32_t jettyCount, uint32_t mode, uint32_t firstJetty, uint32_t kernelJettyCount,
    void* traces, uint32_t traceBatchesPerJetty, void* sqTraces, uint32_t traceOperationsPerJetty);
#endif

namespace {
using namespace multi_jetty_batch_perf;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMilliseconds = 100U;
constexpr CommProtocol kProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendTag = "multiJettyBatchPerfSend";
constexpr const char* kRecvTag = "multiJettyBatchPerfRecv";
constexpr uint32_t kModeSimtOneActiveGroup = 3U;
constexpr uint32_t kModeSimtSerialCommit = 4U;
constexpr uint64_t kTraceMagic = 0x53494D5442415443ULL;
constexpr uint32_t kTraceWqeWords = 12U;
constexpr uint32_t kTraceMaxWqeWords = 16U;

struct alignas(128) SimtBatchTraceRecord {
    uint64_t magic, jetty, operationBegin, previousHead, head, reservedHead, sqTail;
    uint64_t workspaceAddr, sqBaseAddr, headAddr, sqTailAddr, dwqeAddr, tailSqAddr, nextSqAddr;
    uint64_t sqWqeSize, sqDepth, itemBb, groupSize, reservationStatus, appendStatus, commitStatus;
    uint64_t sqWqe[kTraceWqeWords], publisherWqe[kTraceWqeWords];
    uint64_t drainPhase, drainStatus, drainHead, drainExpectedCqTail, drainCqTail, drainSqTail;
    uint64_t drainCqeWord0, drainCqeWord1, drainExpectedOwner, drainRetry, drainReason;
    uint64_t guardBeforeMismatch, guardAfterMismatch;
};

struct alignas(128) SimtSqTraceRecord {
    uint64_t magic, jetty, operation, logicalHead, sqAddr, sqDepth, wordCount;
    uint64_t word1Base, remoteEidL, remoteEidH, tokenWord;
    uint64_t words[kTraceMaxWqeWords];
};

#if MULTI_JETTY_SCOPE_BUILD >= 6
// Every 8-byte word independently identifies the complete request and its word position.
uint64_t IdentityWord(bool remote, uint32_t jetty, uint32_t operation, uint32_t word)
{
    return ((static_cast<uint64_t>(jetty) << 32U) | operation) ^
        (0xD6E8FEB86659FD93ULL * (word + 1U)) ^ (remote ? 0xA5A5A5A5A5A5A5A5ULL : 0ULL);
}

uint8_t IdentityByte(bool remote, uint32_t jetty, uint32_t operation, uint32_t byte)
{
    return static_cast<uint8_t>(IdentityWord(remote, jetty, operation, byte / 8U) >> ((byte % 8U) * 8U));
}

bool CheckIdentityBuffer(const std::vector<uint8_t>& actual, bool remote, uint32_t jettyCount,
    uint32_t operationCount, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes,
    uint32_t batchSize = 256U, uint32_t api = kApiWrite)
{
    uint32_t badRequests = 0U, badPublishers = 0U, badNotifies = 0U;
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        for (uint32_t operation = 0U; operation < operationCount; ++operation) {
            const size_t offset = static_cast<size_t>(jetty) * regionBytes +
                static_cast<size_t>(operation) * slotBytes;
            uint32_t bad[2] = {}, initial[2] = {};
            for (uint32_t byte = 0U; byte < payloadBytes; ++byte) {
                const uint32_t half = byte < payloadBytes / 2U ? 0U : 1U;
                bad[half] += actual[offset + byte] != IdentityByte(remote, jetty, operation, byte);
                initial[half] += actual[offset + byte] == IdentityByte(!remote, jetty, operation, byte);
            }
            if (api == kApiNotify) {
                uint64_t notify = 0U;
                std::memcpy(&notify, actual.data() + offset + slotBytes - sizeof(notify), sizeof(notify));
                if (notify != NotifyValue(jetty, operation)) {
                    if (badNotifies++ < 8U) std::cerr << "notify mismatch: jetty=" << jetty
                        << ", operation=" << operation << std::endl;
                }
            }
            if (bad[0] + bad[1] == 0U) continue;
            const bool publisher = operation % batchSize + 1U == batchSize;
            ++badRequests; badPublishers += publisher;
            if (badRequests == 1U) std::cerr << "data mismatch: jetty=" << jetty
                                           << ", operation=" << operation << std::endl;
            if (badRequests > 8U) continue;
            std::cout << "DATA_AUDIT | Jetty=" << jetty << " | Operation=" << operation
                      << " | Batch=" << operation / batchSize << " | Lane=" << operation % batchSize
                      << " | Publisher=" << publisher << " | BadFirst32=" << bad[0]
                      << " | BadLast32=" << bad[1] << " | InitialFirst32=" << initial[0]
                      << " | InitialLast32=" << initial[1] << std::endl;
            for (uint32_t half = 0U; half < 2U; ++half) {
                uint64_t identity = 0U;
                bool consistent = true;
                std::cout << "DATA_WORDS | Jetty=" << jetty << " | Operation=" << operation
                          << " | Half=" << half << " | Actual=" << std::hex;
                for (uint32_t index = 0U; index < 4U; ++index) {
                    const uint32_t word = half * 4U + index;
                    uint64_t value = 0U;
                    std::memcpy(&value, actual.data() + offset + word * 8U, sizeof(value));
                    const uint64_t decoded = value ^ IdentityWord(remote, 0U, 0U, word);
                    if (index == 0U) identity = decoded;
                    else consistent = consistent && decoded == identity;
                    std::cout << value << ',';
                }
                std::cout << " | Expected=";
                for (uint32_t index = 0U; index < 4U; ++index)
                    std::cout << IdentityWord(remote, jetty, operation, half * 4U + index) << ',';
                const uint32_t sourceJetty = static_cast<uint32_t>(identity >> 32U);
                const uint32_t sourceOperation = static_cast<uint32_t>(identity);
                const bool recognized = consistent && sourceJetty < jettyCount && sourceOperation < operationCount;
                std::cout << std::dec << " | IdentityMatch=" << recognized;
                if (recognized) std::cout << " | SourceJetty=" << sourceJetty << " | SourceOperation=" << sourceOperation;
                std::cout << std::endl;
            }
        }
    }
    std::cout << "DATA_AUDIT_SUMMARY | Pattern=RequestIdentityV1 | CheckedRequests="
              << static_cast<uint64_t>(jettyCount) * operationCount << " | BadRequests=" << badRequests
              << " | BadPublishers=" << badPublishers << " | BadOrdinary=" << badRequests - badPublishers
              << " | DetailsPrinted=" << std::min(badRequests, 8U) << std::endl;
    if (api == kApiNotify) std::cout << "NOTIFY_AUDIT_SUMMARY | CheckedRequests="
        << static_cast<uint64_t>(jettyCount) * operationCount << " | BadNotifies=" << badNotifies << std::endl;
    return badRequests == 0U && badNotifies == 0U;
}

bool PrintCompletionProfile(const CompletionProfile& total, const CompletionProfile& warmup,
    uint64_t warmupExpected, uint64_t timedExpected, uint64_t drainTailTicks)
{
    auto fields = [](const CompletionProfile& p) {
        return std::array<uint64_t, 9>{p.calls, p.probes, p.notReady, p.processed, p.probeTicks,
            p.notReadyTicks, p.accountTicks, p.ackTicks, p.progressTicks};
    };
    const auto all = fields(total);
    const auto pre = fields(warmup);
    std::array<uint64_t, 9> timed{};
    bool valid = true;
    for (size_t i = 0U; i < timed.size(); ++i) {
        valid = valid && all[i] >= pre[i];
        timed[i] = all[i] >= pre[i] ? all[i] - pre[i] : 0U;
    }
    auto print = [&](const char* phase, const std::array<uint64_t, 9>& p, uint64_t expected) {
        const uint64_t attributed = p[4] + p[6] + p[7];
        valid = valid && p[0] != 0U && p[3] == expected && p[1] >= p[2] &&
            p[1] - p[2] == p[3] && p[4] >= p[5] && p[8] >= attributed;
        std::cout << "COMPLETION_PROFILE | Version=1 | Phase=" << phase
                  << " | Calls=" << p[0] << " | Probes=" << p[1] << " | NotReady=" << p[2]
                  << " | Processed=" << p[3] << " | ProbeTicks=" << p[4]
                  << " | NotReadyProbeTicks=" << p[5] << " | AccountTicks=" << p[6]
                  << " | AckTicks=" << p[7] << " | ProgressTicks=" << p[8]
                  << " | OtherProgressTicks=" << (p[8] >= attributed ? p[8] - attributed : 0U)
                  << std::endl;
    };
    print("Warmup", pre, warmupExpected);
    print("Timed", timed, timedExpected);
    valid = valid && drainTailTicks >= timed[8];
    std::cout << "COMPLETION_PROFILE_TAIL | DrainTailTicks=" << drainTailTicks
              << " | OutsideProgressTicks=" << (drainTailTicks >= timed[8] ? drainTailTicks - timed[8] : 0U)
              << " | Instrumented=1" << std::endl;
    std::cout << "COMPLETION_PROFILE_CHECK | Status=" << (valid ? "PASS" : "FAIL") << std::endl;
    return valid;
}

void PrintPublishAudit(const std::vector<PublishAuditSummary>& summaries,
    const std::vector<PublishAuditCqe>& records, uint32_t operationCount)
{
    for (uint32_t jetty = 0U; jetty < summaries.size(); ++jetty) {
        const auto& summary = summaries[jetty];
        const bool available = summary.magic == kPublishAuditMagic && summary.cqBase != 0U &&
            summary.cqDepth != 0U && summary.cqeSize >= 12U && summary.initialHead != ~0ULL &&
            summary.captured != 0U && summary.captured <= operationCount && summary.captured <= summary.cqDepth;
        std::cout << "PUBLISH_AUDIT | Version=" << (MULTI_JETTY_SCOPE_BUILD == 7 ? 10 : 9) << " | ResourceJetty=" << jetty
                  << " | Snapshot=AfterCompletionDrain"
                  << " | Phase=" << summary.phase << " | DrainStatus=" << static_cast<int32_t>(summary.status)
                  << " | InitialHead=0x" << std::hex << summary.initialHead
                  << " | PublishedHead=0x" << summary.publishedHead
                  << " | Channel=0x" << summary.channel << " | SqContext=0x" << summary.sqContext
                  << " | CqContext=0x" << summary.cqContext << " | SqBase=0x" << summary.sqBase
                  << " | CqBase=0x" << summary.cqBase << " | SqTailAddr=0x" << summary.sqTailAddr
                  << " | CqTailAddr=0x" << summary.cqTailAddr << " | JfsId=0x" << summary.jfsId
                  << " | JfcId=0x" << summary.jfcId << std::dec
                  << " | CqDepth=" << summary.cqDepth << " | SqDepth=" << summary.sqDepth
                  << " | CqeSize=" << summary.cqeSize << " | CqTail=" << summary.cqTail
                  << " | SqTail=" << summary.sqTail << " | SampleStart=" << summary.sampleStart
                  << " | Captured=" << summary.captured << " | Sample=" << (available ? "AVAILABLE" : "UNAVAILABLE")
                  << " | OperationMapping=NONE" << std::endl;
        std::cout << "COMPLETION_STATE | Version=" << summary.completionVersion << " | Jetty=" << jetty
                  << " | WarmupCompleted=" << summary.warmupCompleted << " | WarmupCq=" << summary.warmupCq
                  << " | WarmupSq=" << summary.warmupSq << " | Completed=" << summary.completed
                  << " | Target=" << summary.target << " | TargetBb=" << summary.targetBb
                  << " | CqConsumed=" << summary.cqTail << " | CqAcknowledged=" << summary.acknowledged
                  << " | SqTail=" << summary.sqTail << std::endl;
        if (summary.completionFault != 0U || summary.status != 0U) {
            std::cout << "COMPLETION_ERROR | Kind=" << summary.completionFault
                      << " | Phase=" << summary.phase << " | Cq=";
            if (summary.completionVersion != 0U && summary.errorCq != 0xFFFFFFFFU) std::cout << summary.errorCq;
            else std::cout << "NA";
            std::cout << " | Jetty=";
            if (summary.completionVersion != 0U && summary.errorJetty != 0xFFFFFFFFU) std::cout << summary.errorJetty;
            else std::cout << "NA";
            std::cout << " | Sequence=" << summary.errorSequence
                      << " | Word0=0x" << std::hex << summary.errorWord0
                      << " | Word1=0x" << summary.errorWord1 << " | Word2=0x" << summary.errorWord2
                      << " | LocalNum=0x" << ((summary.errorWord1 >> 16U) | ((summary.errorWord2 & 15U) << 16U))
                      << std::dec;
            if (summary.completionFault == 3U) std::cout << " | Status=" << (summary.errorWord0 >> 24U)
                << " | SubStatus=" << ((summary.errorWord0 >> 16U) & 255U);
            else std::cout << " | Status=NA | SubStatus=NA";
            std::cout << std::endl;
        }
        for (uint32_t prior = 0U; prior < jetty; ++prior) {
            const auto& other = summaries[prior];
            if (summary.magic != kPublishAuditMagic || other.magic != kPublishAuditMagic) continue;
            std::cout << "CQ_RESOURCE_PAIR | JettyA=" << prior << " | JettyB=" << jetty
                      << " | SameChannel=" << (summary.channel == other.channel)
                      << " | SameSqBase=" << (summary.sqBase == other.sqBase)
                      << " | SameJfsId=" << (summary.jfsId == other.jfsId)
                      << " | SameCqBase=" << (summary.cqBase == other.cqBase)
                      << " | SameJfcId=" << (summary.jfcId == other.jfcId)
                      << " | SameCqTailAddr=" << (summary.cqTailAddr == other.cqTailAddr) << std::endl;
        }
        if (!available) continue;
        uint32_t duplicate = jetty;
        for (uint32_t prior = 0U; prior < jetty; ++prior) {
            const auto& other = summaries[prior];
            if (other.magic != kPublishAuditMagic || other.cqBase != summary.cqBase ||
                other.jfcId != summary.jfcId || other.cqDepth != summary.cqDepth ||
                other.cqeSize != summary.cqeSize || other.sampleStart != summary.sampleStart ||
                other.captured != summary.captured) continue;
            bool same = true;
            for (uint32_t index = 0U; index < summary.captured; ++index) {
                const auto& x = records[prior * operationCount + index];
                const auto& y = records[jetty * operationCount + index];
                same = same && x.word0 == y.word0 && x.word1 == y.word1 && x.word2 == y.word2;
            }
            if (same) { duplicate = prior; break; }
        }
        if (duplicate != jetty) {
            std::cout << "CQ_AUDIT_VIEW | ResourceJetty=" << jetty << " | SameSampleAs=" << duplicate << std::endl;
            continue;
        }
        uint32_t valid = 0U, errors = 0U, invalid = 0U, extraPrinted = 0U, unmatched = 0U;
        std::vector<uint32_t> sourceCounts(summaries.size(), 0U);
        for (uint32_t index = 0U; index < summary.captured; ++index) {
            const auto& record = records[jetty * operationCount + index];
            const uint32_t sequence = summary.sampleStart + index;
            const uint32_t expectedOwner = 1U - ((sequence / summary.cqDepth) & 1U);
            const bool ownerValid = ((record.word0 >> 2U) & 1U) == expectedOwner;
            const uint32_t status = (record.word0 >> 24U) & 0xFFU;
            const uint32_t subStatus = (record.word0 >> 16U) & 0xFFU;
            const uint32_t localNum = (record.word1 >> 16U) | ((record.word2 & 0xFU) << 16U);
            const bool sendJetty = (record.word0 & 3U) == 2U;
            uint32_t matches = 0U, matchedJetty = 0U;
            if (ownerValid && sendJetty) {
                for (uint32_t candidate = 0U; candidate < summaries.size(); ++candidate) {
                    const auto& resource = summaries[candidate];
                    if (resource.magic == kPublishAuditMagic && resource.cqBase == summary.cqBase &&
                        resource.jfcId == summary.jfcId && resource.jfsId == localNum) {
                        ++matches; matchedJetty = candidate;
                    }
                }
            }
            const bool firstSource = matches == 1U && sourceCounts[matchedJetty] == 0U;
            if (matches == 1U) ++sourceCounts[matchedJetty];
            if (ownerValid && matches != 1U) ++unmatched;
            const bool error = ownerValid && (status != 0U || subStatus != 0U);
            valid += ownerValid; invalid += !ownerValid; errors += error;
            if (index >= 8U && !firstSource &&
                !((error || !ownerValid || matches != 1U) && extraPrinted++ < 8U)) continue;
            std::cout << "CQ_AUDIT | ResourceJetty=" << jetty << " | Sequence=" << sequence
                      << " | Slot=" << sequence % summary.cqDepth << " | ExpectedOwner=" << expectedOwner
                      << " | OwnerValid=" << ownerValid << " | Word0=0x" << std::hex << record.word0
                      << " | Word1=0x" << record.word1 << " | Word2=0x" << record.word2 << std::dec;
            if (ownerValid) {
                std::cout << " | Status=" << status << " | SubStatus=" << subStatus
                          << " | EntryIdx=" << (record.word1 & 0xFFFFU) << " | LocalNum=0x" << std::hex
                          << localNum << std::dec << " | SendJetty=" << sendJetty << " | IdMatches=" << matches;
                if (matches == 1U) std::cout << " | MatchedJetty=" << matchedJetty;
                else std::cout << " | MatchedJetty=NA";
            } else std::cout << " | Status=NA | SubStatus=NA | EntryIdx=NA | LocalNum=NA | MatchedJetty=NA";
            std::cout << std::endl;
        }
        std::cout << "CQ_AUDIT_SUMMARY | ResourceJetty=" << jetty << " | OwnerValid=" << valid
                  << " | ErrorCqes=" << errors << " | NotOwnerValid=" << invalid
                  << " | UnmatchedOrAmbiguous=" << unmatched << " | Scope=BoundedCqStreamSample" << std::endl;
        for (uint32_t source = 0U; source < sourceCounts.size(); ++source) {
            if (sourceCounts[source] != 0U) std::cout << "CQ_SOURCE | ResourceJetty=" << jetty
                << " | MatchedJetty=" << source << " | Count=" << sourceCounts[source] << std::endl;
        }
    }
}

bool CheckCompletionAudit(const std::vector<PublishAuditSummary>& summaries, uint32_t warmup,
    uint32_t operationCount, uint32_t batchSize, uint32_t api = 0U, uint32_t jettysPerKernel = 1024U)
{
    bool valid = !summaries.empty() && batchSize != 0U && jettysPerKernel != 0U;
    if (!valid) return false;
    for (uint32_t index = 0U; index < summaries.size(); ++index) {
        const auto& summary = summaries[index];
        const uint32_t first = index / jettysPerKernel * jettysPerKernel;
        const uint32_t end = std::min(static_cast<uint32_t>(summaries.size()), first + jettysPerKernel);
        uint32_t priorMembers = 0U, members = 0U;
        for (uint32_t other = 0U; other < end; ++other) {
            if (summaries[other].cqBase != summary.cqBase || summaries[other].jfcId != summary.jfcId) continue;
            if (other < first) ++priorMembers;
            else ++members;
        }
        const uint32_t bbEnd = api == 2U ? 2U * operationCount : operationCount + operationCount / batchSize;
        const uint32_t warmupBb = api == 2U ? 2U * warmup : warmup + warmup / batchSize;
        valid = valid && summary.magic == kPublishAuditMagic && summary.completionVersion == 1U &&
            summary.phase == 2U && summary.status == 0U && summary.completionFault == 0U &&
            summary.warmupCompleted == warmup && summary.warmupCq == priorMembers * operationCount + members * warmup &&
            summary.warmupSq == warmupBb &&
            summary.completed == operationCount && summary.target == operationCount &&
            summary.targetBb == bbEnd && summary.sqTail == bbEnd &&
            summary.publishedHead == ((static_cast<uint64_t>(operationCount) << 32U) | bbEnd) &&
            summary.cqTail == (priorMembers + members) * operationCount && summary.acknowledged == summary.cqTail;
    }
    std::cout << "COMPLETION_CHECK | Status=" << (valid ? "PASS" : "FAIL") << std::endl;
    return valid;
}

#endif

uint64_t ExpectedSqWord(const SimtSqTraceRecord& trace, uint32_t word, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint64_t remoteBase, uint64_t localBase)
{
    const bool publisher = api == kApiNotify || trace.wordCount == kTraceWqeWords;
    const uint32_t nextHead = static_cast<uint32_t>(trace.logicalHead) + (publisher ? 2U : 1U);
    const uint64_t offset = static_cast<uint64_t>(trace.jetty) * regionBytes +
        static_cast<uint64_t>(trace.operation) * slotBytes;
    const uint64_t remote = remoteBase + offset, local = localBase + offset;
    const uint64_t opcode = api == kApiWrite ? 3U : (api == kApiNotify ? 5U : 6U);
    const uint64_t owner = (static_cast<uint32_t>(trace.logicalHead) & trace.sqDepth) == 0U ? 1U : 0U;
    const uint64_t word0 = (nextHead & 0xFFFFU) | (0x2DULL << 16U) | (1ULL << 28U) | (1ULL << 29U) |
        (owner << 31U) | (opcode << 40U);
    const uint64_t word1 = trace.word1Base | (static_cast<uint64_t>(api == kApiNotify ? 1U :
        (publisher ? 2U : 1U)) << 24U);
    if (word == 0U) return word0;
    if (word == 1U) return word1;
    if (word == 2U) return trace.remoteEidL;
    if (word == 3U) return trace.remoteEidH;
    if (word == 4U) return trace.tokenWord;
    if (word == 5U) return remote;
    if (api == kApiNotify) {
        if (word == 6U) return ((word1 >> 32U) & 0xFFFFFU) | ((trace.tokenWord & 0xFFFFFFFFULL) << 32U);
        if (word == 7U) return remote + slotBytes - sizeof(uint64_t);
        if (word == 8U) return NotifyValue(static_cast<uint32_t>(trace.jetty), static_cast<uint32_t>(trace.operation));
        if (word == 9U) return 0U;
        if (word == 10U) return payloadBytes;
        return local;
    }
    if (!publisher) return word == 6U ? payloadBytes : local;
    const uint32_t firstLen = payloadBytes >> 1U;
    if (word == 6U) return firstLen;
    if (word == 7U) return local;
    if (word == 8U) return payloadBytes - firstLen;
    if (word == 9U) return local + firstLen;
    return 0U;
}

bool SqTraceRecordOk(const SimtSqTraceRecord& trace, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint64_t remoteBase, uint64_t localBase)
{
    if (trace.magic != kTraceMagic || trace.wordCount == 0U || trace.wordCount > kTraceMaxWqeWords ||
        trace.sqDepth == 0U) return false;
    for (uint32_t word = 0U; word < trace.wordCount; ++word) {
        if (trace.words[word] != ExpectedSqWord(
                trace, word, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase)) return false;
    }
    return true;
}

void PrintSqTraceRecord(const SimtSqTraceRecord& trace, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint64_t remoteBase, uint64_t localBase)
{
    uint32_t firstBadWord = 0U;
    while (firstBadWord < trace.wordCount && trace.words[firstBadWord] == ExpectedSqWord(
        trace, firstBadWord, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase)) {
        ++firstBadWord;
    }
    std::cout << "SQ_TRACE | Jetty=" << trace.jetty << " | Operation=" << trace.operation
              << " | LogicalHead=" << trace.logicalHead << " | SqAddr=0x" << std::hex << trace.sqAddr
              << " | CacheLine=0x" << (trace.sqAddr & ~127ULL) << std::dec
              << " | CacheLineOffset=" << (trace.sqAddr & 127ULL) << " | Words=" << trace.wordCount
              << " | State=" << (SqTraceRecordOk(
                    trace, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase) ? "PASS" : "FAIL")
              << " | FirstBadWord=" << firstBadWord << " | Actual=0x" << std::hex
              << (firstBadWord < kTraceMaxWqeWords ? trace.words[firstBadWord] : 0U)
              << " | Expected=0x" << (firstBadWord < kTraceMaxWqeWords ? ExpectedSqWord(
                    trace, firstBadWord, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase) : 0U)
              << std::dec << std::endl;
}

bool TraceRecordOk(const SimtBatchTraceRecord& trace)
{
    if (trace.magic != kTraceMagic || trace.sqDepth == 0U) return false;
    const uint64_t totalBb = trace.groupSize * trace.itemBb + (trace.itemBb == 1U ? 1U : 0U);
    const uint64_t delta = (trace.groupSize << 32U) | totalBb;
    const uint64_t nextSq = trace.sqBaseAddr + trace.sqWqeSize *
        (static_cast<uint32_t>(trace.reservedHead) % trace.sqDepth);
    return trace.head == trace.reservedHead && trace.reservedHead == trace.previousHead + delta &&
        trace.nextSqAddr == nextSq &&
        static_cast<int64_t>(trace.reservationStatus) == 0 && static_cast<int64_t>(trace.appendStatus) == 0 &&
        static_cast<int64_t>(trace.commitStatus) == 0 &&
        std::equal(trace.sqWqe, trace.sqWqe + kTraceWqeWords, trace.publisherWqe);
}

void PrintTraceRecord(const SimtBatchTraceRecord& trace)
{
    std::cout << "BATCH_TRACE | Jetty=" << trace.jetty << " | OperationBegin=" << trace.operationBegin
              << " | PreviousHead=0x" << std::hex << trace.previousHead << " | Head=0x" << trace.head
              << " | ReservedHead=0x" << trace.reservedHead << " | SqTail=0x" << trace.sqTail
              << " | Workspace=0x" << trace.workspaceAddr << " | SqBase=0x" << trace.sqBaseAddr
              << " | TailSq=0x" << trace.tailSqAddr
              << " | NextSq=0x" << trace.nextSqAddr << " | HeadAddr=0x" << trace.headAddr
              << " | SqTailAddr=0x" << trace.sqTailAddr << " | DwqeAddr=0x" << trace.dwqeAddr
              << " | HeadLine=0x" << (trace.headAddr & ~127ULL)
              << " | TailLine=0x" << (trace.sqTailAddr & ~127ULL)
              << " | DwqeLine=0x" << (trace.dwqeAddr & ~127ULL)
              << std::dec << " | ItemBB=" << trace.itemBb
              << " | GroupSize=" << trace.groupSize << " | Reservation=" << static_cast<int64_t>(trace.reservationStatus)
              << " | Append=" << static_cast<int64_t>(trace.appendStatus)
              << " | Commit=" << static_cast<int64_t>(trace.commitStatus)
              << " | State=" << (TraceRecordOk(trace) ? "PASS" : "FAIL") << std::endl;
    std::cout << "BATCH_TRACE_WQE | SQ=" << std::hex;
    for (uint64_t word : trace.sqWqe) std::cout << word << ',';
    std::cout << " | Publisher=";
    for (uint64_t word : trace.publisherWqe) std::cout << word << ',';
    std::cout << std::dec << std::endl;
    if (trace.drainPhase != 0U) {
        const uint32_t cqeWord0 = static_cast<uint32_t>(trace.drainCqeWord0);
        std::cout << "DRAIN_TRACE | Jetty=" << trace.jetty
                  << " | Phase=" << (trace.drainPhase == 1U ? "Warmup" : "Timed")
                  << " | Status=" << static_cast<int64_t>(trace.drainStatus)
                  << " | Head=0x" << std::hex << trace.drainHead << std::dec
                  << " | ExpectedCqTail=" << trace.drainExpectedCqTail
                  << " | CqTail=" << trace.drainCqTail << " | SqTail=" << trace.drainSqTail
                  << " | ExpectedOwner=" << trace.drainExpectedOwner
                  << " | CqeOwner=" << ((cqeWord0 >> 2U) & 1U)
                  << " | CqeWord0=0x" << std::hex << trace.drainCqeWord0
                  << " | CqeWord1=0x" << trace.drainCqeWord1 << std::dec
                  << " | CqeStatus=" << ((cqeWord0 >> 24U) & 0xFFU)
                  << " | CqeSubStatus=" << ((cqeWord0 >> 16U) & 0xFFU)
                  << " | CqeEntry=" << (static_cast<uint32_t>(trace.drainCqeWord1) & 0xFFFFU)
                  << " | PollReason=" << trace.drainReason << " | Retry=" << trace.drainRetry
                  << " | GuardBefore=" << trace.guardBeforeMismatch
                  << " | GuardAfter=" << trace.guardAfterMismatch
                  << std::endl;
    }
}

bool FileReady(const std::string& path, size_t minimumBytes = 1U)
{
    struct stat status {};
    return stat(path.c_str(), &status) == 0 && status.st_size >= static_cast<off_t>(minimumBytes);
}

HcclResult WriteRootInfo(const std::string& path, const HcclRootInfo& rootInfo)
{
    const std::string temporaryPath = path + ".tmp";
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
    output.close();
    if (!output || std::rename(temporaryPath.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfo(const std::string& path, HcclRootInfo* rootInfo)
{
    const uint32_t maximumPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMilliseconds;
    for (uint32_t poll = 0U; poll < maximumPolls; ++poll) {
        if (FileReady(path, sizeof(*rootInfo))) {
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char*>(rootInfo), sizeof(*rootInfo));
            if (input.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
                return HCCL_SUCCESS;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMilliseconds));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitComm(uint32_t rank, uint32_t rankCount, const std::string& rootPath, HcclComm* comm)
{
    HcclRootInfo rootInfo {};
    HcclResult result = HCCL_SUCCESS;
    if (rank == kRootRank) {
        CHECK_HCCL(HcclGetRootInfo(&rootInfo));
        result = WriteRootInfo(rootPath, rootInfo);
    } else {
        result = ReadRootInfo(rootPath, &rootInfo);
    }
    if (result != HCCL_SUCCESS) {
        return result;
    }
    return HcclCommInitRootInfo(rankCount, &rootInfo, rank, comm);
}

std::string SyncDirectory()
{
    const char* value = std::getenv("MULTI_JETTY_BATCH_PERF_SYNC_DIR");
    return value == nullptr ? "/tmp/multi_jetty_batch_perf" : value;
}

HcclResult HostBarrier(uint32_t rank, uint32_t rankCount, const char* name)
{
    const std::string directory = SyncDirectory();
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = directory + "/" + name + ".";
    std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
    marker << '1';
    marker.close();
    const uint32_t maximumPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMilliseconds;
    for (uint32_t poll = 0U; poll < maximumPolls; ++poll) {
        bool ready = true;
        for (uint32_t peer = 0U; peer < rankCount; ++peer) {
            ready = ready && FileReady(prefix + std::to_string(peer));
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMilliseconds));
    }
    return HCCL_E_TIMEOUT;
}

uint32_t ApiIndex(const std::string& name)
{
    if (name == "write") {
        return kApiWrite;
    }
    if (name == "read") {
        return kApiRead;
    }
    return kApiNotify;
}

uint32_t ModeIndex(const std::string& name)
{
    if (name == "simd-serial") {
        return kModeSimdSerial;
    }
    if (name == "simt-serial") {
        return kModeSimtSerial;
    }
    return kModeSimtParallel;
}

bool ValidBatchSize(uint32_t size)
{
    return size == 1U || size == 8U || size == 32U || size == 64U || size == 128U || size == 256U;
}

bool ValidJettyCount(uint32_t count)
{
    return count == 1U || count == 2U || count == 4U || count == 8U;
}

aclError InitializeBuffers(
    void* sendBuffer, void* recvBuffer, size_t bufferBytes, uint32_t jettyCount, uint32_t operationCount,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes)
{
    std::vector<uint8_t> send(bufferBytes, 0U);
    std::vector<uint8_t> recv(bufferBytes, 0U);
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        for (uint32_t operation = 0U; operation < operationCount; ++operation) {
            const size_t offset = static_cast<size_t>(jetty) * regionBytes +
                static_cast<size_t>(operation) * slotBytes;
            for (uint32_t byte = 0U; byte < payloadBytes; ++byte) {
#if MULTI_JETTY_SCOPE_BUILD >= 6
                send[offset + byte] = IdentityByte(false, jetty, operation, byte);
                recv[offset + byte] = IdentityByte(true, jetty, operation, byte);
#else
                send[offset + byte] = PayloadPattern(false, jetty, operation, byte);
                recv[offset + byte] = PayloadPattern(true, jetty, operation, byte);
#endif
            }
        }
    }
    aclError result = aclrtMemcpy(
        sendBuffer, bufferBytes, send.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return result == ACL_SUCCESS ? aclrtMemcpy(
        recvBuffer, bufferBytes, recv.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE) : result;
}

bool CheckBuffer(
    void* buffer, size_t bufferBytes, bool expectRemotePattern, uint32_t api, uint32_t jettyCount,
    uint32_t operationCount, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t batchSize = 256U)
{
    std::vector<uint8_t> actual(bufferBytes);
    if (aclrtMemcpy(actual.data(), bufferBytes, buffer, bufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
#if MULTI_JETTY_SCOPE_BUILD >= 6
    return CheckIdentityBuffer(actual, expectRemotePattern, jettyCount, operationCount,
        payloadBytes, slotBytes, regionBytes, batchSize, api);
#else
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        for (uint32_t operation = 0U; operation < operationCount; ++operation) {
            const size_t offset = static_cast<size_t>(jetty) * regionBytes +
                static_cast<size_t>(operation) * slotBytes;
            for (uint32_t byte = 0U; byte < payloadBytes; ++byte) {
                const uint8_t expected = PayloadPattern(expectRemotePattern, jetty, operation, byte);
                if (actual[offset + byte] != expected) {
                    std::cerr << "data mismatch: jetty=" << jetty << ", operation=" << operation
                              << ", byte=" << byte << std::endl;
                    return false;
                }
            }
            if (api == kApiNotify) {
                uint64_t notify = 0U;
                std::memcpy(&notify, actual.data() + offset + slotBytes - sizeof(notify), sizeof(notify));
                if (notify != NotifyValue(jetty, operation)) {
                    std::cerr << "notify mismatch: jetty=" << jetty << ", operation=" << operation << std::endl;
                    return false;
                }
            }
        }
    }
    return true;
#endif
}

HcclResult BuildChannelDesc(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, HcclChannelDesc& desc)
{
    CHECK_HCCL(HcclChannelDescInit(&desc, 1U));
    desc.remoteRank = peer;
    desc.channelProtocol = kProtocol;
    desc.notifyNum = 0U;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;
    uint32_t* layers = nullptr;
    uint32_t layerCount = 0U;
    CHECK_HCCL(HcclRankGraphGetLayers(comm, &layers, &layerCount));
    for (uint32_t layer = 0U; layer < layerCount; ++layer) {
        CommLink* links = nullptr;
        uint32_t linkCount = 0U;
        CHECK_HCCL(HcclRankGraphGetLinks(comm, layers[layer], rank, peer, &links, &linkCount));
        for (uint32_t link = 0U; link < linkCount; ++link) {
            if (links[link].linkAttr.linkProtocol == kProtocol) {
                desc.localEndpoint = links[link].srcEndpointDesc;
                desc.remoteEndpoint = links[link].dstEndpointDesc;
                return HCCL_SUCCESS;
            }
        }
    }
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ResolveRemoteRecv(
    HcclComm comm, ChannelHandle channel, uint64_t& remoteRecvAddress, uint32_t& remoteRecvIndex)
{
    uint32_t memoryCount = 0U;
    CommMem* remoteMemories = nullptr;
    char** tags = nullptr;
    CHECK_HCCL(HcclChannelGetRemoteMems(comm, channel, &memoryCount, &remoteMemories, &tags));
    for (uint32_t memory = 0U; memory < memoryCount; ++memory) {
        if (tags[memory] != nullptr && std::strcmp(tags[memory], kRecvTag) == 0) {
            remoteRecvAddress = reinterpret_cast<uint64_t>(remoteMemories[memory].addr);
            remoteRecvIndex = memory;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

HcclResult AcquireChannels(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles,
    std::vector<ChannelHandle>& channels, std::vector<uint64_t>& remoteRecvAddresses,
    std::vector<uint32_t>& remoteRecvIndices)
{
    const uint32_t channelCount = static_cast<uint32_t>(channels.size());
    if (channelCount == 0U || remoteRecvAddresses.size() != channelCount ||
        remoteRecvIndices.size() != channelCount) {
        return HCCL_E_PARA;
    }

    std::vector<HcclChannelDesc> descs(channelCount);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(BuildChannelDesc(comm, rank, peer, handles, descs[channel]));
    }
    CHECK_HCCL(HcclChannelAcquire(
        comm, COMM_ENGINE_AIV, descs.data(), channelCount, channels.data()));
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(ResolveRemoteRecv(
            comm, channels[channel], remoteRecvAddresses[channel], remoteRecvIndices[channel]));
    }
    return HCCL_SUCCESS;
}

std::string OutputMode(uint32_t mode, uint32_t kernelLaunchCount)
{
    if (mode == kModeSimdSerial) {
        return "SIMD-Serial";
    }
    if (mode == kModeSimtSerial) {
        return "SIMT-Serial";
    }
    if (kernelLaunchCount == 1U) {
        return "SIMT-Parallel";
    }
    return "SIMT-Parallel-" + std::to_string(kernelLaunchCount) + "KernelLaunches";
}

} // namespace

int main(int argc, char* argv[])
{
    using namespace multi_jetty_batch_perf;
    if (argc == 2 && std::strcmp(argv[1], "--build-info") == 0) {
        std::cout << "BUILD_CONFIG | Backend=" << MULTI_JETTY_BUILD_BACKEND
                  << " | Fast=" << MULTI_JETTY_BUILD_FAST;
#if MULTI_JETTY_SCOPE_BUILD != 0
        std::cout << " | Scope=" << MULTI_JETTY_BUILD_SCOPE;
#endif
#if MULTI_JETTY_SCOPE_BUILD >= 6
        std::cout << " | AuditVersion=" << (MULTI_JETTY_SCOPE_BUILD == 7 ? 10 : 9) << " | CompletionVersion=1";
#if MULTI_JETTY_SCOPE_BUILD == 7
        std::cout << " | MatrixBatch=" << MULTI_JETTY_MATRIX_BATCH << " | Profiling=0";
#endif
#endif
        std::cout << std::endl;
        return 0;
    }
    if (argc != 11) {
        std::cerr << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_file> <simd-serial|simt-serial|simt-parallel>"
                     " <write|read|notify> <iterations> <warmup> <payload> <batch_size> <jettys>"
                  << std::endl;
        return 1;
    }
    const uint32_t rank = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const uint32_t rankCount = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const std::string rootFile = argv[3];
    const std::string modeName = argv[4];
    const std::string apiName = argv[5];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const uint32_t batchSize = static_cast<uint32_t>(std::strtoul(argv[9], nullptr, 10));
    const uint32_t jettyCount = static_cast<uint32_t>(std::strtoul(argv[10], nullptr, 10));
    if ((rankCount != 2U && rankCount != 8U) || rank >= rankCount ||
        (modeName != "simd-serial" && modeName != "simt-serial" && modeName != "simt-parallel") ||
        (apiName != "write" && apiName != "read" && apiName != "notify") || iterations == 0U ||
        payloadBytes == 0U || !ValidBatchSize(batchSize) || !ValidJettyCount(jettyCount) ||
        iterations % batchSize != 0U || warmup % batchSize != 0U) {
        return 1;
    }

    const uint32_t api = ApiIndex(apiName);
    uint32_t mode = ModeIndex(modeName);
    if ((mode == kModeSimdSerial && !MULTI_JETTY_ENABLE_SIMD) ||
        (mode != kModeSimdSerial && !MULTI_JETTY_ENABLE_SIMT)) {
        std::cerr << "Requested backend was not compiled into this binary" << std::endl;
        return 1;
    }
    if (MULTI_JETTY_BUILD_FAST && mode != kModeSimdSerial &&
        (mode != kModeSimtParallel || (batchSize != 32U && batchSize != 256U))) {
        std::cerr << "Fast build supports only SIMT-Parallel BatchSize 32 or 256" << std::endl;
        return 1;
    }
    const char* diagnostic = std::getenv("MULTI_JETTY_PARALLEL_CONTROL");
#if MULTI_JETTY_SCOPE_BUILD >= 6
    if (diagnostic == nullptr || std::strcmp(diagnostic, "publisher-core-parallel") != 0) {
        std::cerr << "S parallel control requires MULTI_JETTY_PARALLEL_CONTROL=publisher-core-parallel" << std::endl;
        return 1;
    }
#elif MULTI_JETTY_SCOPE_BUILD == 5
    if (diagnostic == nullptr || std::strcmp(diagnostic, "publisher-core") != 0) {
        std::cerr << "Publish core artifact requires MULTI_JETTY_PARALLEL_CONTROL=publisher-core" << std::endl;
        return 1;
    }
#elif MULTI_JETTY_SCOPE_BUILD == 4
    if (diagnostic == nullptr || std::strcmp(diagnostic, "publisher-parallel") != 0) {
        std::cerr << "Parallel publish artifact requires MULTI_JETTY_PARALLEL_CONTROL=publisher-parallel" << std::endl;
        return 1;
    }
#elif MULTI_JETTY_SCOPE_BUILD == 3
    if (diagnostic == nullptr || std::strcmp(diagnostic, "publisher-sq-prepare") != 0) {
        std::cerr << "SQ prepare artifact requires MULTI_JETTY_PARALLEL_CONTROL=publisher-sq-prepare" << std::endl;
        return 1;
    }
#elif MULTI_JETTY_SCOPE_BUILD == 2
    if (diagnostic == nullptr || std::strcmp(diagnostic, "publisher-commit") != 0) {
        std::cerr << "Publisher artifact requires MULTI_JETTY_PARALLEL_CONTROL=publisher-commit" << std::endl;
        return 1;
    }
#else
    if (mode == kModeSimtParallel && diagnostic != nullptr) {
        if (std::strcmp(diagnostic, "one-active") == 0) {
            mode = kModeSimtOneActiveGroup;
        } else if (std::strcmp(diagnostic, "serial-commit") == 0) {
            mode = kModeSimtSerialCommit;
        } else if (diagnostic[0] != '\0') {
            std::cerr << "Unknown or retired parallel control: " << diagnostic << std::endl;
            return 1;
        }
    }
#endif
    const bool sender = (rank & 1U) == 0U;
    const uint32_t peer = rank ^ 1U;
    const uint32_t operationCount = warmup + iterations;
    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const uint32_t regionBytes = slotBytes * operationCount;
    const size_t bufferBytes = static_cast<size_t>(regionBytes) * jettyCount;
    const uint32_t jettysPerKernel = std::max(1U, kMaxActiveLanes / batchSize);
    const uint32_t kernelLaunchCount = mode >= kModeSimtParallel ?
        (jettyCount + jettysPerKernel - 1U) / jettysPerKernel : 1U;
    const uint32_t activeJettyCount = mode == kModeSimtOneActiveGroup ? 1U : jettyCount;
    const char* traceOption = std::getenv("MULTI_JETTY_BATCH_TRACE");
    const bool traceEnabled = mode >= kModeSimtParallel && traceOption != nullptr && std::strcmp(traceOption, "0") != 0;
    const bool fullSqTrace = traceEnabled &&
        (std::strcmp(traceOption, "full") == 0 || std::strcmp(traceOption, "expert") == 0);
    const uint32_t traceBatchesPerJetty = operationCount / batchSize;

    const char* aclConfigPath = std::getenv("MULTI_JETTY_ACL_CONFIG");
    if (aclConfigPath != nullptr && aclConfigPath[0] == '\0') aclConfigPath = nullptr;
#if MULTI_JETTY_SCOPE_BUILD != 0
#if MULTI_JETTY_SCOPE_BUILD == 7
    if (modeName != "simt-parallel" || batchSize != MULTI_JETTY_MATRIX_BATCH || rankCount != 2U ||
        iterations != 1024U || warmup != AlignUp(96U, batchSize) || payloadBytes != 64U ||
        traceEnabled || aclConfigPath != nullptr) {
        std::cerr << "Matrix requires selected batch, 64B, 1024 requests, aligned warmup96, two ranks, no trace" << std::endl;
        return 1;
    }
#else
    if (modeName != "simt-parallel" || api != kApiWrite || batchSize != 256U || jettyCount != 4U ||
        rankCount != 2U || iterations != 1024U || warmup != 256U || payloadBytes != 64U ||
        mode == kModeSimtOneActiveGroup || traceEnabled || aclConfigPath != nullptr) {
        std::cerr << "Scope experiment requires Write/64B/BS256/J4, 1024 requests, warmup256, "
                     "two ranks, no trace and default ACL configuration" << std::endl;
        return 1;
    }
#endif
    const char* scopeSchedule = MULTI_JETTY_SCOPE_BUILD >= 6 ? "S-parallel-3barriers" : MULTI_JETTY_SCOPE_BUILD == 5 ? "S" :
        MULTI_JETTY_SCOPE_BUILD == 4 ? "R" :
        MULTI_JETTY_SCOPE_BUILD == 3 ? "Q" : MULTI_JETTY_SCOPE_BUILD == 2 ? "P" :
        (mode == kModeSimtSerialCommit ? "A" : "N");
    std::cout << "SCOPE_CASE | Scope=" << MULTI_JETTY_BUILD_SCOPE << " | Schedule=" << scopeSchedule
              << " | Rank=" << rank << std::endl;
#endif
    if (aclConfigPath != nullptr) {
        std::cout << "ACL_INIT_CONFIG | Rank=" << rank << " | Path=" << aclConfigPath << std::endl;
    }
    CHECK_ACL(aclInit(aclConfigPath));
    CHECK_ACL(aclrtSetDevice(static_cast<int32_t>(rank)));
    aclrtStream stream = nullptr;
    CHECK_ACL(aclrtCreateStream(&stream));

    HcclComm comm = nullptr;
    CHECK_HCCL(InitComm(rank, rankCount, rootFile, &comm));

    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    CHECK_ACL(aclrtMalloc(&sendBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&recvBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(InitializeBuffers(
        sendBuffer, recvBuffer, bufferBytes, jettyCount, operationCount, payloadBytes, slotBytes, regionBytes));
    CommMem sendMemory {COMM_MEM_TYPE_DEVICE, sendBuffer, bufferBytes};
    CommMem recvMemory {COMM_MEM_TYPE_DEVICE, recvBuffer, bufferBytes};
    std::array<HcclMemHandle, 2U> handles {nullptr, nullptr};
    std::vector<ChannelHandle> channels(jettyCount, 0U);
    std::vector<uint64_t> remoteRecvAddresses(jettyCount, 0U);
    std::vector<uint32_t> remoteRecvIndices(jettyCount, 0U);
    CHECK_HCCL(HcclCommMemReg(comm, kSendTag, &sendMemory, &handles[0]));
    CHECK_HCCL(HcclCommMemReg(comm, kRecvTag, &recvMemory, &handles[1]));
    CHECK_HCCL(AcquireChannels(
        comm, rank, peer, handles.data(), channels, remoteRecvAddresses, remoteRecvIndices));
    for (uint32_t lhs = 0U; lhs < jettyCount; ++lhs) {
        if (channels[lhs] == 0U) {
            std::cerr << "Jetty channel is invalid: " << lhs << std::endl;
            return HCCL_E_INTERNAL;
        }
        for (uint32_t rhs = lhs + 1U; rhs < jettyCount; ++rhs) {
            if (channels[lhs] == channels[rhs]) {
                std::cerr << "Jetty channels are not independent: " << lhs << " and " << rhs << std::endl;
                return HCCL_E_INTERNAL;
            }
        }
    }
    CHECK_HCCL(HostBarrier(rank, rankCount, "ready"));

    bool pass = true;
    void* timingDevice = nullptr;
    void* channelsDevice = nullptr;
    void* remoteDevice = nullptr;
    void* traceDevice = nullptr;
    void* sqTraceDevice = nullptr;
    if (sender) {
#if MULTI_JETTY_SCOPE_BUILD >= 6
        std::vector<PublishAuditSummary> publishSummaries(jettyCount);
        std::vector<PublishAuditCqe> publishCqes(static_cast<size_t>(jettyCount) * operationCount);
        CHECK_ACL(aclrtMalloc(&traceDevice, kPublishAuditAllocationBytes, ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(traceDevice, kPublishAuditAllocationBytes, 0, kPublishAuditAllocationBytes));
        CHECK_ACL(aclrtMalloc(&sqTraceDevice, publishCqes.size() * sizeof(PublishAuditCqe), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(sqTraceDevice, publishCqes.size() * sizeof(PublishAuditCqe), 0,
            publishCqes.size() * sizeof(PublishAuditCqe)));
#endif
        std::vector<uint64_t> timing(static_cast<size_t>(kernelLaunchCount) * kTimingWords, 0U);
        std::vector<SimtBatchTraceRecord> traces(traceEnabled ? jettyCount * traceBatchesPerJetty : 0U);
        std::vector<SimtSqTraceRecord> sqTraces(fullSqTrace ? jettyCount * operationCount : 0U);
        std::vector<uint64_t> channelValues(channels.begin(), channels.end());
        CHECK_ACL(aclrtMalloc(&timingDevice, timing.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(
            timingDevice, timing.size() * sizeof(uint64_t), 0, timing.size() * sizeof(uint64_t)));
        if (traceEnabled) {
            CHECK_ACL(aclrtMalloc(&traceDevice, traces.size() * sizeof(SimtBatchTraceRecord), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMemset(traceDevice, traces.size() * sizeof(SimtBatchTraceRecord), 0,
                traces.size() * sizeof(SimtBatchTraceRecord)));
        }
        if (fullSqTrace) {
            CHECK_ACL(aclrtMalloc(
                &sqTraceDevice, sqTraces.size() * sizeof(SimtSqTraceRecord), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMemset(
                sqTraceDevice, sqTraces.size() * sizeof(SimtSqTraceRecord), 0,
                sqTraces.size() * sizeof(SimtSqTraceRecord)));
        }
        CHECK_ACL(aclrtMalloc(
            &channelsDevice, channelValues.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemcpy(
            channelsDevice, channelValues.size() * sizeof(uint64_t), channelValues.data(),
            channelValues.size() * sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
        if (mode == kModeSimdSerial) {
#if MULTI_JETTY_ENABLE_SIMD
            CHECK_ACL(aclrtMalloc(
                &remoteDevice, remoteRecvAddresses.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMemcpy(
                remoteDevice, remoteRecvAddresses.size() * sizeof(uint64_t), remoteRecvAddresses.data(),
                remoteRecvAddresses.size() * sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
            LaunchSimdMultiJettyBatchPerf(
                stream, channelsDevice, remoteDevice, sendBuffer, timingDevice, api, payloadBytes, slotBytes,
                regionBytes, iterations, warmup, batchSize, jettyCount);
#endif
        } else {
#if MULTI_JETTY_ENABLE_SIMT
            CHECK_ACL(aclrtMalloc(
                &remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMemcpy(
                remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), remoteRecvIndices.data(),
                remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEMCPY_HOST_TO_DEVICE));
            for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
                const uint32_t firstJetty = launch * jettysPerKernel;
                const uint32_t kernelJettyCount = std::min(jettyCount - firstJetty, jettysPerKernel);
                void* launchTiming = static_cast<uint8_t*>(timingDevice) +
                    static_cast<size_t>(launch) * kTimingWords * sizeof(uint64_t);
                LaunchSimtMultiJettyBatchPerf(
                    stream, channelsDevice, remoteDevice, launchTiming, api, payloadBytes, slotBytes, regionBytes,
                    iterations, warmup, batchSize, jettyCount, mode, firstJetty, kernelJettyCount,
                    traceDevice, traceBatchesPerJetty, sqTraceDevice, operationCount);
            }
#endif
        }
        CHECK_ACL(aclrtSynchronizeStream(stream));
        CHECK_ACL(aclrtMemcpy(
            timing.data(), timing.size() * sizeof(uint64_t), timingDevice, timing.size() * sizeof(uint64_t),
            ACL_MEMCPY_DEVICE_TO_HOST));
#if MULTI_JETTY_SCOPE_BUILD >= 6
        CHECK_ACL(aclrtMemcpy(publishSummaries.data(), publishSummaries.size() * sizeof(PublishAuditSummary),
            traceDevice, publishSummaries.size() * sizeof(PublishAuditSummary), ACL_MEMCPY_DEVICE_TO_HOST));
        CHECK_ACL(aclrtMemcpy(publishCqes.data(), publishCqes.size() * sizeof(PublishAuditCqe),
            sqTraceDevice, publishCqes.size() * sizeof(PublishAuditCqe), ACL_MEMCPY_DEVICE_TO_HOST));
        PrintPublishAudit(publishSummaries, publishCqes, operationCount);
        pass = CheckCompletionAudit(publishSummaries, warmup, operationCount, batchSize, api, jettysPerKernel) && pass;
#if MULTI_JETTY_SCOPE_BUILD == 6
        CompletionProfile profiles[2]{};
        for (uint32_t i = 0U; i < 2U; ++i) {
            CHECK_ACL(aclrtMemcpy(&profiles[i], sizeof(CompletionProfile),
                static_cast<uint8_t*>(traceDevice) + kCompletionWorkspaceOffset +
                    kCompletionProfileOffset + i * 128U,
                sizeof(CompletionProfile), ACL_MEMCPY_DEVICE_TO_HOST));
        }
        pass = PrintCompletionProfile(profiles[0], profiles[1],
            static_cast<uint64_t>(warmup) * jettyCount, static_cast<uint64_t>(iterations) * jettyCount,
            timing[kCompletionCyclesIndex] - timing[kIssueCyclesIndex]) && pass;
#endif
#endif
        if (traceEnabled) {
            CHECK_ACL(aclrtMemcpy(traces.data(), traces.size() * sizeof(SimtBatchTraceRecord), traceDevice,
                traces.size() * sizeof(SimtBatchTraceRecord), ACL_MEMCPY_DEVICE_TO_HOST));
            for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
                for (uint32_t batch = 0U; batch < traceBatchesPerJetty; ++batch) {
                    const auto& trace = traces[jetty * traceBatchesPerJetty + batch];
                    const uint32_t warmupBatchCount = warmup / batchSize;
                    const bool nearTimedBoundary = batch + 2U >= warmupBatchCount &&
                        batch < warmupBatchCount + 2U;
                    if (batch < 2U || nearTimedBoundary || batch + 2U >= traceBatchesPerJetty ||
                        trace.drainPhase != 0U ||
                        !TraceRecordOk(trace)) {
                        PrintTraceRecord(trace);
                    }
                }
            }
            for (uint32_t lhs = 0U; lhs < jettyCount; ++lhs) {
                const auto& left = traces[lhs * traceBatchesPerJetty];
                for (uint32_t rhs = lhs + 1U; rhs < jettyCount; ++rhs) {
                    const auto& right = traces[rhs * traceBatchesPerJetty];
                    if (left.magic != kTraceMagic || right.magic != kTraceMagic) continue;
                    const bool workspaceOverlap = left.workspaceAddr < right.workspaceAddr + kSimtBatchWorkspaceBytes &&
                        right.workspaceAddr < left.workspaceAddr + kSimtBatchWorkspaceBytes;
                    const uint64_t leftLines[] = {
                        left.headAddr & ~127ULL, left.sqTailAddr & ~127ULL, left.dwqeAddr & ~127ULL};
                    const uint64_t rightLines[] = {
                        right.headAddr & ~127ULL, right.sqTailAddr & ~127ULL, right.dwqeAddr & ~127ULL};
                    bool queueLineAlias = false;
                    for (uint64_t leftLine : leftLines) {
                        for (uint64_t rightLine : rightLines) queueLineAlias |= leftLine == rightLine;
                    }
                    if (workspaceOverlap || queueLineAlias) {
                        std::cout << "MEMORY_ALIAS | JettyA=" << lhs << " | JettyB=" << rhs
                                  << " | Workspace=" << workspaceOverlap << " | QueueLine=" << queueLineAlias
                                  << std::endl;
                    }
                }
            }
        }
        if (fullSqTrace) {
            CHECK_ACL(aclrtMemcpy(
                sqTraces.data(), sqTraces.size() * sizeof(SimtSqTraceRecord), sqTraceDevice,
                sqTraces.size() * sizeof(SimtSqTraceRecord), ACL_MEMCPY_DEVICE_TO_HOST));
            uint32_t recorded = 0U, mismatches = 0U, printed = 0U;
            for (const auto& trace : sqTraces) {
                if (trace.magic != kTraceMagic) continue;
                ++recorded;
                const uint64_t remoteBase = trace.jetty < remoteRecvAddresses.size() ?
                    remoteRecvAddresses[trace.jetty] : 0U;
                const uint64_t localBase = reinterpret_cast<uint64_t>(sendBuffer);
                if (remoteBase == 0U || !SqTraceRecordOk(
                        trace, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase)) {
                    ++mismatches;
                    if (printed++ < 8U) PrintSqTraceRecord(
                        trace, api, payloadBytes, slotBytes, regionBytes, remoteBase, localBase);
                }
            }
            std::cout << "SQ_AUDIT | Recorded=" << recorded << " | SemanticMismatches=" << mismatches
                      << " | State=" << (mismatches == 0U ? "PASS" : "FAIL") << std::endl;
        }
        if (traceEnabled) {
            uint32_t checks = 0U, violations = 0U;
            for (const auto& trace : traces) {
                if (trace.drainPhase == 0U || trace.guardBeforeMismatch == ~0ULL) continue;
                ++checks;
                violations += trace.guardBeforeMismatch != 0U || trace.guardAfterMismatch != 0U;
            }
            std::cout << "MEMORY_GUARD | Checks=" << checks << " | Violations=" << violations
                      << " | State=" << (checks != 0U && violations == 0U ? "PASS" : "FAIL") << std::endl;
        }

        uint64_t issueCycles = 0U;
        uint64_t completionCycles = 0U;
        uint64_t completed = 0U;
        uint64_t doorbells = 0U;
        for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
            const uint64_t* result = timing.data() + static_cast<size_t>(launch) * kTimingWords;
            pass = pass && static_cast<int64_t>(result[kStatusIndex]) == 0;
            issueCycles += result[kIssueCyclesIndex];
            completionCycles += result[kCompletionCyclesIndex];
            completed += result[kCompletedIndex];
            doorbells += result[kDoorbellCountIndex];
        }
        const uint64_t totalOperations = static_cast<uint64_t>(iterations) * activeJettyCount;
        pass = pass && completed == totalOperations;
        if (pass && api == kApiRead) {
            pass = CheckBuffer(
                sendBuffer, bufferBytes, true, api, activeJettyCount, operationCount, payloadBytes, slotBytes,
                regionBytes, batchSize);
        }
        const double issueTime = totalOperations == 0U ? 0.0 :
            static_cast<double>(issueCycles) / static_cast<double>(totalOperations);
        const double bandwidth = completionCycles == 0U ? 0.0 :
            static_cast<double>(payloadBytes) * static_cast<double>(totalOperations) /
            static_cast<double>(completionCycles);
        const uint32_t lanesPerJetty = mode == kModeSimdSerial ? 1U : batchSize;
        std::cout << std::fixed << std::setprecision(6)
                  << "PERF_DATA | API=" << apiName << " | DataSize/B=" << payloadBytes
                  << " | Jettys=" << jettyCount << " | LanesPerJetty=" << lanesPerJetty
                  << " | BatchSize=" << batchSize << " | BatchesPerJetty=" << iterations / batchSize
                  << " | TotalDBs=" << doorbells << " | Mode=" << OutputMode(mode, kernelLaunchCount);
        if (diagnostic != nullptr) {
            std::cout << " | Control=" << diagnostic;
        }
#if MULTI_JETTY_SCOPE_BUILD != 0
        std::cout << " | Scope=" << MULTI_JETTY_BUILD_SCOPE << " | Schedule=" << scopeSchedule;
#endif
        if (traceEnabled) std::cout << " | Trace=" << (fullSqTrace ? "FullSQ+Cq" : "PostCommit+Cq");
        if (aclConfigPath != nullptr) std::cout << " | AclConfig=" << aclConfigPath;
#if MULTI_JETTY_SCOPE_BUILD >= 6
        std::cout << " | Measurement=" << (MULTI_JETTY_SCOPE_BUILD == 7 ? "RawDeviceTicks" : "DiagnosticOnly")
                  << " | Instrumented=" << (MULTI_JETTY_SCOPE_BUILD == 7 ? 0 : 1)
                  << " | KernelLaunches=" << kernelLaunchCount << " | MaxConcurrentJettys="
                  << std::min(jettyCount, jettysPerKernel)
                  << " | Pattern=RequestIdentityV1 | IssueTicksPerRequest="
                  << issueTime << " | BytesPerCompletionTick=" << bandwidth << std::endl;
#else
        std::cout << " | IssueTime/ns=" << issueTime << " | Bandwidth/GB/s=" << bandwidth << std::endl;
#endif
    }

    CHECK_HCCL(HostBarrier(rank, rankCount, "completed"));
    if (!sender && api != kApiRead) {
        pass = CheckBuffer(
            recvBuffer, bufferBytes, false, api, activeJettyCount, operationCount, payloadBytes, slotBytes,
            regionBytes, batchSize);
    }
    if (timingDevice != nullptr) {
        CHECK_ACL(aclrtFree(timingDevice));
    }
    if (channelsDevice != nullptr) {
        CHECK_ACL(aclrtFree(channelsDevice));
    }
    if (remoteDevice != nullptr) {
        CHECK_ACL(aclrtFree(remoteDevice));
    }
    if (traceDevice != nullptr) {
        CHECK_ACL(aclrtFree(traceDevice));
    }
    if (sqTraceDevice != nullptr) {
        CHECK_ACL(aclrtFree(sqTraceDevice));
    }
    CHECK_ACL(aclrtFree(sendBuffer));
    CHECK_ACL(aclrtFree(recvBuffer));
    CHECK_HCCL(HcclCommDestroy(comm));
    CHECK_ACL(aclrtDestroyStream(stream));
    CHECK_ACL(aclFinalize());
    return pass ? 0 : 2;
}
