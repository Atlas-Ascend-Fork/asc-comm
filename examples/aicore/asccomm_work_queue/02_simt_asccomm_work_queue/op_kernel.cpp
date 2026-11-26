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
#include "asccomm_work_queue/asccomm_work_queue.h"
#include "asccomm_work_queue/asccomm_work_queue_simt.h"

#include "simt_work_queue_common.h"

namespace {

using namespace simt_work_queue;

struct alignas(16) simt_work_queue_ub_layout {
    asc::asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP> jetty_info;
    asc::asccomm_peer_entity<asc::COMM_PROTOCOL_UB_CTP> peer_info;
    alignas(16) uint64_t dwqe_stage[128U] = {};
};

__simt_callee__ __forceinline__ __ubuf__ simt_work_queue_ub_layout* get_simt_work_queue_ub_layout()
{
    return reinterpret_cast<__ubuf__ simt_work_queue_ub_layout*>(0);
}

template <bool multi_sge, bool measure>
__simt_callee__ inline void simt_work_queue_warp_write_impl(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    const asc::simt::asccomm_peer<asc::COMM_PROTOCOL_UB_CTP>& peer,
    asc::simt::asccomm_work_queue<asc::COMM_PROTOCOL_UB_CTP>& work_queue, uint64_t& write_cycles,
    uint64_t& commit_cycles, uint64_t& completion_cycles)
{
    const asc::simt::asccomm_coop_warp warp{};
    __gm__ uint8_t* local_src = asc::simt::get_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_dst = asc::simt::get_remote_buffer_addr(channel, recv_buf_idx);
    uint64_t begin = 0U;
    if constexpr (measure) {
        begin = clock();
    }
    if constexpr (multi_sge) {
        asc::simt::asccomm_sge sges[CONFIGURED_SGE_NUM] = {};
        for (uint32_t sge_idx = 0U; sge_idx < CONFIGURED_SGE_NUM; ++sge_idx) {
            sges[sge_idx].addr = reinterpret_cast<uint64_t>(local_src) + static_cast<uint64_t>(sge_idx) * SLOT_BYTES;
            sges[sge_idx].len = SLOT_BYTES;
        }
        work_queue.template write_nbi<1000000, CONFIGURED_SGE_NUM>(warp, peer, remote_dst, sges);
    } else {
        work_queue.template write_nbi<1000000>(warp, peer, remote_dst, local_src, CONFIGURED_SGE_NUM * SLOT_BYTES);
    }
    uint64_t write_end = 0U;
    if constexpr (measure) {
        write_end = clock();
    }
    const uint64_t commit_end = write_end;
    work_queue.template drain<1000000>(warp);
    if constexpr (measure) {
        write_cycles += write_end - begin;
        commit_cycles += commit_end - write_end;
        completion_cycles += clock() - begin;
    }
}

template <bool multi_sge>
__simt_vf__ __launch_bounds__(1024) inline void simt_work_queue_warp_write_vf(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx,
    __gm__ uint64_t* timing, uint32_t iterations, uint32_t warmup)
{
    const asc::simt::asccomm_coop_warp warp{};
    auto* ub_layout = get_simt_work_queue_ub_layout();
    asc::simt::asccomm_peer<asc::COMM_PROTOCOL_UB_CTP> peer(
        &ub_layout->peer_info, reinterpret_cast<__gm__ uint8_t*>(jetty_ptr_table), 0);
    asc::simt::asccomm_work_queue<asc::COMM_PROTOCOL_UB_CTP> work_queue(
        &ub_layout->jetty_info, reinterpret_cast<__ubuf__ uint8_t*>(ub_layout->dwqe_stage),
        reinterpret_cast<__gm__ uint8_t*>(jetty_ptr_table), 0);
    warp.sync();

    uint64_t write_cycles = 0U;
    uint64_t commit_cycles = 0U;
    uint64_t completion_cycles = 0U;
    for (uint32_t i = 0U; i < warmup; ++i) {
        simt_work_queue_warp_write_impl<multi_sge, false>(
            channel, send_buf_idx, recv_buf_idx, peer, work_queue, write_cycles, commit_cycles, completion_cycles);
    }
    if (timing != nullptr) {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_work_queue_warp_write_impl<multi_sge, true>(
                channel, send_buf_idx, recv_buf_idx, peer, work_queue, write_cycles, commit_cycles, completion_cycles);
        }
        if (warp.thread_rank() == 0) {
            timing[0] = write_cycles;
            timing[1] = commit_cycles;
            timing[2] = completion_cycles;
            timing[3] = iterations;
        }
    } else {
        for (uint32_t i = 0U; i < iterations; ++i) {
            simt_work_queue_warp_write_impl<multi_sge, false>(
                channel, send_buf_idx, recv_buf_idx, peer, work_queue, write_cycles, commit_cycles, completion_cycles);
        }
    }
}

} // namespace

extern "C" __global__ __vector__ void simt_work_queue_warp_write(
    uint64_t channel, __gm__ void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing,
    uint32_t iterations, uint32_t warmup, uint32_t multi_sge)
{
    if (multi_sge != 0U) {
        asc_vf_call<simt_work_queue_warp_write_vf<true>>(
            dim3(32), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
            iterations, warmup);
    } else {
        asc_vf_call<simt_work_queue_warp_write_vf<false>>(
            dim3(32), channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),
            iterations, warmup);
    }
}

void launch_simt_work_queue(
    void* stream, uint64_t channel, void* jetty_ptr_table, uint32_t send_buf_idx, uint32_t recv_buf_idx, uint32_t mode,
    void* timing, uint32_t iterations, uint32_t warmup)
{
    switch (mode) {
        case MODE_WRITE:
        case MODE_WRITE_SGE:
            simt_work_queue_warp_write<<<1, 0, stream>>>(
                channel, jetty_ptr_table, send_buf_idx, recv_buf_idx, timing, iterations, warmup, mode);
            break;
        default:
            std::printf("[simt_work_queue] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
