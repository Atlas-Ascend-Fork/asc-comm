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
 * \brief Asccomm SIMT URMA功能样例Kernel侧实现，覆盖asccomm_write_nbi/asccomm_write_value_nbi/
 *        asccomm_write_with_notify_nbi/asccomm_atomic_faa_nbi/asccomm_atomic_cas_nbi
 *
 * 每个kernel都由单个lane发起：一个channel必须由一个lane驱动，延迟WQE要等后续
 * commit=true的提交发布覆盖整批的PI，而该PI无法区分是哪个lane写了哪个basic block。
 */

#include <cstdio>

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "asccomm/asccomm_simt.h"

// Mark this as the kernel TU for simt_urma_common.h: its shared helpers (slot_value, is_write_mode)
// must be __aicore__ device functions here because the __simt_vf__ kernels call them, but they
// must stay plain host functions in the .asc host TU, so the guard keys on this marker plus the
// __aicore__ macro provided by the kernel headers included above.
#define SIMT_URMA_KERNEL_TU 1
#include "simt_urma_common.h"

namespace {
using namespace simt_urma;

// Every kernel below runs on lane 0 only. Returning from the other lanes here keeps the
// single-lane-per-channel contract in one place instead of repeating the guard per kernel.
__simt_callee__ inline bool is_driving_lane() { return threadIdx.x == 0U && threadIdx.y == 0U && threadIdx.z == 0U; }

// ---------------------------------------------------------------------------------------------
// Write family

__simt_vf__ __launch_bounds__(1024) inline void simt_write_single_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local_addr = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_addr = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < WRITE_SLOT_COUNT; ++i) {
        (void)asccomm.asccomm_write_nbi<true>(
            channel, remote_addr + i * SLOT_BYTES, local_addr + i * SLOT_BYTES, SLOT_BYTES);
    }
    (void)asccomm.asccomm_drain(channel);
}

// 最后一次立即提交的门铃发布覆盖整批的 PI，把前面的延迟 WQE 一并带出。单 lane 不会观察到自己
// 写了一半的 WQE，因此不需要 barrier。
__simt_vf__ __launch_bounds__(1024) inline void simt_write_batch_last_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local_addr = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote_addr = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < WRITE_SLOT_COUNT - 1U; ++i) {
        (void)asccomm.asccomm_write_nbi<false>(
            channel, remote_addr + i * SLOT_BYTES, local_addr + i * SLOT_BYTES, SLOT_BYTES);
    }
    (void)asccomm.asccomm_write_nbi(
        channel, remote_addr + (WRITE_SLOT_COUNT - 1U) * SLOT_BYTES, local_addr + (WRITE_SLOT_COUNT - 1U) * SLOT_BYTES,
        SLOT_BYTES);
    (void)asccomm.asccomm_drain(channel);
}

// 值内联在 WQE 中，因此不传本地地址、完全不读发送缓冲区。写入值仍取 slot_value(i)，
// 使接收侧对两个 write 系列使用同一套校验。
__simt_vf__ __launch_bounds__(1024) inline void simt_write_value_single_vf(uint64_t channel, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* remote_addr = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < WRITE_SLOT_COUNT; ++i) {
        (void)asccomm.asccomm_write_value_nbi<uint64_t, true>(channel, remote_addr + i * SLOT_BYTES, slot_value(i));
    }
    (void)asccomm.asccomm_drain(channel);
}

__simt_vf__ __launch_bounds__(1024) inline void simt_write_value_batch_last_vf(uint64_t channel, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* remote_addr = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    if (asccomm.asccomm_init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < WRITE_SLOT_COUNT - 1U; ++i) {
        (void)asccomm.asccomm_write_value_nbi<uint64_t, false>(channel, remote_addr + i * SLOT_BYTES, slot_value(i));
    }
    (void)asccomm.asccomm_write_value_nbi<uint64_t, true>(
        channel, remote_addr + (WRITE_SLOT_COUNT - 1U) * SLOT_BYTES, slot_value(WRITE_SLOT_COUNT - 1U));
    (void)asccomm.asccomm_drain(channel);
}

// ---------------------------------------------------------------------------------------------
// Notify/atomic family

