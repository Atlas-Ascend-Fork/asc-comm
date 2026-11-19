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
 * \file ain.h
 * \brief ain interface
 */
#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__
#endif

#ifndef INCLUDE_COMM_API_AICORE_AIN_AIN_H
#define INCLUDE_COMM_API_AICORE_AIN_AIN_H

#include "../asccomm/asccomm.h"
#include "ain_common.h"
#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl_def.h"

namespace asc {
/*!
 * @brief Get a pointer to a remote peer's memory location within a symmetric window.
 * @param [in] peer: Peer rank id within the team.
 * @param [in] window: Symmetric window handle.
 * @param [in] offset: Byte offset into the window.
 * @return An opaque memory handle pointing to the remote peer's location.
 */
AIN_DEVICE HcommMemHandle get_peer_pointer(uint32_t peer, HcclCommSymWindow window, size_t offset);

/*!
 * @brief Deprecated PascalCase alias. Forwards to get_peer_pointer.
 */
ASC_DEPRECATED(9.3.0, "2027/12/31", get_peer_pointer)
AIN_DEVICE HcommMemHandle GetPeerPointer(uint32_t peer, HcclCommSymWindow window, size_t offset)
{
    return get_peer_pointer(peer, window, offset);
}

/*!
 * @class ain
 * @brief This class provides device-side one-sided communication primitives (put/get) layered on top
 *        of the Hcomm point-to-point engine. It resolves the per-peer communication channel from the
 *        device communication table, translates symmetric-window handles into remote/local addresses,
 *        and submits tasks immediately or defers submission until a subsequent immediate-commit task.
 *        Pending tasks can be awaited via flush() or flush_async()/wait(). Only CQE-generating tasks can be
 *        tracked; see flush() for details.
 */
template <unsigned comm_engine_mask = AIN_MASK_DEFAULT>
class ain {
public:
    /*!
     * @brief Construct an ain instance bound to a device communication context.
     * @param [in] context_index: Index of the communication context to operate on.
     */
    AIN_DEVICE ain(uint32_t context_index = 0);
    AIN_DEVICE ~ain();

    /*!
     * @brief Drain every peer channel in the team, blocking until all CQE-generating pending tasks have
     *        completed.
     * @param [in] team: The communication team handle.
     * @note Completion status is tracked through CQEs: only tasks that generate a CQE (the cqe bit set in the
     *       wqe_entry config, which is the default) can be tracked and awaited. Tasks submitted without a CQE
     *       request are executed by the engine but generate no CQE, so their completion cannot be tracked by
     *       this interface; synchronize them through other means if their completion matters.
     */
    AIN_DEVICE void flush(HcommTeamHandle team);

    /*!
     * @brief Deprecated PascalCase alias. Forwards to flush.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", flush)
    AIN_DEVICE void Flush(HcommTeamHandle team) { flush(team); }

    /*!
     * @brief Get the channel handle of a given peer without blocking, for deferred wait.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [out] out_channel_handle: Output the resolved peer channel handle.
     * @note Each rank currently has only one channel; channel index 0 is used.
     */
    AIN_DEVICE void flush_async(HcommTeamHandle team, uint32_t peer, ChannelHandle* out_channel_handle);

    /*!
     * @brief Deprecated PascalCase alias. Forwards to flush_async.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", flush_async)
    AIN_DEVICE void FlushAsync(HcommTeamHandle team, uint32_t peer, ChannelHandle* outChannelHandle)
    {
        flush_async(team, peer, outChannelHandle);
    }

    /*!
     * @brief Block until all CQE-generating tasks submitted to the given channel have completed.
     * @param [in] channel_handle: The channel handle returned by FlushAsync.
     * @note Same completion semantics as flush(): only tasks that generate a CQE (the cqe bit set in the
     *       wqe_entry config, which is the default) can be tracked; tasks submitted without a CQE request
     *       are not covered.
     */
    AIN_DEVICE void wait(ChannelHandle& channel_handle);

    /*!
     * @brief Deprecated PascalCase alias. Forwards to wait.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", wait)
    AIN_DEVICE void Wait(ChannelHandle& channelHandle) { wait(channelHandle); }

    /*!
     * @brief Issue a one-sided write (put) from a local window to a remote peer's window.
     * @tparam remote_action: Optional remote-side action tag, ain_remote_none by default.
     * @tparam descriptor_ubuf: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam config: URMA WQE control config, only used by URMA.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the peer.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] src_win: Source symmetric window of the local rank.
     * @param [in] src_offset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] action: Remote signal action fired after write; ain_remote_none by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename remote_action = ain_remote_none, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_DEFAULT_CFG>
    AIN_DEVICE void put(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, HcclCommSymWindow src_win,
        uint64_t src_offset, uint64_t bytes, remote_action action = remote_action{},
        const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to put.
     */
    template <
        typename remote_action = ain_remote_none, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", put)
    AIN_DEVICE void Put(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, HcclCommSymWindow srcWin,
        uint64_t srcOffset, uint64_t bytes, remote_action action = remote_action{},
        const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        put<remote_action, descriptor_ubuf, commit_flags, config>(
            team, peer, dstWin, dstOffset, srcWin, srcOffset, bytes, action, ubuf);
    }

