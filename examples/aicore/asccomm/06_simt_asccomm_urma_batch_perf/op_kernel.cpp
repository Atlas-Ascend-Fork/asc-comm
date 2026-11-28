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
using TileScratch = cooperative_groups::block_tile_memory<MAX_GROUP_LANES>;

template <uint32_t api, typename asccomm_t, typename... Groups>
// 调用相应的批量接口，为当前线程添加一个任务。
__simt_callee__ inline int32_t append_one_wqe(
    asccomm_t& asccomm, asc::simt::ubc_ctp_batch_handle& batch, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payload_bytes, uint32_t slot_bytes, uint32_t slot, const Groups&... groups)
{
    const uint64_t offset = static_cast<uint64_t>(slot) * slot_bytes;
    if constexpr (api == API_WRITE) {
        return asccomm.write_nbi(batch, remote + offset, local + offset, payload_bytes, groups...);
    } else if constexpr (api == API_READ) {
        return asccomm.read_nbi(batch, local + offset, remote + offset, payload_bytes, groups...);
    } else if constexpr (api == API_NOTIFY) {
        return asccomm.write_with_notify_nbi(
            batch, remote + offset, local + offset, payload_bytes, remote + offset + slot_bytes - sizeof(uint64_t),
            NOTIFY_VALUE, groups...);
    } else if constexpr (api == API_WRITE_VALUE) {
        return asccomm.template write_value_nbi<uint64_t>(batch, remote + offset, INLINE_VALUE, groups...);
    } else if constexpr (api == API_FAA) {
        return asccomm.template atomic_faa_nbi<uint64_t>(batch, remote + offset, local + offset, FAA_ADD, groups...);
    } else {
        return asccomm.template atomic_cas_nbi<uint64_t>(
            batch, remote + offset, local + offset, CAS_INITIAL, CAS_SWAP, groups...);
    }
}

template <uint32_t api>
// 单lane批量：每批仅提交一个任务请求，逐批添加并batch_commit，批次间复用同一句柄。
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
        status = asccomm.batch_commit(batch);
        if (status != 0) {
            return status;
        }
        ++publish_count;
    }
    return 0;
}

template <uint32_t api, typename asccomm_t, typename Group>
// 每轮由组内所有lane各添加一个任务，组内线程共同提交批次。
__simt_callee__ inline int32_t submit_group_batches(
    asccomm_t& asccomm, const Group& group, asc::simt::ubc_ctp_batch_handle& batch, __gm__ uint8_t* remote,
    __gm__ uint8_t* local, uint32_t payload_bytes, uint32_t slot_bytes, uint32_t count, uint32_t& publish_count,
    __ubuf__ uint32_t* drain_status)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    publish_count = 0U;
    bool pending = false;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        const uint32_t operation = begin + rank;
        int32_t append_status = append_one_wqe<api>(
            asccomm, batch, remote, local, payload_bytes, slot_bytes, operation % SLOT_COUNT, group);
        if (append_status != 0 && pending) {
            // 本轮添加失败时，先提交已成功添加的轮次，再等待完成以释放队列空间。
            const int32_t commit_status = asccomm.batch_commit(batch, group);
            if (commit_status != 0) {
                return commit_status;
            }
            ++publish_count;
            if (rank == 0U) {
                *drain_status = static_cast<uint32_t>(asccomm.drain(batch));
            }
            // 组内线程根据同一等待结果决定是否重试失败的轮次。
            group.sync();
            append_status = static_cast<int32_t>(*drain_status);
            if (append_status == 0) {
                append_status = append_one_wqe<api>(
                    asccomm, batch, remote, local, payload_bytes, slot_bytes, operation % SLOT_COUNT, group);
            }
        }
        if (append_status != 0) {
            return append_status;
        }
        pending = true;
    }
    if (pending) {
        const int32_t status = asccomm.batch_commit(batch, group);
        publish_count += status == 0 ? 1U : 0U;
        return status;
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
    __gm__ uint8_t* local = asc::simt::get_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::get_remote_buffer_addr(channel, recv_buf_idx);
    asc::simt::asccomm<> asccomm;
    if (asccomm.init(nullptr, 0) != 0) {
        timing[STATUS_INDEX] = static_cast<uint64_t>(-1);
        return;
    }

    uint32_t publish_count = 0U;
    int32_t status = 0;
    // 单lane批量：每批仅提交一个任务请求，逐批添加并batch_commit，批次间复用同一句柄；
    // 批量工作区至少256字节，由asccomm_make_batch_handle绑定（不经过asccomm_init）。
    asc::simt::ubc_ctp_batch_handle batch = asccomm.make_batch_handle(channel, batch_buffer, BATCH_BUFFER_BYTES);
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
        status = asccomm.drain(batch);
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
    status = asccomm.drain(batch);
    uint64_t completion_end = clock();
    timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
    timing[COMPLETION_CYCLES_INDEX] = completion_end - begin;
    timing[STATUS_INDEX] = static_cast<uint64_t>(status);
    timing[COMPLETED_INDEX] = status == 0 ? iterations : 0U;
    timing[PUBLISH_COUNT_INDEX] = publish_count;
}

