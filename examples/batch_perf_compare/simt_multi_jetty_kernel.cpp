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
#include "batch_perf_compare_common.h"

namespace {
using namespace batch_perf_compare;

__simt_callee__ inline void CaptureBatchFailure(
    __gm__ uint64_t* timing, uint32_t jetty, uint32_t phase, int32_t status, uint32_t operation,
    const AscendC::simt::UbcCtpBatchHandle& batch)
{
    __gm__ uint64_t* diag = timing + kDiagBaseIndex + jetty * kDiagWordsPerJetty;
    diag[0] = phase;
    diag[1] = static_cast<uint64_t>(static_cast<int64_t>(status));
    diag[2] = operation;
    diag[3] = batch.groupRank;
    if (batch.context == nullptr) {
        return;
    }

    auto* context = reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context);
    diag[5] = context->reservedHead;
    diag[7] = context->cachedSqTail;
    diag[8] = context->sqDepth;
    diag[9] = context->itemBb;
    diag[10] = context->groupSize;
    diag[11] = static_cast<uint64_t>(static_cast<int64_t>(context->reservationStatus));
    diag[28] = context->headAddr;
    diag[29] = context->dwqeAddr;
    diag[30] = context->sqBaseAddr;
    diag[31] = reinterpret_cast<uint64_t>(batch.context);
    if (context->headAddr == 0U || context->sqTailAddr == 0U || context->sqDepth == 0U) {
        return;
    }

    const uint64_t headVal = *reinterpret_cast<__gm__ uint64_t*>(context->headAddr);
    diag[4] = headVal;
    diag[6] = *reinterpret_cast<__gm__ uint32_t*>(context->sqTailAddr);
    auto* channelEntity = reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity*>(batch.channel);
    auto* cqContexts = reinterpret_cast<__gm__ AscendC::CqContext*>(channelEntity->cqContextAddr);
    auto* cqCtx = &cqContexts[AscendC::HCOMM_URMA_DEFAULT_QP_IDX];
    auto* cqTailAddr = reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.tailAddr);
    const uint32_t cqTail = *cqTailAddr;
    diag[12] = cqTail;
    const uint32_t cqDepth = cqCtx->contextInfo.ubJfc.cqDepth;
    diag[13] = cqDepth;
    if (cqDepth == 0U) {
        return;
    }
    auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
        cqCtx->contextInfo.ubJfc.scqVa + cqCtx->contextInfo.ubJfc.cqeSize *
        (cqTail % cqDepth));
    diag[14] = cqe[0];
    diag[15] = cqe[1];

    const uint32_t totalBb = context->groupSize * context->itemBb +
        (context->itemBb == 1U ? 1U : 0U);
    const uint32_t publisherHead = AscendC::simt::HcommSimtHeadIdx(headVal) - totalBb +
        (context->groupSize - 1U) * context->itemBb;
    diag[16] = publisherHead;
    auto* publisher = AscendC::simt::HcommSimtBatchPublisherWords(context);
    auto* sqFirst = reinterpret_cast<__gm__ uint64_t*>(
        context->sqBaseAddr + context->sqWqeSize * (publisherHead % context->sqDepth));
    auto* sqSecond = reinterpret_cast<__gm__ uint64_t*>(
        (publisherHead % context->sqDepth) + 1U == context->sqDepth ? context->sqBaseAddr :
        reinterpret_cast<uint64_t>(sqFirst + 8U));
    auto* dwqe = reinterpret_cast<__gm__ uint64_t*>(context->dwqeAddr);
    uint32_t sqMismatch = 0U;
    uint32_t dwqeMismatch = 0U;
    for (uint32_t word = 0U; word < 12U; ++word) {
        const uint64_t sqWord = word < 8U ? sqFirst[word] : sqSecond[word - 8U];
        const uint64_t ubWord = publisher[word];
        if (sqWord != ubWord) {
            sqMismatch |= 1U << word;
            if (diag[19] == 0U) {
                diag[19] = word + 1U;
                diag[20] = ubWord;
                diag[21] = sqWord;
            }
        }
        if (dwqe[word] != ubWord) {
            dwqeMismatch |= 1U << word;
            if (diag[22] == 0U) {
                diag[22] = word + 1U;
                diag[23] = ubWord;
                diag[24] = dwqe[word];
            }
        }
    }
    diag[17] = sqMismatch;
    diag[18] = dwqeMismatch;
    diag[25] = publisher[0];
    diag[26] = sqFirst[0];
    diag[27] = dwqe[0];
}