    /*!
     * @brief Issue an inline one-sided write (put) of a typed value to a remote peer's window.
     * @tparam T: Value type to write.
     * @tparam remote_action: Signal action type, for example ain_signal_inc, ain_signal_add.
     * @tparam descriptor_ubuf: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam config: URMA WQE control config, only used by URMA.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE + inline enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the peer.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] value: Typed value to write inline.
     * @param [in] action: Remote signal action fired after write; ain_remote_none by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename T, typename remote_action = ain_remote_none, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_INLINE_CFG>
    AIN_DEVICE void put_value(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, T value,
        remote_action action = remote_action{}, const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to put_value.
     */
    template <
        typename T, typename remote_action = ain_remote_none, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_INLINE_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", put_value)
    AIN_DEVICE void PutValue(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, T value,
        remote_action action = remote_action{}, const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        put_value<T, remote_action, descriptor_ubuf, commit_flags, config>(
            team, peer, dstWin, dstOffset, value, action, ubuf);
    }

    /*!
     * @brief Issue a one-sided read (get) from a remote peer's window into a local window.
     * @tparam descriptor_ubuf: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam config: URMA WQE control config, only used by URMA.
     *         Default: odr = Relax Order(RO) with in-order completion + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the local rank.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] src_win: Source symmetric window of the peer.
     * @param [in] src_offset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename descriptor_ubuf = ain_descriptor_ubuf, ain_commit_flags commit_flags = AIN_COMMIT_IMMED,
        auto const& config = URMA_DEFAULT_CFG>
    AIN_DEVICE void get(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dst_win, uint64_t dst_offset, HcclCommSymWindow src_win,
        uint64_t src_offset, uint64_t bytes, const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to get.
     */
    template <
        typename descriptor_ubuf = ain_descriptor_ubuf, ain_commit_flags commit_flags = AIN_COMMIT_IMMED,
        auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", get)
    AIN_DEVICE void Get(
        HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, HcclCommSymWindow srcWin,
        uint64_t srcOffset, uint64_t bytes, const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        get<descriptor_ubuf, commit_flags, config>(team, peer, dstWin, dstOffset, srcWin, srcOffset, bytes, ubuf);
    }

    /*!
     * @brief Atomically update a remote peer's signal via Fetch-and-Add.
     * @tparam remote_action: Signal action type, for example ain_signal_inc, ain_signal_add.
     * @tparam descriptor_ubuf: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] action: Signal action; ain_signal_inc adds 1, ain_signal_add adds a custom value.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename remote_action, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_DEFAULT_CFG>
    AIN_DEVICE void signal(
        HcommTeamHandle team, uint32_t peer, remote_action action, const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to signal.
     */
    template <
        typename remote_action, typename descriptor_ubuf = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = AIN_COMMIT_IMMED, auto const& config = URMA_DEFAULT_CFG>
    ASC_DEPRECATED(9.3.0, "2027/12/31", signal)
    AIN_DEVICE void Signal(
        HcommTeamHandle team, uint32_t peer, remote_action action, const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        signal<remote_action, descriptor_ubuf, commit_flags, config>(team, peer, action, ubuf);
    }

    /*!
     * @brief Read the local signal value, masked to the lower `bits` bits.
     * @param [in] team: The communication team handle.
     * @param [in] signal_window: Symmetric window containing the signal.
     * @param [in] signal_offset: Byte offset into the signal window.
     * @param [in] bits: Number of low-order bits to read (1-64).
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     * @return The masked signal value.
     */
    AIN_DEVICE uint64_t read_signal(
        HcommTeamHandle team, HcclCommSymWindow signal_window, size_t signal_offset, uint32_t bits = 64,
        ain_memory_order order = AIN_MEMORY_ORDER_RELAX) const;

