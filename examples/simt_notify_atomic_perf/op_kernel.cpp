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

#include "hcomm/hcomm_simt.h"

#include "simt_notify_atomic_common.h"

namespace {
using namespace simt_notify_atomic_perf;

constexpr uint32_t kWqeBytes = 128U;
constexpr uint32_t kRemoteCacheBytes = 32U;
constexpr uint32_t kPostContextBytes = 128U;
constexpr uint32_t kWorkspaceAlign = 128U;

#define HCOMM_CACHED_WORKSPACE_BYTES(laneCount)                                                            \
    ((((laneCount) * (kWqeBytes + kRemoteCacheBytes)) + kWorkspaceAlign - 1U) & ~(kWorkspaceAlign - 1U)) + \
        kPostContextBytes

// Keep the largest individual static UB object at 128KiB for t1024. Hcomm::Init
// treats the registration cache and shared post context as optional, so this
// path retains the per-lane WQE slots and falls back to GM metadata resolution.
#define HCOMM_WORKSPACE_BYTES(laneCount) \
    ((laneCount) == 1024U ? (laneCount) * kWqeBytes : HCOMM_CACHED_WORKSPACE_BYTES(laneCount))

enum class PerfApi : uint32_t { NOTIFY, FAA, CAS };

template <PerfApi api, bool commit>
__simt_callee__ inline int32_t SubmitOne(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t notifyOffset, uint32_t lane)
{
    if constexpr (api == PerfApi::NOTIFY) {
        uint64_t offset = static_cast<uint64_t>(lane) * (notifyOffset + sizeof(uint64_t));
        __gm__ uint8_t* remote = remoteBase + offset;
        __gm__ uint8_t* local = localBase + offset;
        return hcomm.WriteWithNotifyNbi<commit>(
            channel, remote, local, payloadBytes, remote + notifyOffset, kNotifySignal);
    } else {
        __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(remoteBase);
        __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(localBase);
        if constexpr (api == PerfApi::FAA) {
            return hcomm.AtomicFAA<uint64_t, commit>(channel, &remote[lane], &local[lane], kFaaAdd);
        } else {
            return hcomm.AtomicCAS<uint64_t, commit>(channel, &remote[lane], &local[lane], kCasInitial, kCasSwap);
        }
    }
}

template <PerfApi api>
__simt_callee__ inline int32_t SubmitLastCommitSeries(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t notifyOffset, uint32_t count)
{
    if (count == 0U) {
        return 0;
    }
    for (uint32_t i = 1U; i < count; ++i) {
        int32_t status = SubmitOne<api, false>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, 0U);
        if (status != 0) {
            return status;
        }
    }
    return SubmitOne<api, true>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, 0U);
}

template <PerfApi api, uint32_t kLaneCount>
__simt_callee__ inline int32_t SubmitMultiLaneLastCommitSeries(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t notifyOffset, uint32_t count, __ubuf__ uint32_t* postStatus)
{
    if (count == 0U) {
        return 0;
    }

    uint32_t lane = threadIdx.x;
    if (lane == 0U) {
        postStatus[0] = 0U;
    }
    asc_syncthreads();

    uint32_t deferredCount = count - 1U;
    for (uint32_t i = lane; i < deferredCount; i += kLaneCount) {
        int32_t status = SubmitOne<api, false>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, lane);
        if (status != 0) {
            (void)asc_atomic_exch(postStatus, 1U);
        }
    }
    asc_syncthreads();

    // Exactly one lane publishes the PI after every deferred WQE is complete.
    uint32_t commitLane = deferredCount % kLaneCount;
    if (postStatus[0] == 0U && lane == commitLane) {
        int32_t status =
            SubmitOne<api, true>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, commitLane);
        if (status != 0) {
            (void)asc_atomic_exch(postStatus, 1U);
        }
    }
    asc_syncthreads();
    return postStatus[0] == 0U ? 0 : -1;
}

template <PerfApi api, uint32_t kLaneCount>
__simt_callee__ inline int32_t SubmitLastCommitWorkload(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t notifyOffset, uint32_t count, __ubuf__ uint32_t* postStatus)
{
    if constexpr (kLaneCount == 1U) {
        return SubmitLastCommitSeries<api>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, count);
    } else {
        return SubmitMultiLaneLastCommitSeries<api, kLaneCount>(
            hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, count, postStatus);
    }
}

