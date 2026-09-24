/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository.
 */
#pragma once

#include "kernel_operator.h"
#include "ain/ain.h"
#include "blaze/gemm/block/block_mmad_matmul_basic.h"
#include "blaze/gemm/policy/dispatch_policy.h"
#include "tensor_api/tensor.h"
#include "tiling.h"

namespace ain_agmm {

// Mode 0 is used only for the AIV completion barrier. Mode 2 flags are distinct
// from that barrier and from SyncAll's reserved flags. Never use round as flagId.
constexpr uint16_t AIV_BARRIER = 0;
constexpr uint16_t READY_BASE = 2;
constexpr uint16_t FREE_BASE = 4;

__aicore__ inline uint64_t DivUp(uint64_t n, uint64_t d) { return (n + d - 1) / d; }
__aicore__ inline uint64_t Smaller(uint64_t a, uint64_t b) { return a < b ? a : b; }

// Diagnostic variant only. Separate cache-line-aligned AIV/AIC regions prevent
// their scalar stores from sharing a dirty cache line. Timestamps are same-device
// system cycles; a phase interval is not a measurement of network busy cycles.
__aicore__ inline void RecordCycle(GM_ADDR trace, uint64_t index)
{
    if (AscendC::GetBlockIdx() == 0) {
        AscendC::PipeBarrier<PIPE_ALL>();
        const uint64_t cycle = static_cast<uint64_t>(AscendC::GetSystemCycle());
        AscendC::GlobalTensor<uint64_t> ticks;
        ticks.SetGlobalBuffer(reinterpret_cast<__gm__ uint64_t*>(trace));
        ticks.SetValue(index, cycle);
        AscendC::DataCacheCleanAndInvalid<uint64_t, AscendC::CacheLine::SINGLE_CACHE_LINE,
                                         AscendC::DcciDst::CACHELINE_OUT>(ticks[index]);
    }
}

__aicore__ inline void GatherRound(AscendC::Ain<>& ain, AscendC::AinDescriptorUbuf& ub,
                                  GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin,
                                  const Tiling& t, uint32_t rank, uint32_t round)
{
    const uint64_t row = static_cast<uint64_t>(round) * t.tileM;
    const uint64_t rows = Smaller(t.tileM, t.m - row);
    const uint64_t srcOffset = row * t.k * 2;
    // Ascending logical peer index skips self. Each channel has exactly one owner.
    for (uint32_t i = AscendC::GetBlockIdx(); i < t.ranks - 1; i += t.cores) {
        const uint32_t peer = i < rank ? i : i + 1;
        const uint64_t dstOffset = (static_cast<uint64_t>(peer) * t.m + row) * t.k * 2;
        ain.Get(team, peer, dstWin, dstOffset, srcWin, srcOffset, rows * t.k * 2, ub);
    }
    for (uint32_t i = AscendC::GetBlockIdx(); i < t.ranks - 1; i += t.cores) {
        const uint32_t peer = i < rank ? i : i + 1;
        AscendC::ChannelHandle channel;
        ain.FlushAsync(team, peer, &channel);
        ain.Wait(channel);
    }
}

template <bool Fused, bool Trace = false>
__aicore__ inline void Gather(GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin, const Tiling& t, uint32_t rank,
                              GM_ADDR trace = nullptr)
{
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> buffer;
    pipe.InitBuffer(buffer, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    auto local = buffer.Get<uint8_t>();
    AscendC::AinDescriptorUbuf ub{reinterpret_cast<__ubuf__ uint8_t*>(local.GetPhyAddr()),
                                 AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0};
    AscendC::Ain<> ain(0);
    for (uint32_t round = 0; round < t.rounds; ++round) {
        if constexpr (Fused) {
            // First two slots are initially available. Later reuses consume one
            // acknowledgement; the GM data itself is never overwritten in-kernel.
            if (round >= 2) {
                AscendC::CrossCoreWaitFlag<2, PIPE_S>(FREE_BASE + (round & 1));
            }
        }
        if constexpr (Trace) { RecordCycle(trace, static_cast<uint64_t>(round) * 2); }
        GatherRound(ain, ub, team, srcWin, dstWin, t, rank, round);
        if constexpr (Fused) {
            // All AIVs participate, including those without an assigned peer.
            AscendC::CrossCoreSetFlag<0, PIPE_MTE3>(AIV_BARRIER);
            AscendC::CrossCoreWaitFlag<0, PIPE_S>(AIV_BARRIER);
            if constexpr (Trace) { RecordCycle(trace, static_cast<uint64_t>(round) * 2 + 1); }
            AscendC::CrossCoreSetFlag<2, PIPE_MTE3>(READY_BASE + (round & 1));
        }
    }
    if constexpr (Fused) {
        // Drain the final one/two acknowledgements: leave no live flag tokens.
        const uint32_t begin = t.rounds > 2 ? t.rounds - 2 : 0;
        for (uint32_t round = begin; round < t.rounds; ++round) {
            AscendC::CrossCoreWaitFlag<2, PIPE_S>(FREE_BASE + (round & 1));
        }
    }
}

template <typename T>
class Compute {
    using Layout = asc::te::nd_ext_layout_ptn;
    using Mmad = Blaze::Gemm::Block::BlockMmad<Blaze::Gemm::MatmulMultiBlockBasic<>,
                                             T, Layout, T, Layout, T, Layout, T, Layout>;
    using MakeLayout = asc::te::frame_layout_format<Layout>;
    Mmad mm_;
public:
    __aicore__ inline void Init()
    {
        typename Mmad::Params p;
        p.mL1 = CUBE_M;
        p.nL1 = CUBE_N;
        p.kL1 = CUBE_K * 2;
        p.mL0 = CUBE_M;
        p.nL0 = CUBE_N;
        p.kL0 = CUBE_K;
        p.l1Stages = 2;
        p.l0cStages = 1;
        mm_.Init(p);
    }

    __aicore__ inline void Run(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c,
                               const Tiling& t, uint32_t rank, uint64_t row, uint64_t rows, bool local)
    {
        const uint64_t mTiles = DivUp(rows, CUBE_M);
        const uint64_t nTiles = DivUp(t.n, CUBE_N);
        const uint64_t tilesPerPeer = mTiles * nTiles;
        const uint64_t tiles = tilesPerPeer * (local ? 1 : t.ranks - 1);
        auto gmB = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(
            reinterpret_cast<__gm__ T*>(b)), MakeLayout{}(static_cast<int64_t>(t.k), static_cast<int64_t>(t.n)));
        for (uint64_t task = AscendC::GetBlockIdx(); task < tiles; task += t.cores) {
            const uint32_t index = static_cast<uint32_t>(task / tilesPerPeer);
            const uint32_t peer = local ? rank : (index < rank ? index : index + 1);
            const uint64_t tile = task % tilesPerPeer;
            const uint64_t m = (tile / nTiles) * CUBE_M;
            const uint64_t n = (tile % nTiles) * CUBE_N;
            const int64_t actualM = static_cast<int64_t>(Smaller(CUBE_M, rows - m));
            const int64_t actualN = static_cast<int64_t>(Smaller(CUBE_N, t.n - n));
            const uint64_t globalRow = static_cast<uint64_t>(peer) * t.m + row + m;
            auto aPtr = reinterpret_cast<__gm__ T*>(local ? a : gathered) +
                        (local ? row + m : globalRow) * t.k;
            auto cPtr = reinterpret_cast<__gm__ T*>(c) + globalRow * t.n;
            auto gmA = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(aPtr),
                MakeLayout{}(actualM, static_cast<int64_t>(t.k)));
            // Received GM may have been cached during a previous kernel invocation.
            gmA.set_l2_cache_hint(asc::te::cache_mode::disable);
            auto gmC = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(cPtr),
                MakeLayout{}(actualM, static_cast<int64_t>(t.n)));
            auto blockB = gmB.slice(asc::te::make_coord(0L, static_cast<int64_t>(n)),
                asc::te::make_shape(static_cast<int64_t>(t.k), actualN));
            auto blockC = gmC.slice(asc::te::make_coord(0L, static_cast<int64_t>(n)),
                asc::te::make_shape(actualM, actualN));
            // Bias is disabled. Supply a valid unused tensor, never a null Tensor pointer.
            auto bias = blockC;
            typename Mmad::TupleShape shape{actualM, actualN, static_cast<int64_t>(t.k), 1L};
            mm_(gmA, blockB, bias, blockC, shape);
        }
    }
};

