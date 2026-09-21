/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"
#include "simt_api/cooperative_groups.h"

#if MULTI_JETTY_SCOPE_BUILD >= 6
#include "multi_jetty_batch_perf_common.h"
#if MULTI_JETTY_SCOPE_BUILD == 6
namespace multi_jetty_batch_perf {
__simt_callee__ inline void RecordCompletionProfile(__gm__ uint8_t* workspace, const CompletionProfile& sample)
{
    auto* out = reinterpret_cast<__gm__ CompletionProfile*>(workspace + kCompletionProfileOffset);
    out->calls += sample.calls; out->probes += sample.probes;
    out->notReady += sample.notReady; out->processed += sample.processed;
    out->probeTicks += sample.probeTicks; out->notReadyTicks += sample.notReadyTicks;
    out->accountTicks += sample.accountTicks; out->ackTicks += sample.ackTicks;
    out->progressTicks += sample.progressTicks;
}
} // namespace multi_jetty_batch_perf
#define HCOMM_SIMT_COMPLETION_PROFILE_TYPE multi_jetty_batch_perf::CompletionProfile
#define HCOMM_SIMT_COMPLETION_PROFILE_WRITE(workspace, sample) \
    multi_jetty_batch_perf::RecordCompletionProfile(workspace, sample)
#endif // Completion profiling is enabled only in the single-case diagnostic.
#ifndef MULTI_JETTY_HCOMM_SIMT_HEADER
#error "publish-audit requires the private Hcomm entry configured by CMake"
#endif
#ifdef INCLUDE_ADV_API_HCOMM_HCOMM_SIMT_H
#error "Hcomm was included before the publish-audit private entry"
#endif
#include MULTI_JETTY_HCOMM_SIMT_HEADER
#undef HCOMM_SIMT_COMPLETION_PROFILE_TYPE
#undef HCOMM_SIMT_COMPLETION_PROFILE_WRITE
static_assert(AscendC::simt::HCOMM_SIMT_COMPLETION_VERSION == 1U,
    "publish-audit requires this checkout's CompletionV1 headers");
#else
#include "hcomm/hcomm_simt.h"
#endif
#include "multi_jetty_batch_perf_common.h"

namespace {
using namespace multi_jetty_batch_perf;
constexpr uint32_t kModeSimtOneActiveGroup = 3U;
constexpr uint32_t kModeSimtSerialCommit = 4U;
constexpr uint64_t kTraceMagic = 0x53494D5442415443ULL;
constexpr uint32_t kTraceWqeWords = 12U;
constexpr uint32_t kTraceMaxWqeWords = 16U;
constexpr uint64_t kWorkspaceCanary = 0xC3A55A3CC3A55A3CULL;
constexpr uint32_t kWorkspaceGuardBytes = 128U;
constexpr uint32_t kWorkspaceStride = kSimtBatchWorkspaceBytes + kWorkspaceGuardBytes;
constexpr uint32_t kWorkspaceStorageBytes = kMaxJettyCount * kWorkspaceStride + kWorkspaceGuardBytes;
static_assert(kSimtBatchWorkspaceBytes == AscendC::simt::HcommSimtBatchBufferBytes(1U));

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

__simt_callee__ inline __ubuf__ uint8_t* BatchWorkspace(__ubuf__ uint8_t* storage, uint32_t jetty)
{
#ifdef MULTI_JETTY_DIAGNOSTIC_FAST_BUILD
    return storage + kWorkspaceGuardBytes + jetty * kWorkspaceStride;
#else
    return storage + jetty * kSimtBatchWorkspaceBytes;
#endif
}

__simt_callee__ inline void CaptureWorkspaceGuards(
    __ubuf__ uint8_t* storage, uint32_t jetty, __gm__ SimtBatchTraceRecord* trace)
{
    if (trace == nullptr) return;
#ifdef MULTI_JETTY_DIAGNOSTIC_FAST_BUILD
    auto* before = reinterpret_cast<__ubuf__ uint64_t*>(storage + jetty * kWorkspaceStride);
    auto* after = reinterpret_cast<__ubuf__ uint64_t*>(storage + (jetty + 1U) * kWorkspaceStride);
    uint64_t beforeMismatch = 0U, afterMismatch = 0U;
    for (uint32_t word = 0U; word < kWorkspaceGuardBytes / sizeof(uint64_t); ++word) {
        beforeMismatch += before[word] != kWorkspaceCanary;
        afterMismatch += after[word] != kWorkspaceCanary;
    }
    trace->guardBeforeMismatch = beforeMismatch;
    trace->guardAfterMismatch = afterMismatch;
#else
    (void)storage; (void)jetty;
    trace->guardBeforeMismatch = ~0ULL;
    trace->guardAfterMismatch = ~0ULL;
#endif
}

template <typename HcommT>
__simt_callee__ inline int32_t DrainWithTrace(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t jetty, uint32_t phase,
    __gm__ SimtBatchTraceRecord* trace)
{
    if (trace == nullptr) return hcomm.Drain(batch);
    auto* channel = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(batch.channel);
    auto* sqContexts = reinterpret_cast<__gm__ AscendC::SqContext*>(channel->sqContextAddr);
    auto* cqContexts = reinterpret_cast<__gm__ AscendC::CqContext*>(channel->cqContextAddr);
    auto* sqCtx = &sqContexts[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
    auto* cqCtx = &cqContexts[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
    auto* headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
    auto* cqTailAddr = reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.tailAddr);
    auto* sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(sqCtx->contextInfo.ubJfs.tailAddr);
    asc_dcci_single(headAddr); asc_dcci_single(cqTailAddr); asc_dcci_single(sqTailAddr);
    const uint64_t head = *headAddr;
    const uint32_t expectedCqTail = AscendC::simt::HcommSimtWqeCnt(head);
    uint32_t cqTail = *cqTailAddr, sqTail = *sqTailAddr, retry = 0U, reason = 0U;
    const uint32_t cqDepth = cqCtx->contextInfo.ubJfc.cqDepth;
    const uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    uint32_t cqeWord0 = 0U, cqeWord1 = 0U;
    while (cqTail != expectedCqTail && reason == 0U) {
        auto* cqe = reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.scqVa +
            cqCtx->contextInfo.ubJfc.cqeSize * (cqTail & (cqDepth - 1U)));
        const uint32_t expectedOwner = (cqTail / cqDepth) & 1U;
        retry = 0U; asc_dcci_single(cqe);
        while ((((cqe[0] >> 2U) & 1U) == expectedOwner) &&
               retry < AscendC::simt::HCOMM_SIMT_MAX_CQ_RETRY) {
            asc_threadfence(); asc_dcci_single(cqe); ++retry;
        }
        cqeWord0 = cqe[0]; cqeWord1 = cqe[1];
        if (retry >= AscendC::simt::HCOMM_SIMT_MAX_CQ_RETRY) reason = 1U;
        else if (((cqeWord0 >> 24U) & 0xFFU) != 0U || ((cqeWord0 >> 16U) & 0xFFU) != 0U) reason = 2U;
        else if (sqDepth == 0U) reason = 3U;
        else {
            const uint32_t sqMask = sqDepth - 1U;
            sqTail += (((cqeWord1 & 0xFFFFU) + sqDepth - (sqTail & sqMask)) & sqMask) + 1U;
            ++cqTail;
        }
    }
    if (reason == 0U) {
        *cqTailAddr = cqTail;
        *reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.dbVa) = cqTail & 0xFFFFFFU;
        *sqTailAddr = sqTail;
        asc_threadfence(); asc_dcci_single(cqTailAddr); asc_dcci_single(sqTailAddr); asc_threadfence();
    }
    trace->jetty = jetty;
    trace->drainPhase = phase;
    trace->drainStatus = reason == 0U ? 0U : static_cast<uint64_t>(-1LL);
    trace->drainHead = head;
    trace->drainExpectedCqTail = expectedCqTail;
    trace->drainCqTail = cqTail;
    trace->drainSqTail = sqTail;
    trace->drainCqeWord0 = cqeWord0;
    trace->drainCqeWord1 = cqeWord1;
    trace->drainExpectedOwner = (cqTail / cqDepth) & 1U;
    trace->drainRetry = retry;
    trace->drainReason = reason;
    return reason == 0U ? AscendC::HCOMM_SUCCESS : AscendC::HCOMM_FAILED;
}