__simt_vf__ __launch_bounds__(1024) inline void simt_notify_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    (void)asccomm.asccomm_write_with_notify_nbi(
        channel, remote + NOTIFY_DATA_SLOT * SLOT_BYTES, local, SLOT_BYTES, remote + NOTIFY_SIGNAL_SLOT * SLOT_BYTES,
        NOTIFY_SIGNAL);
    reinterpret_cast<__gm__ uint64_t*>(local)[NOTIFY_DRAIN_SLOT] =
        static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

// 每个 2BB Notify WQE 各敲一次 DWQE 门铃，整批发完才 Drain。
__simt_vf__ __launch_bounds__(1024) inline void simt_notify_immediate_repeat_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    for (uint32_t i = 0; i < NOTIFY_IMMEDIATE_REPEAT_COUNT; ++i) {
        (void)asccomm.asccomm_write_with_notify_nbi<true>(
            channel, remote + NOTIFY_DATA_SLOT * SLOT_BYTES, local, SLOT_BYTES,
            remote + NOTIFY_SIGNAL_SLOT * SLOT_BYTES, NOTIFY_SIGNAL);
    }
    reinterpret_cast<__gm__ uint64_t*>(local)[NOTIFY_DRAIN_SLOT] =
        static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

__simt_vf__ __launch_bounds__(1024) inline void simt_faa_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    (void)asccomm.asccomm_atomic_faa_nbi<uint64_t>(
        channel, remote + FAA_TARGET_SLOT * SLOT_BYTES, local + FAA_FETCH_SLOT * SLOT_BYTES, FAA_ADD);
    reinterpret_cast<__gm__ uint64_t*>(local)[FAA_DRAIN_SLOT] = static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

__simt_vf__ __launch_bounds__(1024) inline void simt_cas_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    (void)asccomm.asccomm_atomic_cas_nbi<uint64_t>(
        channel, remote + CAS_TARGET_SLOT * SLOT_BYTES, local + CAS_FETCH_SLOT * SLOT_BYTES, CAS_INITIAL, CAS_SWAP);
    reinterpret_cast<__gm__ uint64_t*>(local)[CAS_DRAIN_SLOT] = static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

// 逐个操作等 CQE 返回后再复用 DWQE 窗口。Drain 返回值写进发送侧空闲 slot，便于定位失败环节。
__simt_vf__ __launch_bounds__(1024) inline void simt_notify_atomic_single_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    (void)asccomm.asccomm_write_with_notify_nbi(
        channel, remote + NOTIFY_DATA_SLOT * SLOT_BYTES, local, SLOT_BYTES, remote + NOTIFY_SIGNAL_SLOT * SLOT_BYTES,
        NOTIFY_SIGNAL);
    reinterpret_cast<__gm__ uint64_t*>(local)[NOTIFY_DRAIN_SLOT] =
        static_cast<uint64_t>(asccomm.asccomm_drain(channel));
    (void)asccomm.asccomm_atomic_faa_nbi<uint64_t>(
        channel, remote + FAA_TARGET_SLOT * SLOT_BYTES, local + FAA_FETCH_SLOT * SLOT_BYTES, FAA_ADD);
    reinterpret_cast<__gm__ uint64_t*>(local)[FAA_DRAIN_SLOT] = static_cast<uint64_t>(asccomm.asccomm_drain(channel));
    (void)asccomm.asccomm_atomic_cas_nbi<uint64_t>(
        channel, remote + CAS_TARGET_SLOT * SLOT_BYTES, local + CAS_FETCH_SLOT * SLOT_BYTES, CAS_INITIAL, CAS_SWAP);
    reinterpret_cast<__gm__ uint64_t*>(local)[CAS_DRAIN_SLOT] = static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

