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
#include "simt_batch_perf_common.h"

namespace {
using namespace simt_batch_perf;

template <uint32_t api>
__simt_callee__ inline int32_t SubmitImmediate(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t slot)
{
    const uint64_t offset = static_cast<uint64_t>(slot) * slotBytes;
    if constexpr (api == kApiWrite) {
        return hcomm.WriteNbi(channel, remote + offset, local + offset, payloadBytes);
    } else if constexpr (api == kApiRead) {
        return hcomm.ReadNbi(channel, local + offset, remote + offset, payloadBytes);
    } else if constexpr (api == kApiNotify) {
        return hcomm.WriteWithNotifyNbi(
            channel, remote + offset, local + offset, payloadBytes, remote + offset + slotBytes - sizeof(uint64_t),
            kNotifyValue);
    } else if constexpr (api == kApiWriteValue) {
        return hcomm.WriteValueNbi<uint64_t>(channel, remote + offset, kInlineValue);
    } else if constexpr (api == kApiFaa) {
        return hcomm.AtomicFAA<uint64_t>(channel, remote + offset, local + offset, kFaaAdd);
    } else {
        return hcomm.AtomicCAS<uint64_t>(channel, remote + offset, local + offset, kCasInitial, kCasSwap);
    }
}

template <uint32_t api, typename HcommT, typename... Groups>
__simt_callee__ inline int32_t AppendOneWqe(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t slot, const Groups&... groups)
{
    const uint64_t offset = static_cast<uint64_t>(slot) * slotBytes;
    if constexpr (api == kApiWrite) {
        return hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes, groups...);
    } else if constexpr (api == kApiRead) {
        return hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes, groups...);
    } else if constexpr (api == kApiNotify) {
        return hcomm.WriteWithNotifyNbi(
            batch, remote + offset, local + offset, payloadBytes, remote + offset + slotBytes - sizeof(uint64_t),
            kNotifyValue, groups...);
    } else if constexpr (api == kApiWriteValue) {
        return hcomm.template WriteValueNbi<uint64_t>(batch, remote + offset, kInlineValue, groups...);
    } else if constexpr (api == kApiFaa) {
        return hcomm.template AtomicFAA<uint64_t>(batch, remote + offset, local + offset, kFaaAdd, groups...);
    } else {
        return hcomm.template AtomicCAS<uint64_t>(
            batch, remote + offset, local + offset, kCasInitial, kCasSwap, groups...);
    }
}

template <uint32_t api>
__simt_callee__ inline int32_t SubmitImmediateSeries(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t count)
{
    for (uint32_t i = 0U; i < count; ++i) {
        int32_t status = SubmitImmediate<api>(hcomm, channel, remote, local, payloadBytes, slotBytes, i % kSlotCount);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

template <uint32_t api>
__simt_callee__ inline int32_t SubmitBatchSeries(
    AscendC::simt::Hcomm<>& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t count, uint32_t& publishCount)
{
    publishCount = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        int32_t status = AppendOneWqe<api>(hcomm, batch, remote, local, payloadBytes, slotBytes, i % kSlotCount);
        if (status != 0) {
            return status;
        }
        status = hcomm.BatchCommit(batch);
        if (status != 0) {
            return status;
        }
        ++publishCount;
    }
    return 0;
}

template <uint32_t api, typename HcommT, typename Group>
__simt_callee__ inline int32_t SubmitGroupBatches(
    HcommT& hcomm, const Group& group, AscendC::simt::UbcCtpBatchHandle& batch, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t count, uint32_t& publishCount)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    publishCount = 0U;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        const uint32_t operation = begin + rank;
        // One group lane prepares exactly one WQE for this batch.
        const int32_t appendStatus =
            AppendOneWqe<api>(hcomm, batch, remote, local, payloadBytes, slotBytes, operation % kSlotCount, group);
        int32_t commitStatus = appendStatus == 0 ? hcomm.BatchCommit(batch, group) : appendStatus;
        if (appendStatus != 0 || commitStatus != 0) {
            return appendStatus != 0 ? appendStatus : commitStatus;
        }
        ++publishCount;
    }
    return 0;
}