template <typename Group>
__simt_callee__ inline void CaptureBatchTrace(
    const Group& group, AscendC::simt::UbcCtpBatchHandle& batch, int32_t appendStatus,
    int32_t commitStatus, uint32_t operationBase, uint32_t begin, uint32_t jetty,
    __gm__ SimtBatchTraceRecord* traces, uint32_t traceBatchesPerJetty)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    if (traces == nullptr || rank + 1U != size) return;
    auto* context = reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context);
    const uint32_t totalBb = AscendC::simt::HcommSimtBatchTotalBb(size, context->itemBb);
    const uint64_t previousHead = context->reservedHead - ((static_cast<uint64_t>(size) << 32U) | totalBb);
    const uint32_t tailHead = AscendC::simt::HcommSimtHeadIdx(previousHead) + rank * context->itemBb;
    auto* firstBb = reinterpret_cast<__gm__ uint64_t*>(
        context->sqBaseAddr + context->sqWqeSize * (tailHead % context->sqDepth));
    auto* secondBb = (tailHead % context->sqDepth) + 1U == context->sqDepth ?
        reinterpret_cast<__gm__ uint64_t*>(context->sqBaseAddr) : firstBb + 8U;
    auto* trace = &traces[jetty * traceBatchesPerJetty + (operationBase + begin) / size];
    trace->jetty = jetty; trace->operationBegin = operationBase + begin;
    trace->previousHead = previousHead; trace->head = *reinterpret_cast<__gm__ uint64_t*>(context->headAddr);
    trace->reservedHead = context->reservedHead;
    trace->sqTail = *reinterpret_cast<__gm__ uint32_t*>(context->sqTailAddr);
    trace->workspaceAddr = reinterpret_cast<uint64_t>(context);
    trace->sqBaseAddr = context->sqBaseAddr; trace->headAddr = context->headAddr;
    trace->sqTailAddr = context->sqTailAddr;
    trace->dwqeAddr = context->dwqeAddr; trace->tailSqAddr = reinterpret_cast<uint64_t>(firstBb);
    trace->nextSqAddr = context->sqBaseAddr + context->sqWqeSize *
        (AscendC::simt::HcommSimtHeadIdx(context->reservedHead) % context->sqDepth);
    trace->sqWqeSize = context->sqWqeSize; trace->sqDepth = context->sqDepth;
    trace->itemBb = context->itemBb; trace->groupSize = size;
    trace->reservationStatus = static_cast<uint64_t>(static_cast<int64_t>(context->reservationStatus));
    trace->appendStatus = static_cast<uint64_t>(static_cast<int64_t>(appendStatus));
    trace->commitStatus = static_cast<uint64_t>(static_cast<int64_t>(commitStatus));
    auto* publisher = AscendC::simt::HcommSimtBatchPublisherWords(context);
#pragma unroll
    for (uint32_t word = 0U; word < kTraceWqeWords; ++word) {
        trace->sqWqe[word] = word < 8U ? firstBb[word] : secondBb[word - 8U];
        trace->publisherWqe[word] = publisher[word];
    }
    trace->magic = kTraceMagic;
}

template <typename HcommT, typename Group>
__simt_callee__ inline int32_t AppendOne(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation, uint32_t jetty, const Group& group)
{
    const uint64_t offset = static_cast<uint64_t>(operation) * slotBytes;
    if (api == kApiWrite) {
        return hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes, group);
    }
    if (api == kApiRead) {
        return hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes, group);
    }
    const uint64_t notifyValue = kNotifyValueBase | (static_cast<uint64_t>(jetty) << 32U) | operation;
    return hcomm.WriteWithNotifyNbi(
        batch, remote + offset, local + offset, payloadBytes,
        remote + offset + slotBytes - sizeof(uint64_t), notifyValue, group);
}

template <typename Group>
__simt_callee__ inline void CaptureSqTrace(
    const Group& group, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation, uint32_t jetty,
    __gm__ SimtSqTraceRecord* traces, uint32_t traceOperationsPerJetty)
{
    if (traces == nullptr) return;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank()), size = static_cast<uint32_t>(group.size());
    auto* context = reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context);
    const uint64_t previousHead = context->reservedHead - ((static_cast<uint64_t>(size) << 32U) |
        AscendC::simt::HcommSimtBatchTotalBb(size, context->itemBb));
    const uint32_t logicalHead = AscendC::simt::HcommSimtHeadIdx(previousHead) + rank * context->itemBb;
    auto* firstBb = reinterpret_cast<__gm__ uint64_t*>(context->sqBaseAddr +
        context->sqWqeSize * (logicalHead % context->sqDepth));
    auto* secondBb = (logicalHead % context->sqDepth) + 1U == context->sqDepth ?
        reinterpret_cast<__gm__ uint64_t*>(context->sqBaseAddr) : firstBb + 8U;
    auto* trace = &traces[jetty * traceOperationsPerJetty + operation];
    const bool publisher = rank + 1U == size;
    const uint32_t wordCount = api == kApiNotify || publisher ? kTraceWqeWords : 8U;
    (void)remote; (void)local; (void)payloadBytes; (void)slotBytes;
    trace->jetty = jetty; trace->operation = operation; trace->logicalHead = logicalHead;
    trace->sqAddr = reinterpret_cast<uint64_t>(firstBb); trace->sqDepth = context->sqDepth;
    trace->wordCount = wordCount;
    trace->word1Base = context->word1Base; trace->remoteEidL = context->remoteEidL;
    trace->remoteEidH = context->remoteEidH; trace->tokenWord = context->tokenWord;
#pragma unroll
    for (uint32_t word = 0U; word < kTraceMaxWqeWords; ++word) {
        trace->words[word] = word < 8U ? asc_ldcg(&firstBb[word]) :
            (word < wordCount ? asc_ldcg(&secondBb[word - 8U]) : 0U);
    }
    trace->magic = kTraceMagic;
}

template <typename HcommT, typename Group>
__simt_callee__ inline int32_t SubmitGroupBatches(
    HcommT& hcomm, const Group& group, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api,
    __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes,
    uint32_t operationBase, uint32_t count, uint32_t jetty,
    __gm__ SimtBatchTraceRecord* traces = nullptr, uint32_t traceBatchesPerJetty = 0U,
    __gm__ SimtSqTraceRecord* sqTraces = nullptr, uint32_t traceOperationsPerJetty = 0U)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    for (uint32_t begin = 0U; begin < count; begin += size) {
        const int32_t appendStatus = AppendOne(
            hcomm, batch, api, remote, local, payloadBytes, slotBytes, operationBase + begin + rank, jetty, group);
        group.sync();
        const int32_t commitStatus = hcomm.BatchCommit(batch, group);
        CaptureSqTrace(group, batch, api, remote, local, payloadBytes, slotBytes,
            operationBase + begin + rank, jetty, sqTraces, traceOperationsPerJetty);
        CaptureBatchTrace(
            group, batch, appendStatus, commitStatus, operationBase, begin, jetty,
            traces, traceBatchesPerJetty);
        if (appendStatus != AscendC::HCOMM_SUCCESS || commitStatus != AscendC::HCOMM_SUCCESS) {
            return appendStatus != AscendC::HCOMM_SUCCESS ? appendStatus : commitStatus;
        }
    }
    return AscendC::HCOMM_SUCCESS;
}

