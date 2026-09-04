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

constexpr uint32_t kRemoteBufferIndex = 1U;
constexpr uint32_t kMaxWaves = 2U;

struct ResolvedRemote {
    AscendC::simt::HcommSimtBatchRemoteContext context;
    __gm__ uint8_t* base;
};

__simt_callee__ inline ResolvedRemote ResolveRemote(
    const __gm__ AscendC::MultiChannelRemoteInfo* info)
{
    auto* buffers = reinterpret_cast<__gm__ AscendC::RegedBufferEntity*>(info->remoteBufferAddr);
    auto* buffer = &buffers[kRemoteBufferIndex];
    return {
        {
            static_cast<uint64_t>(info->tpId & 0xFFFFFFU) |
                (static_cast<uint64_t>(buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU) << 32U),
            info->remoteEidLow,
            info->remoteEidHigh,
            buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue},
        reinterpret_cast<__gm__ uint8_t*>(buffer->bufferInfo.rma.addr)};
}

__simt_callee__ inline int32_t AppendSharedWqe(
    AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation,
    const AscendC::simt::HcommSimtBatchRemoteContext& remoteContext)
{
    auto* context = reinterpret_cast<__ubuf__ AscendC::simt::HcommSimtBatchStaticContext*>(batch.context);
    const uint64_t offset = static_cast<uint64_t>(operation % kSlotCount) * slotBytes;
    if (api == kApiWrite) {
        return AscendC::simt::HcommSimtAppendFinalBatchWqe(
            context, batch.groupRank, batch.groupSize,
            AscendC::simt::HcommSimtDataDesc<false, AscendC::HcommUrmaOpCode::WRITE, AscendC::URMA_DEFAULT_CFG>{
                remote + offset, local + offset, payloadBytes}, remoteContext);
    }
    if (api == kApiRead) {
        return AscendC::simt::HcommSimtAppendFinalBatchWqe(
            context, batch.groupRank, batch.groupSize,
            AscendC::simt::HcommSimtDataDesc<false, AscendC::HcommUrmaOpCode::READ, AscendC::URMA_DEFAULT_CFG>{
                remote + offset, local + offset, payloadBytes}, remoteContext);
    }
    return AscendC::simt::HcommSimtAppendFinalBatchWqe(
        context, batch.groupRank, batch.groupSize,
        AscendC::simt::HcommSimtNotifyDesc<true, AscendC::URMA_DEFAULT_CFG>{
            remote + offset, local + offset, payloadBytes,
            remote + offset + slotBytes - sizeof(uint64_t), kNotifyValue}, remoteContext);
}

template <typename HcommT>
__simt_callee__ inline int32_t SubmitSharedBatches(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api,
    __gm__ uint8_t** remoteBases, __gm__ uint8_t** localBases,
    const AscendC::simt::HcommSimtBatchRemoteContext* remoteContexts,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t count, uint32_t batchSize,
    uint32_t waveCount)
{
    for (uint32_t begin = 0U; begin < count; begin += batchSize) {
        for (uint32_t wave = 0U; wave < waveCount; ++wave) {
            const uint32_t operation = begin + (wave * batch.groupSize + batch.groupRank) % batchSize;
            const int32_t appendStatus = AppendSharedWqe(
                batch, api, remoteBases[wave], localBases[wave], payloadBytes, slotBytes,
                operation, remoteContexts[wave]);
            if (appendStatus != AscendC::HCOMM_SUCCESS) {
                return appendStatus;
            }
            const int32_t commitStatus = hcomm.BatchCommit(batch);
            if (commitStatus != AscendC::HCOMM_SUCCESS) {
                return commitStatus;
            }
        }
    }
    return AscendC::HCOMM_SUCCESS;
}

