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
#include "simt_api/cooperative_groups.h"

#include "asccomm/asccomm_simt.h"
#include "simt_batch_perf_common.h"

namespace {
using namespace simt_batch_perf;

template <uint32_t api, typename asccomm_t, typename... Groups>
// group内单lane构造一个WQE请求并追加到当前批次。
__simt_callee__ inline int32_t append_one_wqe(
    asccomm_t& asccomm, asc::simt::ubc_ctp_batch_handle& batch, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payload_bytes, uint32_t slot_bytes, uint32_t slot, const Groups&... groups)
{
    const uint64_t offset = static_cast<uint64_t>(slot) * slot_bytes;
    if constexpr (api == API_WRITE) {
        return asccomm.asccomm_write_nbi(batch, remote + offset, local + offset, payload_bytes, groups...);
    } else if constexpr (api == API_READ) {
        return asccomm.asccomm_read_nbi(batch, local + offset, remote + offset, payload_bytes, groups...);
    } else if constexpr (api == API_NOTIFY) {
        return asccomm.asccomm_write_with_notify_nbi(
            batch, remote + offset, local + offset, payload_bytes, remote + offset + slot_bytes - sizeof(uint64_t),
            NOTIFY_VALUE, groups...);
    } else if constexpr (api == API_WRITE_VALUE) {
        return asccomm.template asccomm_write_value_nbi<uint64_t>(batch, remote + offset, INLINE_VALUE, groups...);
    } else if constexpr (api == API_FAA) {
        return asccomm.template asccomm_atomic_faa_nbi<uint64_t>(
            batch, remote + offset, local + offset, FAA_ADD, groups...);
    } else {
        return asccomm.template asccomm_atomic_cas_nbi<uint64_t>(
            batch, remote + offset, local + offset, CAS_INITIAL, CAS_SWAP, groups...);
    }
}

template <uint32_t api>
// 单lane批量：每批仅提交一个任务请求，逐批添加并asccomm_batch_commit，批次间复用同一句柄。
__simt_callee__ inline int32_t submit_batch_series(
    asc::simt::asccomm<>& asccomm, asc::simt::ubc_ctp_batch_handle& batch, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payload_bytes, uint32_t slot_bytes, uint32_t count, uint32_t& publish_count)
{
    publish_count = 0U;
    for (uint32_t i = 0U; i < count; ++i) {
        int32_t status = append_one_wqe<api>(asccomm, batch, remote, local, payload_bytes, slot_bytes, i % SLOT_COUNT);
        if (status != 0) {
            return status;
        }
        status = asccomm.asccomm_batch_commit(batch);
        if (status != 0) {
            return status;
        }
        ++publish_count;
    }
    return 0;
}

template <uint32_t api, typename asccomm_t, typename Group>
// 多lane协同批量：group内每lane各构造一个请求添加到批次，asccomm_batch_commit为协作调用（所有lane
// 均须调用，由发布lane统一发布整批，每批任务数等于lane数）；同批请求必须使用相同WQE宽度，
// 添加失败不得对本批调用asccomm_batch_commit。
__simt_callee__ inline int32_t submit_group_batches(
    asccomm_t& asccomm, const Group& group, asc::simt::ubc_ctp_batch_handle& batch, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payload_bytes, uint32_t slot_bytes, uint32_t count, uint32_t& publish_count)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    publish_count = 0U;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        const uint32_t operation = begin + rank;
        // One group lane prepares exactly one WQE for this batch.
        const int32_t append_status = append_one_wqe<api>(
            asccomm, batch, remote, local, payload_bytes, slot_bytes, operation % SLOT_COUNT, group);
        int32_t commit_status = append_status == 0 ? asccomm.asccomm_batch_commit(batch, group) : append_status;
        if (append_status != 0 || commit_status != 0) {
            return append_status != 0 ? append_status : commit_status;
        }
        ++publish_count;
    }
    return 0;
}

