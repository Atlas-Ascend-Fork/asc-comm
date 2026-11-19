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

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message("This is an internal ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__
#endif

#ifndef IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H
#define IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#include "ain_impl_def.h"

namespace asc {

AIN_DEVICE HcommMemHandle get_peer_pointer(uint32_t peer, HcclCommSymWindow window, size_t offset)
{
    auto hcomm_window = reinterpret_cast<__gm__ HcommWindow*>(reinterpret_cast<uint64_t>(window));
    size_t peer_offset = peer * hcomm_window->lsaWin.stride + offset;
    HcommMemHandle ptr =
        reinterpret_cast<HcommMemHandle>(reinterpret_cast<uintptr_t>(hcomm_window->lsaWin.baseVa) + peer_offset);
    return ptr;
}

AIN_DEVICE ChannelHandle get_channel_handle(const __gm__ HcommTeam* team, const uint32_t peer, const uint32_t index)
{
    const auto channel_cnt_accumulate_per_member = team->channelCntAccumulatePerMember;
    const auto channel_index_base = ReadGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(
        reinterpret_cast<uintptr_t>(channel_cnt_accumulate_per_member) + peer * sizeof(uint32_t)));
    uint64_t channel_index = index + channel_index_base;
    ChannelHandle channel = team->channelsBaseAddr + channel_index * sizeof(ChannelEntity);
    return channel;
}

AIN_DEVICE GM_ADDR
get_comm_mem_ptr(const __gm__ HcommTeam* team, const HcclCommSymWindow window, uint32_t member_id, uint64_t offset)
{
    auto hcomm_win = reinterpret_cast<__gm__ HcommWindow*>(reinterpret_cast<uint64_t>(window));
    auto remote_mem = reinterpret_cast<uint64_t*>(hcomm_win->netWin.baseRemoteMemAddr);
    auto world_team_accumulate_id = hcomm_win->netWin.worldTeamAccumulateId;
    uint64_t remote_mem_id = team->worldTeamIds[member_id] + world_team_accumulate_id[team->netLayer];
    uint64_t mem_handle = remote_mem[remote_mem_id] + offset;
    return reinterpret_cast<GM_ADDR>(mem_handle);
}

// ===================== ain =====================

template <unsigned comm_engine_mask>
AIN_DEVICE ain<comm_engine_mask>::ain(uint32_t context_index) : context_index(context_index)
{}

template <unsigned comm_engine_mask>
AIN_DEVICE ain<comm_engine_mask>::~ain()
{}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::flush(HcommTeamHandle team)
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint32_t rank_size = hcomm_team->memberNum;
    uint32_t my_rank = hcomm_team->selfMemberId;

    for (uint32_t i = 0; i < rank_size; i++) {
        if (i == my_rank) {
            continue;
        }
        ChannelHandle channel = get_channel_handle(hcomm_team, i, context_index);
        comm.asccomm_drain(channel);
    }
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::flush_async(
    HcommTeamHandle team, uint32_t peer, ChannelHandle* out_channel_handle)
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    *out_channel_handle = get_channel_handle(hcomm_team, peer, context_index);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::wait(ChannelHandle& channel_handle)
{
    comm.asccomm_drain(channel_handle);
}

template <unsigned comm_engine_mask>
template <typename remote_action, typename descriptor_ubuf, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::put(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, HcclCommSymWindow src_win,
    uint64_t src_offset, uint64_t bytes, remote_action action, const descriptor_ubuf& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf, ain_descriptor_ubuf_none>::value,
        "put requires a valid descriptor_ubuf; ain_descriptor_ubuf_none is not allowed");
    comm.asccomm_init(ubuf.addr, ubuf.bytes);

    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remote_addr = get_comm_mem_ptr(hcomm_team, dst_win, peer, dst_offset);
    auto local_addr = get_comm_mem_ptr(hcomm_team, src_win, hcomm_team->selfMemberId, src_offset);

    ChannelHandle channel = get_channel_handle(hcomm_team, peer, context_index);

    if constexpr (commit_flags == AIN_COMMIT_DELAYED) {
        comm.template asccomm_write_nbi<false, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, local_addr, bytes);
    } else {
        comm.template asccomm_write_nbi<true, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, local_addr, bytes);
    }

    if constexpr (
        IsSameType<remote_action, ain_signal_inc>::value || IsSameType<remote_action, ain_signal_add>::value) {
        this->template signal<remote_action, descriptor_ubuf, commit_flags, SIGNAL_WQE_CONFIG>(
            team, peer, action, ubuf);
    }
}

