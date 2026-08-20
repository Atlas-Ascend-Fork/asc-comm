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
 * \file ain_impl.h
 * \brief ain implementation
 */

#if !defined(ASCENDC_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define ASCENDC_INCLUDE_INTERNAL_HEADERS
#define UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H
#endif

#ifndef IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H
#define IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#include "ain_impl_def.h"

namespace AscendC {

AIN_DEVICE channel_handle get_channel_handle(const __gm__ HcommTeam* team, const uint32_t peer, const uint32_t index)
{
    uint64_t channel_index = index;
    for (uint32_t i = 0; i < peer; ++i) {
        channel_index += team->channelNumPerMember[i];
    }
    channel_handle channel = team->channelsBaseAddr + channel_index * sizeof(channel_entity);
    return channel;
}

AIN_DEVICE GM_ADDR
get_comm_mem_ptr(const __gm__ HcommTeam* team, const asccomm_window_handle window, uint32_t member_id, uint64_t offset)
{
    auto win = reinterpret_cast<__gm__ HcommWindow*>(reinterpret_cast<uint64_t>(window));
    CommMem mem = win->mems[member_id];
    // volatile: Prevent complier folding optimization from causing address space cast failure.
    volatile uint64_t mem_handle = reinterpret_cast<uint64_t>(mem.addr) + offset;
    return reinterpret_cast<GM_ADDR>(mem_handle);
}

// ===================== ain =====================

template <unsigned comm_engine_mask>
AIN_DEVICE ain<comm_engine_mask>::ain(uint32_t context_index) : context_index_(context_index)
{}

template <unsigned comm_engine_mask>
AIN_DEVICE ain<comm_engine_mask>::~ain()
{}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::flush(asccomm_team_handle team)
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint32_t rank_size = asccomm_team->memberNum;
    uint32_t my_rank = asccomm_team->selfMemberId;

    for (uint32_t i = 0; i < rank_size; i++) {
        if (i == my_rank) {
            continue;
        }
        channel_handle channel = get_channel_handle(asccomm_team, i, context_index_);
        asccomm_.drain(channel);
    }
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::flush_async(asccomm_team_handle team, uint32_t peer, channel_handle* out_channel_handle)
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    *out_channel_handle = get_channel_handle(asccomm_team, peer, context_index_);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::wait(channel_handle& channel_handle)
{
    asccomm_.drain(channel_handle);
}

template <unsigned comm_engine_mask>
template <typename remote_action_t, typename descriptor_ubuf_t, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::put(
    asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, asccomm_window_handle src_win,
    uint64_t src_offset, uint64_t bytes, remote_action_t remote_action, const descriptor_ubuf_t& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf_t, ain_descriptor_ubuf_none>::value,
        "put requires a valid descriptor_ubuf_t; ain_descriptor_ubuf_none is not allowed");
    asccomm_.init(ubuf.addr, ubuf.bytes);

    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remote_addr = get_comm_mem_ptr(asccomm_team, dst_win, peer, dst_offset);
    auto local_addr = get_comm_mem_ptr(asccomm_team, src_win, asccomm_team->selfMemberId, src_offset);

    channel_handle channel = get_channel_handle(asccomm_team, peer, context_index_);

    if constexpr (commit_flags == ain_commit_delayed) {
        asccomm_.template write_nbi<false, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, local_addr, bytes);
    } else {
        asccomm_.template write_nbi<true, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, local_addr, bytes);
    }

    if constexpr (IsSameType<remote_action_t, ain_signal_inc>::value || IsSameType<remote_action_t, ain_signal_add>::value) {
        this->template signal<remote_action_t, descriptor_ubuf_t, commit_flags, config>(team, peer, remote_action, ubuf);
    }
}

