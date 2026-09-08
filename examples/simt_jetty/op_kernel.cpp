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

template <bool kPadSecondSge>
__simt_callee__ inline void SimtJettyWriteThreadImpl(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
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

    jetty.template Put<2>(peer, remoteDst, sges);
    jetty.template Drain<1000000>();
}

template <typename Coop, bool kPadSecondSge>
__simt_callee__ inline void SimtJettyWriteCooperativeImpl(
    const Coop& coop, uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
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

    jetty.template Put<2, false>(coop, peer, remoteDst, sge);
    jetty.Commit(coop);
    jetty.template Drain<1000000>(coop);
}

__simt_callee__ inline void SimtJettyWriteDispatchImpl(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    AscendC::simt::HcommCoopGroup group;
    AscendC::simt::HcommCoopWarp warp;
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::jetty::HcommPeer peer(jettyPtrTable, 0);
    AscendC::simt::jetty::HcommJetty jetty(jettyPtrTable, 0);

    AscendC::simt::jetty::HcommSge sge{};
    const int laneIdx = warp.thread_rank();
    if (laneIdx < 2) {
        sge.addr = reinterpret_cast<uint64_t>(localSrc) + static_cast<uint64_t>(laneIdx) * kSlotBytes;
        sge.len = static_cast<int>(kSlotBytes);
    }

    // Each warp reserves one WQE. The block commits the actual number of WQEs after all warps finish.
    jetty.template Write<12>(warp, peer, remoteDst, sge);
    jetty.Commit(group);
    jetty.template Drain<1000000>(group);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteThreadVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    SimtJettyWriteThreadImpl<false>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteWarpPadZeroVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopWarp, true>(
        AscendC::simt::HcommCoopWarp{}, channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteGroupVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    SimtJettyWriteCooperativeImpl<AscendC::simt::HcommCoopGroup, false>(
        AscendC::simt::HcommCoopGroup{}, channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteDispatchVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    SimtJettyWriteDispatchImpl(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

} // namespace

extern "C" __global__ __vector__ void SimtJettyWriteThread(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtJettyWriteThreadVf>(dim3(1), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtJettyWriteWarpPadZero(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtJettyWriteWarpPadZeroVf>(dim3(32), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtJettyWriteGroup(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtJettyWriteGroupVf>(dim3(32), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtJettyWriteDispatch(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtJettyWriteDispatchVf>(dim3(64), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

void LaunchSimtJetty(
    void* stream, uint64_t channel, void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode)
{
    switch (mode) {
        case 0U:
            SimtJettyWriteThread<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
            break;
        case 1U:
            SimtJettyWriteWarpPadZero<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
            break;
        case 2U:
            SimtJettyWriteGroup<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
            break;
        case 3U:
            SimtJettyWriteDispatch<<<1, 0, stream>>>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
            break;
        default:
            std::printf("[simt_jetty] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