template <unsigned comm_engine_mask>
template <
    typename T, typename remote_action, typename descriptor_ubuf, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::put_value(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, T value, remote_action action,
    const descriptor_ubuf& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf, ain_descriptor_ubuf_none>::value,
        "putValue requires a valid descriptor_ubuf; ain_descriptor_ubuf_none is not allowed");
    comm.asccomm_init(ubuf.addr, ubuf.bytes);

    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remote_addr = get_comm_mem_ptr(hcomm_team, dst_win, peer, dst_offset);

    ChannelHandle channel = get_channel_handle(hcomm_team, peer, context_index);

    if constexpr (commit_flags == AIN_COMMIT_DELAYED) {
        comm.template asccomm_write_value_nbi<T, false, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, value);
    } else {
        comm.template asccomm_write_value_nbi<T, true, PIPE_S, PIPE_MTE3, config>(channel, remote_addr, value);
    }

    if constexpr (
        IsSameType<remote_action, ain_signal_inc>::value || IsSameType<remote_action, ain_signal_add>::value) {
        this->template signal<remote_action, descriptor_ubuf, commit_flags, SIGNAL_WQE_CONFIG>(
            team, peer, action, ubuf);
    }
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::get(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, HcclCommSymWindow src_win,
    uint64_t src_offset, uint64_t bytes, const descriptor_ubuf& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf, ain_descriptor_ubuf_none>::value,
        "get requires a valid descriptor_ubuf; ain_descriptor_ubuf_none is not allowed");
    comm.asccomm_init(ubuf.addr, ubuf.bytes);

    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto local_addr = get_comm_mem_ptr(hcomm_team, dst_win, hcomm_team->selfMemberId, dst_offset);
    auto remote_addr = get_comm_mem_ptr(hcomm_team, src_win, peer, src_offset);

    ChannelHandle channel = get_channel_handle(hcomm_team, peer, context_index);

    if constexpr (commit_flags == AIN_COMMIT_DELAYED) {
        comm.template asccomm_read_nbi<false, PIPE_S, PIPE_MTE3, config>(channel, local_addr, remote_addr, bytes);
    } else {
        comm.template asccomm_read_nbi<true, PIPE_S, PIPE_MTE3, config>(channel, local_addr, remote_addr, bytes);
    }
}

template <typename T>
AIN_DEVICE uint64_t get_signal_op_arg(T action)
{
    if constexpr (IsSameType<T, ain_signal_inc>::value) {
        return 1;
    } else {
        return action.value;
    }
}

template <unsigned comm_engine_mask>
template <typename remote_action, typename descriptor_ubuf, ain_commit_flags commit_flags, auto const& config>
AIN_DEVICE void ain<comm_engine_mask>::signal(
    HcommTeamHandle team, uint32_t peer, remote_action action, const descriptor_ubuf& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf, ain_descriptor_ubuf_none>::value,
        "signal requires a valid descriptor_ubuf; ain_descriptor_ubuf_none is not allowed");
    comm.asccomm_init(ubuf.addr, ubuf.bytes);

    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    ChannelHandle channel = get_channel_handle(hcomm_team, peer, context_index);

    uint64_t context_base_id = hcomm_team->memberNum * context_index;

    uint64_t signal_op_arg = get_signal_op_arg(action);

    auto remote_addr = get_comm_mem_ptr(hcomm_team, action.signal_window, peer, action.signal_offset);

    // amo_addr: the address to store the fetched old value required by AtomicFAA.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amo_addr.
    auto local_base = hcomm_team->syncMem.shadowMem.addr;
    uint64_t amo_offset = (context_base_id + hcomm_team->selfMemberId) * sizeof(uint64_t);
    auto amo_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(local_base) + amo_offset);

    if constexpr (commit_flags == AIN_COMMIT_DELAYED) {
        comm.template asccomm_atomic_faa_nbi<uint64_t, false, PIPE_S, PIPE_MTE3, config>(
            channel, remote_addr, amo_addr, signal_op_arg);
    } else {
        comm.template asccomm_atomic_faa_nbi<uint64_t, true, PIPE_S, PIPE_MTE3, config>(
            channel, remote_addr, amo_addr, signal_op_arg);
    }
}