// One lane defers Notify and FAA, then CAS with commit=true publishes the batch.
__simt_vf__ __launch_bounds__(1024) inline void simt_notify_atomic_batch_last_vf(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    if (!is_driving_lane()) {
        return;
    }
    __gm__ uint8_t* local = asc::simt::asccomm_local_buffer_addr(channel, send_buf_idx);
    __gm__ uint8_t* remote = asc::simt::asccomm_remote_buffer_addr(channel, recv_buf_idx);

    asc::simt::asccomm<> asccomm;
    (void)asccomm.asccomm_init(nullptr, 0);
    (void)asccomm.asccomm_write_with_notify_nbi<false>(
        channel, remote + NOTIFY_DATA_SLOT * SLOT_BYTES, local, SLOT_BYTES, remote + NOTIFY_SIGNAL_SLOT * SLOT_BYTES,
        NOTIFY_SIGNAL);
    (void)asccomm.asccomm_atomic_faa_nbi<uint64_t, false>(
        channel, remote + FAA_TARGET_SLOT * SLOT_BYTES, local + FAA_FETCH_SLOT * SLOT_BYTES, FAA_ADD);
    (void)asccomm.asccomm_atomic_cas_nbi<uint64_t>(
        channel, remote + CAS_TARGET_SLOT * SLOT_BYTES, local + CAS_FETCH_SLOT * SLOT_BYTES, CAS_INITIAL, CAS_SWAP);
    reinterpret_cast<__gm__ uint64_t*>(local)[NOTIFY_DRAIN_SLOT] =
        static_cast<uint64_t>(asccomm.asccomm_drain(channel));
}

} // namespace

// 写-立即提交：逐笔asccomm_write_nbi并提交。
extern "C" __global__ __vector__ void simt_write_single(uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_write_single_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 写-批量最后提交：延迟WQE由最后一笔立即提交带出。
extern "C" __global__ __vector__ void simt_write_batch_last(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_write_batch_last_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 写值-立即提交：8字节值内联在WQE中。
extern "C" __global__ __vector__ void simt_write_value_single(uint64_t channel, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_write_value_single_vf>(dim3(1), channel, recv_buf_idx);
}

// 写值-批量最后提交。
extern "C" __global__ __vector__ void simt_write_value_batch_last(uint64_t channel, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_write_value_batch_last_vf>(dim3(1), channel, recv_buf_idx);
}

// 写通知：写数据后向对端notify地址写通知值。
extern "C" __global__ __vector__ void simt_notify(uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_notify_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 远端原子累加。
extern "C" __global__ __vector__ void simt_faa(uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_faa_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 远端原子比较交换。
extern "C" __global__ __vector__ void simt_cas(uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_cas_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 重复投递同一立即通知。
extern "C" __global__ __vector__ void simt_notify_immediate_repeat(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_notify_immediate_repeat_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 通知+原子串行：逐操作等CQE后再发起下一个。
extern "C" __global__ __vector__ void simt_notify_atomic_single(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_notify_atomic_single_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 通知+原子批量最后提交。
extern "C" __global__ __vector__ void simt_notify_atomic_batch_last(
    uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx)
{
    asc_vf_call<simt_notify_atomic_batch_last_vf>(dim3(1), channel, send_buf_idx, recv_buf_idx);
}

// 按模式分发下发对应kernel。
void launch_simt_urma(void* stream, uint64_t channel, uint32_t send_buf_idx, uint32_t recv_buf_idx, uint32_t mode)
{
    using namespace simt_urma;
    switch (mode) {
        case MODE_WRITE_SINGLE:
            simt_write_single<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_WRITE_BATCH_LAST:
            simt_write_batch_last<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_WRITE_VALUE_SINGLE:
            simt_write_value_single<<<1, 0, stream>>>(channel, recv_buf_idx);
            break;
        case MODE_WRITE_VALUE_BATCH_LAST:
            simt_write_value_batch_last<<<1, 0, stream>>>(channel, recv_buf_idx);
            break;
        case MODE_NOTIFY:
            simt_notify<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_FAA:
            simt_faa<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_CAS:
            simt_cas<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_NOTIFY_IMMEDIATE_REPEAT:
            simt_notify_immediate_repeat<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_NOTIFY_ATOMIC_BATCH_LAST:
            simt_notify_atomic_batch_last<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        case MODE_NOTIFY_ATOMIC_SINGLE:
            simt_notify_atomic_single<<<1, 0, stream>>>(channel, send_buf_idx, recv_buf_idx);
            break;
        default:
            std::printf("[simt_urma] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
