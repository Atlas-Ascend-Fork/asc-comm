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
#include "utils/debug/asc_time.h"

#include "hcomm/hcomm_simt.h"

#include "simt_notify_atomic_common.h"

namespace {
using namespace simt_notify_atomic;

constexpr uint32_t kCyclesIndex = 0U;
constexpr uint32_t kDrainStatusIndex = 1U;
constexpr uint32_t kCompletedIndex = 2U;
constexpr uint32_t kCompletionCyclesIndex = 3U;
constexpr uint32_t kNotifyAlignment = sizeof(uint64_t);
constexpr uint32_t kWqeStagingWords = 128U / sizeof(uint64_t);
constexpr uint32_t kDwqeBatchStateWords = 4U;
// Hcomm owns the layout; the benchmark only supplies an opaque 128-byte UB region.
constexpr uint32_t kPostContextWords = 128U / sizeof(uint64_t);
constexpr int64_t kPostContextInitFailed = -3;

template <bool commit>
__simt_callee__ inline void SubmitNotify(AscendC::simt::Hcomm<> &hcomm,
    uint64_t channel, __gm__ uint8_t *local, __gm__ uint8_t *remote,
    uint32_t payloadBytes, uint32_t notifyOffset)
{
    (void)hcomm.WriteWithNotifyNbi<commit>(
        channel, remote, local, payloadBytes,
        remote + notifyOffset, kNotifySignal);
}

// This is the validated notify_immediate_repeat functional flow with timing added
// around only the measured WQE-fill + doorbell loop.
// The t1 path launches one lane. Keep the compile-time bound within the
// 1..256 range so the compiler can use the larger per-thread register budget.
__simt_vf__ __launch_bounds__(32) inline void SimtNotifyPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ uint64_t *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup,
    __ubuf__ uint8_t *wqeStaging, uint32_t wqeStagingBytes,
    __ubuf__ uint8_t *postContext)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t *local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t *remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    uint32_t notifyOffset =
        (payloadBytes + kNotifyAlignment - 1U) & ~(kNotifyAlignment - 1U);
    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(wqeStaging, wqeStagingBytes) != 0) {
        timing[kCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
        timing[kCompletedIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        return;
    }
    hcomm.BindUbPostContext(postContext);
    if (hcomm.InitUbPostContext(channel) != 0) {
        timing[kCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(kPostContextInitFailed);
        timing[kCompletedIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        return;
    }

    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    int32_t status = warmup == 0U ? 0 : hcomm.Drain(channel);
    if (status != 0) {
        timing[kCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
        timing[kCompletedIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    uint64_t end = clock();

    status = hcomm.Drain(channel);
    uint64_t completionEnd = clock();
    timing[kCyclesIndex] = end - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
    timing[kCompletedIndex] = status == 0 ? iterations : 0U;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
}

// A group of kLaneCount lanes constructs kLaneCount WQEs in parallel. Lanes
// 1..N-1 defer their WQEs, then lane 0 posts the final immediate WQE and rings
// one DWQE doorbell for the whole group.
template <uint32_t kLaneCount>
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyBatchPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ uint64_t *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup,
    __ubuf__ uint32_t *batchState)
{
    if (threadIdx.x >= kLaneCount || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t *local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t *remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    uint32_t notifyOffset =
        (payloadBytes + kNotifyAlignment - 1U) & ~(kNotifyAlignment - 1U);
    uint32_t lane = threadIdx.x;
    AscendC::simt::Hcomm<> hcomm;

    uint32_t warmupGroups = (warmup + kLaneCount - 1U) / kLaneCount;
    for (uint32_t group = 0U; group < warmupGroups; ++group) {
        uint32_t groupStart = group * kLaneCount;
        uint32_t groupSize = warmup - groupStart;
        groupSize = groupSize < kLaneCount ? groupSize : kLaneCount;
        if (hcomm.ReserveDwqeBatch(channel, batchState, lane < groupSize, groupSize, 0U) != 0) {
            return;
        }
        if (lane != 0U && lane < groupSize) {
            uint64_t laneOffset =
                static_cast<uint64_t>(lane) * (notifyOffset + sizeof(uint64_t));
            SubmitNotify<false>(hcomm, channel, local + laneOffset,
                remote + laneOffset, payloadBytes, notifyOffset);
        }
        asc_syncthreads();
        if (lane == 0U) {
            SubmitNotify<true>(hcomm, channel, local, remote, payloadBytes, notifyOffset);
        } else {
            // Pair with the barrier inside the final commit=true submission.
            asc_syncthreads();
        }
        // Do not start constructing the next group until its doorbell is written.
        asc_syncthreads();
    }

    if (lane == 0U) {
        int32_t status = warmup == 0U ? 0 : hcomm.Drain(channel);
        timing[kCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
        timing[kCompletedIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        asc_threadfence();
    }
    asc_syncthreads();
    if (static_cast<int64_t>(timing[kDrainStatusIndex]) != 0) {
        return;
    }

    uint64_t begin = 0U;
    if (lane == 0U) {
        begin = clock();
    }
    asc_syncthreads();
    uint32_t groups = (iterations + kLaneCount - 1U) / kLaneCount;
    for (uint32_t group = 0U; group < groups; ++group) {
        uint32_t groupStart = group * kLaneCount;
        uint32_t groupSize = iterations - groupStart;
        groupSize = groupSize < kLaneCount ? groupSize : kLaneCount;
        if (hcomm.ReserveDwqeBatch(channel, batchState, lane < groupSize, groupSize, 0U) != 0) {
            if (lane == 0U) {
                timing[kCyclesIndex] = 0U;
                timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
                timing[kCompletedIndex] = 0U;
                timing[kCompletionCyclesIndex] = 0U;
            }
            return;
        }
        if (lane != 0U && lane < groupSize) {
            uint64_t laneOffset =
                static_cast<uint64_t>(lane) * (notifyOffset + sizeof(uint64_t));
            SubmitNotify<false>(hcomm, channel, local + laneOffset,
                remote + laneOffset, payloadBytes, notifyOffset);
        }
        asc_syncthreads();
        if (lane == 0U) {
            SubmitNotify<true>(hcomm, channel, local, remote, payloadBytes, notifyOffset);
        } else {
            asc_syncthreads();
        }
        asc_syncthreads();
    }

    if (lane == 0U) {
        uint64_t end = clock();
        int32_t status = hcomm.Drain(channel);
        uint64_t completionEnd = clock();
        timing[kCyclesIndex] = end - begin;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
        timing[kCompletedIndex] = status == 0 ? iterations : 0U;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
    }
}
} // namespace

extern "C" __global__ __vector__ void SimtNotifyPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    alignas(128) __ubuf__ uint64_t wqeStaging[kWqeStagingWords];
    alignas(128) __ubuf__ uint64_t postContextStorage[kPostContextWords];
    asc_vf_call<SimtNotifyPerfVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), payloadBytes,
        iterations, warmup, (__ubuf__ uint8_t *)wqeStaging, sizeof(wqeStaging),
        (__ubuf__ uint8_t *)postContextStorage);
}

void LaunchSimtNotifyPerf(
    void *stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    void *timing, uint32_t payloadBytes, uint32_t iterations, uint32_t warmup)
{
    SimtNotifyPerf<<<1, 0, stream>>>(
        channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
}

extern "C" __global__ __vector__ void SimtNotifyBatchPerf32(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    __ubuf__ uint32_t batchState[kDwqeBatchStateWords];
    asc_vf_call<SimtNotifyBatchPerfVf<32U>>(
        dim3(32), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), payloadBytes,
        iterations, warmup, batchState);
}

extern "C" __global__ __vector__ void SimtNotifyBatchPerf1024(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    __ubuf__ uint32_t batchState[kDwqeBatchStateWords];
    asc_vf_call<SimtNotifyBatchPerfVf<1024U>>(
        dim3(1024), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), payloadBytes,
        iterations, warmup, batchState);
}

void LaunchSimtNotifyBatchPerf(
    void *stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    void *timing, uint32_t payloadBytes, uint32_t iterations,
    uint32_t warmup, uint32_t laneCount)
{
    if (laneCount == 32U) {
        SimtNotifyBatchPerf32<<<1, 0, stream>>>(
            channel, sendBufIdx, recvBufIdx, timing,
            payloadBytes, iterations, warmup);
    } else {
        SimtNotifyBatchPerf1024<<<1, 0, stream>>>(
            channel, sendBufIdx, recvBufIdx, timing,
            payloadBytes, iterations, warmup);
    }
}