template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t SubmitSerialCommitBatches(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch,
    uint32_t api, __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t operationBase, uint32_t count, uint32_t jetty,
    uint32_t jettyInKernel, uint32_t kernelJettyCount,
    __gm__ SimtBatchTraceRecord* traces, uint32_t traceBatchesPerJetty,
    __gm__ SimtSqTraceRecord* sqTraces, uint32_t traceOperationsPerJetty)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    int32_t status = batch.context == nullptr ? AscendC::HCOMM_FAILED : AscendC::HCOMM_SUCCESS;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        for (uint32_t turn = 0U; turn < kernelJettyCount; ++turn) {
            block.sync();
            if (jettyInKernel == turn && status == AscendC::HCOMM_SUCCESS) {
                const int32_t appendStatus = AppendOne(
                    hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                    operationBase + begin + rank, jetty, group);
                group.sync();
                const int32_t commitStatus = hcomm.BatchCommit(batch, group);
                status = appendStatus == AscendC::HCOMM_SUCCESS ? commitStatus : appendStatus;
                CaptureSqTrace(group, batch, api, remote, local, payloadBytes, slotBytes,
                    operationBase + begin + rank, jetty, sqTraces, traceOperationsPerJetty);
                CaptureBatchTrace(
                    group, batch, appendStatus, commitStatus, operationBase, begin, jetty,
                    traces, traceBatchesPerJetty);
            }
        }
        block.sync();
    }
    return status;
}

#if MULTI_JETTY_SCOPE_BUILD == 3 || MULTI_JETTY_SCOPE_BUILD == 4 || MULTI_JETTY_SCOPE_BUILD == 5 || MULTI_JETTY_SCOPE_BUILD >= 6
// Example-local split of Group BatchCommit with producer-owned SQ maintenance.
// Ordinary WQEs are complete here; the publisher has not constructed its own WQE yet.
template <typename Group>
__simt_callee__ inline void PrepareOrdinarySq(
    AscendC::simt::UbcCtpBatchHandle& batch, const Group& group)
{
    using namespace AscendC::simt;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    if (context->reservationStatus != AscendC::HCOMM_SUCCESS) {
        return;
    }
    group.sync();
    if (!publisher) {
        HcommSimtFlushBatchLaneSq(context, rank, size);
    }
    group.sync();
    if (publisher) {
        // SQ maintenance is owned by the producing lanes; do not scan it a second time.
        // Retain the existing publication ordering boundary before the block rendezvous.
        asc_threadfence();
        asc_threadfence_block();
    }
}

#endif

#if MULTI_JETTY_SCOPE_BUILD == 3 || MULTI_JETTY_SCOPE_BUILD == 4
template <typename Group>
__simt_callee__ inline int32_t CommitPreparedOrdinarySq(
    AscendC::simt::UbcCtpBatchHandle& batch, const Group& group)
{
    using namespace AscendC::simt;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    if (context->reservationStatus != AscendC::HCOMM_SUCCESS) {
        return AscendC::HCOMM_FAILED;
    }
    group.sync();
    if (publisher) {
        HcommSimtFlushBatchLaneSq(context, rank, size);
    }
    group.sync();
    if (publisher) {
        HcommSimtResolvedPost post;
        HcommSimtLoadBatchStaticPost(context, post);
        const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
        // SQ maintenance is owned by the producing lanes; do not scan it a second time.
        asc_threadfence();
        asc_threadfence_block();
        const uint64_t publishedHead = context->reservedHead +
            ((static_cast<uint64_t>(size) << 32U) | totalBb);
        asc_dcci_single(post.headAddr);
        asc_threadfence();
        *post.headAddr = publishedHead;
        asc_threadfence();
        asc_dcci_single(post.headAddr);
        asc_threadfence();
        asc_dcci_single(post.headAddr);
        asc_threadfence();
        HcommSimtRingBatchDwqe(post.dwqeAddr, HcommSimtBatchPublisherWords(context));
        HcommSimtPlanBatchFromHead(context, publishedHead);
    }
    group.sync();
    asc_threadfence_block();
    asc_threadfence();
    group.sync();
    return AscendC::HCOMM_SUCCESS;
}
#endif

#if MULTI_JETTY_SCOPE_BUILD == 5 || MULTI_JETTY_SCOPE_BUILD >= 6
// S calls this only from the selected publisher, after all ordinary SQ preparation.
__simt_callee__ inline void PublishPreparedPublisherSq(
    AscendC::simt::UbcCtpBatchHandle& batch, uint32_t size)
{
    using namespace AscendC::simt;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    HcommSimtFlushBatchLaneSq(context, size - 1U, size);
    HcommSimtResolvedPost post;
    HcommSimtLoadBatchStaticPost(context, post);
    const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
    // SQ maintenance is owned by the producing lanes; do not scan it a second time.
    asc_threadfence();
    asc_threadfence_block();
    const uint64_t publishedHead = context->reservedHead +
        ((static_cast<uint64_t>(size) << 32U) | totalBb);
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    *post.headAddr = publishedHead;
    asc_threadfence();
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    HcommSimtRingBatchDwqe(post.dwqeAddr, HcommSimtBatchPublisherWords(context));
}

template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t SubmitPublisherCoreBatches(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch,
    uint32_t api, __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t operationBase, uint32_t count, uint32_t jetty,
    uint32_t jettyInKernel, uint32_t kernelJettyCount)
{
    using namespace AscendC::simt;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    int32_t status = context == nullptr ? AscendC::HCOMM_FAILED : AscendC::HCOMM_SUCCESS;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        // Freeze the group-uniform status before any next-batch Plan can change it.
        // Current noncollective Append fails only for this shared reservationStatus.
        if (status == AscendC::HCOMM_SUCCESS && context->reservationStatus != AscendC::HCOMM_SUCCESS) {
            status = AscendC::HCOMM_FAILED;
        }
        if (status == AscendC::HCOMM_SUCCESS) {
            if (!publisher) {
                (void)AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                    operationBase + begin + rank, jetty, group);
            }
            PrepareOrdinarySq(batch, group);
        }