template <uint32_t api, bool explicitBatch>
__simt_vf__ __launch_bounds__(32) inline void SimtBatchPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batchBuffer)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        timing[kStatusIndex] = static_cast<uint64_t>(-1);
        return;
    }

    uint32_t publishCount = 0U;
    int32_t status = 0;
    AscendC::simt::UbcCtpBatchHandle batch;
    if constexpr (explicitBatch) {
        constexpr uint32_t itemBb = api == kApiNotify || api == kApiFaa || api == kApiCas ? 2U : 1U;
        batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, itemBb);
        if (batch.context == nullptr) {
            status = AscendC::HCOMM_FAILED;
        } else {
            status = SubmitBatchSeries<api>(hcomm, batch, remote, local, payloadBytes, slotBytes, warmup, publishCount);
        }
    } else {
        status = SubmitImmediateSeries<api>(hcomm, channel, remote, local, payloadBytes, slotBytes, warmup);
    }
    if (status != 0) {
        timing[kStatusIndex] = static_cast<uint64_t>(status);
        timing[kPublishCountIndex] = publishCount;
        return;
    }
    if (warmup != 0U) {
        if constexpr (explicitBatch) {
            status = hcomm.Drain(batch);
        } else {
            status = hcomm.Drain(channel);
        }
        if (status != 0) {
            timing[kStatusIndex] = static_cast<uint64_t>(status);
            timing[kPublishCountIndex] = publishCount;
            return;
        }
    }

    uint64_t begin = clock();
    if constexpr (explicitBatch) {
        status = SubmitBatchSeries<api>(hcomm, batch, remote, local, payloadBytes, slotBytes, iterations, publishCount);
    } else {
        status = SubmitImmediateSeries<api>(hcomm, channel, remote, local, payloadBytes, slotBytes, iterations);
        publishCount = iterations;
    }
    uint64_t issueEnd = clock();
    if (status != 0) {
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(status);
        timing[kPublishCountIndex] = publishCount;
        return;
    }
    if constexpr (explicitBatch) {
        status = hcomm.Drain(batch);
    } else {
        status = hcomm.Drain(channel);
    }
    uint64_t completionEnd = clock();
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kStatusIndex] = static_cast<uint64_t>(status);
    timing[kCompletedIndex] = status == 0 ? iterations : 0U;
    timing[kPublishCountIndex] = publishCount;
}

template <uint32_t groupCapacity, bool staticTile>
__simt_callee__ inline auto MakePerfGroup(__ubuf__ uint8_t* tileBuffer)
{
    if constexpr (groupCapacity <= 32U) {
        (void)tileBuffer;
        return cooperative_groups::coalesced_threads();
    } else if constexpr (staticTile) {
        auto& tileMemory =
            *reinterpret_cast<__ubuf__ cooperative_groups::block_tile_memory<groupCapacity>*>(tileBuffer);
        auto block = cooperative_groups::this_thread_block(tileMemory);
        return cooperative_groups::tiled_partition<groupCapacity>(block);
    } else {
        (void)tileBuffer;
        return cooperative_groups::this_thread_block();
    }
}

