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
            return hcomm.AtomicFAA<uint64_t, commit>(channel, &remote[0], &local[0], kFaaAdd);
        } else {
            return hcomm.AtomicCAS<uint64_t, commit>(channel, &remote[0], &local[0], kCasInitial, kCasSwap);
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
    // Every WQE but the last is deferred, so each reserves its own SQ space; the final
    // committed WQE publishes the whole batch with one head update.
    for (uint32_t i = 1U; i < count; ++i) {
        int32_t status = SubmitOne<api, false>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, 0U);
        if (status != 0) {
            return status;
        }
    }
    return SubmitOne<api, true>(hcomm, channel, remoteBase, localBase, payloadBytes, notifyOffset, 0U);
}

template <PerfApi api, uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtLastCommitPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ int32_t* drainStatus)
{
    uint32_t notifyOffset = (payloadBytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init() != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
        timing[kCompletedIndex] = 0U;
        return;
    }

    int32_t status = warmup == 0U ?
                         0 :
                         SubmitLastCommitSeries<api>(hcomm, channel, remote, local, payloadBytes, notifyOffset, warmup);
    drainStatus[0] = status != 0 ? status : (warmup == 0U ? 0 : hcomm.Drain(channel));
    if (drainStatus[0] != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    status = SubmitLastCommitSeries<api>(hcomm, channel, remote, local, payloadBytes, notifyOffset, iterations);
    uint64_t issueEnd = clock();
    drainStatus[0] = status != 0 ? status : hcomm.Drain(channel);
    uint64_t completionEnd = clock();
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
    timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
}

template <uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtNotifyPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ int32_t* drainStatus)
{
    uint32_t notifyOffset = (payloadBytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init() != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
        timing[kCompletedIndex] = 0U;
        return;
    }
    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.WriteWithNotifyNbi(channel, remote, local, payloadBytes, remote + notifyOffset, kNotifySignal);
    }
    drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    if (drainStatus[0] != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.WriteWithNotifyNbi(channel, remote, local, payloadBytes, remote + notifyOffset, kNotifySignal);
    }
    uint64_t issueEnd = clock();
    drainStatus[0] = hcomm.Drain(channel);
    uint64_t completionEnd = clock();
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
    timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
}

template <uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtFaaPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ int32_t* drainStatus)
{
    (void)payloadBytes;
    __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::LocalBufferAddr(channel, sendBufIdx));
    __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::RemoteBufferAddr(channel, recvBufIdx));

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init() != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
        timing[kCompletedIndex] = 0U;
        return;
    }
    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.AtomicFAA<uint64_t>(channel, &remote[0], &local[0], kFaaAdd);
    }
    drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    if (drainStatus[0] != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.AtomicFAA<uint64_t>(channel, &remote[0], &local[0], kFaaAdd);
    }
    uint64_t issueEnd = clock();
    drainStatus[0] = hcomm.Drain(channel);
    uint64_t completionEnd = clock();
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
    timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
}

template <uint32_t kLaunchBounds>
__simt_vf__ __launch_bounds__(kLaunchBounds) inline void SimtCasPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, __ubuf__ int32_t* drainStatus)
{
    (void)payloadBytes;
    __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::LocalBufferAddr(channel, sendBufIdx));
    __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(AscendC::simt::RemoteBufferAddr(channel, recvBufIdx));

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init() != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(-1);
        timing[kCompletedIndex] = 0U;
        return;
    }
    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.AtomicCAS<uint64_t>(channel, &remote[0], &local[0], kCasInitial, kCasSwap);
    }
    drainStatus[0] = warmup == 0U ? 0 : hcomm.Drain(channel);
    if (drainStatus[0] != 0) {
        timing[kIssueCyclesIndex] = 0U;
        timing[kCompletionCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
        timing[kCompletedIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.AtomicCAS<uint64_t>(channel, &remote[0], &local[0], kCasInitial, kCasSwap);
    }
    uint64_t issueEnd = clock();
    drainStatus[0] = hcomm.Drain(channel);
    uint64_t completionEnd = clock();
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus[0]);
    timing[kCompletedIndex] = drainStatus[0] == 0 ? iterations : 0U;
}

#define DEFINE_PERF_KERNEL(apiName, vfName, laneCount, launchBounds)                                            \
    extern "C" __global__ __vector__ void apiName##laneCount(                                                   \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes, \
        uint32_t iterations, uint32_t warmup)                                                                   \
    {                                                                                                           \
        __ubuf__ int32_t drainStatus[1];                                                                        \
        asc_vf_call<vfName<launchBounds>>(                                                                      \
            dim3(laneCount), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),       \
            payloadBytes, iterations, warmup, drainStatus);                                                     \
    }

#define DEFINE_LAST_COMMIT_PERF_KERNEL(apiName, api, laneCount, launchBounds)                                   \
    extern "C" __global__ __vector__ void apiName##laneCount(                                                   \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes, \
        uint32_t iterations, uint32_t warmup)                                                                   \
    {                                                                                                           \
        __ubuf__ int32_t drainStatus[1];                                                                        \
        asc_vf_call<SimtLastCommitPerfVf<api, launchBounds>>(                                                   \
            dim3(laneCount), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),       \
            payloadBytes, iterations, warmup, drainStatus);                                                     \
    }

DEFINE_PERF_KERNEL(SimtNotifyPerf, SimtNotifyPerfVf, 1U, 32U)
DEFINE_PERF_KERNEL(SimtFaaPerf, SimtFaaPerfVf, 1U, 32U)
DEFINE_PERF_KERNEL(SimtCasPerf, SimtCasPerfVf, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtNotifyLastCommitPerf, PerfApi::NOTIFY, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtFaaLastCommitPerf, PerfApi::FAA, 1U, 32U)
DEFINE_LAST_COMMIT_PERF_KERNEL(SimtCasLastCommitPerf, PerfApi::CAS, 1U, 32U)

#undef DEFINE_LAST_COMMIT_PERF_KERNEL
#undef DEFINE_PERF_KERNEL

} // namespace

void LaunchSimtNotifyPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    SimtNotifyPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
}

void LaunchSimtFaaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup)
{
    SimtFaaPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
}

void LaunchSimtCasPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup)
{
    SimtCasPerf1U<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
}

void LaunchSimtLastCommitPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t api)
{
    if (api == static_cast<uint32_t>(PerfApi::NOTIFY)) {
        SimtNotifyLastCommitPerf1U<<<1, 0, stream>>>(
            channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
    } else if (api == static_cast<uint32_t>(PerfApi::FAA)) {
        SimtFaaLastCommitPerf1U<<<1, 0, stream>>>(
            channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
    } else {
        SimtCasLastCommitPerf1U<<<1, 0, stream>>>(
            channel, sendBufIdx, recvBufIdx, timing, sizeof(uint64_t), iterations, warmup);
    }
}
