/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. Please make sure you use this file only in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE. See
 * LICENSE in the root of the software directory for the full text of the License.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"
#include <cstdio>

#include "hcomm/hcomm_simt.h"

#include "simt_jetty_common.h"

namespace {

using namespace simt_jetty;

template <bool kPadSecondSge, bool kMeasure>
__simt_callee__ inline void SimtJettyWriteThreadImpl(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, uint64_t& issueCycles,
    uint64_t& completionCycles)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::jetty::HcommPeer peer(jettyPtrTable, 0);
    AscendC::simt::jetty::HcommJetty jetty(jettyPtrTable, 0);
    AscendC::simt::jetty::HcommSge sges[2] = {
        {reinterpret_cast<uint64_t>(localSrc), static_cast<int>(kSlotBytes)},
        {reinterpret_cast<uint64_t>(localSrc) + kSlotBytes, static_cast<int>(kSlotBytes)},
    };
    if constexpr (kPadSecondSge) {
        sges[1].addr = 0;
        sges[1].len = 0;
    }

    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    jetty.template Put<2>(peer, remoteDst, sges);
    uint64_t issueEnd = 0U;
    if constexpr (kMeasure) {
        issueEnd = clock();
    }
    jetty.template Drain<1000000>();
    if constexpr (kMeasure) {
        issueCycles += issueEnd - begin;
        completionCycles += clock() - begin;
    }
}

template <typename Coop, bool kPadSecondSge, bool kMeasure>
__simt_callee__ inline void SimtJettyWriteCooperativeImpl(
    const Coop& coop, uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx,
    uint64_t& issueCycles, uint64_t& completionCycles)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::jetty::HcommPeer peer(jettyPtrTable, 0);
    AscendC::simt::jetty::HcommJetty jetty(jettyPtrTable, 0);

    AscendC::simt::jetty::HcommSge sge{};
    const int laneIdx = coop.thread_rank();
    if (laneIdx < 2 && (!kPadSecondSge || laneIdx == 0)) {
        sge.addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(laneIdx) * kSlotBytes;
        sge.len = static_cast<int>(kSlotBytes);
    }

    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    jetty.template Put<2>(coop, peer, remoteDst, sge);
    uint64_t issueEnd = 0U;
    if constexpr (kMeasure) {
        issueEnd = clock();
    }
    jetty.template Drain<1000000>(coop);
    if constexpr (kMeasure) {
        issueCycles += issueEnd - begin;
        completionCycles += clock() - begin;
    }
}

template <bool kMeasure>
__simt_callee__ inline void SimtJettyWriteDispatchImpl(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, uint64_t& issueCycles,
    uint64_t& completionCycles)
{
    AscendC::simt::HcommCoopGroup group;
    AscendC::simt::HcommCoopWarp warp;
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::jetty::HcommPeer peer(jettyPtrTable, 0);
    AscendC::simt::jetty::HcommJetty jetty(jettyPtrTable, 0);
    group.sync();

    AscendC::simt::jetty::HcommSge sge{};
    const int laneIdx = warp.thread_rank();
    if (laneIdx < static_cast<int>(kSlotCount)) {
        sge.addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(laneIdx) * kSlotBytes;
        sge.len = static_cast<int>(kSlotBytes);
    }

    // Each warp reserves one WQE. The block commits the actual number of WQEs after all warps finish.
    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    jetty.template Write<12>(warp, peer, remoteDst, sge);
    jetty.Commit(group);
    uint64_t issueEnd = 0U;
    if constexpr (kMeasure) {
        issueEnd = clock();
    }
    jetty.template Drain<1000000>(group);
    if constexpr (kMeasure) {
        issueCycles += issueEnd - begin;
        completionCycles += clock() - begin;
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteThreadVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    uint64_t issueCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteThreadImpl<false, false>(
            channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteThreadImpl<false, true>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
        timing[0] = issueCycles;
        timing[1] = completionCycles;
        timing[2] = iterations;
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteThreadImpl<false, false>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteWarpPadZeroVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    const AscendC::simt::HcommCoopWarp warp{};
    uint64_t issueCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, true, false>(
            warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, true, true>(
                warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = issueCycles;
            timing[1] = completionCycles;
            timing[2] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, true, false>(
                warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteWarpVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    const AscendC::simt::HcommCoopWarp warp{};
    uint64_t issueCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, false, false>(
            warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, false, true>(
                warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = issueCycles;
            timing[1] = completionCycles;
            timing[2] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, false, false>(
                warp, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteGroupVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    const AscendC::simt::HcommCoopGroup group{};
    uint64_t issueCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopGroup, false, false>(
            group, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopGroup, false, true>(
                group, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
        if (group.thread_rank() == 0) {
            timing[0] = issueCycles;
            timing[1] = completionCycles;
            timing[2] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopGroup, false, false>(
                group, channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteDispatchVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    uint64_t issueCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteDispatchImpl<false>(
            channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteDispatchImpl<true>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
        if (threadIdx.x == 0U) {
            timing[0] = issueCycles;
            timing[1] = completionCycles;
            timing[2] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteDispatchImpl<false>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, issueCycles, completionCycles);
        }
    }
}

} // namespace

extern "C" __global__ __vector__ void SimtJettyWriteThread(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtJettyWriteThreadVf>(
        dim3(1), channel, jettyPtrTable, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), iterations,
        warmup);
}

extern "C" __global__ __vector__ void SimtJettyWriteWarpPadZero(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtJettyWriteWarpPadZeroVf>(
        dim3(32), channel, jettyPtrTable, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __vector__ void SimtJettyWriteWarp(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtJettyWriteWarpVf>(
        dim3(32), channel, jettyPtrTable, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), iterations,
        warmup);
}

extern "C" __global__ __vector__ void SimtJettyWriteGroup(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtJettyWriteGroupVf>(
        dim3(32), channel, jettyPtrTable, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), iterations,
        warmup);
}

extern "C" __global__ __vector__ void SimtJettyWriteDispatch(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtJettyWriteDispatchVf>(
        dim3(64), channel, jettyPtrTable, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), iterations,
        warmup);
}

void LaunchSimtJetty(
    void* stream, uint64_t channel, void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode,
    void* timing, uint32_t iterations, uint32_t warmup)
{
    switch (mode) {
        case 0U:
            SimtJettyWriteThread<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        case 1U:
            SimtJettyWriteWarpPadZero<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        case 2U:
            SimtJettyWriteGroup<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations,
                                                  warmup);
            break;
        case 3U:
            SimtJettyWriteDispatch<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        case 4U:
            SimtJettyWriteWarp<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        default:
            std::printf("[simt_jetty] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