#if MULTI_JETTY_SCOPE_BUILD >= 6
        // First barrier: ordinary SQ ready. Second: every publisher finished before Plan.
        // Retain the separate final barrier after Plan/group cleanup below.
        for (uint32_t turn = 0U; turn < 2U; ++turn) {
#else
        for (uint32_t turn = 0U; turn < kernelJettyCount; ++turn) {
#endif
            // Failed groups also participate in every publication boundary.
            block.sync();
#if MULTI_JETTY_SCOPE_BUILD >= 6
            // All Jettys publish in the first turn; the second only waits for completion of publication.
            if (turn == 0U && publisher && status == AscendC::HCOMM_SUCCESS) {
#else
            if (jettyInKernel == turn && publisher && status == AscendC::HCOMM_SUCCESS) {
#endif
                (void)AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                    operationBase + begin + rank, jetty, group);
                PublishPreparedPublisherSq(batch, size);
            }
        }
        if (status == AscendC::HCOMM_SUCCESS) {
            if (publisher) {
                const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
                const uint64_t publishedHead = context->reservedHead +
                    ((static_cast<uint64_t>(size) << 32U) | totalBb);
                // Other Jettys may finish their private reservation while the last one publishes.
                // As in Q, failure to reserve the next batch is consumed on its next Append/Commit.
                HcommSimtPlanBatchFromHead(context, publishedHead);
            }
            group.sync();
            asc_threadfence_block();
            asc_threadfence();
            group.sync();
        }
        // All publication, reservation and group cleanup finishes before the next batch.
        block.sync();
    }
    return status;
}
#endif

#if MULTI_JETTY_SCOPE_BUILD == 4
// R retains Q's preparation and batch boundaries, but has no cross-Jetty publisher turn.
template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t SubmitParallelPublisherBatches(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch,
    uint32_t api, __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t operationBase, uint32_t count, uint32_t jetty)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    int32_t status = batch.context == nullptr ? AscendC::HCOMM_FAILED : AscendC::HCOMM_SUCCESS;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        int32_t appendStatus = AscendC::HCOMM_SUCCESS;
        if (status == AscendC::HCOMM_SUCCESS && !publisher) {
            appendStatus = AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                operationBase + begin + rank, jetty, group);
        }
        if (status == AscendC::HCOMM_SUCCESS) {
            PrepareOrdinarySq(batch, group);
        }
        // All ordinary SQ maintenance finishes before any publisher begins its WQE.
        block.sync();
        if (status == AscendC::HCOMM_SUCCESS) {
            if (publisher) {
                appendStatus = AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                    operationBase + begin + rank, jetty, group);
            }
            group.sync();
            const int32_t commitStatus = CommitPreparedOrdinarySq(batch, group);
            status = appendStatus == AscendC::HCOMM_SUCCESS ? commitStatus : appendStatus;
        }
        // Failed groups also rendezvous; no next-batch Append overlaps the current publications.
        block.sync();
    }
    return status;
}
#endif

#if MULTI_JETTY_SCOPE_BUILD == 2 || MULTI_JETTY_SCOPE_BUILD == 3
// Separate P/Q artifacts; neither candidate is compiled into the A/N control VF.
template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t SubmitPublisherCommitBatches(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch,
    uint32_t api, __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t operationBase, uint32_t count, uint32_t jetty,
    uint32_t jettyInKernel, uint32_t kernelJettyCount)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    int32_t status = batch.context == nullptr ? AscendC::HCOMM_FAILED : AscendC::HCOMM_SUCCESS;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        int32_t appendStatus = AscendC::HCOMM_SUCCESS;
        // Group Append is non-collective; each ordinary lane owns its reserved SQ slot.
        if (status == AscendC::HCOMM_SUCCESS && !publisher) {
            appendStatus = AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                operationBase + begin + rank, jetty, group);
        }
#if MULTI_JETTY_SCOPE_BUILD == 3
        if (status == AscendC::HCOMM_SUCCESS) {
            PrepareOrdinarySq(batch, group);
        }
#endif
        for (uint32_t turn = 0U; turn < kernelJettyCount; ++turn) {
            // Failed groups also rendezvous; the first turn completes every ordinary Append.
            block.sync();
            if (jettyInKernel == turn && status == AscendC::HCOMM_SUCCESS) {
                if (publisher) {
                    appendStatus = AppendOne(hcomm, batch, api, remote, local, payloadBytes, slotBytes,
                        operationBase + begin + rank, jetty, group);
                }
                group.sync();
#if MULTI_JETTY_SCOPE_BUILD == 3
                const int32_t commitStatus = CommitPreparedOrdinarySq(batch, group);
#else
                const int32_t commitStatus = hcomm.BatchCommit(batch, group);
#endif
                // Current Append errors use shared reservationStatus, so group status stays uniform.
                status = appendStatus == AscendC::HCOMM_SUCCESS ? commitStatus : appendStatus;
            }
        }
        // No next-batch Append can overlap another Jetty's current Commit.
        block.sync();
    }
    return status;
}
#endif

template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t DrainSerialJettys(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch,
    int32_t status, uint32_t jettyInKernel, uint32_t kernelJettyCount,
    uint32_t jetty, uint32_t phase, __gm__ SimtBatchTraceRecord* trace)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    for (uint32_t turn = 0U; turn < kernelJettyCount; ++turn) {
        block.sync();
        if (jettyInKernel == turn && rank == 0U && status == AscendC::HCOMM_SUCCESS) {
            status = DrainWithTrace(hcomm, batch, jetty, phase, trace);
        }
    }
    block.sync();
    return status;
}

template <uint32_t batchSize, uint32_t laneCapacity>
__simt_callee__ inline auto MakeSerialGroup(__ubuf__ uint8_t* tileBuffer)
{
    if constexpr (batchSize <= 32U) {
        (void)tileBuffer;
        return cooperative_groups::coalesced_threads();
    } else if constexpr (batchSize == 128U) {
        auto& tileMemory =
            *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<laneCapacity>*>(tileBuffer);
        auto block = cooperative_groups::this_thread_block(tileMemory);
        return cooperative_groups::tiled_partition<batchSize>(block);
    } else {
        (void)tileBuffer;
        return cooperative_groups::this_thread_block();
    }
}

template <uint32_t batchSize, typename Block>
__simt_callee__ inline auto MakeParallelGroup(const Block& block)
{
    if constexpr (batchSize < 32U) {
        return cooperative_groups::tiled_partition(cooperative_groups::coalesced_threads(), batchSize);
    } else if constexpr (batchSize == 32U) {
        (void)block;
        return cooperative_groups::coalesced_threads();
    } else {
        return cooperative_groups::tiled_partition<batchSize>(block);
    }
}

template <uint32_t jettyCapacity>
struct LaneBatchHandles;

template <>
struct LaneBatchHandles<1U> {
    AscendC::simt::UbcCtpBatchHandle handle;

    __simt_callee__ inline AscendC::simt::UbcCtpBatchHandle& Get(uint32_t)
    {
        return handle;
    }
};

template <uint32_t jettyCapacity>
struct LaneBatchHandles {
    AscendC::simt::UbcCtpBatchHandle handles[jettyCapacity];

    __simt_callee__ inline AscendC::simt::UbcCtpBatchHandle& Get(uint32_t jetty)
    {
        return handles[jetty];
    }
};

