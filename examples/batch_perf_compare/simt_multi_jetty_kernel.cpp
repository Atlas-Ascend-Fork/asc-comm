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
    uint32_t rank, uint32_t groupSize)
{
    int32_t status = 0;
    for (uint32_t begin = 0U; begin < count; begin += groupSize) {
        const int32_t appendStatus = AppendSimt(
            hcomm, batch, api, remote, local, payloadBytes, slotBytes, begin + rank);
        if (appendStatus != 0) {
            status = appendStatus;
            break;
        }
        const int32_t commitStatus = hcomm.BatchCommit(batch);
        if (commitStatus != 0) {
            status = commitStatus;
            break;
        }
    }
    return status;
}

template <uint32_t batchSize, uint32_t laneCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtMultiJettyPerfVf(
    __gm__ uint64_t* channels, __gm__ uint64_t* remotes, __gm__ uint8_t* local, __gm__ uint64_t* timing,
    uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t jettyCount, __ubuf__ uint8_t* batchBuffers, __ubuf__ uint32_t* groupStates,
    __ubuf__ int32_t* statuses, __ubuf__ uint8_t* tileBuffer)
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
        auto* remote = reinterpret_cast<__gm__ uint8_t*>(remotes[jetty]) + jetty * regionBytes;
        (void)hcomm.Init(reinterpret_cast<__ubuf__ uint8_t*>(groupState), 3U * sizeof(uint32_t));
        batches[wave] = hcomm.MakeBatchHandle(
            channels[jetty], batchBuffers + jetty * kSimtBatchBufferBytes, kSimtBatchBufferBytes,
            remote, api == kApiNotify ? 2U : 1U);
    }

    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        auto* remote = reinterpret_cast<__gm__ uint8_t*>(remotes[jetty]) + jetty * regionBytes;
        const int32_t status = SubmitJetty(
            hcomm, batches[wave], api, remote, local + jetty * regionBytes,
            payloadBytes, slotBytes, warmup, rank, batchSize);
        if (rank == 0U) {
            statuses[jetty] = status;
        }
    }
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        if (rank == 0U && statuses[jetty] == 0 && warmup != 0U) {
            statuses[jetty] = hcomm.Drain(batches[wave]);
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
        auto* remote = reinterpret_cast<__gm__ uint8_t*>(remotes[jetty]) + jetty * regionBytes;
        const int32_t status = SubmitJetty(
            hcomm, batches[wave], api, remote, local + jetty * regionBytes,
            payloadBytes, slotBytes, iterations, rank, batchSize);
        if (rank == 0U) {
            statuses[jetty] = status;
        }
    }
    block.sync();
    const uint64_t issueEnd = threadIdx.x == 0U ? clock() : 0U;
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t jetty = wave * physicalGroupCount + physicalGroup;
        if (rank == 0U && statuses[jetty] == 0) {
            statuses[jetty] = hcomm.Drain(batches[wave]);
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
    __gm__ void* channelsAddr, __gm__ void* remotesAddr, __gm__ void* localAddr, __gm__ void* timingAddr,
    uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    auto* channels = reinterpret_cast<__gm__ uint64_t*>(channelsAddr);
    auto* remotes = reinterpret_cast<__gm__ uint64_t*>(remotesAddr);
    auto* local = reinterpret_cast<__gm__ uint8_t*>(localAddr);
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);
    alignas(128) __ubuf__ uint8_t batchBuffers[kMaxJettyCount * kSimtBatchBufferBytes];
    alignas(16) __ubuf__ uint32_t groupStates[kMaxJettyCount * 3U];
    alignas(16) __ubuf__ int32_t statuses[kMaxJettyCount];
    alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<1024U>)];
    uint32_t laneCount = batchSize * jettyCount;
    laneCount = laneCount < 1024U ? laneCount : 1024U;
#define LAUNCH_BATCH(size, capacity)                                                                           \
    asc_vf_call<SimtMultiJettyPerfVf<size##U, capacity##U>>(                                                    \
        dim3(laneCount), channels, remotes, local, timing, api, payloadBytes, slotBytes, regionBytes,       \
        iterations, warmup,                                                                                 \
        jettyCount, batchBuffers, groupStates, statuses, tileBuffer)
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
    void* stream, void* channels, void* remotes, void* local, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    SimtMultiJettyPerfKernel<<<1U, 0U, stream>>>(
        channels, remotes, local, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup, batchSize,
        jettyCount);
}
