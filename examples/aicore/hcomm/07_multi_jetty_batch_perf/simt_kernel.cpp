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
#include "hcomm/hcomm_simt.h"
#include "multi_jetty_batch_perf_common.h"
#include "simt_submit.h"
#include "simt_completion.h"

static_assert(AscendC::simt::HCOMM_SIMT_COMPLETION_VERSION == 1U);
namespace {
using namespace multi_jetty_batch_perf;

template <uint32_t batchSize, uint32_t laneCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtParallelMultiJettyVf(
    __gm__ uint64_t* channels, __gm__ uint32_t* remoteIndices, __gm__ uint64_t* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t firstJetty, uint32_t kernelJettyCount, uint32_t totalJettyCount, __gm__ uint8_t* completionBuffer,
    __ubuf__ uint8_t* batchWorkspaces, __ubuf__ int32_t* statuses, __ubuf__ uint8_t* tileBuffer)
{
    auto& tileMemory = *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<laneCapacity>*>(tileBuffer);
    auto block = cooperative_groups::this_thread_block(tileMemory);
    auto group = MakeParallelGroup<batchSize>(block);
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t jettyInKernel = static_cast<uint32_t>(threadIdx.x) / batchSize;
    const uint32_t jetty = firstJetty + jettyInKernel;
    AscendC::simt::Hcomm<> hcomm;
    __gm__ uint8_t* remote = nullptr;
    __gm__ uint8_t* local = nullptr;
    AscendC::simt::UbcCtpBatchHandle batch{};
    int32_t status = AscendC::HCOMM_SUCCESS;
    remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
             static_cast<uint64_t>(jetty) * regionBytes;
    local =
        AscendC::simt::LocalBufferAddr(channels[jetty], kLocalSendIndex) + static_cast<uint64_t>(jetty) * regionBytes;
    (void)hcomm.Init(nullptr, 0U);
    batch = hcomm.MakeBatchHandle(
        channels[jetty], batchWorkspaces + jetty * kSimtBatchWorkspaceBytes, kSimtBatchWorkspaceBytes, remote,
        api == kApiNotify ? 2U : 1U, group);
    // Capture the request-sequence origin during initialization, before the existing block barrier.
    if (rank == 0U) {
        reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer)[jetty].initialHead =
            batch.context == nullptr ?
                ~0ULL :
                reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context)->reservedHead;
        auto* channel = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(channels[jetty]);
        auto* cq =
            &reinterpret_cast<__gm__ AscendC::CqContext*>(channel->cqContextAddr)[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        asc_dcci_single(cqTail);
        reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer)[jetty].sampleStart = *cqTail;
    }
    if (threadIdx.x == 0U) {
        auto set = CompletionSet(completionBuffer);
        if (firstJetty == 0U) {
            status = hcomm.InitCompletionSet(set, channels, totalJettyCount, set.workspace, kCompletionWorkspaceBytes);
        } else {
            status = ResumeCompletionSet(set, firstJetty);
        }
        if (status == AscendC::HCOMM_SUCCESS)
            status = PrepareCompletionWindows(hcomm, set, kernelJettyCount, warmup, batchSize, api, firstJetty);
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
    }
    // Complete all Handle initialization before the candidate's ordinary Appends.
    block.sync();
    if (static_cast<int64_t>(timing[kStatusIndex]) != AscendC::HCOMM_SUCCESS) {
        if (threadIdx.x == 0U) {
            for (uint32_t i = firstJetty; i < firstJetty + kernelJettyCount; ++i)
                statuses[i] = AscendC::HCOMM_FAILED;
            CaptureCompletion(
                channels, reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer), kernelJettyCount,
                warmup + iterations, 0U, statuses, firstJetty);
        }
        return;
    }
    status = SubmitPublisherCoreBatches(
        hcomm, group, block, batch, api, remote, local, payloadBytes, slotBytes, 0U, warmup, jetty);
    group.sync();
    if (rank == 0U) {
        statuses[jetty] = status;
    }
    block.sync();
    if (threadIdx.x == 0U) {
        for (uint32_t index = 0U; index < kernelJettyCount; ++index) {
            if (statuses[firstJetty + index] != AscendC::HCOMM_SUCCESS) {
                timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(statuses[firstJetty + index]));
                break;
            }
        }
        if (static_cast<int64_t>(timing[kStatusIndex]) == AscendC::HCOMM_SUCCESS) {
            auto set = CompletionSet(completionBuffer);
            status = hcomm.Drain(set);
            CaptureWarmupCompletion(
                set, reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer), kernelJettyCount, firstJetty);
            if (status == AscendC::HCOMM_SUCCESS)
                status = PrepareCompletionWindows(hcomm, set, kernelJettyCount, iterations, batchSize, api, firstJetty);
            timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
            if (status != AscendC::HCOMM_SUCCESS)
                statuses[firstJetty] = status;
        }
    }
    block.sync();
    if (static_cast<int64_t>(timing[kStatusIndex]) != AscendC::HCOMM_SUCCESS) {
        if (threadIdx.x == 0U)
            CaptureCompletion(
                channels, reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer), kernelJettyCount,
                warmup + iterations, 1U, statuses, firstJetty);
        return;
    }
    block.sync();
    const uint64_t begin = threadIdx.x == 0U ? clock() : 0U;
    status = SubmitPublisherCoreBatches(
        hcomm, group, block, batch, api, remote, local, payloadBytes, slotBytes, warmup, iterations, jetty);
    group.sync();
    if (rank == 0U) {
        statuses[jetty] = status;
    }
    block.sync();
    const uint64_t issueEnd = threadIdx.x == 0U ? clock() : 0U;
    block.sync();
    if (threadIdx.x == 0U) {
        int32_t submitStatus = AscendC::HCOMM_SUCCESS;
        for (uint32_t i = 0U; i < kernelJettyCount; ++i) {
            if (statuses[firstJetty + i] != AscendC::HCOMM_SUCCESS)
                submitStatus = statuses[firstJetty + i];
        }
        if (submitStatus == AscendC::HCOMM_SUCCESS) {
            auto set = CompletionSet(completionBuffer);
            submitStatus = hcomm.Drain(set);
        }
        if (submitStatus != AscendC::HCOMM_SUCCESS)
            statuses[firstJetty] = submitStatus;
    }
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
        const uint32_t activeJettyCount = kernelJettyCount;
        const uint32_t totalOperations = iterations * activeJettyCount;
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(finalStatus));
        timing[kCompletedIndex] = finalStatus == AscendC::HCOMM_SUCCESS ? totalOperations : 0U;
        timing[kDoorbellCountIndex] = iterations / batchSize * activeJettyCount;
        CaptureCompletion(
            channels, reinterpret_cast<__gm__ CompletionSummary*>(completionBuffer), kernelJettyCount,
            warmup + iterations, 2U, statuses, firstJetty);
    }
}

} // namespace

