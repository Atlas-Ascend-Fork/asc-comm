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

// 同步分两层：mode 0 汇合本卡所有 AIV，mode 2 连接同 blockIdx 的 AIV/AIC。
// ready[0/1] 表示通信数据就绪，free[0/1] 表示计算已消费该轮通知。
// flag ID 固定复用，不能直接用 round 作 ID；也不能与 SyncAll 的保留标志混用。
constexpr uint16_t AIV_BARRIER = 0;
constexpr uint16_t READY_BASE = 2;
constexpr uint16_t FREE_BASE = 4;

__aicore__ inline uint64_t DivUp(uint64_t n, uint64_t d) { return (n + d - 1) / d; }
__aicore__ inline uint64_t Smaller(uint64_t a, uint64_t b) { return a < b ? a : b; }

// 仅供诊断：记录当前阶段边界，只由 AIV0/AIC0 写各自区域。
// 先同步流水，再取本设备系统 cycle，并将标量写入刷出 cache；两类核的区域分开对齐。
// 区间包含阶段内等待，不等同于网络忙碌时间，也不能直接跨设备比较绝对 cycle。
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

// 拉取一轮 M 分块：从所有远端 A_peer[row:row+rows, :] 写入本卡 gathered[peer, row:row+rows, :]。
// Get 的 offset/length 单位均为字节，FP16/BF16 每元素占 2 字节；尾轮按实际 rows 搬运。
__aicore__ inline void GatherRound(AscendC::Ain<>& ain, AscendC::AinDescriptorUbuf& ub,
                                  GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin,
                                  const Tiling& t, uint32_t rank, uint32_t round)
{
    const uint64_t row = static_cast<uint64_t>(round) * t.tileM;
    const uint64_t rows = Smaller(t.tileM, t.m - row);
    const uint64_t srcOffset = row * t.k * 2;
    // i 是剔除自身后的 peer 序号，例如 rank=1 时，i=0/1/2 对应 peer=0/2/3。
    // 按 blockIdx 跨步分配，确保同一 peer channel 只有一个 AIV 提交和等待。
    for (uint32_t i = AscendC::GetBlockIdx(); i < t.ranks - 1; i += t.cores) {
        const uint32_t peer = i < rank ? i : i + 1;
        const uint64_t dstOffset = (static_cast<uint64_t>(peer) * t.m + row) * t.k * 2;
        ain.Get(team, peer, dstWin, dstOffset, srcWin, srcOffset, rows * t.k * 2, ub);
    }
    // 先提交本核所有 peer 的 Get，再逐 channel 等待，避免每提交一个 peer 就立刻串行等待。
    // FlushAsync 在此取得 channel，真正的完成等待由 Wait 承担；返回后本核负责的数据才可使用。
    for (uint32_t i = AscendC::GetBlockIdx(); i < t.ranks - 1; i += t.cores) {
        const uint32_t peer = i < rank ? i : i + 1;
        AscendC::ChannelHandle channel;
        ain.FlushAsync(team, peer, &channel);
        ain.Wait(channel);
    }
}