template <uint32_t batchSize, uint32_t laneCapacity, uint32_t jettyCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtSerialMultiJettyVf(
    __gm__ uint64_t* channels, __gm__ uint32_t* remoteIndices, __gm__ uint64_t* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t jettyCount, __ubuf__ uint8_t* batchWorkspaces, __ubuf__ int32_t* statuses,
    __ubuf__ uint8_t* tileBuffer)
{
    auto group = MakeSerialGroup<batchSize, laneCapacity>(tileBuffer);
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0U);
    LaneBatchHandles<jettyCapacity> batches;

    int32_t status = AscendC::HCOMM_SUCCESS;
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            static_cast<uint64_t>(jetty) * regionBytes;
        auto& batch = batches.Get(jetty);
        batch = hcomm.MakeBatchHandle(
            channels[jetty], BatchWorkspace(batchWorkspaces, jetty), kSimtBatchWorkspaceBytes,
            remote, api == kApiNotify ? 2U : 1U, group);
        if (batch.context == nullptr) {
            status = AscendC::HCOMM_FAILED;
        }
    }

    for (uint32_t jetty = 0U; jetty < jettyCount && status == AscendC::HCOMM_SUCCESS; ++jetty) {
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            static_cast<uint64_t>(jetty) * regionBytes;
        auto* local = AscendC::simt::LocalBufferAddr(channels[jetty], kLocalSendIndex) +
            static_cast<uint64_t>(jetty) * regionBytes;
        status = SubmitGroupBatches(
            hcomm, group, batches.Get(jetty), api, remote, local, payloadBytes, slotBytes, 0U, warmup, jetty);
    }
    group.sync();
    if (rank == 0U) {
        if (warmup != 0U) {
            for (uint32_t jetty = 0U; jetty < jettyCount && status == AscendC::HCOMM_SUCCESS; ++jetty) {
                status = hcomm.Drain(batches.Get(jetty));
            }
        }
        statuses[0] = status;
    }
    group.sync();
    status = statuses[0];
    if (status != AscendC::HCOMM_SUCCESS) {
        if (rank == 0U) {
            timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
        }
        return;
    }

    group.sync();
    const uint64_t begin = rank == 0U ? clock() : 0U;
    for (uint32_t jetty = 0U; jetty < jettyCount && status == AscendC::HCOMM_SUCCESS; ++jetty) {
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            static_cast<uint64_t>(jetty) * regionBytes;
        auto* local = AscendC::simt::LocalBufferAddr(channels[jetty], kLocalSendIndex) +
            static_cast<uint64_t>(jetty) * regionBytes;
        status = SubmitGroupBatches(
            hcomm, group, batches.Get(jetty), api, remote, local, payloadBytes, slotBytes, warmup, iterations,
            jetty);
    }
    const uint64_t issueEnd = rank == 0U ? clock() : 0U;
    if (rank == 0U) {
        for (uint32_t jetty = 0U; jetty < jettyCount && status == AscendC::HCOMM_SUCCESS; ++jetty) {
            status = hcomm.Drain(batches.Get(jetty));
        }
        const uint64_t completionEnd = clock();
        const uint32_t totalOperations = iterations * jettyCount;
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
        timing[kCompletedIndex] = status == AscendC::HCOMM_SUCCESS ? totalOperations : 0U;
        timing[kDoorbellCountIndex] = iterations / batchSize * jettyCount;
    }
    group.sync();
}