template <unsigned comm_engine_mask>
AIN_DEVICE uint64_t ain<comm_engine_mask>::read_signal(
    HcommTeamHandle team, HcclCommSymWindow signal_window, size_t signal_offset, uint32_t bits,
    ain_memory_order order) const
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signal_addr = reinterpret_cast<__gm__ uint64_t*>(
        get_comm_mem_ptr(hcomm_team, signal_window, hcomm_team->selfMemberId, signal_offset));
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signal_addr, 0);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain<comm_engine_mask>::wait_signal(
    HcommTeamHandle team, HcclCommSymWindow signal_window, size_t signal_offset, uint64_t least, uint32_t bits,
    ain_memory_order order) const
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signal_addr = reinterpret_cast<__gm__ uint64_t*>(
        get_comm_mem_ptr(hcomm_team, signal_window, hcomm_team->selfMemberId, signal_offset));
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
    ain<comm_engine_mask>* ain, HcommTeamHandle team, uint32_t index)
{
    barrier_impl.ain = ain;
    barrier_impl.team = team;
    barrier_impl.index = index;
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    barrier_impl.signal_base_id = index * hcomm_team->memberNum;
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf>
AIN_DEVICE void ain_barrier_session<comm_engine_mask>::sync(ain_memory_order order, const descriptor_ubuf& ubuf)
{
    barrier_impl.template sync_impl<false>(order, 0, ubuf);
}

template <unsigned comm_engine_mask>
template <typename descriptor_ubuf>
AIN_DEVICE int32_t ain_barrier_session<comm_engine_mask>::sync(
    ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf& ubuf)
{
    return barrier_impl.template sync_impl<true>(order, timeout_cycles, ubuf);
}

template <unsigned comm_engine_mask>
template <bool enable_timeout, typename descriptor_ubuf>
AIN_DEVICE int32_t ain_barrier_session_impl<comm_engine_mask>::sync_impl(
    ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf& ubuf)
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(this->team));

    // Phase 1: signal all peers first.
    for (int32_t i = 0; i + 1 < hcomm_team->memberNum; ++i) {
        int peer = 1 + hcomm_team->selfMemberId + i;
        if (hcomm_team->memberNum <= peer) {
            peer -= hcomm_team->memberNum;
        }
        this->signal(this->team, peer, ubuf);
    }

    // Phase 2: wait for all peer signals.
    for (int32_t i = 0; i + 1 < hcomm_team->memberNum; ++i) {
        int peer = 1 + hcomm_team->selfMemberId + i;
        if (hcomm_team->memberNum <= peer) {
            peer -= hcomm_team->memberNum;
        }

        size_t signal_offset = (this->signal_base_id + peer) * sizeof(uint64_t);
        auto signal_shadow = hcomm_team->syncMem.shadowMem.addr;
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
template <typename descriptor_ubuf>
AIN_DEVICE void ain_barrier_session_impl<comm_engine_mask>::signal(
    HcommTeamHandle team, uint32_t peer, const descriptor_ubuf& ubuf)
{
    static_assert(
        !IsSameType<descriptor_ubuf, ain_descriptor_ubuf_none>::value,
        "signal requires a valid descriptor_ubuf; ain_descriptor_ubuf_none is not allowed");
    comm.asccomm_init(ubuf.addr, ubuf.bytes);

    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    ChannelHandle channel = get_channel_handle(hcomm_team, peer, index);

    uint64_t signal_offset = (this->signal_base_id + hcomm_team->selfMemberId) * sizeof(uint64_t);
    auto signal_base = hcomm_team->syncMem.remoteMems[peer].addr;
    auto remote_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);

    // amo_addr: the address to store the fetched old value required by AtomicFAA.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amo_addr.
    auto local_base = hcomm_team->syncMem.shadowMem.addr;
    auto amo_addr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(local_base) + signal_offset);

    comm.template asccomm_atomic_faa_nbi<>(channel, remote_addr, amo_addr, (uint64_t)1);
}

template <unsigned comm_engine_mask>
AIN_DEVICE uint64_t ain_barrier_session_impl<comm_engine_mask>::read_signal(
    HcommTeamHandle team, uint32_t signal_id, uint32_t bits, ain_memory_order order) const
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signal_offset = (signal_base_id + signal_id) * sizeof(uint64_t);
    auto signal_base = hcomm_team->syncMem.remoteMems[hcomm_team->selfMemberId].addr;
    __gm__ uint64_t* signal_addr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signal_addr, 0);
}

template <unsigned comm_engine_mask>
AIN_DEVICE void ain_barrier_session_impl<comm_engine_mask>::wait_signal(
    HcommTeamHandle team, uint32_t signal_id, uint64_t least, uint32_t bits, ain_memory_order order) const
{
    auto hcomm_team = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signal_offset = (signal_base_id + signal_id) * sizeof(uint64_t);
    auto signal_base = hcomm_team->syncMem.remoteMems[hcomm_team->selfMemberId].addr;
    __gm__ uint64_t* signal_addr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signal_base) + signal_offset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    while (true) {
        if (least <= (ld_dev(signal_addr, 0) & mask)) {
            break;
        }
    }
}

} // namespace asc

#endif // IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__
#endif