template <uint32_t laneCapacity>
__simt_vf__ __launch_bounds__(laneCapacity) inline void SimtSharedJettyPerfVf(
    uint64_t multiChannel, __gm__ uint64_t* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount, __ubuf__ uint8_t* batchBuffer,
    __ubuf__ uint32_t* groupState, __ubuf__ uint8_t* tileBuffer)
{
    auto& tileMemory =
        *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<laneCapacity>*>(tileBuffer);
    auto group = cooperative_groups::this_thread_block(tileMemory);
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const uint32_t totalItems = batchSize * jettyCount;
    const uint32_t waveCount = totalItems / size;

    auto* entity = reinterpret_cast<__gm__ AscendC::MultiChannelEntity*>(multiChannel);
    auto* remoteInfos = reinterpret_cast<__gm__ AscendC::MultiChannelRemoteInfo*>(entity->remoteInfoAddr);
    const AscendC::ChannelHandle channel = entity->channelHandle;
    __gm__ uint8_t* localBuffer = AscendC::simt::LocalBufferAddr(channel, kRemoteBufferIndex);
    __gm__ uint8_t* remoteBases[kMaxWaves];
    __gm__ uint8_t* localBases[kMaxWaves];
    AscendC::simt::HcommSimtBatchRemoteContext remoteContexts[kMaxWaves];
    for (uint32_t wave = 0U; wave < waveCount; ++wave) {
        const uint32_t item = wave * size + rank;
        const uint32_t jetty = item / batchSize;
        const ResolvedRemote remote = ResolveRemote(&remoteInfos[jetty]);
        remoteContexts[wave] = remote.context;
        remoteBases[wave] = remote.base + jetty * regionBytes;
        localBases[wave] = localBuffer + jetty * regionBytes;
    }

    AscendC::simt::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP, decltype(group)> hcomm(group);
    (void)hcomm.Init(reinterpret_cast<__ubuf__ uint8_t*>(groupState), 3U * sizeof(uint32_t));
    auto batch = hcomm.MakeBatchHandle(
        channel, batchBuffer, kSimtBatchBufferBytes, remoteBases[0], api == kApiNotify ? 2U : 1U);
    if (batch.context == nullptr) {
        if (rank == 0U) {
            timing[kStatusIndex] = static_cast<uint64_t>(AscendC::HCOMM_FAILED);
        }
        return;
    }

    int32_t status = SubmitSharedBatches(
        hcomm, batch, api, remoteBases, localBases, remoteContexts, payloadBytes, slotBytes,
        warmup, batchSize, waveCount);
    group.sync();
    if (rank == 0U && status == AscendC::HCOMM_SUCCESS && warmup != 0U) {
        status = hcomm.Drain(batch);
    }
    group.sync();
    if (rank == 0U) {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
    }
    group.sync();
    if (static_cast<int64_t>(timing[kStatusIndex]) != AscendC::HCOMM_SUCCESS) {
        return;
    }

    const uint64_t begin = rank == 0U ? clock() : 0U;
    status = SubmitSharedBatches(
        hcomm, batch, api, remoteBases, localBases, remoteContexts, payloadBytes, slotBytes,
        iterations, batchSize, waveCount);
    group.sync();
    const uint64_t issueEnd = rank == 0U ? clock() : 0U;
    if (rank == 0U && status == AscendC::HCOMM_SUCCESS) {
        status = hcomm.Drain(batch);
    }
    group.sync();
    if (rank == 0U) {
        const uint32_t totalOperations = iterations * jettyCount;
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = clock() - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
        timing[kCompletedIndex] = status == AscendC::HCOMM_SUCCESS ? totalOperations : 0U;
        timing[kPublishCountIndex] = iterations / batchSize * waveCount;
    }
}
} // namespace

extern "C" __global__ __vector__ void SimtMultiJettyPerfKernel(
    uint64_t multiChannel, __gm__ void* timingAddr, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);
    alignas(128) __ubuf__ uint8_t batchBuffer[kSimtBatchBufferBytes];
    alignas(16) __ubuf__ uint32_t groupState[3U];
    alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<1024U>)];
    uint32_t laneCount = batchSize * jettyCount;
    laneCount = laneCount < 1024U ? laneCount : 1024U;
#define LAUNCH_SHARED(capacity)                                                                                \
    asc_vf_call<SimtSharedJettyPerfVf<capacity##U>>(                                                           \
        dim3(laneCount), multiChannel, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup, \
        batchSize, jettyCount, batchBuffer, groupState, tileBuffer)
    if (laneCount <= 32U) {
        LAUNCH_SHARED(32);
    } else if (laneCount <= 64U) {
        LAUNCH_SHARED(64);
    } else if (laneCount <= 256U) {
        LAUNCH_SHARED(256);
    } else if (laneCount <= 512U) {
        LAUNCH_SHARED(512);
    } else {
        LAUNCH_SHARED(1024);
    }
#undef LAUNCH_SHARED
}

void LaunchSimtMultiJettyPerf(
    void* stream, uint64_t multiChannel, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    SimtMultiJettyPerfKernel<<<1U, 0U, stream>>>(
        multiChannel, timing, api, payloadBytes, slotBytes, regionBytes,
        iterations, warmup, batchSize, jettyCount);
}