template <PerfApi api, uint32_t kLaneCount, uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtLastCommitPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __ubuf__ int32_t* drainStatus, __ubuf__ uint32_t* postStatus, __ubuf__ uint64_t* beginCycle)
{
    uint32_t notifyOffset = (payloadBytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        if (threadIdx.x == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }

    int32_t status = warmup == 0U ? 0 :
                                    SubmitLastCommitWorkload<api, kLaneCount>(
                                        hcomm, channel, remote, local, payloadBytes, notifyOffset, warmup, postStatus);
    if (threadIdx.x == 0U) {
        drainStatus[0] = status != 0 ? status : (warmup == 0U ? 0 : hcomm.Drain(channel));
    }
    if constexpr (kLaneCount > 1U) {
        asc_syncthreads();
    }
    if (drainStatus[0] != 0) {
        if (threadIdx.x == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }

    uint64_t singleLaneBegin = 0U;
    if constexpr (kLaneCount == 1U) {
        singleLaneBegin = clock();
    } else {
        if (threadIdx.x == 0U) {
            beginCycle[0] = clock();
        }
        asc_syncthreads();
    }
    status = SubmitLastCommitWorkload<api, kLaneCount>(
        hcomm, channel, remote, local, payloadBytes, notifyOffset, iterations, postStatus);
    if (threadIdx.x == 0U) {
        uint64_t issueEnd = clock();
        uint64_t begin = kLaneCount == 1U ? singleLaneBegin : beginCycle[0];
        drainStatus[0] = status != 0 ? status : hcomm.Drain(channel);
        uint64_t completionEnd = clock();
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
    }
}

template <uint32_t kLaneCount, uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtNotifyPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __ubuf__ int32_t* drainStatus, __ubuf__ uint64_t* beginCycle)
{
    uint32_t lane = threadIdx.x;
    uint32_t notifyOffset = (payloadBytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
    uint64_t laneOffset = static_cast<uint64_t>(lane) * (notifyOffset + sizeof(uint64_t));
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx) + laneOffset;
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx) + laneOffset;

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }
    for (uint32_t i = lane; i < warmup; i += kLaneCount) {
        (void)hcomm.WriteWithNotifyNbi(channel, remote, local, payloadBytes, remote + notifyOffset, kNotifySignal);
    }
    asc_syncthreads();
    if (lane == 0U) {
        drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    }
    asc_syncthreads();
    if (drainStatus[0] != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }

    if (lane == 0U) {
        beginCycle[0] = clock();
    }
    asc_syncthreads();
    for (uint32_t i = lane; i < iterations; i += kLaneCount) {
        (void)hcomm.WriteWithNotifyNbi(channel, remote, local, payloadBytes, remote + notifyOffset, kNotifySignal);
    }
    asc_syncthreads();
    if (lane == 0U) {
        uint64_t issueEnd = clock();
        drainStatus[0] = hcomm.Drain(channel);
        uint64_t completionEnd = clock();
        timing[kIssueCyclesIndex] = issueEnd - beginCycle[0];
        timing[kCompletionCyclesIndex] = completionEnd - beginCycle[0];
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
    }
}

template <uint32_t kLaneCount, uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtFaaPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __ubuf__ int32_t* drainStatus, __ubuf__ uint64_t* beginCycle)
{
    (void)payloadBytes;
    uint32_t lane = threadIdx.x;
    __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::LocalBufferAddr(channel, sendBufIdx));
    __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::RemoteBufferAddr(channel, recvBufIdx));

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }
    for (uint32_t i = lane; i < warmup; i += kLaneCount) {
        (void)hcomm.AtomicFAA<uint64_t>(channel, &remote[lane], &local[lane], kFaaAdd);
    }
    asc_syncthreads();
    if (lane == 0U) {
        drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    }
    asc_syncthreads();
    if (drainStatus[0] != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }

    if (lane == 0U) {
        beginCycle[0] = clock();
    }
    asc_syncthreads();
    for (uint32_t i = lane; i < iterations; i += kLaneCount) {
        (void)hcomm.AtomicFAA<uint64_t>(channel, &remote[lane], &local[lane], kFaaAdd);
    }
    asc_syncthreads();
    if (lane == 0U) {
        uint64_t issueEnd = clock();
        drainStatus[0] = hcomm.Drain(channel);
        uint64_t completionEnd = clock();
        timing[kIssueCyclesIndex] = issueEnd - beginCycle[0];
        timing[kCompletionCyclesIndex] = completionEnd - beginCycle[0];
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
    }
}

template <uint32_t kLaneCount, uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtCasPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __ubuf__ int32_t* drainStatus, __ubuf__ uint64_t* beginCycle)
{
    (void)payloadBytes;
    uint32_t lane = threadIdx.x;
    __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::LocalBufferAddr(channel, sendBufIdx));
    __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::RemoteBufferAddr(channel, recvBufIdx));

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }
    for (uint32_t i = lane; i < warmup; i += kLaneCount) {
        (void)hcomm.AtomicCAS<uint64_t>(channel, &remote[lane], &local[lane], kCasInitial, kCasSwap);
    }
    asc_syncthreads();
    if (lane == 0U) {
        drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    }
    asc_syncthreads();
    if (drainStatus[0] != 0) {
        if (lane == 0U) {
            timing[kIssueCyclesIndex] = 0U;
            timing[kCompletionCyclesIndex] = 0U;
            timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
            timing[kCompletedIndex] = 0U;
        }
        return;
    }

    if (lane == 0U) {
        beginCycle[0] = clock();
    }
    asc_syncthreads();
    for (uint32_t i = lane; i < iterations; i += kLaneCount) {
        (void)hcomm.AtomicCAS<uint64_t>(channel, &remote[lane], &local[lane], kCasInitial, kCasSwap);
    }
    asc_syncthreads();
    if (lane == 0U) {
        uint64_t issueEnd = clock();
        drainStatus[0] = hcomm.Drain(channel);
        uint64_t completionEnd = clock();
        timing[kIssueCyclesIndex] = issueEnd - beginCycle[0];
        timing[kCompletionCyclesIndex] = completionEnd - beginCycle[0];
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
    }
}