template <typename HcommT>
__simt_callee__ inline int32_t AppendSimt(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation)
{
    const uint64_t offset = static_cast<uint64_t>(operation % kSlotCount) * slotBytes;
    if (api == kApiWrite) {
        return hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes);
    }
    if (api == kApiRead) {
        return hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes);
    }
    return hcomm.WriteWithNotifyNbi(
        batch, remote + offset, local + offset, payloadBytes,
        remote + offset + slotBytes - sizeof(uint64_t), kNotifyValue);
}

template <typename HcommT>
__simt_callee__ inline int32_t SubmitJetty(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t count,
    uint32_t rank, uint32_t groupSize, uint32_t appendPhase, uint32_t commitPhase,
    uint32_t& failurePhase, uint32_t& failureOperation)
{
    int32_t status = 0;
    for (uint32_t begin = 0U; begin < count; begin += groupSize) {
        const int32_t appendStatus = AppendSimt(
            hcomm, batch, api, remote, local, payloadBytes, slotBytes, begin + rank);
        if (appendStatus != 0) {
            status = appendStatus;
            failurePhase = appendPhase;
            failureOperation = begin + rank;
            break;
        }
        const int32_t commitStatus = hcomm.BatchCommit(batch);
        if (commitStatus != 0) {
            status = commitStatus;
            failurePhase = commitPhase;
            failureOperation = begin + rank;
            break;
        }
    }
    return status;
}

template <uint32_t batchSize, uint32_t laneCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtMultiJettyPerfVf(
    __gm__ uint64_t* channels, __gm__ uint32_t* remoteIndices, __gm__ uint64_t* timing,
    uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t jettyCount, __ubuf__ uint8_t* batchBuffers, __ubuf__ uint32_t* groupStates,
    __ubuf__ int32_t* statuses, __ubuf__ uint32_t* failurePhases, __ubuf__ uint32_t* failureOperations,
    __ubuf__ uint8_t* tileBuffer)
{
    auto& tileMemory =
        *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<laneCapacity>*>(tileBuffer);
    auto block = cooperative_groups::this_thread_block(tileMemory);
    auto group = cooperative_groups::tiled_partition<batchSize>(block);
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t physicalGroup = static_cast<uint32_t>(group.meta_group_rank());
    const uint32_t physicalGroupCount = static_cast<uint32_t>(block.size()) / batchSize;
    const uint32_t waveCount = (jettyCount + physicalGroupCount - 1U) / physicalGroupCount;
    AscendC::simt::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP, decltype(group)> hcomm(group);
    AscendC::simt::UbcCtpBatchHandle batches[2U];

    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        auto* groupState = groupStates + jetty * 3U;
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            jetty * regionBytes;
        (void)hcomm.Init(reinterpret_cast<__ubuf__ uint8_t*>(groupState), 3U * sizeof(uint32_t));
        batches[wave] = hcomm.MakeBatchHandle(
            channels[jetty], batchBuffers + jetty * kSimtBatchBufferBytes, kSimtBatchBufferBytes,
            remote, api == kApiNotify ? 2U : 1U);
        if (rank == 0U) {
            statuses[jetty] = batches[wave].context == nullptr ? -1 : 0;
            failurePhases[jetty] = batches[wave].context == nullptr ? kDiagPhaseMakeHandle : kDiagPhaseNone;
            failureOperations[jetty] = 0U;
        }
    }

    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        if (batches[wave].context == nullptr) {
            continue;
        }
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            jetty * regionBytes;
        auto* local = AscendC::simt::LocalBufferAddr(channels[jetty], 1U) + jetty * regionBytes;
        uint32_t failurePhase = kDiagPhaseNone;
        uint32_t failureOperation = 0U;
        const int32_t status = SubmitJetty(
            hcomm, batches[wave], api, remote, local,
            payloadBytes, slotBytes, warmup, rank, batchSize, kDiagPhaseWarmupAppend, kDiagPhaseWarmupCommit,
            failurePhase, failureOperation);
        if (rank == 0U) {
            statuses[jetty] = status;
            failurePhases[jetty] = failurePhase;
            failureOperations[jetty] = failureOperation;
        }
    }
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        if (rank == 0U && statuses[jetty] == 0 && warmup != 0U) {
            statuses[jetty] = hcomm.Drain(batches[wave]);
            if (statuses[jetty] != 0) {
                failurePhases[jetty] = kDiagPhaseWarmupDrain;
                failureOperations[jetty] = warmup;
            }
        }
        if (rank == 0U && statuses[jetty] != 0) {
            CaptureBatchFailure(timing, jetty, failurePhases[jetty], statuses[jetty],
                failureOperations[jetty], batches[wave]);
        }
        group.sync();
    }
    block.sync();
    if (threadIdx.x == 0U) {
        for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
            if (statuses[jetty] != 0) {
                timing[kStatusIndex] = static_cast<uint64_t>(statuses[jetty]);
                break;
            }
        }
    }
    block.sync();
    if (static_cast<int64_t>(timing[kStatusIndex]) != 0) {
        return;
    }

    block.sync();
    const uint64_t begin = threadIdx.x == 0U ? clock() : 0U;
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        auto* remote = AscendC::simt::RemoteBufferAddr(channels[jetty], remoteIndices[jetty]) +
            jetty * regionBytes;
        auto* local = AscendC::simt::LocalBufferAddr(channels[jetty], 1U) + jetty * regionBytes;
        uint32_t failurePhase = kDiagPhaseNone;
        uint32_t failureOperation = 0U;
        const int32_t status = SubmitJetty(
            hcomm, batches[wave], api, remote, local,
            payloadBytes, slotBytes, iterations, rank, batchSize, kDiagPhaseTimedAppend, kDiagPhaseTimedCommit,
            failurePhase, failureOperation);
        if (rank == 0U) {
            statuses[jetty] = status;
            failurePhases[jetty] = failurePhase;
            failureOperations[jetty] = failureOperation;
        }
    }
    block.sync();
    const uint64_t issueEnd = threadIdx.x == 0U ? clock() : 0U;
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        if (rank == 0U && statuses[jetty] == 0) {
            statuses[jetty] = hcomm.Drain(batches[wave]);
            if (statuses[jetty] != 0) {
                failurePhases[jetty] = kDiagPhaseTimedDrain;
                failureOperations[jetty] = iterations;
            }
        }
        if (rank == 0U && statuses[jetty] != 0) {
            CaptureBatchFailure(timing, jetty, failurePhases[jetty], statuses[jetty],
                failureOperations[jetty], batches[wave]);
        }
        group.sync();
    }
    block.sync();
    if (threadIdx.x == 0U) {
        int32_t finalStatus = 0;
        for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
            if (statuses[jetty] != 0) {
                finalStatus = statuses[jetty];
                break;
            }
        }
        const uint64_t completionEnd = clock();
        const uint32_t totalOperations = iterations * jettyCount;
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(finalStatus);
        timing[kCompletedIndex] = finalStatus == 0 ? totalOperations : 0U;
        timing[kPublishCountIndex] = iterations / batchSize * jettyCount;
    }
}
} // namespace