template <uint32_t api>
__simt_vf__ __launch_bounds__(32) inline void simt_batch_perf_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer)
{
    // 单lane驱动通道：仅lane 0发起通信，其余lane直接返回。
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);
    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        timing[STATUS_INDEX] = static_cast<uint64_t>(-1);
        return;
    }

    uint32_t publish_count = 0U;
    int32_t status = 0;
    // 单lane批量：每批仅提交一个任务请求，逐批添加并asccomm_batch_commit，批次间复用同一句柄；
    // 批量工作区至少256字节，由asccomm_make_batch_handle绑定（不经过asccomm_init）。
    asc::simt::ubc_ctp_batch_handle batch =
        asccomm.asccomm_make_batch_handle(channel, batch_buffer, BATCH_BUFFER_BYTES);
    if (batch.context == nullptr) {
        status = asc::HCOMM_FAILED;
    } else {
        status =
            submit_batch_series<api>(asccomm, batch, remote, local, payload_bytes, slot_bytes, warmup, publish_count);
    }
    if (status != 0) {
        timing[STATUS_INDEX] = static_cast<uint64_t>(status);
        timing[PUBLISH_COUNT_INDEX] = publish_count;
        return;
    }
    if (warmup != 0U) {
        status = asccomm.asccomm_drain(batch);
        if (status != 0) {
            timing[STATUS_INDEX] = static_cast<uint64_t>(status);
            timing[PUBLISH_COUNT_INDEX] = publish_count;
            return;
        }
    }

    uint64_t begin = clock();
    status =
        submit_batch_series<api>(asccomm, batch, remote, local, payload_bytes, slot_bytes, iterations, publish_count);
    uint64_t issue_end = clock();
    if (status != 0) {
        timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
        timing[STATUS_INDEX] = static_cast<uint64_t>(status);
        timing[PUBLISH_COUNT_INDEX] = publish_count;
        return;
    }
    status = asccomm.asccomm_drain(batch);
    uint64_t completion_end = clock();
    timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
    timing[COMPLETION_CYCLES_INDEX] = completion_end - begin;
    timing[STATUS_INDEX] = static_cast<uint64_t>(status);
    timing[COMPLETED_INDEX] = status == 0 ? iterations : 0U;
    timing[PUBLISH_COUNT_INDEX] = publish_count;
}

template <uint32_t api, uint32_t group_capacity>
__simt_vf__ __launch_bounds__(group_capacity) inline void simt_group_batch_perf_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer,
    __ubuf__ uint32_t* drain_status)
{
    auto group = cooperative_groups::this_thread_block();
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);
    asc::simt::asccomm<> asccomm;
    int32_t status = asccomm.asccomm_init(nullptr, 0U);
    auto batch = asccomm.asccomm_make_batch_handle(channel, batch_buffer, BATCH_BUFFER_BYTES, group);
    if (batch.context == nullptr) {
        status = asc::HCOMM_FAILED;
    }
    uint32_t publish_count = 0U;
    if (status == 0) {
        status = submit_group_batches<api>(
            asccomm, group, batch, remote, local, payload_bytes, slot_bytes, warmup, publish_count);
    }
    if (status == 0 && warmup != 0U) {
        // asccomm_drain不接收group：group.sync()后由一个lane（thread_rank 0）统一调用。
        if (rank == 0U) {
            status = asccomm.asccomm_drain(batch);
            drain_status[0] = static_cast<uint32_t>(status);
        }
        group.sync();
        status = static_cast<int32_t>(drain_status[0]);
    }
    if (status != 0) {
        if (rank == 0U) {
            timing[STATUS_INDEX] = static_cast<uint64_t>(status);
            timing[PUBLISH_COUNT_INDEX] = publish_count;
        }
        return;
    }

    group.sync();
    uint64_t begin = rank == 0U ? clock() : 0U;
    status = submit_group_batches<api>(
        asccomm, group, batch, remote, local, payload_bytes, slot_bytes, iterations, publish_count);
    uint64_t issue_end = rank == 0U ? clock() : 0U;
    if (status == 0 && rank == 0U) {
        // 仅rank 0统一asccomm_drain该批量句柄累计提交的任务。
        status = asccomm.asccomm_drain(batch);
    }
    if (rank == 0U) {
        const uint64_t completion_end = clock();
        timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
        timing[COMPLETION_CYCLES_INDEX] = completion_end - begin;
        timing[STATUS_INDEX] = static_cast<uint64_t>(status);
        timing[COMPLETED_INDEX] = status == 0 ? iterations : 0U;
        timing[PUBLISH_COUNT_INDEX] = publish_count;
    }
    group.sync();
}

} // namespace

#define DEFINE_BATCH_PERF_KERNEL(kernel_name, api)                                                                   \
    extern "C" __global__ __vector__ void kernel_name(                                                               \
        uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing, uint32_t payload_bytes, \
        uint32_t slot_bytes, uint32_t iterations, uint32_t warmup)                                                   \
    {                                                                                                                \
        alignas(128) __ubuf__ uint64_t batch_buffer[BATCH_BUFFER_BYTES / sizeof(uint64_t)];                          \
        asc_vf_call<simt_batch_perf_vf<api>>(                                                                        \
            dim3(1), channel, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing), payload_bytes, \
            slot_bytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batch_buffer));                      \
    }