#define DEFINE_PERF_KERNEL(apiName, vfName, laneCount, launchBounds)                                             \
    extern "C" __global__ __vector__ void apiName##laneCount(                                                    \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes,  \
        uint32_t iterations, uint32_t warmup)                                                                    \
    {                                                                                                            \
        alignas(128) __ubuf__ uint64_t workspace[HCOMM_WORKSPACE_BYTES(laneCount) / sizeof(uint64_t)];           \
        __ubuf__ int32_t drainStatus[1];                                                                         \
        __ubuf__ uint64_t beginCycle[1];                                                                         \
        asc_vf_call<vfName<laneCount, launchBounds>>(                                                            \
            dim3(laneCount), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),        \
            payloadBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(workspace), sizeof(workspace), \
            drainStatus, beginCycle);                                                                            \
    }

#define DEFINE_LAST_COMMIT_PERF_KERNEL(apiName, api, laneCount, launchBounds)                                    \
    extern "C" __global__ __vector__ void apiName##laneCount(                                                    \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes,  \
        uint32_t iterations, uint32_t warmup)                                                                    \
    {                                                                                                            \
        alignas(128) __ubuf__ uint64_t workspace[HCOMM_WORKSPACE_BYTES(laneCount) / sizeof(uint64_t)];           \
        __ubuf__ int32_t drainStatus[1];                                                                         \
        __ubuf__ uint32_t postStatus[1];                                                                         \
        __ubuf__ uint64_t beginCycle[1];                                                                         \
        asc_vf_call<SimtLastCommitPerfVf<api, laneCount, launchBounds>>(                                         \
            dim3(laneCount), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),        \
            payloadBytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(workspace), sizeof(workspace), \
            drainStatus, postStatus, beginCycle);                                                                \
    }

DEFINE_PERF_KERNEL(SimtNotifyPerf, SimtNotifyPerfVf, 1U, 32U)
DEFINE_PERF_KERNEL(SimtFaaPerf, SimtFaaPerfVf, 1U, 32U)
DEFINE_PERF_KERNEL(SimtCasPerf, SimtCasPerfVf, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtNotifyLastCommitPerf, PerfApi::NOTIFY, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtNotifyLastCommitPerf, PerfApi::NOTIFY, 32U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtNotifyLastCommitPerf, PerfApi::NOTIFY, 1024U, 1024U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtFaaLastCommitPerf, PerfApi::FAA, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtFaaLastCommitPerf, PerfApi::FAA, 32U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtFaaLastCommitPerf, PerfApi::FAA, 1024U, 1024U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtCasLastCommitPerf, PerfApi::CAS, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtCasLastCommitPerf, PerfApi::CAS, 32U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtCasLastCommitPerf, PerfApi::CAS, 1024U, 1024U)

#undef DEFINE_LAST_COMMIT_PERF_KERNEL
#undef DEFINE_PERF_KERNEL
#undef HCOMM_WORKSPACE_BYTES
#undef HCOMM_CACHED_WORKSPACE_BYTES

} // namespace

void LaunchSimtNotifyPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t laneCount)
{
    (void)laneCount;
    SimtNotifyPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
}

void LaunchSimtFaaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup, uint32_t laneCount)
{
    (void)laneCount;
    SimtFaaPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
}

void LaunchSimtCasPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup, uint32_t laneCount)
{
    (void)laneCount;
    SimtCasPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
}

void LaunchSimtLastCommitPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t api, uint32_t laneCount)
{
    if (api == static_cast<uint32_t>(PerfApi::NOTIFY)) {
        if (laneCount == 1U) {
            SimtNotifyLastCommitPerf1U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
        } else if (laneCount == 32U) {
            SimtNotifyLastCommitPerf32U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
        } else {
            SimtNotifyLastCommitPerf1024U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
        }
    } else if (api == static_cast<uint32_t>(PerfApi::FAA)) {
        if (laneCount == 1U) {
            SimtFaaLastCommitPerf1U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        } else if (laneCount == 32U) {
            SimtFaaLastCommitPerf32U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        } else {
            SimtFaaLastCommitPerf1024U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        }
    } else {
        if (laneCount == 1U) {
            SimtCasLastCommitPerf1U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        } else if (laneCount == 32U) {
            SimtCasLastCommitPerf32U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        } else {
            SimtCasLastCommitPerf1024U<<<1, 0, stream>>>(
                channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
        }
    }
}
