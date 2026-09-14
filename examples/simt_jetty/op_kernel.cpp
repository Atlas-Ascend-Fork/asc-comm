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
#include "hcomm/hcomm_jetty.h"

#include "simt_jetty_common.h"

namespace {

using namespace simt_jetty;

struct alignas(16) SimtJettyUbLayout {
    AscendC::simt::jetty::HcommJettyInfo jetty_info;
    AscendC::simt::jetty::HcommPeerInfo peer_info;
    alignas(16) uint64_t dwqe_stage[2 * AscendC::simt::jetty::HcommJetty::kNumWQEBBLanes] = {};
    uint32_t mixed_ready = 0U;
};

static_assert(sizeof(AscendC::simt::jetty::HcommJettyInfo) == sizeof(AscendC::HcommJettyInfo),
              "SIMT and SIMD Jetty metadata must share an ABI");
static_assert(offsetof(AscendC::simt::jetty::HcommJettyInfo, packedHead) ==
                  offsetof(AscendC::HcommJettyInfo, packedHead),
              "SIMT/SIMD packed head offset mismatch");
static_assert(sizeof(AscendC::simt::jetty::HcommPeerInfo) == sizeof(AscendC::HcommJettyPeerInfo),
              "SIMT and SIMD peer metadata must share an ABI");

__simt_callee__ __forceinline__ __ubuf__ SimtJettyUbLayout* GetSimtJettyUbLayout()
{
    return reinterpret_cast<__ubuf__ SimtJettyUbLayout*>(0);
}

template <typename Coop>
__simt_callee__ __forceinline__ void PrepareJetty(
    const Coop& coop, uint64_t channel, __gm__ void* jettyPtrTable, __ubuf__ SimtJettyUbLayout* ub_layout,
    AscendC::simt::jetty::HcommPeer& peer, AscendC::simt::jetty::HcommJetty& jetty)
{
    if (coop.thread_rank() == 0) {
        AscendC::simt::jetty::HcommPeer peer_init(&ub_layout->peer_info, jettyPtrTable, 0);
        AscendC::simt::jetty::HcommJetty jetty_init(
            &ub_layout->jetty_info, reinterpret_cast<__ubuf__ uint8_t*>(ub_layout->dwqe_stage), jettyPtrTable, 0);
    }
    coop.sync();
    peer = AscendC::simt::jetty::HcommPeer(&ub_layout->peer_info, channel);
    jetty = AscendC::simt::jetty::HcommJetty(
        &ub_layout->jetty_info, reinterpret_cast<__ubuf__ uint8_t*>(ub_layout->dwqe_stage), channel);
}

template <bool kMeasure, bool kStandaloneWrite = false>
__simt_callee__ inline void SimtJettyWriteThreadImpl(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, const AscendC::simt::jetty::HcommPeer& peer,
    AscendC::simt::jetty::HcommJetty& jetty, uint64_t& writeCycles, uint64_t& commitCycles,
    uint64_t& completionCycles)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::jetty::JettySge sges[kConfiguredSgeNum] = {};
#pragma unroll
    for (uint32_t sge_idx = 0U; sge_idx < kConfiguredSgeNum; ++sge_idx) {
        sges[sge_idx].addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(sge_idx) * kSlotBytes;
        sges[sge_idx].len = static_cast<int>(kSlotBytes);
    }

    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    if constexpr (kStandaloneWrite) {
        jetty.template Write<kConfiguredSgeNum, false>(peer, remoteDst, sges);
    } else {
        jetty.template Write<kConfiguredSgeNum>(peer, remoteDst, sges);
    }
    uint64_t writeEnd = 0U;
    if constexpr (kMeasure) {
        writeEnd = clock();
    }
    uint64_t commitEnd = writeEnd;
    if constexpr (kStandaloneWrite) {
        jetty.AdvanceSq();
        jetty.PublishSq();
        if constexpr (kMeasure) {
            commitEnd = clock();
        }
    }
    jetty.template Drain<1000000>();
    if constexpr (kMeasure) {
        writeCycles += writeEnd - begin;
        commitCycles += commitEnd - writeEnd;
        completionCycles += clock() - begin;
    }
}

