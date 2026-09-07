/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. Please make sure you use this file only in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software directory for the full text of the License.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"
#include <cstdio>

#include "hcomm/hcomm_simt.h"

#include "simt_jetty_common.h"

namespace {

using namespace simt_jetty;

template <bool kPadSecondSge>
__simt_callee__ inline void SimtJettyWriteRank0(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    __gm__ uint8_t* localSrc = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteDst = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    __ubuf__ AscendC::HcommPeerInfo peerInfo{};
    __ubuf__ AscendC::HcommJettyInfo jettyInfo{};
    alignas(8) __ubuf__ int counters[2];
    counters[0] = 0;
    counters[1] = 1;
    alignas(8) __ubuf__ uint8_t sqeBuf[AscendC::simt::jetty::HcommJetty::kNumMaxSQEBytes] = {};
    alignas(8) __ubuf__ uint64_t dwqeStage[32] = {};

    AscendC::simt::jetty::HcommPeer peer(&peerInfo, jettyPtrTable, 0);
    AscendC::simt::jetty::HcommJetty jetty;
    jetty.Init(&jettyInfo, &counters[0], &counters[1], sqeBuf, dwqeStage, jettyPtrTable, 0);

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
__simt_callee__ inline void SimtJettyWriteImpl(
    const Coop& coop, uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    coop.sync();
    if (coop.thread_rank() == 0) {
        SimtJettyWriteRank0<kPadSecondSge>(channel, jettyPtrTable, sendBufIdx, recvBufIdx);
    }
    coop.sync();
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteThreadVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    SimtJettyWriteImpl<AscendC::simt::HcommCoopThread, false>(
        AscendC::simt::HcommCoopThread{}, channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteWarpPadZeroVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    SimtJettyWriteImpl<AscendC::simt::HcommCoopWarp, true>(
        AscendC::simt::HcommCoopWarp{}, channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

__simt_vf__ __launch_bounds__(1024) inline void SimtJettyWriteGroupVf(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    SimtJettyWriteImpl<AscendC::simt::HcommCoopGroup, false>(
        AscendC::simt::HcommCoopGroup{}, channel, jettyPtrTable, sendBufIdx, recvBufIdx);
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
    asc_vf_call<SimtJettyWriteWarpPadZeroVf>(dim3(1), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtJettyWriteGroup(
    uint64_t channel, __gm__ void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtJettyWriteGroupVf>(dim3(1), channel, jettyPtrTable, sendBufIdx, recvBufIdx);
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
        default:
            std::printf("[simt_jetty] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