// AIV 侧通信流水：Fused=true 时与 AIC 交换 ready/free；独立通信 kernel 不执行配对通知。
template <bool Fused, bool Trace = false>
__aicore__ inline void Gather(GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin, const Tiling& t, uint32_t rank,
                              GM_ADDR trace = nullptr)
{
    // 每个 AIV 独占一块 UB，供 AIN 构造通信描述符；实际矩阵数据位于对称 GM window。
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> buffer;
    pipe.InitBuffer(buffer, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    auto local = buffer.Get<uint8_t>();
    AscendC::AinDescriptorUbuf ub{reinterpret_cast<__ubuf__ uint8_t*>(local.GetPhyAddr()),
                                 AscendC::HCOMM_URMA_TMP_BUF_SIZE, 0};
    AscendC::Ain<> ain(0);
    for (uint32_t round = 0; round < t.rounds; ++round) {
        if constexpr (Fused) {
            // 前两轮可直接使用两个通知槽；第 r 轮复用槽前，消费第 r-2 轮的 free。
            // 双槽限制通知的超前量，不是 GM 双缓冲：各轮数据写入不同 GM 行，不会覆盖。
            if (round >= 2) {
                AscendC::CrossCoreWaitFlag<2, PIPE_S>(FREE_BASE + (round & 1));
            }
        }
        if constexpr (Trace) { RecordCycle(trace, static_cast<uint64_t>(round) * 2); }
        GatherRound(ain, ub, team, srcWin, dstWin, t, rank, round);
        if constexpr (Fused) {
            // 先等本卡所有 AIV 的 Get 完成，再通知 AIC；AIC 会跨所有 peer 分配计算任务。
            // 即使本 AIV 没分到 peer，也必须参加 barrier 并通知配对 AIC。
            AscendC::CrossCoreSetFlag<0, PIPE_MTE3>(AIV_BARRIER);
            AscendC::CrossCoreWaitFlag<0, PIPE_S>(AIV_BARRIER);
            if constexpr (Trace) { RecordCycle(trace, static_cast<uint64_t>(round) * 2 + 1); }
            AscendC::CrossCoreSetFlag<2, PIPE_MTE3>(READY_BASE + (round & 1));
        }
    }
    if constexpr (Fused) {
        // 循环中只消费了复用槽时的 free；退出前排空最后一到两轮的确认，避免遗留通知。
        const uint32_t begin = t.rounds > 2 ? t.rounds - 2 : 0;
        for (uint32_t round = begin; round < t.rounds; ++round) {
            AscendC::CrossCoreWaitFlag<2, PIPE_S>(FREE_BASE + (round & 1));
        }
    }
}

// 封装单个 AIC 的 Blaze 块矩阵乘；外层负责 peer/轮次/核分工，Blaze 负责块内 K 循环和搬运。
template <typename T>
class Compute {
    using Layout = asc::te::nd_ext_layout_ptn;
    using Mmad = Blaze::Gemm::Block::BlockMmad<Blaze::Gemm::MatmulMultiBlockBasic<>,
                                             T, Layout, T, Layout, T, Layout, T, Layout>;
    using MakeLayout = asc::te::frame_layout_format<Layout>;
    Mmad mm_;
public:
    // 设置 Cube tile：M/N=256，K 在 L1/L0 分别为 128/64，L1 使用两级缓冲。
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

    // 计算 [row, row+rows) 行范围。local=true 只读本地 A，否则读所有远端 peer 的接收区。
    // B 始终是当前 rank 的 B；C 直接写入源 rank 对应的行段，后续无需重排。
    __aicore__ inline void Run(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c,
                               const Tiling& t, uint32_t rank, uint64_t row, uint64_t rows, bool local)
    {
        const uint64_t mTiles = DivUp(rows, CUBE_M);
        const uint64_t nTiles = DivUp(t.n, CUBE_N);
        const uint64_t tilesPerPeer = mTiles * nTiles;
        const uint64_t tiles = tilesPerPeer * (local ? 1 : t.ranks - 1);
        auto gmB = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(
            reinterpret_cast<__gm__ T*>(b)), MakeLayout{}(static_cast<int64_t>(t.k), static_cast<int64_t>(t.n)));
        // 将 peer × M tile × N tile 展平成任务序号，按核号跨步领取，避免按 peer 固定绑定计算核。
        for (uint64_t task = AscendC::GetBlockIdx(); task < tiles; task += t.cores) {
            const uint32_t index = static_cast<uint32_t>(task / tilesPerPeer);
            const uint32_t peer = local ? rank : (index < rank ? index : index + 1);
            const uint64_t tile = task % tilesPerPeer;
            const uint64_t m = (tile / nTiles) * CUBE_M;
            const uint64_t n = (tile % nTiles) * CUBE_N;
            const int64_t actualM = static_cast<int64_t>(Smaller(CUBE_M, rows - m));
            const int64_t actualN = static_cast<int64_t>(Smaller(CUBE_N, t.n - n));
            // gathered 看作 [P*M,K]，C 看作 [P*M,N]；peer*M 定位源 rank，row+m 定位块内行。
            const uint64_t globalRow = static_cast<uint64_t>(peer) * t.m + row + m;
            auto aPtr = reinterpret_cast<__gm__ T*>(local ? a : gathered) +
                        (local ? row + m : globalRow) * t.k;
            auto cPtr = reinterpret_cast<__gm__ T*>(c) + globalRow * t.n;
            auto gmA = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(aPtr),
                MakeLayout{}(actualM, static_cast<int64_t>(t.k)));
            // window 跨调用复用，接收地址可能曾被缓存；关闭 A 的 L2 hint，配合 ready 等待读取新数据。
            gmA.set_l2_cache_hint(asc::te::cache_mode::disable);
            auto gmC = asc::te::make_tensor(asc::te::make_mem_ptr<asc::te::location::gm>(cPtr),
                MakeLayout{}(actualM, static_cast<int64_t>(t.n)));
            auto blockB = gmB.slice(asc::te::make_coord(0L, static_cast<int64_t>(n)),
                asc::te::make_shape(static_cast<int64_t>(t.k), actualN));
            auto blockC = gmC.slice(asc::te::make_coord(0L, static_cast<int64_t>(n)),
                asc::te::make_shape(actualM, actualN));
            // 本策略关闭 bias；接口仍需传入合法 Tensor 占位，不传空 Tensor 指针。
            auto bias = blockC;
            typename Mmad::TupleShape shape{actualM, actualN, static_cast<int64_t>(t.k), 1L};
            mm_(gmA, blockB, bias, blockC, shape);
        }
    }
};