#if MULTI_JETTY_SCOPE_BUILD >= 6
static_assert(AscendC::simt::HcommSimtCompletionBufferBytes(kMaxJettyCount) <= kCompletionWorkspaceBytes);
static_assert(AscendC::simt::HcommSimtCompletionBufferBytes(kMaxJettyCount) <= kCompletionProfileOffset);
__simt_callee__ inline AscendC::simt::UbcCtpCompletionSet CompletionSet(__gm__ SimtBatchTraceRecord* traces)
{
    return {reinterpret_cast<__gm__ uint8_t*>(traces) + kCompletionWorkspaceOffset};
}
// A later stream-ordered kernel resumes the same CQ cursors and per-Jetty windows.
__simt_callee__ inline int32_t ResumeCompletionSet(
    AscendC::simt::UbcCtpCompletionSet& set, uint32_t firstJetty)
{
    for (uint32_t offset = 0U; offset < kCompletionProfileOffset; offset += 64U)
        asc_dcci_single(set.workspace + offset);
    if (!AscendC::simt::HcommSimtCompletionReady(set)) return AscendC::HCOMM_FAILED;
    auto* entries = AscendC::simt::HcommSimtCompletionChannels(set);
    for (uint32_t i = 0U; i < firstJetty; ++i) {
        if (entries[i].completed != entries[i].target) return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}
template <typename HcommT>
__simt_callee__ inline int32_t PrepareCompletionWindows(
    HcommT& hcomm, AscendC::simt::UbcCtpCompletionSet& set, uint32_t jettyCount,
    uint32_t count, uint32_t batchSize, uint32_t api = kApiWrite, uint32_t firstJetty = 0U)
{
    if (count == 0U) return AscendC::HCOMM_SUCCESS;
    const uint32_t bbCount = api == kApiNotify ? 2U * count : count + count / batchSize;
    for (uint32_t i = 0U; i < jettyCount; ++i) {
        if (hcomm.PrepareCompletion(set, firstJetty + i, count, bbCount) != AscendC::HCOMM_SUCCESS)
            return AscendC::HCOMM_FAILED;
    }
    return AscendC::HCOMM_SUCCESS;
}
__simt_callee__ inline void CaptureWarmupCompletion(
    AscendC::simt::UbcCtpCompletionSet& set, __gm__ PublishAuditSummary* summaries, uint32_t count, uint32_t firstJetty = 0U)
{
#if MULTI_JETTY_SCOPE_BUILD == 6
    auto* profile = reinterpret_cast<__gm__ uint64_t*>(set.workspace + kCompletionProfileOffset);
    auto* warmup = reinterpret_cast<__gm__ uint64_t*>(set.workspace + kCompletionProfileOffset + 128U);
    for (uint32_t i = 0U; i < sizeof(CompletionProfile) / sizeof(uint64_t); ++i) warmup[i] = profile[i];
#endif
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
// Called by thread 0 after completion processing and timing stores. CQ raw sampling is
// diagnostic only; the authoritative completion counts/cursor come from CompletionSet.
__simt_callee__ inline void CapturePublishAudit(
    __gm__ uint64_t* channels, __gm__ PublishAuditSummary* summaries,
    __gm__ PublishAuditCqe* records, uint32_t jettyCount, uint32_t operationCount,
    uint32_t phase, __ubuf__ int32_t* statuses, uint32_t firstJetty = 0U)
{
#if MULTI_JETTY_SCOPE_BUILD == 6
    // Diagnostic writeback after the existing timing boundary; queue state is untouched.
    auto* profile = reinterpret_cast<__gm__ uint8_t*>(summaries) +
        kCompletionWorkspaceOffset + kCompletionProfileOffset;
    for (uint32_t offset = 0U; offset < 256U; offset += 64U) asc_dcci_single(profile + offset);
#endif
    for (uint32_t jetty = firstJetty; jetty < firstJetty + jettyCount; ++jetty) {
        auto* channel = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(channels[jetty]);
        auto* sq = &reinterpret_cast<__gm__ AscendC::SqContext*>(channel->sqContextAddr)
            [AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* cq = &reinterpret_cast<__gm__ AscendC::CqContext*>(channel->cqContextAddr)
            [AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* out = &summaries[jetty];
        auto* head = reinterpret_cast<__gm__ uint64_t*>(sq->contextInfo.ubJfs.headAddr);
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(sq->contextInfo.ubJfs.tailAddr);
        asc_dcci_single(head); asc_dcci_single(cqTail); asc_dcci_single(sqTail);
        out->channel = channels[jetty];
        out->sqContext = channel->sqContextAddr; out->cqContext = channel->cqContextAddr;
        out->sqBase = sq->contextInfo.ubJfs.sqVa; out->cqBase = cq->contextInfo.ubJfc.scqVa;
        out->sqTailAddr = sq->contextInfo.ubJfs.tailAddr; out->cqTailAddr = cq->contextInfo.ubJfc.tailAddr;
        out->jfsId = sq->contextInfo.ubJfs.jfsID; out->jfcId = cq->contextInfo.ubJfc.jfcID;
        out->cqeSize = cq->contextInfo.ubJfc.cqeSize;
        out->publishedHead = *head;
        out->cqTail = *cqTail; out->sqTail = *sqTail;
        auto set = CompletionSet(reinterpret_cast<__gm__ SimtBatchTraceRecord*>(summaries));
        auto* completion = AscendC::simt::HcommSimtCompletionHeader(set);
        out->completionVersion = completion->version;
        out->completionFault = completion->fault;
        out->errorCq = completion->errorCq; out->errorJetty = completion->errorChannel;
        out->errorSequence = completion->errorSequence; out->errorWord0 = completion->errorWord0;
        out->errorWord1 = completion->errorWord1; out->errorWord2 = completion->errorWord2;
        if (completion->version == 1U && jetty < completion->channelCount) {
            auto* entry = &AscendC::simt::HcommSimtCompletionChannels(set)[jetty];
            out->completed = entry->completed; out->target = entry->target; out->targetBb = entry->targetBb;
            if (entry->cqIndex < completion->cqCount) {
                auto* sharedCq = &AscendC::simt::HcommSimtCompletionCqs(set)[entry->cqIndex];
                out->cqTail = sharedCq->consumed; out->acknowledged = sharedCq->acknowledged;
            }
        }
        out->cqDepth = cq->contextInfo.ubJfc.cqDepth;
        out->sqDepth = sq->contextInfo.ubJfs.sqDepth;
        out->requested = operationCount; out->phase = phase;
        out->status = static_cast<uint32_t>(statuses[jetty]);
        // A bounded CQ-stream sample, starting at its actual pre-run consumer cursor.
        // It is not this Jetty's operation sequence or a full shared-CQ completion audit.
        const uint32_t first = out->sampleStart;
        uint32_t count = MULTI_JETTY_SCOPE_BUILD == 7 ? 0U :
            (operationCount < out->cqDepth ? operationCount : out->cqDepth);
        if (out->initialHead == ~0ULL || out->cqeSize < 12U) count = 0U;
        for (uint32_t index = 0U; index < count; ++index) {
            auto* address = reinterpret_cast<__gm__ uint8_t*>(
                out->cqBase + out->cqeSize * ((first + index) % out->cqDepth));
            auto* words = reinterpret_cast<__gm__ volatile uint32_t*>(address);
            asc_dcci_single(address);
            auto* record = &records[jetty * operationCount + index];
            record->word0 = words[0]; record->word1 = words[1]; record->word2 = words[2];
        }
        out->captured = count;
        out->magic = kPublishAuditMagic;
#if MULTI_JETTY_SCOPE_BUILD == 7
        // Each launch leaves its own summary durable for the final host check.
        asc_threadfence();
        for (uint32_t offset = 0U; offset < sizeof(PublishAuditSummary); offset += 64U)
            asc_dcci_single(reinterpret_cast<__gm__ uint8_t*>(out) + offset);
#endif
    }
}
#endif

template <uint32_t batchSize, uint32_t laneCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtParallelMultiJettyVf(
    __gm__ uint64_t* channels, __gm__ uint32_t* remoteIndices, __gm__ uint64_t* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t firstJetty, uint32_t kernelJettyCount, uint32_t mode, uint32_t totalJettyCount,
    __gm__ SimtBatchTraceRecord* traces,
    uint32_t traceBatchesPerJetty, __gm__ SimtSqTraceRecord* sqTraces, uint32_t traceOperationsPerJetty,
    __ubuf__ uint8_t* batchWorkspaces,
    __ubuf__ int32_t* statuses, __ubuf__ uint8_t* tileBuffer)
{
    auto& tileMemory =
        *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<laneCapacity>*>(tileBuffer);
    auto block = cooperative_groups::this_thread_block(tileMemory);
    auto group = MakeParallelGroup<batchSize>(block);
#ifdef MULTI_JETTY_DIAGNOSTIC_FAST_BUILD
    if (traces != nullptr) {
        constexpr uint32_t guardWords = kWorkspaceGuardBytes / sizeof(uint64_t);
        auto* storageWords = reinterpret_cast<__ubuf__ uint64_t*>(batchWorkspaces);
        for (uint32_t index = static_cast<uint32_t>(threadIdx.x);
             index < (kMaxJettyCount + 1U) * guardWords; index += static_cast<uint32_t>(block.size())) {
            storageWords[(index / guardWords) * (kWorkspaceStride / sizeof(uint64_t)) + index % guardWords] =
                kWorkspaceCanary;
        }
        block.sync();
    }
#endif
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t jettyInKernel = static_cast<uint32_t>(threadIdx.x) / batchSize;
    const uint32_t jetty = firstJetty + jettyInKernel;
    const bool active = mode != kModeSimtOneActiveGroup || jettyInKernel == 0U;
    AscendC::simt::Hcomm<> hcomm;
    __gm__ uint8_t* remote = nullptr;
    __gm__ uint8_t* local = nullptr;
    AscendC::simt::UbcCtpBatchHandle batch {};
    int32_t status = AscendC::HCOMM_SUCCESS;
    if (active) {
        remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            static_cast<uint64_t>(jetty) * regionBytes;
        local = AscendC::simt::LocalBufferAddr(channels[jetty], kLocalSendIndex) +
            static_cast<uint64_t>(jetty) * regionBytes;
    }
    if (active) {
        (void)hcomm.Init(nullptr, 0U);
        batch = hcomm.MakeBatchHandle(
            channels[jetty], BatchWorkspace(batchWorkspaces, jetty), kSimtBatchWorkspaceBytes,
            remote, api == kApiNotify ? 2U : 1U, group);
    }
#if MULTI_JETTY_SCOPE_BUILD >= 6
    // Capture the request-sequence origin during initialization, before the existing block barrier.
    if (rank == 0U) {
        reinterpret_cast<__gm__ PublishAuditSummary*>(traces)[jetty].initialHead = batch.context == nullptr ? ~0ULL :
            reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context)->reservedHead;
        auto* channel = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(channels[jetty]);
        auto* cq = &reinterpret_cast<__gm__ AscendC::CqContext*>(channel->cqContextAddr)
            [AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        asc_dcci_single(cqTail);
        reinterpret_cast<__gm__ PublishAuditSummary*>(traces)[jetty].sampleStart = *cqTail;
    }
    if (threadIdx.x == 0U) {
        auto set = CompletionSet(traces);
        if (firstJetty == 0U) {
            status = hcomm.InitCompletionSet(set, channels, totalJettyCount, set.workspace, kCompletionWorkspaceBytes);
        } else {
            status = ResumeCompletionSet(set, firstJetty);
        }
        if (status == AscendC::HCOMM_SUCCESS)
            status = PrepareCompletionWindows(hcomm, set, kernelJettyCount, warmup, batchSize, api, firstJetty);
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
    }
#endif
#if MULTI_JETTY_SCOPE_BUILD == 2 || MULTI_JETTY_SCOPE_BUILD == 3 || MULTI_JETTY_SCOPE_BUILD == 4 || MULTI_JETTY_SCOPE_BUILD == 5 || MULTI_JETTY_SCOPE_BUILD >= 6
    // Complete all Handle initialization before the candidate's ordinary Appends.
    block.sync();
#endif
#if MULTI_JETTY_SCOPE_BUILD >= 6
    if (static_cast<int64_t>(timing[kStatusIndex]) != AscendC::HCOMM_SUCCESS) {
        if (threadIdx.x == 0U) {
            for (uint32_t i = firstJetty; i < firstJetty + kernelJettyCount; ++i) statuses[i] = AscendC::HCOMM_FAILED;
            CapturePublishAudit(channels, reinterpret_cast<__gm__ PublishAuditSummary*>(traces),
                reinterpret_cast<__gm__ PublishAuditCqe*>(sqTraces), kernelJettyCount,
                warmup + iterations, 0U, statuses, firstJetty);
        }
        return;
    }
#endif
    if (active) {
#if MULTI_JETTY_SCOPE_BUILD == 5 || MULTI_JETTY_SCOPE_BUILD >= 6
        status = SubmitPublisherCoreBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, 0U, warmup, jetty, jettyInKernel, kernelJettyCount);
#elif MULTI_JETTY_SCOPE_BUILD == 4
        status = SubmitParallelPublisherBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, 0U, warmup, jetty);
#elif MULTI_JETTY_SCOPE_BUILD == 2 || MULTI_JETTY_SCOPE_BUILD == 3
        status = SubmitPublisherCommitBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, 0U, warmup, jetty, jettyInKernel, kernelJettyCount);
#else
        status = mode == kModeSimtSerialCommit ? SubmitSerialCommitBatches(
                hcomm, group, block, batch, api, remote, local, payloadBytes, slotBytes, 0U, warmup,
                jetty, jettyInKernel, kernelJettyCount, traces, traceBatchesPerJetty,
                sqTraces, traceOperationsPerJetty) :
            (batch.context == nullptr ? AscendC::HCOMM_FAILED :
                SubmitGroupBatches(
                    hcomm, group, batch, api, remote, local, payloadBytes, slotBytes, 0U, warmup, jetty,
                    traces, traceBatchesPerJetty, sqTraces, traceOperationsPerJetty));
#endif
    }
    group.sync();
#if MULTI_JETTY_SCOPE_BUILD < 6
    if (active && rank == 0U && status == AscendC::HCOMM_SUCCESS && warmup != 0U) {
        auto* trace = traces == nullptr ? nullptr :
            &traces[jetty * traceBatchesPerJetty + warmup / batchSize - 1U];
        status = DrainWithTrace(hcomm, batch, jetty, 1U, trace);
        CaptureWorkspaceGuards(batchWorkspaces, jetty, trace);
    }
#endif
    if (rank == 0U) {
        statuses[jetty] = status;
    }
    block.sync();
    if (threadIdx.x == 0U) {
        for (uint32_t index = 0U; index < kernelJettyCount; ++index) {
            if (statuses[firstJetty + index] != AscendC::HCOMM_SUCCESS) {
                timing[kStatusIndex] = static_cast<uint64_t>(
                    static_cast<int64_t>(statuses[firstJetty + index]));
                break;
            }
        }
#if MULTI_JETTY_SCOPE_BUILD >= 6
        if (static_cast<int64_t>(timing[kStatusIndex]) == AscendC::HCOMM_SUCCESS) {
            auto set = CompletionSet(traces);
            status = hcomm.Drain(set);
            CaptureWarmupCompletion(set, reinterpret_cast<__gm__ PublishAuditSummary*>(traces), kernelJettyCount, firstJetty);
            if (status == AscendC::HCOMM_SUCCESS)
                status = PrepareCompletionWindows(hcomm, set, kernelJettyCount, iterations, batchSize, api, firstJetty);
            timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
            if (status != AscendC::HCOMM_SUCCESS) statuses[firstJetty] = status;
        }
#endif
    }
    block.sync();
    if (static_cast<int64_t>(timing[kStatusIndex]) != AscendC::HCOMM_SUCCESS) {
#if MULTI_JETTY_SCOPE_BUILD >= 6
        if (threadIdx.x == 0U) CapturePublishAudit(channels,
            reinterpret_cast<__gm__ PublishAuditSummary*>(traces),
            reinterpret_cast<__gm__ PublishAuditCqe*>(sqTraces), kernelJettyCount,
            warmup + iterations, 1U, statuses, firstJetty);
#endif
        return;
    }

    block.sync();
    const uint64_t begin = threadIdx.x == 0U ? clock() : 0U;
    if (active) {
#if MULTI_JETTY_SCOPE_BUILD == 5 || MULTI_JETTY_SCOPE_BUILD >= 6
        status = SubmitPublisherCoreBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, warmup, iterations, jetty, jettyInKernel, kernelJettyCount);
#elif MULTI_JETTY_SCOPE_BUILD == 4
        status = SubmitParallelPublisherBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, warmup, iterations, jetty);
#elif MULTI_JETTY_SCOPE_BUILD == 2 || MULTI_JETTY_SCOPE_BUILD == 3
        status = SubmitPublisherCommitBatches(hcomm, group, block, batch, api, remote, local,
            payloadBytes, slotBytes, warmup, iterations, jetty, jettyInKernel, kernelJettyCount);
#else
        status = mode == kModeSimtSerialCommit ? SubmitSerialCommitBatches(
            hcomm, group, block, batch, api, remote, local, payloadBytes, slotBytes, warmup, iterations,
            jetty, jettyInKernel, kernelJettyCount, traces, traceBatchesPerJetty,
            sqTraces, traceOperationsPerJetty) : SubmitGroupBatches(
            hcomm, group, batch, api, remote, local, payloadBytes, slotBytes, warmup, iterations, jetty,
            traces, traceBatchesPerJetty, sqTraces, traceOperationsPerJetty);
#endif
    }
    group.sync();
    if (rank == 0U) {
        statuses[jetty] = status;
    }
    block.sync();
    const uint64_t issueEnd = threadIdx.x == 0U ? clock() : 0U;
    block.sync();