template <typename T, bool Fused, bool Trace = false>
__aicore__ inline void Matmul(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c,
                             const Tiling& t, uint32_t rank, GM_ADDR trace = nullptr)
{
    Compute<T> compute;
    compute.Init();
    const uint64_t traceBase = DivUp(static_cast<uint64_t>(t.rounds) * 2, 32) * 32;
    if constexpr (Trace) { RecordCycle(trace, traceBase); }
    compute.Run(a, gathered, b, c, t, rank, 0, t.m, true);
    if constexpr (Trace) { RecordCycle(trace, traceBase + 1); }
    for (uint32_t round = 0; round < t.rounds; ++round) {
        if constexpr (Fused) {
            if constexpr (Trace) {
                AscendC::CrossCoreWaitFlag<2, PIPE_S>(READY_BASE + (round & 1));
            } else {
                AscendC::CrossCoreWaitFlag<2, PIPE_MTE2>(READY_BASE + (round & 1));
            }
        }
        if constexpr (Trace) { RecordCycle(trace, traceBase + 2 + static_cast<uint64_t>(round) * 2); }
        const uint64_t row = static_cast<uint64_t>(round) * t.tileM;
        compute.Run(a, gathered, b, c, t, rank, row, Smaller(t.tileM, t.m - row), false);
        if constexpr (Trace) { RecordCycle(trace, traceBase + 3 + static_cast<uint64_t>(round) * 2); }
        if constexpr (Fused) {
            AscendC::CrossCoreSetFlag<2, PIPE_FIX>(FREE_BASE + (round & 1));
        }
    }
    AscendC::PipeBarrier<PIPE_ALL>();
}

template <typename T, bool Trace = false>
__global__ __aicore__ __schedmode__(1) void FusedKernel(
    GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin, GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c,
    Tiling tiling, uint32_t rank, GM_ADDR trace)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_MIX_AIC_1_1);
    AscendC::InitSocState();
    if ASCEND_IS_AIV { Gather<true, Trace>(team, srcWin, dstWin, tiling, rank, trace); }
    if ASCEND_IS_AIC { Matmul<T, true, Trace>(a, gathered, b, c, tiling, rank, trace); }
}

__global__ __aicore__ void GatherKernel(GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin, Tiling tiling, uint32_t rank)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    AscendC::InitSocState();
    Gather<false>(team, srcWin, dstWin, tiling, rank);
}

template <typename T>
__global__ __aicore__ void MatmulKernel(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c, Tiling tiling, uint32_t rank)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIC_ONLY);
    AscendC::InitSocState();
    Matmul<T, false>(a, gathered, b, c, tiling, rank);
}

} // namespace ain_agmm