extern "C" __global__ __vector__ void SimtMultiJettyPerfKernel(
    __gm__ void* channelsAddr, __gm__ void* remoteIndicesAddr, __gm__ void* timingAddr,
    uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    auto* channels = reinterpret_cast<__gm__ uint64_t*>(channelsAddr);
    auto* remoteIndices = reinterpret_cast<__gm__ uint32_t*>(remoteIndicesAddr);
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);
    alignas(128) __ubuf__ uint8_t batchBuffers[kMaxJettyCount * kSimtBatchBufferBytes];
    alignas(16) __ubuf__ uint32_t groupStates[kMaxJettyCount * 3U];
    alignas(16) __ubuf__ int32_t statuses[kMaxJettyCount];
    alignas(16) __ubuf__ uint32_t failurePhases[kMaxJettyCount];
    alignas(16) __ubuf__ uint32_t failureOperations[kMaxJettyCount];
    alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<1024U>)];
    uint32_t laneCount = batchSize * jettyCount;
    laneCount = laneCount < 1024U ? laneCount : 1024U;
#define LAUNCH_BATCH(size, capacity)                                                                           \
    asc_vf_call<SimtMultiJettyPerfVf<size##U, capacity##U>>(                                                    \
        dim3(laneCount), channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes,      \
        iterations, warmup,                                                                                 \
        jettyCount, batchBuffers, groupStates, statuses, failurePhases, failureOperations, tileBuffer)
    if (batchSize == 1U) {
        LAUNCH_BATCH(1, 32);
    } else if (batchSize == 8U) {
        LAUNCH_BATCH(8, 64);
    } else if (batchSize == 32U) {
        LAUNCH_BATCH(32, 256);
    } else if (batchSize == 64U) {
        LAUNCH_BATCH(64, 512);
    } else if (batchSize == 128U) {
        LAUNCH_BATCH(128, 1024);
    } else {
        LAUNCH_BATCH(256, 1024);
    }
#undef LAUNCH_BATCH
}

void LaunchSimtMultiJettyPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    SimtMultiJettyPerfKernel<<<1U, 0U, stream>>>(
        channels, remoteIndices, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup, batchSize,
        jettyCount);
}
