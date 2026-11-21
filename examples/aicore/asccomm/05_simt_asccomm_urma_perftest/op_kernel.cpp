/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file op_kernel.cpp
 * \brief Device side of the SIMT URMA performance example.
 *
 * Five interfaces are measured: asccomm_write_nbi, asccomm_write_value_nbi, asccomm_write_with_notify_nbi, asccomm_atomic_faa_nbi and
 * asccomm_atomic_cas_nbi. Two publishing strategies apply to each:
 *   Immediate  every WQE rings its own doorbell (commit=true).
 *   Last       every WQE but the last is deferred (commit=false); the final committed WQE
 *              publishes the whole batch with one head update.
 *
 * The two differ in SQ footprint as well as in doorbell count: a deferred asccomm_write_nbi fits in one
 * 64-byte basic block, while a committed one splits its payload across two SGEs to fill the
 * 128-byte DWQE window. So Immediate consumes twice the SQ space per asccomm_write_nbi. Notify and the
 * atomics are fixed 2-BB WQEs either way.
 *
 * Every kernel posts from a single lane: one channel is driven by one lane.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "asccomm/asccomm_simt.h"

#include "simt_urma_perftest_common.h"

namespace {
using namespace simt_urma_perftest;

enum class perf_api : uint32_t { WRITE, WRITE_VALUE, NOTIFY, FAA, CAS };

// 地址逐次前移，避免连续 WQE 都落在同一条 cache line 上——否则测到的是远端内存系统而不是下发
// 路径。两个原子接口例外：必须打在同一个 word 上，累加才有意义。
template <perf_api api, bool commit>
// 提交单个通信任务：按api分派到asccomm_write_nbi/asccomm_write_value_nbi/asccomm_write_with_notify_nbi/asccomm_atomic_faa_nbi/asccomm_atomic_cas_nbi。
__simt_callee__ inline int32_t submit_one(
    asc::simt::asccomm<>& asccomm, uint64_t channel, __gm__ uint8_t* remote_base, __gm__ uint8_t* local_base,
    uint32_t payload_bytes, uint32_t slot_bytes, uint32_t slot)
{
    uint64_t offset = static_cast<uint64_t>(slot) * slot_bytes;
    if constexpr (api == perf_api::WRITE) {
        return asccomm.asccomm_write_nbi<commit>(channel, remote_base + offset, local_base + offset, payload_bytes);
    } else if constexpr (api == perf_api::WRITE_VALUE) {
        return asccomm.asccomm_write_value_nbi<uint64_t, commit>(channel, remote_base + offset, INLINE_VALUE);
    } else if constexpr (api == perf_api::NOTIFY) {
        // The signal word sits immediately after the payload, rounded up to 8 bytes so the
        // notify address stays aligned.
        uint32_t notify_offset = (payload_bytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
        __gm__ uint8_t* remote = remote_base + offset;
        return asccomm.asccomm_write_with_notify_nbi<commit>(
            channel, remote, local_base + offset, payload_bytes, remote + notify_offset, NOTIFY_SIGNAL);
    } else {
        __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(remote_base);
        __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(local_base);
        if constexpr (api == perf_api::FAA) {
            return asccomm.asccomm_atomic_faa_nbi<uint64_t, commit>(channel, &remote[0], &local[0], FAA_ADD);
        } else {
            return asccomm.asccomm_atomic_cas_nbi<uint64_t, commit>(
                channel, &remote[0], &local[0], CAS_INITIAL, CAS_SWAP);
        }
    }
}

template <perf_api api>
// 该api是否轮转slot地址：原子类必须固定同一地址，累加才有意义。
__simt_callee__ inline constexpr bool rotates_slots()
{
    return api != perf_api::FAA && api != perf_api::CAS;
}

// 首个失败即返回，避免用一个无意义的周期数报告一次坏的运行。
template <perf_api api, bool commit_each>
// 提交一个系列：立即模式逐笔提交；延迟模式前n-1笔延迟、末笔立即提交带出整批。
__simt_callee__ inline int32_t submit_series(
    asc::simt::asccomm<>& asccomm, uint64_t channel, __gm__ uint8_t* remote_base, __gm__ uint8_t* local_base,
    uint32_t payload_bytes, uint32_t slot_bytes, uint32_t slot_count, uint32_t count)
{
    if (count == 0U) {
        return 0;
    }
    if constexpr (commit_each) {
        for (uint32_t i = 0U; i < count; ++i) {
            uint32_t slot = rotates_slots<api>() ? i % slot_count : 0U;
            int32_t status =
                submit_one<api, true>(asccomm, channel, remote_base, local_base, payload_bytes, slot_bytes, slot);
            if (status != 0) {
                return status;
            }
        }
        return 0;
    } else {
        // 延迟提交的 WQE 各自预留 SQ 空间，最后一条 commit=true 用一次 head 更新发布整批。
        // 循环覆盖操作 0..count-2、末条提交是操作 count-1，slot 序列与 commit_each 分支完全一致。
        // 若从 i=1 起循环会跳过 slot 0 且重复写末 slot（count=2 时访问 [1,1] 而非 [0,1]），
        // 既扭曲访问分布，也会让接收侧对合法小迭代数报 FAIL。
        for (uint32_t i = 0U; i + 1U < count; ++i) {
            uint32_t slot = rotates_slots<api>() ? i % slot_count : 0U;
            int32_t status =
                submit_one<api, false>(asccomm, channel, remote_base, local_base, payload_bytes, slot_bytes, slot);
            if (status != 0) {
                return status;
            }
        }
        uint32_t last_slot = rotates_slots<api>() ? (count - 1U) % slot_count : 0U;
        return submit_one<api, true>(asccomm, channel, remote_base, local_base, payload_bytes, slot_bytes, last_slot);
    }
}

// 失败时清零计时并记录状态码。
__simt_callee__ inline void report_failure(__gm__ uint64_t* timing, int32_t status)
{
    timing[ISSUE_CYCLES_INDEX] = 0U;
    timing[COMPLETION_CYCLES_INDEX] = 0U;
    timing[DRAIN_STATUS_INDEX] = static_cast<uint64_t>(status);
    timing[COMPLETED_INDEX] = 0U;
}

// Warmup, then a timed run. issue_cycles covers only the posting loop; completion_cycles also
// covers the Drain that waits for every CQE, so the two separate issue cost from end-to-end
// latency. A failure in either phase reports through drain_status with zeroed cycle counts.
template <perf_api api, bool commit_each>
__simt_vf__ __launch_bounds__(32) inline void simt_urma_perf_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t slot_count, uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        report_failure(timing, -1);
        return;
    }

