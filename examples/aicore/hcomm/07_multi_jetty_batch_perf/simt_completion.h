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

#include <cstddef>
#include <cstdint>

namespace multi_jetty_batch_perf {

using AscendC::ChannelHandle;
using AscendC::CQ_CONTEXT_TYPE_UB_JFC;
using AscendC::CqContext;
using AscendC::HCOMM_FAILED;
using AscendC::HCOMM_SUCCESS;
using AscendC::SQ_CONTEXT_TYPE_UB_JFS;
using AscendC::SqContext;
using AscendC::simt::HcommSimtChannelEntity;

// Completion state is caller-owned and separate from the HCCL channel ABI. One owner lane
// operates a set; publishing lanes never modify this state. Each channel has its own window.
constexpr uint32_t HCOMM_SIMT_COMPLETION_VERSION = 1U;
constexpr uint32_t HCOMM_SIMT_COMPLETION_NONE = 0xFFFFFFFFU;
enum class HcommSimtCompletionFault : uint32_t {
    NONE = 0U,
    RESOURCE = 1U,
    IDENTITY = 2U,
    CQE = 3U,
    COUNT = 4U,
    TIMEOUT = 5U
};
struct alignas(128) HcommSimtCompletionState {
    uint32_t version, channelCount, cqCount, nextCq;
    uint32_t fault, errorCq, errorChannel, errorSequence;
    uint32_t errorWord0, errorWord1, errorWord2, reserved;
};
struct alignas(128) HcommSimtCompletionChannel {
    uint64_t channel, sqBase, headAddr, tailAddr;
    uint32_t jfsId, sqDepth, cqIndex, completed;
    uint32_t target, targetBb;
};
struct alignas(128) HcommSimtCompletionCq {
    uint64_t base, dbAddr;
    uint32_t jfcId, depth, cqeSize, consumed, acknowledged;
};
static_assert(sizeof(HcommSimtCompletionState) == 128U);
static_assert(sizeof(HcommSimtCompletionChannel) == 128U);
static_assert(sizeof(HcommSimtCompletionCq) == 128U);
static_assert(offsetof(HcommSimtCompletionState, reserved) + sizeof(uint32_t) <= 64U);
static_assert(offsetof(HcommSimtCompletionChannel, targetBb) + sizeof(uint32_t) <= 64U);
static_assert(offsetof(HcommSimtCompletionCq, acknowledged) + sizeof(uint32_t) <= 64U);

struct UbcCtpCompletionSet {
    __gm__ uint8_t* workspace = nullptr;
};

// Reserve enough CQ records for the worst case of one distinct CQ per channel.
inline constexpr uint64_t HcommSimtCompletionBufferBytes(uint32_t channelCount)
{
    return channelCount == 0U || channelCount > 65535U ? 0ULL : 128ULL * (1ULL + 2ULL * channelCount);
}

// Shared CQ completion implementation. The owner is a caller-selected lane, not a lock.
__simt_callee__ inline __gm__ HcommSimtCompletionState* HcommSimtCompletionHeader(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionState*>(set.workspace);
}
__simt_callee__ inline __gm__ HcommSimtCompletionChannel* HcommSimtCompletionChannels(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionChannel*>(set.workspace + 128U);
}
__simt_callee__ inline __gm__ HcommSimtCompletionCq* HcommSimtCompletionCqs(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionCq*>(
        set.workspace + 128ULL * (1ULL + HcommSimtCompletionHeader(set)->channelCount));
}
__simt_callee__ inline bool HcommSimtCompletionReady(const UbcCtpCompletionSet& set)
{
    return set.workspace != nullptr && HcommSimtCompletionHeader(set)->version == HCOMM_SIMT_COMPLETION_VERSION &&
           HcommSimtCompletionHeader(set)->fault == 0U;
}
__simt_callee__ inline void HcommSimtCompletionFlush(const UbcCtpCompletionSet& set)
{
    asc_threadfence();
    auto* state = HcommSimtCompletionHeader(set);
    const uint64_t bytes = 128ULL * (1ULL + state->channelCount + state->cqCount);
    // All fields fit in the first 64B of each aligned record (asserted in the definition).
    // Padding and unused CQ records contain no accounting state.
    for (uint64_t offset = 0U; offset < bytes; offset += 128U)
        asc_dcci_single(set.workspace + offset);
    asc_threadfence();
}
__simt_callee__ inline void HcommSimtCompletionAcknowledge(const UbcCtpCompletionSet& set)
{
    // Per-Jetty accounting and SQ reclamation must be durable before releasing CQ slots.
    HcommSimtCompletionFlush(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    for (uint32_t q = 0U; q < HcommSimtCompletionHeader(set)->cqCount; ++q) {
        auto* cq = &cqs[q];
        if (cq->consumed == cq->acknowledged)
            continue;
        *reinterpret_cast<__gm__ uint32_t*>(cq->dbAddr) = cq->consumed & 0xFFFFFFU;
        asc_threadfence();
        cq->acknowledged = cq->consumed;
        asc_dcci_single(cq);
    }
    asc_threadfence();
}
__simt_callee__ inline void HcommSimtCompletionFail(
    UbcCtpCompletionSet& set, HcommSimtCompletionFault fault, uint32_t q, uint32_t channel, uint32_t sequence,
    uint32_t w0 = 0U, uint32_t w1 = 0U, uint32_t w2 = 0U)
{
    auto* state = HcommSimtCompletionHeader(set);
    state->fault = static_cast<uint32_t>(fault);
    state->errorCq = q;
    state->errorChannel = channel;
    state->errorSequence = sequence;
    state->errorWord0 = w0;
    state->errorWord1 = w1;
    state->errorWord2 = w2;
}
__simt_callee__ inline int32_t InitCompletionSet(
    UbcCtpCompletionSet& set, __gm__ ChannelHandle* channels, uint32_t count, __gm__ uint8_t* workspace, uint64_t bytes)
{
    set.workspace = nullptr;
    const uint64_t required = count == 0U || count > 65535U ? 0ULL : 128ULL * (1ULL + 2ULL * count);
    if (channels == nullptr || workspace == nullptr || required == 0U || bytes < required ||
        (reinterpret_cast<uint64_t>(workspace) & 127U) != 0U)
        return HCOMM_FAILED;
    // Caller passes fresh zeroed storage. Refuse reinitializing a live or failed set.
    asc_dcci_single(workspace);
    if (*reinterpret_cast<__gm__ uint32_t*>(workspace) != 0U)
        return HCOMM_FAILED;
    set.workspace = workspace;
    for (uint64_t offset = 0U; offset < required; offset += sizeof(uint64_t))
        *reinterpret_cast<__gm__ uint64_t*>(workspace + offset) = 0U;
    auto* state = HcommSimtCompletionHeader(set);
    state->channelCount = count;
    state->errorCq = HCOMM_SIMT_COMPLETION_NONE;
    state->errorChannel = HCOMM_SIMT_COMPLETION_NONE;
    auto* entries = HcommSimtCompletionChannels(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    for (uint32_t i = 0U; i < count; ++i) {
        if (channels[i] == 0U) {
            state->fault = 1U;
            break;
        }
        auto* channel = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channels[i]);
        if (channel->sqNum == 0U || channel->cqNum == 0U || channel->sqContextAddr == 0U ||
            channel->cqContextAddr == 0U) {
            state->fault = 1U;
            break;
        }
        auto* sq = reinterpret_cast<__gm__ SqContext*>(channel->sqContextAddr);
        auto* cq = reinterpret_cast<__gm__ CqContext*>(channel->cqContextAddr);
        auto* entry = &entries[i];
        entry->channel = channels[i];
        entry->sqBase = sq->contextInfo.ubJfs.sqVa;
        entry->headAddr = sq->contextInfo.ubJfs.headAddr;
        entry->tailAddr = sq->contextInfo.ubJfs.tailAddr;
        entry->jfsId = sq->contextInfo.ubJfs.jfsID;
        entry->sqDepth = sq->contextInfo.ubJfs.sqDepth;
        const uint32_t cqDepth = cq->contextInfo.ubJfc.cqDepth;
        if (sq->type != SQ_CONTEXT_TYPE_UB_JFS || cq->type != CQ_CONTEXT_TYPE_UB_JFC || entry->sqBase == 0U ||
            entry->headAddr == 0U || entry->tailAddr == 0U || (entry->headAddr & 7U) != 0U ||
            (entry->tailAddr & 3U) != 0U || entry->jfsId > 0xFFFFFU || entry->sqDepth < 2U ||
            (entry->sqDepth & (entry->sqDepth - 1U)) != 0U || cqDepth < 2U || (cqDepth & (cqDepth - 1U)) != 0U ||
            cq->contextInfo.ubJfc.scqVa == 0U || cq->contextInfo.ubJfc.dbVa == 0U ||
            cq->contextInfo.ubJfc.tailAddr == 0U || cq->contextInfo.ubJfc.cqeSize < 12U ||
            (cq->contextInfo.ubJfc.cqeSize & 3U) != 0U) {
            state->fault = 1U;
            break;
        }
        auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
        auto* tail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        asc_dcci_single(head);
        asc_dcci_single(tail);
        asc_dcci_single(cqTail);
        if (*head != 0ULL || *tail != 0U || *cqTail != 0U) {
            state->fault = 1U;
            break;
        }
        uint32_t q = 0U;
        for (; q < state->cqCount; ++q) {
            const bool sameBase = cqs[q].base == cq->contextInfo.ubJfc.scqVa;
            const bool sameId = cqs[q].jfcId == cq->contextInfo.ubJfc.jfcID;
            if (sameBase != sameId) {
                state->fault = 1U;
                break;
            }
            if (sameBase) {
                if (cqs[q].depth != cqDepth || cqs[q].cqeSize != cq->contextInfo.ubJfc.cqeSize)
                    state->fault = 1U;
                break;
            }
        }
        if (state->fault != 0U)
            break;
        if (q == state->cqCount) {
            cqs[q].base = cq->contextInfo.ubJfc.scqVa;
            cqs[q].dbAddr = cq->contextInfo.ubJfc.dbVa;
            cqs[q].depth = cqDepth;
            cqs[q].cqeSize = cq->contextInfo.ubJfc.cqeSize;
            cqs[q].jfcId = cq->contextInfo.ubJfc.jfcID;
            ++state->cqCount;
        }
        entry->cqIndex = q;
        for (uint32_t prior = 0U; prior < i; ++prior) {
            if (entries[prior].channel == entry->channel || entries[prior].sqBase == entry->sqBase ||
                entries[prior].headAddr == entry->headAddr || entries[prior].tailAddr == entry->tailAddr ||
                (entries[prior].cqIndex == q && entries[prior].jfsId == entry->jfsId))
                state->fault = 1U;
        }
        if (state->fault != 0U)
            break;
    }
    state->version = HCOMM_SIMT_COMPLETION_VERSION;
    HcommSimtCompletionFlush(set);
    return state->fault == 0U ? HCOMM_SUCCESS : HCOMM_FAILED;
}
__simt_callee__ inline int32_t PrepareCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount)
{
    if (!HcommSimtCompletionReady(set) || channelIndex >= HcommSimtCompletionHeader(set)->channelCount)
        return HCOMM_FAILED;
    auto* entries = HcommSimtCompletionChannels(set);
    auto* entry = &entries[channelIndex];
    // One admitted window per Jetty. No requirement that any other Jetty be idle.
    if (wqeCount == 0U || bbCount < wqeCount || static_cast<uint64_t>(bbCount) > 2ULL * wqeCount ||
        bbCount >= entry->sqDepth || entry->completed != entry->target || wqeCount > 0xFFFFFFFFU - entry->target ||
        bbCount > 0xFFFFFFFFU - entry->targetBb)
        return HCOMM_FAILED;
    auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
    auto* tail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
    asc_dcci_single(head);
    asc_dcci_single(tail);
    const uint64_t expectedHead = (static_cast<uint64_t>(entry->target) << 32U) | entry->targetBb;
    if (*head != expectedHead || *tail != entry->targetBb)
        return HCOMM_FAILED;
    uint64_t pending = wqeCount;
    for (uint32_t i = 0U; i < HcommSimtCompletionHeader(set)->channelCount; ++i) {
        if (entries[i].cqIndex == entry->cqIndex)
            pending += entries[i].target - entries[i].completed;
    }
    if (pending >= HcommSimtCompletionCqs(set)[entry->cqIndex].depth)
        return HCOMM_FAILED;
    entry->target += wqeCount;
    entry->targetBb += bbCount;
    asc_threadfence();
    asc_dcci_single(entry);
    asc_threadfence();
    return HCOMM_SUCCESS;
}
__simt_callee__ inline int32_t Progress(UbcCtpCompletionSet& set, uint32_t budget, uint32_t& processed)
{
    processed = 0U;
    if (!HcommSimtCompletionReady(set) || budget == 0U)
        return HCOMM_FAILED;
    auto* state = HcommSimtCompletionHeader(set);
    auto* entries = HcommSimtCompletionChannels(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    // Set operations have one owner. Publish cursor changes at the call boundary,
    // before acknowledgement, instead of storing the round-robin cursor for every CQE.
    const uint32_t channelCount = state->channelCount;
    const uint32_t cqCount = state->cqCount;
    uint32_t nextCq = state->nextCq;
    uint32_t singleConsumed = cqCount == 1U ? cqs[0].consumed : 0U;
    uint32_t unavailable = 0U;
    while (processed < budget && unavailable < cqCount) {
        const uint32_t q = nextCq;
        nextCq = q + 1U == cqCount ? 0U : q + 1U;
        auto* cq = &cqs[q];
        const uint32_t sequence = cqCount == 1U ? singleConsumed : cq->consumed;
        auto* words = reinterpret_cast<__gm__ volatile uint32_t*>(
            cq->base + static_cast<uint64_t>(cq->cqeSize) * (sequence & (cq->depth - 1U)));
        asc_dcci_single(const_cast<__gm__ uint32_t*>(words));
        const uint32_t w0 = words[0];
        if (((w0 >> 2U) & 1U) == ((sequence / cq->depth) & 1U)) {
            ++unavailable;
            continue;
        }
        const uint32_t w1 = words[1], w2 = words[2];
        const uint32_t localNum = (w1 >> 16U) | ((w2 & 0xFU) << 16U);
        uint32_t match = HCOMM_SIMT_COMPLETION_NONE;
        for (uint32_t i = 0U; i < channelCount; ++i) {
            if (entries[i].cqIndex == q && entries[i].jfsId == localNum) {
                match = i;
                break;
            }
        }
        if ((w0 & 3U) != 2U || match == HCOMM_SIMT_COMPLETION_NONE) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::IDENTITY, q, match, sequence, w0, w1, w2);
            break;
        }
        if ((w0 & 0xFFFF0000U) != 0U) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::CQE, q, match, sequence, w0, w1, w2);
            break;
        }
        auto* entry = &entries[match];
        if (entry->completed == entry->target) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::COUNT, q, match, sequence, w0, w1, w2);
            break;
        }
        if (entry->completed + 1U == entry->target) {
            // The window's own successful completions prove its full BB range reclaimable.
            // Never apply another Jetty's entryIdx or release an incomplete window.
            auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
            asc_dcci_single(head);
            const uint64_t expectedHead = (static_cast<uint64_t>(entry->target) << 32U) | entry->targetBb;
            if (*head != expectedHead) {
                HcommSimtCompletionFail(set, HcommSimtCompletionFault::COUNT, q, match, sequence, w0, w1, w2);
                break;
            }
            auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
            *sqTail = entry->targetBb;
            asc_threadfence();
            asc_dcci_single(sqTail);
            asc_threadfence();
        }
        ++entry->completed;
        if (cqCount == 1U)
            ++singleConsumed;
        else
            ++cq->consumed;
        ++processed;
        unavailable = 0U;
    }
    state->nextCq = nextCq;
    if (cqCount == 1U)
        cqs[0].consumed = singleConsumed;
    if (processed != 0U || state->fault != 0U) {
        HcommSimtCompletionAcknowledge(set);
    }
    return state->fault == 0U ? HCOMM_SUCCESS : HCOMM_FAILED;
}
__simt_callee__ inline int32_t WaitCompletion(UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t maxIdlePolls)
{
    if (!HcommSimtCompletionReady(set) || channelIndex >= HcommSimtCompletionHeader(set)->channelCount ||
        maxIdlePolls == 0U)
        return HCOMM_FAILED;
    auto* entry = &HcommSimtCompletionChannels(set)[channelIndex];
    uint32_t idle = 0U;
    while (entry->completed != entry->target) {
        const uint32_t before = entry->completed;
        uint32_t processed = 0U;
        if (Progress(set, 64U, processed) != HCOMM_SUCCESS)
            return HCOMM_FAILED;
        // Traffic from a fast Jetty cannot reset the slow Jetty's idle limit forever.
        idle = entry->completed != before ? 0U : idle + 1U;
        if (idle >= maxIdlePolls) {
            auto* cq = &HcommSimtCompletionCqs(set)[entry->cqIndex];
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::TIMEOUT, entry->cqIndex, channelIndex, cq->consumed);
            HcommSimtCompletionAcknowledge(set);
            return HCOMM_FAILED;
        }
    }
    return HCOMM_SUCCESS;
}
__simt_callee__ inline int32_t DrainCompletionSet(UbcCtpCompletionSet& set)
{
    if (!HcommSimtCompletionReady(set))
        return HCOMM_FAILED;
    for (uint32_t i = 0U; i < HcommSimtCompletionHeader(set)->channelCount; ++i) {
        if (WaitCompletion(set, i, 1000000U) != HCOMM_SUCCESS)
            return HCOMM_FAILED;
    }
    return HCOMM_SUCCESS;
}
static_assert(HcommSimtCompletionBufferBytes(kMaxJettyCount) <= kCompletionStateBytes);

