/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "asccomm/asccomm_simt.h"
#include "jetty/asccomm_jetty.h"
#include "jetty/asccomm_jetty_simt.h"

#include "simt_jetty_common.h"

namespace {

using namespace simt_jetty;

struct alignas(16) simt_jetty_ub_layout {
    asc::asccomm_jetty_info jetty_info;
    asc::simt::asccomm_peer_info peer_info;
    alignas(16) uint64_t dwqe_stage[2 * asc::simt::asccomm_jetty::num_wqebb_lanes] = {};
    uint32_t mixed_ready = 0U;
};

static_assert(
    sizeof(asc::asccomm_jetty_info) == sizeof(asc::asccomm_jetty_info),
    "SIMT and SIMD Jetty metadata must share an ABI");
static_assert(
    offsetof(asc::asccomm_jetty_info, packed_head) == offsetof(asc::asccomm_jetty_info, packed_head),
    "SIMT/SIMD packed head offset mismatch");
static_assert(
    sizeof(asc::simt::asccomm_peer_info) == sizeof(asc::asccomm_jetty_peer_info),
    "SIMT and SIMD peer metadata must share an ABI");

__simt_callee__ __forceinline__ __ubuf__ simt_jetty_ub_layout* get_simt_jetty_ub_layout()
{
    return reinterpret_cast<__ubuf__ simt_jetty_ub_layout*>(0);
}

template <typename coop_group>
__simt_callee__ __forceinline__ void prepare_jetty(
    const coop_group& group, uint64_t channel, __gm__ void* jetty_ptr_table, __ubuf__ simt_jetty_ub_layout* ub_layout,
    asc::simt::asccomm_peer& peer, asc::simt::asccomm_jetty& jetty)
{
    if (group.thread_rank() == 0) {
        asc::simt::asccomm_peer peer_init(&ub_layout->peer_info, jetty_ptr_table, 0);
        asc::simt::asccomm_jetty jetty_init(
            &ub_layout->jetty_info, reinterpret_cast<__ubuf__ uint8_t*>(ub_layout->dwqe_stage), jetty_ptr_table, 0);
    }
    group.sync();
    peer.attach(&ub_layout->peer_info, channel);
    jetty.asccomm_jetty_attach(
        &ub_layout->jetty_info, reinterpret_cast<__ubuf__ uint8_t*>(ub_layout->dwqe_stage), channel);
}

template <int sge_num, bool pad_second_sge, bool measure, bool standalone_write = false>
__simt_callee__ inline void simt_jetty_write_thread_impl(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, const asc::simt::asccomm_peer& peer,
    asc::simt::asccomm_jetty& jetty, uint64_t& write_cycles, uint64_t& commit_cycles, uint64_t& completion_cycles)
{
    __gm__ uint8_t* local_src = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_dst = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    static_assert(sge_num >= 2 && sge_num <= 12, "thread SGE count must be in [2, 12]");
    asc::simt::asccomm_sge sges[sge_num] = {};
#pragma unroll
    for (int sge_idx = 0; sge_idx < sge_num; ++sge_idx) {
        sges[sge_idx].addr = reinterpret_cast<uint64_t>(local_src) + static_cast<uint64_t>(sge_idx) * SLOT_BYTES;
        sges[sge_idx].len = static_cast<int>(SLOT_BYTES);
    }
    if constexpr (pad_second_sge) {
        sges[1].addr = 0;
        sges[1].len = 0;
    }

    uint64_t begin = 0U;
    if constexpr (measure) {
        begin = clock();
    }
    // The SIMT DWQE hardware path is limited to a two-BB WRITE. Split larger
    // thread payloads into 2-SGE segments while preserving the contiguous
    // remote layout expected by the sample.
    constexpr int segment_sge_num = 2;
    for (int segment = 0; segment < sge_num; segment += segment_sge_num) {
        asc::simt::asccomm_sge segment_sges[segment_sge_num] = {sges[segment], sges[segment + 1]};
        __gm__ void* segment_dst = remote_dst + static_cast<uint64_t>(segment) * SLOT_BYTES;
        if constexpr (standalone_write) {
            jetty.template asccomm_jetty_write_nbi<segment_sge_num, false>(peer, segment_dst, segment_sges);
            jetty.asccomm_jetty_advance_sq();
            // Each split segment is its own implicit batch; publish it so the NIC
            // fetches before the next segment overwrites the two-BB DWQE window.
            jetty.asccomm_jetty_publish_sq();
        } else {
            jetty.template asccomm_jetty_write_nbi<segment_sge_num>(peer, segment_dst, segment_sges);
        }
    }
    uint64_t write_end = 0U;
    if constexpr (measure) {
        write_end = clock();
    }
    uint64_t commit_end = write_end;
    if constexpr (measure) {
        commit_end = clock();
    }
    jetty.template asccomm_jetty_drain<1000000>();
    if constexpr (measure) {
        write_cycles += write_end - begin;
        commit_cycles += commit_end - write_end;
        completion_cycles += clock() - begin;
    }
}

template <uint32_t sge_num, typename coop_group, bool pad_second_sge, bool measure, bool standalone_write = false>
__simt_callee__ inline void simt_jetty_write_cooperative_impl(
    const coop_group& group, uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    const asc::simt::asccomm_peer& peer, asc::simt::asccomm_jetty& jetty, uint64_t& write_cycles,
    uint64_t& commit_cycles, uint64_t& completion_cycles)
{
    __gm__ uint8_t* local_src = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_dst = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);
    asc::simt::asccomm_sge sge{};
    const int lane_idx = group.thread_rank();
    // warp-pad keeps the original two-SGE padding contract: one valid SGE, the rest
    // zero-length, so the WQE needs NOP padding to fill its fixed slot reservation.
    if (lane_idx < static_cast<int>(sge_num) && (!pad_second_sge || lane_idx == 0)) {
        sge.addr = reinterpret_cast<uint64_t>(local_src) + static_cast<uint64_t>(lane_idx) * SLOT_BYTES;
        sge.len = static_cast<int>(SLOT_BYTES);
    }