template <unsigned comm_engine_mask>
template <typename t, typename remote_action_t, typename descriptor_ubuf_t, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::put_value(
    asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, t value,
    remote_action_t remote_action, const descriptor_ubuf_t& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf_t, ain_descriptor_ubuf_none>::value,
        "put_value requires a valid descriptor_ubuf_t; ain_descriptor_ubuf_none is not allowed");
    asccomm_.init(ubuf.addr, ubuf.bytes);

    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remote_addr = get_comm_mem_ptr(asccomm_team, dst_win, peer, dst_offset);

    channel_handle channel = get_channel_handle(asccomm_team, peer, context_index_);

    if constexpr (commit_flags == ain_commit_delayed) {
        asccomm_.template write_value_nbi<t, false, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, value);
    } else {
        asccomm_.template write_value_nbi<t, true, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, value);
    }

    if constexpr (IsSameType<remote_action_t, ain_signal_inc>::value || IsSameType<remote_action_t, ain_signal_add>::value) {
        this->template signal<remote_action_t, descriptor_ubuf_t, commit_flags>(team, peer, remote_action, ubuf);
    }
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf_t, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::get(
    asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, asccomm_window_handle src_win,
    uint64_t src_offset, uint64_t bytes, const descriptor_ubuf_t& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf_t, ain_descriptor_ubuf_none>::value,
        "get requires a valid descriptor_ubuf_t; ain_descriptor_ubuf_none is not allowed");
    asccomm_.init(ubuf.addr, ubuf.bytes);

    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto local_addr = get_comm_mem_ptr(asccomm_team, dst_win, asccomm_team->selfMemberId, dst_offset);
    auto remote_addr = get_comm_mem_ptr(asccomm_team, src_win, peer, src_offset);

    channel_handle channel = get_channel_handle(asccomm_team, peer, context_index_);

    if constexpr (commit_flags == ain_commit_delayed) {
        asccomm_.template read_nbi<false, PIPE_S, PIPE_MTE3, config>(channel, local_addr, remote_addr, bytes);
    } else {
        asccomm_.template read_nbi<true, PIPE_S, PIPE_MTE3, config>(channel, local_addr, remote_addr, bytes);
    }
}

template <typename t>
AIN_DEVICE uint64_t get_signal_op_arg(t action)
{
    if constexpr (IsSameType<t, ain_signal_inc>::value) {
        return 1;
    } else {
        return action.value;
    }
}

template <unsigned comm_engine_mask>
template <typename remote_action_t, typename descriptor_ubuf_t, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::signal(
    asccomm_team_handle team, uint32_t peer, remote_action_t action, const descriptor_ubuf_t& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf_t, ain_descriptor_ubuf_none>::value,
        "signal requires a valid descriptor_ubuf_t; ain_descriptor_ubuf_none is not allowed");
    asccomm_.init(ubuf.addr, ubuf.bytes);

    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    channel_handle channel = get_channel_handle(asccomm_team, peer, context_index_);

    uint64_t context_base_id = asccomm_team->memberNum * context_index_;

    uint64_t signal_op_arg = get_signal_op_arg(action);

    auto remote_addr = get_comm_mem_ptr(asccomm_team, action.signal_window, peer, action.signal_offset);

    // amo_addr: the address to store the fetched old value required by atomic_faa.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amo_addr.
    auto local_base = asccomm_team->syncMem.shadowMem.addr;
    uint64_t amo_offset = (context_base_id + asccomm_team->selfMemberId) * sizeof(uint64_t);
    auto amo_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(local_base) + amo_offset);

    if constexpr (commit_flags == ain_commit_delayed) {
        asccomm_.template atomic_faa<uint64_t, false, PIPE_S, PIPE_MTE3, config>(
            channel, remote_addr, amo_addr, signal_op_arg);
    } else {
        asccomm_.template atomic_faa<uint64_t, true, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, amo_addr, signal_op_arg);
    }
}

template <unsigned comm_engine_mask>
AIN_DEVICE uint64_t ain<comm_engine_mask>::read_signal(
    asccomm_team_handle team, asccomm_window_handle signal_window, size_t signal_offset, uint32_t bits,
    ain_memory_order order) const
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signal_addr = reinterpret_cast<__gm__ uint64_t*>(
        get_comm_mem_ptr(asccomm_team, signal_window, asccomm_team->selfMemberId, signal_offset));
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signal_addr, 0);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::wait_signal(
    asccomm_team_handle team, asccomm_window_handle signal_window, size_t signal_offset, uint64_t least, uint32_t bits,
    ain_memory_order order) const
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signal_addr = reinterpret_cast<__gm__ uint64_t*>(
        get_comm_mem_ptr(asccomm_team, signal_window, asccomm_team->selfMemberId, signal_offset));
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    while (true) {
        if (least <= (ld_dev(signal_addr, 0) & mask)) {
            break;
        }
    }
}

// ===================== ain_barrier_session =====================