template <uint32_t kSgeNum, typename Coop, bool kMeasure, bool kStandaloneWrite = false>
__simt_callee__ inline void SimtJettyWriteCooperativeImpl(
    const Coop& coop, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    const AscendC::simt::jetty::HcommPeer& peer, AscendC::simt::jetty::HcommJetty& jetty, uint64_t& writeCycles,
    uint64_t& commitCycles, uint64_t& completionCycles)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::jetty::JettySge sge{};
    const int laneIdx = coop.thread_rank();
    if (laneIdx < static_cast<int>(kSgeNum)) {
        sge.addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(laneIdx) * kSlotBytes;
        sge.len = static_cast<int>(kSlotBytes);
    }

    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    if constexpr (kStandaloneWrite) {
        jetty.template Write<kSgeNum, false>(peer, remoteDst, &sge, coop);
    } else {
        jetty.template Write<kSgeNum>(peer, remoteDst, &sge, coop);
    }
    uint64_t writeEnd = 0U;
    if constexpr (kMeasure) {
        writeEnd = clock();
    }
    uint64_t commitEnd = writeEnd;
    if constexpr (kStandaloneWrite) {
        jetty.AdvanceSq(coop);
        jetty.PublishSq(coop);
        if constexpr (kMeasure) {
            commitEnd = clock();
        }
    }
    jetty.template Drain<1000000>(coop);
    if constexpr (kMeasure) {
        writeCycles += writeEnd - begin;
        commitCycles += commitEnd - writeEnd;
        completionCycles += clock() - begin;
    }
}

template <uint32_t kSgeNum, bool kMeasure>
__simt_callee__ inline void SimtJettyWriteDispatchWarpImpl(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, const AscendC::simt::HcommCoopWarp& warp,
    const AscendC::simt::jetty::HcommPeer& peer, AscendC::simt::jetty::HcommJetty& jetty, uint64_t& writeCycles)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::jetty::JettySge laneSge{};
    const int laneIdx = warp.thread_rank();
    if (laneIdx < static_cast<int>(kSgeNum)) {
        laneSge.addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(laneIdx) * kSlotBytes;
        laneSge.len = static_cast<int>(kSlotBytes);
    }
    uint64_t begin = 0U;
    if constexpr (kMeasure) {
        begin = clock();
    }
    jetty.template Write<kSgeNum, false>(peer, remoteDst, &laneSge, warp);
    if constexpr (kMeasure) {
        writeCycles += clock() - begin;
    }
}

template <bool kMeasure, bool kPublish = true, bool kDrain = true>
__simt_callee__ inline void SimtJettyWriteDispatchImpl(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, const AscendC::simt::jetty::HcommPeer& peer,
    AscendC::simt::jetty::HcommJetty& jetty, uint64_t& writeCycles, uint64_t& commitCycles,
    uint64_t& completionCycles)
{
    AscendC::simt::HcommCoopGroup group;
    AscendC::simt::HcommCoopWarp warp;
    group.sync();
    const uint64_t begin = kMeasure ? clock() : 0U;
    SimtJettyWriteDispatchWarpImpl<kSlotCount, kMeasure>(
        channel, sendBufIdx, recvBufIdx, warp, peer, jetty, writeCycles);
    if constexpr (kPublish) {
        const uint64_t commitBegin = kMeasure ? clock() : 0U;
        jetty.AdvanceSq(group);
        jetty.PublishSq(group);
        if constexpr (kMeasure) {
            commitCycles += clock() - commitBegin;
        }
    }
    if constexpr (kDrain) {
        jetty.template Drain<1000000>(group);
    }
    if constexpr (kMeasure) {
        completionCycles += clock() - begin;
    }
}