#if MULTI_JETTY_SCOPE_BUILD >= 6
    if (threadIdx.x == 0U) {
        int32_t submitStatus = AscendC::HCOMM_SUCCESS;
        for (uint32_t i = 0U; i < kernelJettyCount; ++i) {
            if (statuses[firstJetty + i] != AscendC::HCOMM_SUCCESS) submitStatus = statuses[firstJetty + i];
        }
        if (submitStatus == AscendC::HCOMM_SUCCESS) {
            auto set = CompletionSet(traces);
            submitStatus = hcomm.Drain(set);
        }
        if (submitStatus != AscendC::HCOMM_SUCCESS) statuses[firstJetty] = submitStatus;
    }
#else
    if (active && rank == 0U && status == AscendC::HCOMM_SUCCESS) {
        auto* trace = traces == nullptr ? nullptr : &traces[(jetty + 1U) * traceBatchesPerJetty - 1U];
        status = DrainWithTrace(hcomm, batch, jetty, 2U, trace);
        CaptureWorkspaceGuards(batchWorkspaces, jetty, trace);
        statuses[jetty] = status;
    }
#endif
    block.sync();
    if (threadIdx.x == 0U) {
        int32_t finalStatus = AscendC::HCOMM_SUCCESS;
        for (uint32_t index = 0U; index < kernelJettyCount; ++index) {
            if (statuses[firstJetty + index] != AscendC::HCOMM_SUCCESS) {
                finalStatus = statuses[firstJetty + index];
                break;
            }
        }
        const uint64_t completionEnd = clock();
        const uint32_t activeJettyCount = mode == kModeSimtOneActiveGroup ? 1U : kernelJettyCount;
        const uint32_t totalOperations = iterations * activeJettyCount;
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(finalStatus));
        timing[kCompletedIndex] = finalStatus == AscendC::HCOMM_SUCCESS ? totalOperations : 0U;
        timing[kDoorbellCountIndex] = iterations / batchSize * activeJettyCount;