__simt_callee__ inline UbcCtpCompletionSet CompletionSet(__gm__ uint8_t* workspace)
{
    return {workspace + kCompletionWorkspaceOffset};
}

__simt_callee__ inline int32_t ResumeCompletionSet(UbcCtpCompletionSet& set, uint32_t firstJetty)
{
    for (uint32_t offset = 0U; offset < kCompletionStateBytes; offset += 64U)
        asc_dcci_single(set.workspace + offset);
    if (!HcommSimtCompletionReady(set))
        return AscendC::HCOMM_FAILED;
    auto* entries = HcommSimtCompletionChannels(set);
    for (uint32_t i = 0U; i < firstJetty; ++i) {
        if (entries[i].completed != entries[i].target)
            return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}

__simt_callee__ inline int32_t PrepareCompletionWindows(
    UbcCtpCompletionSet& set, uint32_t jettyCount, uint32_t count, uint32_t batchSize, uint32_t api = kApiWrite,
    uint32_t firstJetty = 0U)
{
    if (count == 0U)
        return AscendC::HCOMM_SUCCESS;
    const uint32_t bbCount = api == kApiNotify ? 2U * count : count + count / batchSize;
    for (uint32_t i = 0U; i < jettyCount; ++i) {
        if (PrepareCompletion(set, firstJetty + i, count, bbCount) != AscendC::HCOMM_SUCCESS)
            return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}

__simt_callee__ inline void CaptureWarmupCompletion(
    UbcCtpCompletionSet& set, __gm__ CompletionSummary* summaries, uint32_t count, uint32_t firstJetty = 0U)
{
    auto* entries = HcommSimtCompletionChannels(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    for (uint32_t i = firstJetty; i < firstJetty + count; ++i) {
        summaries[i].warmupCompleted = entries[i].completed;
        summaries[i].warmupCq = cqs[entries[i].cqIndex].consumed;
        auto* tail = reinterpret_cast<__gm__ uint32_t*>(entries[i].tailAddr);
        asc_dcci_single(tail);
        summaries[i].warmupSq = *tail;
    }
}

__simt_callee__ inline void CaptureCompletion(
    __gm__ uint64_t* channels, __gm__ CompletionSummary* summaries, uint32_t jettyCount, uint32_t operationCount,
    uint32_t phase, __ubuf__ int32_t* statuses, uint32_t firstJetty = 0U)
{
    for (uint32_t jetty = firstJetty; jetty < firstJetty + jettyCount; ++jetty) {
        auto* channel = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(channels[jetty]);
        auto* sq =
            &reinterpret_cast<__gm__ AscendC::SqContext*>(channel->sqContextAddr)[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* cq =
            &reinterpret_cast<__gm__ AscendC::CqContext*>(channel->cqContextAddr)[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* out = &summaries[jetty];
        auto* head = reinterpret_cast<__gm__ uint64_t*>(sq->contextInfo.ubJfs.headAddr);
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(sq->contextInfo.ubJfs.tailAddr);
        asc_dcci_single(head);
        asc_dcci_single(cqTail);
        asc_dcci_single(sqTail);
        out->channel = channels[jetty];
        out->sqContext = channel->sqContextAddr;
        out->cqContext = channel->cqContextAddr;
        out->sqBase = sq->contextInfo.ubJfs.sqVa;
        out->cqBase = cq->contextInfo.ubJfc.scqVa;
        out->sqTailAddr = sq->contextInfo.ubJfs.tailAddr;
        out->cqTailAddr = cq->contextInfo.ubJfc.tailAddr;
        out->jfsId = sq->contextInfo.ubJfs.jfsID;
        out->jfcId = cq->contextInfo.ubJfc.jfcID;
        out->cqeSize = cq->contextInfo.ubJfc.cqeSize;
        out->publishedHead = *head;
        out->cqTail = *cqTail;
        out->sqTail = *sqTail;
        auto set = CompletionSet(reinterpret_cast<__gm__ uint8_t*>(summaries));
        auto* completion = HcommSimtCompletionHeader(set);
        out->completionVersion = completion->version;
        out->completionFault = completion->fault;
        out->errorCq = completion->errorCq;
        out->errorJetty = completion->errorChannel;
        out->errorSequence = completion->errorSequence;
        out->errorWord0 = completion->errorWord0;
        out->errorWord1 = completion->errorWord1;
        out->errorWord2 = completion->errorWord2;
        if (completion->version == 1U && jetty < completion->channelCount) {
            auto* entry = &HcommSimtCompletionChannels(set)[jetty];
            out->completed = entry->completed;
            out->target = entry->target;
            out->targetBb = entry->targetBb;
            if (entry->cqIndex < completion->cqCount) {
                auto* sharedCq = &HcommSimtCompletionCqs(set)[entry->cqIndex];
                out->cqTail = sharedCq->consumed;
                out->acknowledged = sharedCq->acknowledged;
            }
        }
        out->cqDepth = cq->contextInfo.ubJfc.cqDepth;
        out->sqDepth = sq->contextInfo.ubJfs.sqDepth;
        out->requested = operationCount;
        out->phase = phase;
        out->status = static_cast<uint32_t>(statuses[jetty]);
        out->magic = kCompletionSummaryMagic;
        // Each launch leaves its own summary durable for the final host check.
        asc_threadfence();
        for (uint32_t offset = 0U; offset < sizeof(CompletionSummary); offset += 64U)
            asc_dcci_single(reinterpret_cast<__gm__ uint8_t*>(out) + offset);
    }
}

} // namespace multi_jetty_batch_perf