    /*!
     * @brief Deprecated PascalCase alias. Forwards to read_signal.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", read_signal)
    AIN_DEVICE uint64_t ReadSignal(
        HcommTeamHandle team, HcclCommSymWindow signalWindow, size_t signalOffset, uint32_t bits = 64,
        ain_memory_order order = AIN_MEMORY_ORDER_RELAX) const
    {
        return read_signal(team, signalWindow, signalOffset, bits, order);
    }

    /*!
     * @brief Block until the local signal reaches at least `least` (masked to `bits` bits).
     * @param [in] team: The communication team handle.
     * @param [in] signal_window: Symmetric window containing the signal.
     * @param [in] signal_offset: Byte offset into the signal window.
     * @param [in] least: Threshold to wait for.
     * @param [in] bits: Number of low-order bits to compare (1-64).
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     */
    AIN_DEVICE void wait_signal(
        HcommTeamHandle team, HcclCommSymWindow signal_window, size_t signal_offset, uint64_t least, uint32_t bits = 64,
        ain_memory_order order = AIN_MEMORY_ORDER_RELAX) const;

    /*!
     * @brief Deprecated PascalCase alias. Forwards to wait_signal.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", wait_signal)
    AIN_DEVICE void WaitSignal(
        HcommTeamHandle team, HcclCommSymWindow signalWindow, size_t signalOffset, uint64_t least, uint32_t bits = 64,
        ain_memory_order order = AIN_MEMORY_ORDER_RELAX) const
    {
        wait_signal(team, signalWindow, signalOffset, least, bits, order);
    }

private:
    static constexpr CommProtocol comm_protocol = mask_to_comm_protocol(comm_engine_mask);
    uint32_t context_index = 0;
    asc::asccomm<comm_protocol> comm;
};

template <unsigned comm_engine_mask = AIN_MASK_DEFAULT>
class ain_barrier_session {
public:
    /*!
     * @brief Construct a barrier session over the given team at the given barrier resource index.
     * @param [in] ain: The ain instance poiter.
     * @param [in] team: The communication team handle.
     * @param [in] index: Barrier resource index; each index isolates an independent barrier.
     */
    AIN_DEVICE ain_barrier_session(ain<comm_engine_mask>* ain, HcommTeamHandle team, uint32_t index);

    /*!
     * @brief Synchronize with all peers in the team, blocking until completion.
     * @param [in] order: Memory order.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     * @note Implements a ring barrier: Signal every other rank, then WaitSignal from each.
     */
    template <typename descriptor_ubuf = ain_descriptor_ubuf>
    AIN_DEVICE void sync(ain_memory_order order, const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to sync.
     */
    template <typename descriptor_ubuf = ain_descriptor_ubuf>
    ASC_DEPRECATED(9.3.0, "2027/12/31", sync)
    AIN_DEVICE void Sync(ain_memory_order order, const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        sync(order, ubuf);
    }

    /*!
     * @brief Synchronize with all peers in the team with a timeout.
     * @param [in] order: Memory order.
     * @param [in] timeout_cycles: Maximum cycles to wait.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     * @return 0 on success, -1 on timeout.
     */
    template <typename descriptor_ubuf = ain_descriptor_ubuf>
    AIN_DEVICE int32_t
    sync(ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf& ubuf = descriptor_ubuf{});

    /*!
     * @brief Deprecated PascalCase alias. Forwards to sync.
     */
    template <typename descriptor_ubuf = ain_descriptor_ubuf>
    ASC_DEPRECATED(9.3.0, "2027/12/31", sync)
    AIN_DEVICE int32_t
        Sync(ain_memory_order order, uint64_t timeoutCycles, const descriptor_ubuf& ubuf = descriptor_ubuf{})
    {
        return sync(order, timeoutCycles, ubuf);
    }

private:
    ain_barrier_session_impl<comm_engine_mask> barrier_impl;
};

} // namespace asc

// Compatibility bridge for pre-rename AscendC-namespace spellings. Ain and AinBarrierSession are
// wrapper class templates rather than alias templates on purpose: pre-rename code spells them
// without a template argument list (e.g. AscendC::Ain ain(0)), and C++17 does not allow using an
// alias template without one.
namespace AscendC {
using asc::ain;
using asc::ain_barrier_session;
using asc::AIN_MASK_DEFAULT;
using asc::get_peer_pointer;
using asc::GetPeerPointer;
template <unsigned comm_engine_mask = AIN_MASK_DEFAULT>
class Ain : public asc::ain<comm_engine_mask> {
public:
    AIN_DEVICE Ain(uint32_t contextIndex = 0) : asc::ain<comm_engine_mask>(contextIndex) {}
};
template <unsigned comm_engine_mask = AIN_MASK_DEFAULT>
class AinBarrierSession : public asc::ain_barrier_session<comm_engine_mask> {
public:
    AIN_DEVICE AinBarrierSession(Ain<comm_engine_mask>* ain, HcommTeamHandle team, uint32_t index)
        : asc::ain_barrier_session<comm_engine_mask>(ain, team, index)
    {}
};
} // namespace AscendC

#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl.h"

#endif // INCLUDE_COMM_API_AICORE_AIN_AIN_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__
#endif