extern "C" __global__ __vector__ void SimtMultiJettyBatchPerfKernel(
    __gm__ void* channelsAddr, __gm__ void* remoteIndicesAddr, __gm__ void* timingAddr, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount, uint32_t firstJetty, uint32_t kernelJettyCount,
    __gm__ void* completionAddr)
{
    using namespace multi_jetty_batch_perf;
    auto* channels = reinterpret_cast<__gm__ uint64_t*>(channelsAddr);
    auto* remoteIndices = reinterpret_cast<__gm__ uint32_t*>(remoteIndicesAddr);
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);
    auto* completionBuffer = reinterpret_cast<__gm__ uint8_t*>(completionAddr);
    alignas(128) __ubuf__ uint8_t batchWorkspaces[kMaxJettyCount * kSimtBatchWorkspaceBytes];
    alignas(16) __ubuf__ int32_t statuses[kMaxJettyCount];
    alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<kMaxActiveLanes>)];
    const uint32_t laneCount = batchSize * kernelJettyCount;
    constexpr uint32_t selectedBatch = MULTI_JETTY_MATRIX_BATCH;
    constexpr uint32_t capacity =
        selectedBatch <= 4U ?
            32U :
            (selectedBatch * kMaxJettyCount < kMaxActiveLanes ? selectedBatch * kMaxJettyCount : kMaxActiveLanes);
    if (batchSize == selectedBatch && laneCount <= capacity) {
        asc_vf_call<SimtParallelMultiJettyVf<selectedBatch, capacity>>(
            dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes, iterations,
            warmup, firstJetty, kernelJettyCount, jettyCount, completionBuffer, batchWorkspaces, statuses, tileBuffer);
    } else {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(AscendC::HCOMM_FAILED));
    }
}

void LaunchSimtMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize,
    uint32_t jettyCount, uint32_t firstJetty, uint32_t kernelJettyCount, void* completionBuffer)
{
    SimtMultiJettyBatchPerfKernel<<<1U, 0U, stream>>>(
        channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup, batchSize,
        jettyCount, firstJetty, kernelJettyCount, completionBuffer);
}