template <bool kMeasure>
__simt_callee__ inline void SimtJettyWriteThreadVfImpl(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    auto* ub_layout = reinterpret_cast<__ubuf__ SimtJettyUbLayout*>(0);
    AscendC::simt::HcommCoopThread thread;
    AscendC::simt::jetty::HcommPeer peer;
    AscendC::simt::jetty::HcommJetty jetty;
    PrepareJetty(thread, channel, jettyPtrTable, ub_layout, peer, jetty);
    uint64_t writeCycles = 0U;
    uint64_t commitCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteThreadImpl<false, true>(
            channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteThreadImpl<kMeasure, true>(
                channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
        timing[0] = writeCycles;
        timing[1] = commitCycles;
        timing[2] = completionCycles;
        timing[3] = iterations;
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteThreadImpl<false, true>(
                channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteThreadVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    SimtJettyWriteThreadVfImpl<true>(
        channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteWarpVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    const AscendC::simt::HcommCoopWarp warp{};
    auto* ub_layout = GetSimtJettyUbLayout();
    AscendC::simt::jetty::HcommPeer peer;
    AscendC::simt::jetty::HcommJetty jetty;
    PrepareJetty(warp, channel, jettyPtrTable, ub_layout, peer, jetty);
    uint64_t writeCycles = 0U;
    uint64_t commitCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopWarp, false, true>(
            warp, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopWarp, true, true>(
                warp, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = writeCycles;
            timing[1] = commitCycles;
            timing[2] = completionCycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopWarp, false, true>(
                warp, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteGroupVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    const AscendC::simt::HcommCoopGroup group{};
    auto* ub_layout = GetSimtJettyUbLayout();
    AscendC::simt::jetty::HcommPeer peer;
    AscendC::simt::jetty::HcommJetty jetty;
    PrepareJetty(group, channel, jettyPtrTable, ub_layout, peer, jetty);
    uint64_t writeCycles = 0U;
    uint64_t commitCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopGroup, false, true>(
            group, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopGroup, true, true>(
                group, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
        if (group.thread_rank() == 0) {
            timing[0] = writeCycles;
            timing[1] = commitCycles;
            timing[2] = completionCycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteCooperativeImpl<kConfiguredSgeNum, AscendC::simt::HcommCoopGroup, false, true>(
                group, channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteDispatchVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing,
    uint32_t iterations, uint32_t warmup)
{
    auto* ub_layout = reinterpret_cast<__ubuf__ SimtJettyUbLayout*>(0);
    const AscendC::simt::HcommCoopGroup group{};
    AscendC::simt::jetty::HcommPeer peer;
    AscendC::simt::jetty::HcommJetty jetty;
    PrepareJetty(group, channel, jettyPtrTable, ub_layout, peer, jetty);
    uint64_t writeCycles = 0U;
    uint64_t commitCycles = 0U;
    uint64_t completionCycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SimtJettyWriteDispatchImpl<false>(
            channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteDispatchImpl<true>(
                channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
        if (threadIdx.x == 0U) {
            timing[0] = writeCycles;
            timing[1] = commitCycles;
            timing[2] = completionCycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            SimtJettyWriteDispatchImpl<false>(
                channel, sendBufIdx, recvBufIdx, peer, jetty, writeCycles, commitCycles, completionCycles);
        }
    }
}

__simt_vf__ __aicore__ __launch_bounds__(64) inline void SimtJettyWriteMixedWorker(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    auto* ub_layout = GetSimtJettyUbLayout();
    AscendC::simt::HcommCoopGroup group;
    AscendC::simt::HcommCoopWarp warp;
    AscendC::simt::jetty::HcommPeer peer;
    AscendC::simt::jetty::HcommJetty jetty;
    PrepareJetty(group, channel, jettyPtrTable, ub_layout, peer, jetty);
    uint64_t writeCycles = 0;
    uint64_t commitCycles = 0;
    uint64_t completionCycles = 0;
    group.sync();
    SimtJettyWriteDispatchWarpImpl<kConfiguredSgeNum, false>(
        channel, sendBufIdx, recvBufIdx, warp, peer, jetty, writeCycles);
    jetty.AdvanceSq(group);
    if (group.thread_rank() == 0) {
        ub_layout->mixed_ready = 1U;
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

extern "C" __global__ __mix__(1, 1) void SimtJettyWriteMixed(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    AscendC::InitSocState();
    if ASCEND_IS_AIC {
        return;
    }

    auto* ub_layout = reinterpret_cast<__ubuf__ SimtJettyUbLayout*>(0);
    ub_layout->mixed_ready = 0U;
    asc_sync_notify(PIPE_S, PIPE_V, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_V, EVENT_ID0);
    AscendC::Simt::VF_CALL<SimtJettyWriteMixedWorker>(
        AscendC::Simt::Dim3(64, 1, 1), channel, jettyPtrTable, sendBufIdx, recvBufIdx);

    while (*reinterpret_cast<volatile __ubuf__ uint32_t*>(&ub_layout->mixed_ready) != 1U) {
        asm volatile("nop");
    }

    AscendC::HcommJetty simd_jetty(
        reinterpret_cast<__ubuf__ AscendC::HcommJettyInfo*>(&ub_layout->jetty_info),
        nullptr, jettyPtrTable, 0);
    simd_jetty.PublishSq();
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
            SimtJettyWriteGroup<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations,
                                                  warmup);
            break;
        case 2U:
            SimtJettyWriteDispatch<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        case 3U:
            SimtJettyWriteWarp<<<1, 0, stream>>>(
                channel, jettyPtrTable, sendBufIdx, recvBufIdx, timing, iterations, warmup);
            break;
        case 4U:
            SimtJettyWriteMixed<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
            break;
        default:
            std::printf("[simt_jetty] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