template <uint32_t api, uint32_t groupCapacity, bool staticTile = true>
__simt_vf__ __launch_bounds__(groupCapacity) inline void SimtGroupBatchPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batchBuffer,
    __ubuf__ uint32_t* drainStatus, __ubuf__ uint8_t* tileBuffer)
{
    auto group = MakePerfGroup<groupCapacity, staticTile>(tileBuffer);
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::Hcomm<> hcomm;
    int32_t status = hcomm.Init(nullptr, 0U);
    constexpr uint32_t itemBb = api == kApiNotify || api == kApiFaa || api == kApiCas ? 2U : 1U;
    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, itemBb, group);
    if (batch.context == nullptr) {
        status = AscendC::HCOMM_FAILED;
    }
    uint32_t publishCount = 0U;
    if (status == 0) {
        status =
            SubmitGroupBatches<api>(hcomm, group, batch, remote, local, payloadBytes, slotBytes, warmup, publishCount);
    }
    if (status == 0 && warmup != 0U) {
        if (rank == 0U) {
            status = hcomm.Drain(batch);
            drainStatus[0] = static_cast<uint32_t>(status);
        }
        group.sync();
        status = static_cast<int32_t>(drainStatus[0]);
    }
    if (status != 0) {
        if (rank == 0U) {
            timing[kStatusIndex] = static_cast<uint64_t>(status);
            timing[kPublishCountIndex] = publishCount;
        }
        return;
    }

    group.sync();
    uint64_t begin = rank == 0U ? clock() : 0U;
    status =
        SubmitGroupBatches<api>(hcomm, group, batch, remote, local, payloadBytes, slotBytes, iterations, publishCount);
    uint64_t issueEnd = rank == 0U ? clock() : 0U;
    if (status == 0 && rank == 0U) {
        status = hcomm.Drain(batch);
    }
    if (rank == 0U) {
        const uint64_t completionEnd = clock();
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(status);
        timing[kCompletedIndex] = status == 0 ? iterations : 0U;
        timing[kPublishCountIndex] = publishCount;
    }
    group.sync();
}

} // namespace

#define DEFINE_BATCH_PERF_KERNEL(kernelName, api, explicitBatch)                                                \
    extern "C" __global__ __vector__ void kernelName(                                                           \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes, \
        uint32_t slotBytes, uint32_t iterations, uint32_t warmup)                                               \
    {                                                                                                           \
        alignas(128) __ubuf__ uint64_t batchBuffer[kBatchBufferBytes / sizeof(uint64_t)];                       \
        asc_vf_call<SimtBatchPerfVf<api, explicitBatch>>(                                                       \
            dim3(1), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), payloadBytes, \
            slotBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer));                   \
    }

DEFINE_BATCH_PERF_KERNEL(SimtWriteImmediatePerf, kApiWrite, false)
DEFINE_BATCH_PERF_KERNEL(SimtWriteBatchPerf, kApiWrite, true)
DEFINE_BATCH_PERF_KERNEL(SimtReadImmediatePerf, kApiRead, false)
DEFINE_BATCH_PERF_KERNEL(SimtReadBatchPerf, kApiRead, true)
DEFINE_BATCH_PERF_KERNEL(SimtNotifyImmediatePerf, kApiNotify, false)
DEFINE_BATCH_PERF_KERNEL(SimtNotifyBatchPerf, kApiNotify, true)
DEFINE_BATCH_PERF_KERNEL(SimtWriteValueImmediatePerf, kApiWriteValue, false)
DEFINE_BATCH_PERF_KERNEL(SimtWriteValueBatchPerf, kApiWriteValue, true)
DEFINE_BATCH_PERF_KERNEL(SimtFaaImmediatePerf, kApiFaa, false)
DEFINE_BATCH_PERF_KERNEL(SimtFaaBatchPerf, kApiFaa, true)
DEFINE_BATCH_PERF_KERNEL(SimtCasImmediatePerf, kApiCas, false)
DEFINE_BATCH_PERF_KERNEL(SimtCasBatchPerf, kApiCas, true)

#undef DEFINE_BATCH_PERF_KERNEL

#if SIMT_BATCH_ENABLE_LARGE_GROUP
#define LAUNCH_LARGE_GROUP_BATCH(api)                                                                      \
    else                                                                                                   \
    {                                                                                                      \
        asc_vf_call<SimtGroupBatchPerfVf<api, kMaxGroupLanes, false>>(                                     \
            dim3(groupLanes), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), \
            payloadBytes, slotBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), \
            drainStatus, tileBuffer);                                                                      \
    }
#else
#define LAUNCH_LARGE_GROUP_BATCH(api)
#endif