// AIC 侧计算流水：先算不依赖通信的本地 A，再逐轮消费远端块，与下一轮 Get 并行。
template <typename T, bool Fused, bool Trace = false>
__aicore__ inline void Matmul(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c,
                             const Tiling& t, uint32_t rank, GM_ADDR trace = nullptr)
{
    Compute<T> compute;
    compute.Init();
    // 前 2*rounds 个 uint64 保存 Get 起止时间；AIC 区域按 32 个 uint64 对齐，避免与 AIV 共用脏行。
    const uint64_t traceBase = DivUp(static_cast<uint64_t>(t.rounds) * 2, 32) * 32;
    if constexpr (Trace) { RecordCycle(trace, traceBase); }
    compute.Run(a, gathered, b, c, t, rank, 0, t.m, true);
    if constexpr (Trace) { RecordCycle(trace, traceBase + 1); }
    for (uint32_t round = 0; round < t.rounds; ++round) {
        if constexpr (Fused) {
            // 诊断版阻塞标量流水，使后续时间戳不包含 ready 等待；普通版在 MTE2 上阻止提前加载 A。
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
            // 在 FIX 流水上归还槽，保持与结果写回的顺序；无计算任务的核也要归还，避免 AIV 死等。
            AscendC::CrossCoreSetFlag<2, PIPE_FIX>(FREE_BASE + (round & 1));
        }
    }
    AscendC::PipeBarrier<PIPE_ALL>();
}

// 融合入口：同一 kernel 启动配对 AIC/AIV，分别进入计算/通信分支；调度属性配合跨核同步。
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

// 串行基线的通信入口：只启动 AIV；Host 在同一 stream 后续排入 MatmulKernel。
__global__ __aicore__ void GatherKernel(GM_ADDR team, GM_ADDR srcWin, GM_ADDR dstWin, Tiling tiling, uint32_t rank)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIV_ONLY);
    AscendC::InitSocState();
    Gather<false>(team, srcWin, dstWin, tiling, rank);
}

// 独立计算入口：远端数据已由通信 kernel、HCCL 或 Host 填好，因此不等待 ready/free。
template <typename T>
__global__ __aicore__ void MatmulKernel(GM_ADDR a, GM_ADDR gathered, GM_ADDR b, GM_ADDR c, Tiling tiling, uint32_t rank)
{
    KERNEL_TASK_TYPE_DEFAULT(KERNEL_TYPE_AIC_ONLY);
    AscendC::InitSocState();
    Matmul<T, false>(a, gathered, b, c, tiling, rank);
}

} // namespace ain_agmm