template <uint32_t api, typename Group>
__simt_callee__ inline void run_group_batch_perf(
    cooperative_groups::thread_block& block, const Group& group, uint32_t channel_idx,
    const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer,
    __ubuf__ uint32_t* drain_status)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint64_t channel = channels[channel_idx].handle;
    const uint64_t warmup_offset = static_cast<uint64_t>(slot_bytes) * SLOT_COUNT;
    const uint64_t buffer_offset = channel_idx * 2U * warmup_offset;
    __gm__ uint8_t* local = asc::simt::get_local_buffer_addr(channel, send_buf_idx) + buffer_offset;
    __gm__ uint8_t* remote =
        asc::simt::get_remote_buffer_addr(channel, channels[channel_idx].remote_recv_idx) + buffer_offset;
    timing += channel_idx * GROUP_TIMING_WORDS;
    batch_buffer += channel_idx * BATCH_BUFFER_BYTES;
    drain_status += channel_idx;
    asc::simt::asccomm<> asccomm;
    int32_t status = asccomm.init(nullptr, 0U);
    auto batch = asccomm.make_batch_handle(channel, batch_buffer, BATCH_BUFFER_BYTES, group);
    if (batch.context == nullptr) {
        status = asc::HCOMM_FAILED;
    }
    uint32_t publish_count = 0U;
    if (status == 0) {
        status = submit_group_batches<api>(
            asccomm, group, batch, remote + warmup_offset, local + warmup_offset, payload_bytes, slot_bytes, warmup,
            publish_count, drain_status);
    }
    if (status == 0 && warmup != 0U) {
        // 由组内rank为0的线程等待预热任务完成。
        if (rank == 0U) {
            status = asccomm.drain(batch);
            drain_status[0] = static_cast<uint32_t>(status);
        }
        group.sync();
        status = static_cast<int32_t>(drain_status[0]);
    }
    // 各通道在此同步计时起点，预热失败的组也必须参与同步。
    block.sync();
    uint64_t begin = 0U;
    uint64_t issue_end = 0U;
    const bool warmed_up = status == 0;
    if (warmed_up) {
        begin = rank == 0U ? clock() : 0U;
        status = submit_group_batches<api>(
            asccomm, group, batch, remote, local, payload_bytes, slot_bytes, iterations, publish_count, drain_status);
        issue_end = rank == 0U ? clock() : 0U;
    }

    // 各组结束提交后再等待剩余任务完成，避免影响其他通道的提交计时。
    block.sync();
    if (rank != 0U) {
        return;
    }
    if (status == 0) {
        status = asccomm.drain(batch);
    }
    if (warmed_up) {
        timing[ISSUE_START_INDEX] = begin;
        timing[ISSUE_CYCLES_INDEX] = issue_end - begin;
        timing[COMPLETION_CYCLES_INDEX] = clock() - begin;
    }
    timing[STATUS_INDEX] = static_cast<uint64_t>(status);
    timing[COMPLETED_INDEX] = status == 0 ? iterations : 0U;
    timing[PUBLISH_COUNT_INDEX] = publish_count;
}

template <uint32_t api, uint32_t group_capacity>
__simt_vf__ __launch_bounds__(group_capacity) inline void simt_group_batch_perf_vf(
    const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer,
    __ubuf__ uint32_t* drain_status)
{
    auto block = cooperative_groups::this_thread_block();
    run_group_batch_perf<api>(
        block, block, 0U, channels, send_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, batch_buffer,
        drain_status);
}