template <unsigned comm_engine_mask>
AIN_DEVICE ain_barrier_session<comm_engine_mask>::ain_barrier_session(
    ain<comm_engine_mask>* ain, asccomm_team_handle team, uint32_t index)
{
    barrier_impl_.ain = ain;
    barrier_impl_.team = team;
    barrier_impl_.index = index;
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    barrier_impl_.signal_base_id = index * asccomm_team->memberNum;
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf_t>
AIN_DEVICE void ain_barrier_session<comm_engine_mask>::sync(ain_memory_order order, const descriptor_ubuf_t& ubuf)
{
    barrier_impl_.template sync_impl<false>(order, 0, ubuf);
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf_t>
AIN_DEVICE int32_t
ain_barrier_session<comm_engine_mask>::sync(ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf_t& ubuf)
{
    return barrier_impl_.template sync_impl<true>(order, timeout_cycles, ubuf);
}

template <unsigned comm_engine_mask>
template <bool enable_timeout, typename descriptor_ubuf_t>
AIN_DEVICE int32_t ain_barrier_session_impl<comm_engine_mask>::sync_impl(
    ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf_t& ubuf)
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(this->team));

    // Phase 1: signal all peers first.
    for (int32_t i = 0; i + 1 < asccomm_team->memberNum; ++i) {
        int peer = 1 + asccomm_team->selfMemberId + i;
        if (asccomm_team->memberNum <= peer) {
            peer -= asccomm_team->memberNum;
        }
        this->signal(this->team, peer, ubuf);
    }

    // Phase 2: wait for all peer signals.
    for (int32_t i = 0; i + 1 < asccomm_team->memberNum; ++i) {
        int peer = 1 + asccomm_team->selfMemberId + i;
        if (asccomm_team->memberNum <= peer) {
            peer -= asccomm_team->memberNum;
        }

        size_t signal_offset = (this->signal_base_id + peer) * sizeof(uint64_t);
        auto signal_shadow = asccomm_team->syncMem.shadowMem.addr;
        __gm__ uint64_t* shadow_ptr =
            reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signal_shadow) + signal_offset);
        uint64_t wait_val = ++(*shadow_ptr);

        if constexpr (enable_timeout) {
            uint64_t retry = 0;
            while (true) {
                uint64_t got = this->read_signal(this->team, peer, 64);
                if (got >= wait_val) {
                    break;
                }
                if (retry++ >= timeout_cycles) {
                    return -1;
                }
            }
        } else {
            this->wait_signal(this->team, peer, wait_val, 64);
        }
    }

    return 0;
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf_t>
AIN_DEVICE void ain_barrier_session_impl<comm_engine_mask>::signal(
    asccomm_team_handle team, uint32_t peer, const descriptor_ubuf_t& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf_t, ain_descriptor_ubuf_none>::value,
        "signal requires a valid descriptor_ubuf_t; ain_descriptor_ubuf_none is not allowed");
    asccomm_.init(ubuf.addr, ubuf.bytes);

    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    channel_handle channel = get_channel_handle(asccomm_team, peer, index);

    uint64_t signal_offset = (this->signal_base_id + asccomm_team->selfMemberId) * sizeof(uint64_t);
    auto signal_base = asccomm_team->syncMem.remoteMems[peer].addr;
    auto remote_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);

    // amo_addr: the address to store the fetched old value required by atomic_faa.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amo_addr.
    auto local_base = asccomm_team->syncMem.shadowMem.addr;
    auto amo_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(local_base) + signal_offset);

    asccomm_.template atomic_faa<>(channel, remote_addr, amo_addr, (uint64_t)1);
}

template <unsigned comm_engine_mask>
AIN_DEVICE uint64_t ain_barrier_session_impl<comm_engine_mask>::read_signal(
    asccomm_team_handle team, uint32_t signal_id, uint32_t bits, ain_memory_order order) const
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signal_offset = (signal_base_id + signal_id) * sizeof(uint64_t);
    auto signal_base = asccomm_team->syncMem.remoteMems[asccomm_team->selfMemberId].addr;
    __gm__ uint64_t* signal_addr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signal_addr, 0);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain_barrier_session_impl<comm_engine_mask>::wait_signal(
    asccomm_team_handle team, uint32_t signal_id, uint64_t least, uint32_t bits, ain_memory_order order) const
{
    auto asccomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signal_offset = (signal_base_id + signal_id) * sizeof(uint64_t);
    auto signal_base = asccomm_team->syncMem.remoteMems[asccomm_team->selfMemberId].addr;
    __gm__ uint64_t* signal_addr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    while (true) {
        if (least <= (ld_dev(signal_addr, 0) & mask)) {
            break;
        }
    }
}

} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#if defined(UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H)
#undef ASCENDC_INCLUDE_INTERNAL_HEADERS
#undef UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H
#endif