#define DEFINE_GROUP_BATCH_PERF_KERNEL(kernelName, api)                                                         \
    extern "C" __global__ __vector__ void kernelName(                                                           \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes, \
        uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t groupLanes)                          \
    {                                                                                                           \
        alignas(128) __ubuf__ uint64_t batchBuffer[kBatchBufferBytes / sizeof(uint64_t)];                       \
        alignas(16) __ubuf__ uint32_t drainStatus[1U];                                                          \
        alignas(16) __ubuf__ uint8_t tileBuffer[sizeof(cooperative_groups::block_tile_memory<kMaxGroupLanes>)]; \
        if (groupLanes <= 32U) {                                                                                \
            asc_vf_call<SimtGroupBatchPerfVf<api, 32U>>(                                                        \
                dim3(groupLanes), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),  \
                payloadBytes, slotBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer),  \
                drainStatus, tileBuffer);                                                                       \
        } else if (groupLanes == 128U) {                                                                        \
            asc_vf_call<SimtGroupBatchPerfVf<api, 128U>>(                                                       \
                dim3(groupLanes), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),  \
                payloadBytes, slotBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer),  \
                drainStatus, tileBuffer);                                                                       \
        } else if (groupLanes <= 128U) {                                                                        \
            asc_vf_call<SimtGroupBatchPerfVf<api, 128U, false>>(                                                \
                dim3(groupLanes), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),  \
                payloadBytes, slotBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer),  \
                drainStatus, tileBuffer);                                                                       \
        }                                                                                                       \
        LAUNCH_LARGE_GROUP_BATCH(api)                                                                           \
    }

DEFINE_GROUP_BATCH_PERF_KERNEL(SimtWriteGroupBatchPerf, kApiWrite)
DEFINE_GROUP_BATCH_PERF_KERNEL(SimtReadGroupBatchPerf, kApiRead)
DEFINE_GROUP_BATCH_PERF_KERNEL(SimtNotifyGroupBatchPerf, kApiNotify)
DEFINE_GROUP_BATCH_PERF_KERNEL(SimtWriteValueGroupBatchPerf, kApiWriteValue)
DEFINE_GROUP_BATCH_PERF_KERNEL(SimtFaaGroupBatchPerf, kApiFaa)
DEFINE_GROUP_BATCH_PERF_KERNEL(SimtCasGroupBatchPerf, kApiCas)

#undef DEFINE_GROUP_BATCH_PERF_KERNEL
#undef LAUNCH_LARGE_GROUP_BATCH

void LaunchSimtBatchPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t api, bool explicitBatch, bool groupBatch,
    uint32_t groupLanes)
{
#define LAUNCH_SET(immediateKernel, batchKernel, groupKernel)                                                  \
    if (groupBatch) {                                                                                          \
        groupKernel<<<1, 0, stream>>>(                                                                         \
            channel, sendBufIdx, recvBufIdx, timing, payloadBytes, slotBytes, iterations, warmup, groupLanes); \
    } else if (explicitBatch) {                                                                                \
        batchKernel<<<1, 0, stream>>>(                                                                         \
            channel, sendBufIdx, recvBufIdx, timing, payloadBytes, slotBytes, iterations, warmup);             \
    } else {                                                                                                   \
        immediateKernel<<<1, 0, stream>>>(                                                                     \
            channel, sendBufIdx, recvBufIdx, timing, payloadBytes, slotBytes, iterations, warmup);             \
    }
    if (api == kApiWrite) {
        LAUNCH_SET(SimtWriteImmediatePerf, SimtWriteBatchPerf, SimtWriteGroupBatchPerf)
    } else if (api == kApiRead) {
        LAUNCH_SET(SimtReadImmediatePerf, SimtReadBatchPerf, SimtReadGroupBatchPerf)
    } else if (api == kApiNotify) {
        LAUNCH_SET(SimtNotifyImmediatePerf, SimtNotifyBatchPerf, SimtNotifyGroupBatchPerf)
    } else if (api == kApiWriteValue) {
        LAUNCH_SET(SimtWriteValueImmediatePerf, SimtWriteValueBatchPerf, SimtWriteValueGroupBatchPerf)
    } else if (api == kApiFaa) {
        LAUNCH_SET(SimtFaaImmediatePerf, SimtFaaBatchPerf, SimtFaaGroupBatchPerf)
    } else {
        LAUNCH_SET(SimtCasImmediatePerf, SimtCasBatchPerf, SimtCasGroupBatchPerf)
    }
#undef LAUNCH_SET
}