    int32_t status =
        submit_series<api, commit_each>(asccomm, channel, remote, local, payload_bytes, slot_bytes, slot_count, warmup);
    int32_t drain_status = status != 0 ? status : (warmup == 0U ? 0 : asccomm.asccomm_drain(channel));
    if (drain_status != 0) {
        report_failure(timing, drain_status);
        return;
    }

    uint64_t begin = clock();
    status = submit_series<api, commit_each>(
        asccomm, channel, remote, local, payload_bytes, slot_bytes, slot_count, iterations);
    uint64_t issue_end = clock();
    drain_status = status != 0 ? status : asccomm.asccomm_drain(channel);
    uint64_t completion_end = clock();

    timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
    timing[COMPLETION_CYCLES_INDEX] = completion_end - begin;
    timing[DRAIN_STATUS_INDEX] = static_cast<uint64_t>(drain_status);
    timing[COMPLETED_INDEX] = drain_status == 0 ? iterations : 0U;
}

} // namespace

#define DEFINE_URMA_PERF_KERNEL(kernel_name, api, commit_each)                                                       \
    extern "C" __global__ __vector__ void kernel_name(                                                               \
        uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing, uint32_t payload_bytes, \
        uint32_t slot_bytes, uint32_t slot_count, uint32_t iterations, uint32_t warmup)                              \
    {                                                                                                                \
        asc_vf_call<simt_urma_perf_vf<api, commit_each>>(                                                            \
            dim3(1), channel, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing), payload_bytes, \
            slot_bytes, slot_count, iterations, warmup);                                                             \
    }

DEFINE_URMA_PERF_KERNEL(simt_write_perf_immediate, perf_api::WRITE, true)
DEFINE_URMA_PERF_KERNEL(simt_write_perf_last, perf_api::WRITE, false)
DEFINE_URMA_PERF_KERNEL(simt_write_value_perf_immediate, perf_api::WRITE_VALUE, true)
DEFINE_URMA_PERF_KERNEL(simt_write_value_perf_last, perf_api::WRITE_VALUE, false)
DEFINE_URMA_PERF_KERNEL(simt_notify_perf_immediate, perf_api::NOTIFY, true)
DEFINE_URMA_PERF_KERNEL(simt_notify_perf_last, perf_api::NOTIFY, false)
DEFINE_URMA_PERF_KERNEL(simt_faa_perf_immediate, perf_api::FAA, true)
DEFINE_URMA_PERF_KERNEL(simt_faa_perf_last, perf_api::FAA, false)
DEFINE_URMA_PERF_KERNEL(simt_cas_perf_immediate, perf_api::CAS, true)
DEFINE_URMA_PERF_KERNEL(simt_cas_perf_last, perf_api::CAS, false)

#undef DEFINE_URMA_PERF_KERNEL

// 按api与提交策略分发下发对应kernel。
void launch_simt_urma_perf(
    void* stream, uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, void* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t slot_count, uint32_t iterations, uint32_t warmup, uint32_t api, bool commit_each)
{
    using namespace simt_urma_perftest;

#define LAUNCH(kernel_name)        \
    kernel_name<<<1, 0, stream>>>( \
        channel, send_buf_idx, recv_buf_idx, timing, payload_bytes, slot_bytes, slot_count, iterations, warmup)

    if (api == API_WRITE) {
        commit_each ? LAUNCH(simt_write_perf_immediate) : LAUNCH(simt_write_perf_last);
    } else if (api == API_WRITE_VALUE) {
        commit_each ? LAUNCH(simt_write_value_perf_immediate) : LAUNCH(simt_write_value_perf_last);
    } else if (api == API_NOTIFY) {
        commit_each ? LAUNCH(simt_notify_perf_immediate) : LAUNCH(simt_notify_perf_last);
    } else if (api == API_FAA) {
        commit_each ? LAUNCH(simt_faa_perf_immediate) : LAUNCH(simt_faa_perf_last);
    } else {
        commit_each ? LAUNCH(simt_cas_perf_immediate) : LAUNCH(simt_cas_perf_last);
    }

#undef LAUNCH
}