template <typename Tile>
struct CrossWarpGroup {
    Tile tile;

    __simt_callee__ inline auto thread_rank() const { return tile.thread_rank(); }
    __simt_callee__ inline auto size() const { return tile.size(); }
    // CANN 9.2.2 BiSheng requires out-of-line tile sync in this VF.
    __simt_callee__ __attribute__((noinline)) void sync() const { tile.sync(); }
};

template <uint32_t group_lanes, uint32_t thread_capacity>
__simt_vf__ __launch_bounds__(thread_capacity) inline void simt_multi_channel_write_group_batch_perf_vf(
    const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ uint64_t* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer,
    __ubuf__ uint32_t* drain_status, __ubuf__ TileScratch* tile_scratch)
{
    auto block = cooperative_groups::this_thread_block();
    if constexpr (group_lanes > 32U) {
        block = cooperative_groups::this_thread_block(*tile_scratch);
    }
    auto group = cooperative_groups::tiled_partition<group_lanes>(block);
    if constexpr (group_lanes > 32U) {
        run_group_batch_perf<API_WRITE>(
            block, CrossWarpGroup<decltype(group)>{group}, group.meta_group_rank(), channels, send_buf_idx, timing,
            payload_bytes, slot_bytes, iterations, warmup, batch_buffer, drain_status);
    } else {
        run_group_batch_perf<API_WRITE>(
            block, group, group.meta_group_rank(), channels, send_buf_idx, timing, payload_bytes, slot_bytes,
            iterations, warmup, batch_buffer, drain_status);
    }
}

template <uint32_t group_lanes, uint32_t thread_capacity = 2U * group_lanes>
__aicore__ inline void call_multi_channel_write_vf(
    dim3 threads, const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ uint64_t* timing,
    uint32_t payload_bytes, uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, __ubuf__ uint8_t* batch_buffer,
    __ubuf__ uint32_t* drain_status, __ubuf__ TileScratch* tile_scratch)
{
    // The multi-channel path starts at two groups. Instantiate only capacities reachable by valid launches.
    if constexpr (thread_capacity < MAX_GROUP_LANES && thread_capacity < MAX_CHANNELS * group_lanes) {
        if (threads.x > thread_capacity) {
            call_multi_channel_write_vf<group_lanes, 2U * thread_capacity>(
                threads, channels, send_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, batch_buffer,
                drain_status, tile_scratch);
            return;
        }
    }
    asc_vf_call<simt_multi_channel_write_group_batch_perf_vf<group_lanes, thread_capacity>>(
        threads, channels, send_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, batch_buffer,
        drain_status, tile_scratch);
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

#define CALL_GROUP_BATCH_PERF_VF(api, capacity)                                                               \
    asc_vf_call<simt_group_batch_perf_vf<api, capacity>>(                                                     \
        dim3(group_lanes), channels, send_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing), payload_bytes, \
        slot_bytes, iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batch_buffer), drain_status)

#define DEFINE_GROUP_BATCH_PERF_KERNEL(kernel_name, api)                                                          \
    extern "C" __global__ __vector__ void kernel_name(                                                            \
        const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ void* timing, uint32_t payload_bytes, \
        uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, uint32_t group_lanes)                          \
    {                                                                                                             \
        alignas(128) __ubuf__ uint64_t batch_buffer[BATCH_BUFFER_BYTES / sizeof(uint64_t)];                       \
        alignas(16) __ubuf__ uint32_t drain_status[1U];                                                           \
        if (group_lanes <= 16U) {                                                                                 \
            CALL_GROUP_BATCH_PERF_VF(api, 16U);                                                                   \
        } else if (group_lanes <= 32U) {                                                                          \
            CALL_GROUP_BATCH_PERF_VF(api, 32U);                                                                   \
        } else if (group_lanes <= 64U) {                                                                          \
            CALL_GROUP_BATCH_PERF_VF(api, 64U);                                                                   \
        } else if (group_lanes <= 128U) {                                                                         \
            CALL_GROUP_BATCH_PERF_VF(api, 128U);                                                                  \
        } else if (group_lanes <= 256U) {                                                                         \
            CALL_GROUP_BATCH_PERF_VF(api, 256U);                                                                  \
        } else if (group_lanes <= 512U) {                                                                         \
            CALL_GROUP_BATCH_PERF_VF(api, 512U);                                                                  \
        } else if (group_lanes <= 1024U) {                                                                        \
            CALL_GROUP_BATCH_PERF_VF(api, 1024U);                                                                 \
        } else {                                                                                                  \
            CALL_GROUP_BATCH_PERF_VF(api, MAX_GROUP_LANES);                                                       \
        }                                                                                                         \
    }