#if MULTI_JETTY_SCOPE_BUILD >= 6
        CapturePublishAudit(channels, reinterpret_cast<__gm__ PublishAuditSummary*>(traces),
            reinterpret_cast<__gm__ PublishAuditCqe*>(sqTraces), kernelJettyCount,
            warmup + iterations, 2U, statuses, firstJetty);
#endif
    }
}

} // namespace

extern "C" __global__ __vector__ void SimtMultiJettyBatchPerfKernel(
    __gm__ void* channelsAddr, __gm__ void* remoteIndicesAddr, __gm__ void* timingAddr, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount, uint32_t mode, uint32_t firstJetty, uint32_t kernelJettyCount,
    __gm__ void* tracesAddr, uint32_t traceBatchesPerJetty, __gm__ void* sqTracesAddr,
    uint32_t traceOperationsPerJetty)
{
    using namespace multi_jetty_batch_perf;
    auto* channels = reinterpret_cast<__gm__ uint64_t*>(channelsAddr);
    auto* remoteIndices = reinterpret_cast<__gm__ uint32_t*>(remoteIndicesAddr);
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);
    auto* traces = reinterpret_cast<__gm__ SimtBatchTraceRecord*>(tracesAddr);
    auto* sqTraces = reinterpret_cast<__gm__ SimtSqTraceRecord*>(sqTracesAddr);
#ifdef MULTI_JETTY_DIAGNOSTIC_FAST_BUILD
    alignas(128) __ubuf__ uint8_t batchWorkspaces[kWorkspaceStorageBytes];
#else
    alignas(128) __ubuf__ uint8_t batchWorkspaces[kMaxJettyCount * kSimtBatchWorkspaceBytes];
#endif
    alignas(16) __ubuf__ int32_t statuses[kMaxJettyCount];
    alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<kMaxActiveLanes>)];
    const bool parallel = mode >= kModeSimtParallel;
    const uint32_t laneCount = parallel ? batchSize * kernelJettyCount : batchSize;

#if MULTI_JETTY_SCOPE_BUILD == 7
    constexpr uint32_t selectedBatch = MULTI_JETTY_MATRIX_BATCH;
    constexpr uint32_t capacity = selectedBatch <= 4U ? 32U :
        (selectedBatch * kMaxJettyCount < kMaxActiveLanes ? selectedBatch * kMaxJettyCount : kMaxActiveLanes);
    if (parallel && batchSize == selectedBatch && laneCount <= capacity) {
        asc_vf_call<SimtParallelMultiJettyVf<selectedBatch, capacity>>(
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,
            iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,
            sqTraces, traceOperationsPerJetty, batchWorkspaces, statuses, tileBuffer);
    } else {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(AscendC::HCOMM_FAILED));
    }
#elif MULTI_JETTY_SCOPE_BUILD != 0
    // Scope builds prune only dispatch. Keep the full-build UB layout above.
    if (parallel && batchSize == 256U && jettyCount == 4U && firstJetty == 0U &&
        kernelJettyCount == 4U && laneCount == 1024U) {
        asc_vf_call<SimtParallelMultiJettyVf<256U, 1024U>>(
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,
            iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,
            sqTraces, traceOperationsPerJetty, batchWorkspaces, statuses, tileBuffer);
    } else {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(AscendC::HCOMM_FAILED));
    }
#elif defined(MULTI_JETTY_DIAGNOSTIC_FAST_BUILD)
#define LAUNCH_DIAGNOSTIC(size, capacity)                                                                      \
    asc_vf_call<SimtParallelMultiJettyVf<size##U, capacity##U>>(                                               \
        dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,           \
        iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,                  \
        sqTraces, traceOperationsPerJetty, batchWorkspaces, statuses, tileBuffer)

    if (parallel && batchSize == 32U && laneCount <= 256U) {
        LAUNCH_DIAGNOSTIC(32, 256);
    } else if (parallel && batchSize == 256U && laneCount <= 1024U) {
        LAUNCH_DIAGNOSTIC(256, 1024);
    } else {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(AscendC::HCOMM_FAILED));
    }
#undef LAUNCH_DIAGNOSTIC
#else
#define LAUNCH_PARALLEL(size)                                                                                  \
    if (laneCount <= 256U) {                                                                                   \
        asc_vf_call<SimtParallelMultiJettyVf<size##U, 256U>>(                                                  \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,              \
            sqTraces, traceOperationsPerJetty,                                                                  \
            batchWorkspaces, statuses, tileBuffer);                                                         \
    } else if (laneCount <= 512U) {                                                                            \
        asc_vf_call<SimtParallelMultiJettyVf<size##U, 512U>>(                                                  \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,              \
            sqTraces, traceOperationsPerJetty,                                                                  \
            batchWorkspaces, statuses, tileBuffer);                                                         \
    } else {                                                                                                   \
        asc_vf_call<SimtParallelMultiJettyVf<size##U, 1024U>>(                                                 \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, firstJetty, kernelJettyCount, mode, jettyCount, traces, traceBatchesPerJetty,              \
            sqTraces, traceOperationsPerJetty,                                                                  \
            batchWorkspaces, statuses, tileBuffer);                                                         \
    }

#define LAUNCH_BATCH(size, serialCapacity)                                                                     \
    if (parallel) {                                                                                            \
        LAUNCH_PARALLEL(size)                                                                                  \
    } else if (jettyCount == 1U) {                                                                             \
        asc_vf_call<SimtSerialMultiJettyVf<size##U, serialCapacity##U, 1U>>(                                   \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, jettyCount, batchWorkspaces, statuses, tileBuffer);                            \
    } else if (jettyCount == 2U) {                                                                             \
        asc_vf_call<SimtSerialMultiJettyVf<size##U, serialCapacity##U, 2U>>(                                   \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, jettyCount, batchWorkspaces, statuses, tileBuffer);                            \
    } else if (jettyCount == 4U) {                                                                             \
        asc_vf_call<SimtSerialMultiJettyVf<size##U, serialCapacity##U, 4U>>(                                   \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, jettyCount, batchWorkspaces, statuses, tileBuffer);                            \
    } else {                                                                                                   \
        asc_vf_call<SimtSerialMultiJettyVf<size##U, serialCapacity##U, 8U>>(                                   \
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,       \
            iterations, warmup, jettyCount, batchWorkspaces, statuses, tileBuffer);                            \
    }
    if (batchSize == 1U) {
        LAUNCH_BATCH(1, 32)
    } else if (batchSize == 8U) {
        LAUNCH_BATCH(8, 32)
    } else if (batchSize == 32U) {
        LAUNCH_BATCH(32, 32)
    } else if (batchSize == 64U) {
        LAUNCH_BATCH(64, 128)
    } else if (batchSize == 128U) {
        LAUNCH_BATCH(128, 128)
    } else {
        LAUNCH_BATCH(256, 1024)
    }
#undef LAUNCH_BATCH
#undef LAUNCH_PARALLEL
#endif
}

void LaunchSimtMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize,
    uint32_t jettyCount, uint32_t mode, uint32_t firstJetty, uint32_t kernelJettyCount,
    void* traces, uint32_t traceBatchesPerJetty, void* sqTraces, uint32_t traceOperationsPerJetty)
{
    SimtMultiJettyBatchPerfKernel<<<1U, 0U, stream>>>(
        channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup,
        batchSize, jettyCount, mode, firstJetty, kernelJettyCount, traces, traceBatchesPerJetty,
        sqTraces, traceOperationsPerJetty);
}