    uint64_t begin = 0U;
    if constexpr (measure) {
        begin = clock();
    }
    if constexpr (standalone_write) {
        jetty.template asccomm_jetty_write_nbi<sge_num, false>(group, peer, remote_dst, sge);
    } else {
        jetty.template asccomm_jetty_write_nbi<sge_num>(group, peer, remote_dst, sge);
    }
    uint64_t write_end = 0U;
    if constexpr (measure) {
        write_end = clock();
    }
    uint64_t commit_end = write_end;
    if constexpr (standalone_write) {
        // advance_sq finalizes the batch header but does not ring the DWQE doorbell;
        // without publish_sq the NIC never fetches the WQEs and drain hangs.
        jetty.asccomm_jetty_advance_sq(group);
        jetty.asccomm_jetty_publish_sq(group);
        if constexpr (measure) {
            commit_end = clock();
        }
    }
    jetty.template asccomm_jetty_drain<1000000>(group);
    if constexpr (measure) {
        write_cycles += write_end - begin;
        commit_cycles += commit_end - write_end;
        completion_cycles += clock() - begin;
    }
}

template <bool measure>
__simt_callee__ inline void simt_jetty_write_dispatch_impl(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, const asc::simt::asccomm_peer& peer,
    asc::simt::asccomm_jetty& jetty, uint64_t& write_cycles, uint64_t& commit_cycles, uint64_t& completion_cycles)
{
    asc::simt::asccomm_coop_group group;
    asc::simt::asccomm_coop_warp warp;
    __gm__ uint8_t* local_src = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_dst = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);
    group.sync();

    asc::simt::asccomm_sge sge{};
    const int lane_idx = warp.thread_rank();
    if (lane_idx < static_cast<int>(SLOT_COUNT)) {
        sge.addr = reinterpret_cast<uint64_t>(local_src) + static_cast<uint64_t>(lane_idx) * SLOT_BYTES;
        sge.len = static_cast<int>(SLOT_BYTES);
    }

    // Each warp reserves one WQE. The block commits the actual number of WQEs after all warps finish.
    uint64_t begin = 0U;
    if constexpr (measure) {
        begin = clock();
    }
    jetty.template asccomm_jetty_write_nbi<CONFIGURED_SGE_NUM, false>(warp, peer, remote_dst, sge);
    uint64_t write_end = 0U;
    if constexpr (measure) {
        write_end = clock();
    }
    jetty.asccomm_jetty_advance_sq(group, 4U, 4U);
    jetty.asccomm_jetty_publish_sq(group);
    uint64_t commit_end = write_end;
    if constexpr (measure) {
        commit_end = clock();
    }
    jetty.template asccomm_jetty_drain<1000000>(group);
    if constexpr (measure) {
        write_cycles += write_end - begin;
        commit_cycles += commit_end - write_end;
        completion_cycles += clock() - begin;
    }
}

