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

namespace multi_jetty_batch_perf {

static_assert(AscendC::simt::HcommSimtCompletionBufferBytes(kMaxJettyCount) <= kCompletionStateBytes);

__simt_callee__ inline AscendC::simt::UbcCtpCompletionSet CompletionSet(__gm__ uint8_t* workspace)
{
    return {workspace + kCompletionWorkspaceOffset};
}

__simt_callee__ inline int32_t ResumeCompletionSet(AscendC::simt::UbcCtpCompletionSet& set, uint32_t firstJetty)
{
    for (uint32_t offset = 0U; offset < kCompletionStateBytes; offset += 64U)
        asc_dcci_single(set.workspace + offset);
    if (!AscendC::simt::HcommSimtCompletionReady(set))
        return AscendC::HCOMM_FAILED;
    auto* entries = AscendC::simt::HcommSimtCompletionChannels(set);
    for (uint32_t i = 0U; i < firstJetty; ++i) {
        if (entries[i].completed != entries[i].target)
            return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}

template <typename HcommT>
__simt_callee__ inline int32_t PrepareCompletionWindows(
    HcommT& hcomm, AscendC::simt::UbcCtpCompletionSet& set, uint32_t jettyCount, uint32_t count, uint32_t batchSize,
    uint32_t api = kApiWrite, uint32_t firstJetty = 0U)
{
    if (count == 0U)
        return AscendC::HCOMM_SUCCESS;
    const uint32_t bbCount = api == kApiNotify ? 2U * count : count + count / batchSize;
    for (uint32_t i = 0U; i < jettyCount; ++i) {
        if (hcomm.PrepareCompletion(set, firstJetty + i, count, bbCount) != AscendC::HCOMM_SUCCESS)
            return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}

__simt_callee__ inline void CaptureWarmupCompletion(
    AscendC::simt::UbcCtpCompletionSet& set, __gm__ CompletionSummary* summaries, uint32_t count,
    uint32_t firstJetty = 0U)
{
    auto* entries = AscendC::simt::HcommSimtCompletionChannels(set);
    auto* cqs = AscendC::simt::HcommSimtCompletionCqs(set);
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
        auto* completion = AscendC::simt::HcommSimtCompletionHeader(set);
        out->completionVersion = completion->version;
        out->completionFault = completion->fault;
        out->errorCq = completion->errorCq;
        out->errorJetty = completion->errorChannel;
        out->errorSequence = completion->errorSequence;
        out->errorWord0 = completion->errorWord0;
        out->errorWord1 = completion->errorWord1;
        out->errorWord2 = completion->errorWord2;
        if (completion->version == 1U && jetty < completion->channelCount) {
            auto* entry = &AscendC::simt::HcommSimtCompletionChannels(set)[jetty];
            out->completed = entry->completed;
            out->target = entry->target;
            out->targetBb = entry->targetBb;
            if (entry->cqIndex < completion->cqCount) {
                auto* sharedCq = &AscendC::simt::HcommSimtCompletionCqs(set)[entry->cqIndex];
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