DEFINE_GROUP_BATCH_PERF_KERNEL(simt_write_group_batch_perf, API_WRITE)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_read_group_batch_perf, API_READ)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_notify_group_batch_perf, API_NOTIFY)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_write_value_group_batch_perf, API_WRITE_VALUE)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_faa_group_batch_perf, API_FAA)
DEFINE_GROUP_BATCH_PERF_KERNEL(simt_cas_group_batch_perf, API_CAS)

#undef DEFINE_GROUP_BATCH_PERF_KERNEL
#undef CALL_GROUP_BATCH_PERF_VF

extern "C" __global__ __vector__ void simt_multi_channel_write_group_batch_perf(
    const __gm__ ChannelConfig* channels, uint32_t send_buf_idx, __gm__ void* timing, uint32_t payload_bytes,
    uint32_t slot_bytes, uint32_t iterations, uint32_t warmup, uint32_t group_lanes, uint32_t channel_count)
{
    alignas(128) __ubuf__ uint64_t batch_buffer[MAX_CHANNELS * BATCH_BUFFER_BYTES / sizeof(uint64_t)];
    alignas(16) __ubuf__ uint32_t drain_status[MAX_CHANNELS];
    alignas(TileScratch) __ubuf__ uint8_t tile_scratch[sizeof(TileScratch)];
    const dim3 threads(group_lanes * channel_count);
#define CALL_MULTI_CHANNEL_VF(lanes)                                                                                \
    case lanes:                                                                                                     \
        call_multi_channel_write_vf<lanes>(                                                                         \
            threads, channels, send_buf_idx, reinterpret_cast<__gm__ uint64_t*>(timing), payload_bytes, slot_bytes, \
            iterations, warmup, reinterpret_cast<__ubuf__ uint8_t*>(batch_buffer), drain_status,                    \
            reinterpret_cast<__ubuf__ TileScratch*>(tile_scratch));                                                 \
        break
    switch (group_lanes) {
        CALL_MULTI_CHANNEL_VF(16U);
        CALL_MULTI_CHANNEL_VF(32U);
        CALL_MULTI_CHANNEL_VF(64U);
        CALL_MULTI_CHANNEL_VF(128U);
        CALL_MULTI_CHANNEL_VF(256U);
        CALL_MULTI_CHANNEL_VF(512U);
        CALL_MULTI_CHANNEL_VF(1024U);
    }
#undef CALL_MULTI_CHANNEL_VF
}

void launch_simt_batch_perf(
    void* stream, const ChannelConfig& single_channel, const ChannelConfig* device_group_channels,
    uint32_t channel_count, uint32_t send_buf_idx, void* timing, uint32_t payload_bytes, uint32_t slot_bytes,
    uint32_t iterations, uint32_t warmup, uint32_t api, uint32_t group_lanes)
{
    if (device_group_channels != nullptr && api == API_WRITE && channel_count > 1U) {
        simt_multi_channel_write_group_batch_perf<<<1, 0, stream>>>(
            device_group_channels, send_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, group_lanes,
            channel_count);
        return;
    }
#define LAUNCH_SET(batch_kernel, group_kernel)                                                                        \
    if (device_group_channels != nullptr) {                                                                           \
        group_kernel<<<1, 0, stream>>>(                                                                               \
            device_group_channels, send_buf_idx, timing, payload_bytes, slot_bytes, iterations, warmup, group_lanes); \
    } else {                                                                                                          \
        batch_kernel<<<1, 0, stream>>>(                                                                               \
            single_channel.handle, send_buf_idx, single_channel.remote_recv_idx, timing, payload_bytes, slot_bytes,   \
            iterations, warmup);                                                                                      \
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