__simt_vf__ __launch_bounds__(1024) inline void simt_jetty_write_thread_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    auto* ub_layout = get_simt_jetty_ub_layout();
    asc::simt::asccomm_coop_thread thread;
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(thread, channel, jetty_ptr_table, ub_layout, peer, jetty);
    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_jetty_write_thread_impl<12, false, false, true>(
            channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_thread_impl<12, false, true, true>(
                channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
        timing[0] = write_cycles;
        timing[1] = commit_cycles;
        timing[2] = completion_cycles;
        timing[3] = iterations;
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_thread_impl<12, false, false>(
                channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void simt_jetty_write_warp_pad_zero_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    const asc::simt::asccomm_coop_warp warp{};
    auto* ub_layout = get_simt_jetty_ub_layout();
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(warp, channel, jetty_ptr_table, ub_layout, peer, jetty);
    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, true, false, true>(
            warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, true, true, true>(
                warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = write_cycles;
            timing[1] = commit_cycles;
            timing[2] = completion_cycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, true, false>(
                warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void simt_jetty_write_warp_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    const asc::simt::asccomm_coop_warp warp{};
    auto* ub_layout = get_simt_jetty_ub_layout();
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(warp, channel, jetty_ptr_table, ub_layout, peer, jetty);
    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, false, false, true>(
            warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, false, true, true>(
                warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = write_cycles;
            timing[1] = commit_cycles;
            timing[2] = completion_cycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_warp, false, false>(
                warp, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void simt_jetty_write_group_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    const asc::simt::asccomm_coop_group group{};
    auto* ub_layout = get_simt_jetty_ub_layout();
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(group, channel, jetty_ptr_table, ub_layout, peer, jetty);
    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_group, false, false, true>(
            group, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_group, false, true, true>(
                group, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles,
                completion_cycles);
        }
        if (group.thread_rank() == 0) {
            timing[0] = write_cycles;
            timing[1] = commit_cycles;
            timing[2] = completion_cycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_cooperative_impl<CONFIGURED_SGE_NUM, asc::simt::asccomm_coop_group, false, false>(
                group, channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles,
                completion_cycles);
        }
    }
}

__simt_vf__ __launch_bounds__(1024) inline void simt_jetty_write_dispatch_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    auto* ub_layout = get_simt_jetty_ub_layout();
    const asc::simt::asccomm_coop_group group{};
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(group, channel, jetty_ptr_table, ub_layout, peer, jetty);
    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_jetty_write_dispatch_impl<false>(
            channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_dispatch_impl<true>(
                channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
        if (threadIdx.x == 0U) {
            timing[0] = write_cycles;
            timing[1] = commit_cycles;
            timing[2] = completion_cycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_jetty_write_dispatch_impl<false>(
                channel, send_buf_idx, recv_buf_idx, peer, jetty, write_cycles, commit_cycles, completion_cycles);
        }
    }
}

__simt_vf__ __aicore__ __launch_bounds__(64) inline void simt_jetty_write_mixed_worker(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    auto* ub_layout = get_simt_jetty_ub_layout();
    asc::simt::asccomm_coop_group group;
    asc::simt::asccomm_coop_warp warp;
    asc::simt::asccomm_peer peer;
    asc::simt::asccomm_jetty jetty;
    prepare_jetty(group, channel, jetty_ptr_table, ub_layout, peer, jetty);

    __gm__ uint8_t* local_src = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_dst = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);
    asc::simt::asccomm_sge lane_sge{};
    const int lane_idx = warp.thread_rank();
    if (lane_idx < static_cast<int>(CONFIGURED_SGE_NUM)) {
        lane_sge.addr = reinterpret_cast<uint64_t>(local_src) + static_cast<uint64_t>(lane_idx) * SLOT_BYTES;
        lane_sge.len = static_cast<int>(SLOT_BYTES);
    }

    group.sync();
    jetty.template asccomm_jetty_write_nbi<CONFIGURED_SGE_NUM, false>(warp, peer, remote_dst, lane_sge);
    jetty.asccomm_jetty_advance_sq(group);
    if (group.thread_rank() == 0) {
        ub_layout->mixed_ready = 1U;
    }
}

} // namespace

extern "C" __global__ __vector__ void simt_jetty_write_thread(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<simt_jetty_write_thread_vf>(
        dim3(1), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __vector__ void simt_jetty_write_warp_pad_zero(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<simt_jetty_write_warp_pad_zero_vf>(
        dim3(32), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __vector__ void simt_jetty_write_warp(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<simt_jetty_write_warp_vf>(
        dim3(32), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __vector__ void simt_jetty_write_group(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<simt_jetty_write_group_vf>(
        dim3(32), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __vector__ void simt_jetty_write_dispatch(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<simt_jetty_write_dispatch_vf>(
        dim3(64), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
        iterations, warmup);
}

extern "C" __global__ __mix__(1, 1) void simt_jetty_write_mixed(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    AscendC::InitSocState();
    if ASCEND_IS_AIC {
        return;
    }

    auto* ub_layout = reinterpret_cast<__ubuf__ simt_jetty_ub_layout*>(0);
    ub_layout->mixed_ready = 0U;
    asc_sync_notify(PIPE_S, PIPE_V, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_V, EVENT_ID0);
    AscendC::Simt::VF_CALL<simt_jetty_write_mixed_worker>(
        AscendC::Simt::Dim3(64, 1, 1), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx);

    while (*reinterpret_cast<volatile __ubuf__ uint32_t*>(&ub_layout->mixed_ready) != 1U) {
        asm volatile("nop");
    }

    asc::asccomm_jetty simd_jetty(
        reinterpret_cast<__ubuf__ asc::asccomm_jetty_info*>(&ub_layout->jetty_info), nullptr,
        reinterpret_cast<__gm__ uint8_t*>(jetty_ptr_table), 0);
    simd_jetty.asccomm_jetty_ring_doorbell();
}

void launch_simt_jetty(
    void* stream, uint64_t channel, void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, uint32_t mode,
    void* timing, uint32_t iterations, uint32_t warmup)
{
    switch (mode) {
        case 0U:
            simt_jetty_write_thread<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup);
            break;
        case 1U:
            simt_jetty_write_warp_pad_zero<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup);
            break;
        case 2U:
            simt_jetty_write_group<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup);
            break;
        case 3U:
            simt_jetty_write_dispatch<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup);
            break;
        case 4U:
            simt_jetty_write_warp<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup);
            break;
        case 5U:
            simt_jetty_write_mixed<<<1, 0, stream>>>(channel, jetty_ptr_table, send_buf_idx, recv_buf_idx);
            break;
        default:
            std::printf("[simt_jetty] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