DEFINE_BATCH_PERF_KERNEL(simt_write_batch_perf, API_WRITE)
DEFINE_BATCH_PERF_KERNEL(simt_read_batch_perf, API_READ)
DEFINE_BATCH_PERF_KERNEL(simt_notify_batch_perf, API_NOTIFY)
DEFINE_BATCH_PERF_KERNEL(simt_write_value_batch_perf, API_WRITE_VALUE)
DEFINE_BATCH_PERF_KERNEL(simt_faa_batch_perf, API_FAA)
DEFINE_BATCH_PERF_KERNEL(simt_cas_batch_perf, API_CAS)

#undef DEFINE_BATCH_PERF_KERNEL

#if SIMT_BATCH_ENABLE_LARGE_GROUP
#define LAUNCH_LARGE_GROUP_BATCH(api)                                                                           \
    else                                                                                                        \
    {                                                                                                           \
        asc_vf_call<simt_group_batch_perf_vf<api, MAX_GROUP_LANES>>(                                            \
            dim3(group_lanes), channel, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing), \
            payload_bytes, slot_bytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batch_buffer),   \
            drain_status);                                                                                      \
    }
#else
#define LAUNCH_LARGE_GROUP_BATCH(api)
#endif

#define DEFINE_GROUP_BATCH_PERF_KERNEL(kernel_name, api)                                                             \
    extern "C" __global__ __vector__ void kernel_name(                                                               \
        uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, __gm__ void* timing, uint32_t payload_bytes, \
        uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, uint32_t group_lanes)                             \
    {                                                                                                                \
        alignas(128) __ubuf__ uint64_t batch_buffer[BATCH_BUFFER_BYTES / sizeof(uint64_t)];                          \
        alignas(16) __ubuf__ uint32_t drain_status[1U];                                                              \
        if (group_lanes <= 128U) {                                                                                   \
            asc_vf_call<simt_group_batch_perf_vf<api, 128U>>(                                                        \
                dim3(group_lanes), channel, send_buf_idx, recv_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing),  \
                payload_bytes, slot_bytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batch_buffer),    \
                drain_status);                                                                                       \
        }                                                                                                            \
        LAUNCH_LARGE_GROUP_BATCH(api)                                                                                \
    }

DEFINE_GROUP_BATCH_PERF_KERNEL(simt_write_group_batch_perf, API_WRITE)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_read_group_batch_perf, API_READ)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_notify_group_batch_perf, API_NOTIFY)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_write_value_group_batch_perf, API_WRITE_VALUE)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_faa_group_batch_perf, API_FAA)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_cas_group_batch_perf, API_CAS)

#undef DEFINE_GROUP_BATCH_PERF_KERNEL
#undef LAUNCH_LARGE_GROUP_BATCH

// 按api与批量形态分发下发对应kernel。
void launch_simt_batch_perf(
    void* stream, uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, void* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, uint32_t api, bool group_batch, uint32_t group_lanes)
{
#define LAUNCH_SET(batch_kernel, group_kernel)                                                                        \
    if (group_batch) {                                                                                                \
        group_kernel<<<1, 0, stream>>>(                                                                               \
            channel, send_buf_idx, recv_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, group_lanes); \
    } else {                                                                                                          \
        batch_kernel<<<1, 0, stream>>>(                                                                               \
            channel, send_buf_idx, recv_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup);              \
    }
    if (api == API_WRITE) {
        LAUNCH_SET(simt_write_batch_perf, simt_write_group_batch_perf)
    } else if (api == API_READ) {
        LAUNCH_SET(simt_read_batch_perf, simt_read_group_batch_perf)
    } else if (api == API_NOTIFY) {
        LAUNCH_SET(simt_notify_batch_perf, simt_notify_group_batch_perf)
    } else if (api == API_WRITE_VALUE) {
        LAUNCH_SET(simt_write_value_batch_perf, simt_write_value_group_batch_perf)
    } else if (api == API_FAA) {
        LAUNCH_SET(simt_faa_batch_perf, simt_faa_group_batch_perf)
    } else {
        LAUNCH_SET(simt_cas_batch_perf, simt_cas_group_batch_perf)
    }
#undef LAUNCH_SET
}
