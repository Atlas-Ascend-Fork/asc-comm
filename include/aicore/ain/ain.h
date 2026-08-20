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
#if !defined(ASCENDC_INCLUDE_INTERNAL_HEADERS)
#define ASCENDC_INCLUDE_INTERNAL_HEADERS
#define UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H
#endif

#ifndef INCLUDE_COMM_API_AICORE_AIN_AIN_H
#define INCLUDE_COMM_API_AICORE_AIN_AIN_H

#include "../hcomm/hcomm.h"
#include "ain_common.h"
#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl_def.h"

namespace AscendC {

/*!
 * @class ain
 * @brief This class provides device-side one-sided communication primitives (put/get) layered on top
 *        of the asccomm point-to-point engine. It resolves the per-peer communication channel from the
 *        device communication table, translates symmetric-window handles into remote/local addresses,
 *        and submits tasks immediately or defers submission until a subsequent immediate-commit task.
 *        Pending tasks can be awaited via flush() or flush_async()/wait().
 */
template <unsigned comm_engine_mask = ain_mask_default>
class ain {
public:
    /*!
     * @brief Construct an ain instance bound to a device communication context.
     * @param [in] context_index: Index of the communication context to operate on.
     */
    AIN_DEVICE ain(uint32_t context_index = 0);
    AIN_DEVICE ~ain();

    /*!
     * @brief drain every peer channel in the team, blocking until all pending tasks complete.
     * @param [in] team: The communication team handle.
     */
    AIN_DEVICE void flush(asccomm_team_handle team);

    /*!
     * @brief get the channel handle of a given peer without blocking, for deferred wait.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [out] out_channel_handle: Output the resolved peer channel handle.
     * @note Each rank currently has only one channel; channel index 0 is used.
     */
    AIN_DEVICE void flush_async(asccomm_team_handle team, uint32_t peer, channel_handle* out_channel_handle);

    /*!
     * @brief Block until all tasks submitted on the given channel are complete.
     * @param [in] channel_handle: The channel handle returned by flush_async.
     */
    AIN_DEVICE void wait(channel_handle& channel_handle);

    /*!
     * @brief Issue a one-sided write (put) from a local window to a remote peer's window.
     * @tparam remote_action: Optional remote-side action tag, ain_remote_none by default.
     * @tparam descriptor_ubuf_t: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: ain_commit_immed rings the doorbell immediately;
     *                      ain_commit_delayed defers submission until a subsequent ain_commit_immed task.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the peer.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] src_win: Source symmetric window of the local rank.
     * @param [in] src_offset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] remote_action: Remote signal action fired after write; ain_remote_none by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     */
    template <
        typename remote_action_t = ain_remote_none, typename descriptor_ubuf_t = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = ain_commit_immed, auto const& config = urma_default_cfg>
    AIN_DEVICE void put(
        asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, asccomm_window_handle src_win,
        uint64_t src_offset, uint64_t bytes, remote_action_t remote_action = remote_action_t{},
        const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

    /*!
     * @brief Issue an inline one-sided write (put) of a typed value to a remote peer's window.
     * @tparam t: Value type to write.
     * @tparam remote_action: signal action type, for example ain_signal_inc, ain_signal_add.
     * @tparam descriptor_ubuf_t: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: ain_commit_immed rings the doorbell immediately;
     *                      ain_commit_delayed defers submission until a subsequent ain_commit_immed task.
     * @tparam config: URMA WQE control config, only used by URMA.
     *         Default: strongly ordered + fence + CQE + inline enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the peer.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] value: Typed value to write inline.
     * @param [in] remote_action: Remote signal action fired after write; ain_remote_none by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     */
    template <
        typename t, typename remote_action_t = ain_remote_none, typename descriptor_ubuf_t = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = ain_commit_immed, auto const& config = urma_inline_cfg>
    AIN_DEVICE void put_value(
        asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, t value,
        remote_action_t remote_action = remote_action_t{}, const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

    /*!
     * @brief Issue a one-sided read (get) from a remote peer's window into a local window.
     * @tparam descriptor_ubuf_t: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: ain_commit_immed rings the doorbell immediately;
     *                      ain_commit_delayed defers submission until a subsequent ain_commit_immed task.
     * @tparam config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dst_win: Destination symmetric window of the local rank.
     * @param [in] dst_offset: Byte offset into the destination window.
     * @param [in] src_win: Source symmetric window of the peer.
     * @param [in] src_offset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     */
    template <
        typename descriptor_ubuf_t = ain_descriptor_ubuf, ain_commit_flags commit_flags = ain_commit_immed,
        auto const& config = urma_default_cfg>
    AIN_DEVICE void get(
        asccomm_team_handle team, uint32_t peer, asccomm_window_handle dst_win, uint64_t dst_offset, asccomm_window_handle src_win,
        uint64_t src_offset, uint64_t bytes, const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

    /*!
     * @brief Atomically update a remote peer's signal via Fetch-and-Add.
     * @tparam remote_action: signal action type, for example ain_signal_inc, ain_signal_add.
     * @tparam descriptor_ubuf_t: UB workspace descriptor type, ain_descriptor_ubuf by default.
     * @tparam commit_flags: ain_commit_immed rings the doorbell immediately;
     *                      ain_commit_delayed defers submission until a subsequent ain_commit_immed task.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] action: signal action; ain_signal_inc adds 1, ain_signal_add adds a custom value.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     */
    template <
        typename remote_action_t, typename descriptor_ubuf_t = ain_descriptor_ubuf,
        ain_commit_flags commit_flags = ain_commit_immed, auto const& config = urma_default_cfg>
    AIN_DEVICE void signal(
        asccomm_team_handle team, uint32_t peer, remote_action_t action, const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

    /*!
     * @brief Read the local signal value, masked to the lower `bits` bits.
     * @param [in] team: The communication team handle.
     * @param [in] signal_window: Symmetric window containing the signal.
     * @param [in] signal_offset: Byte offset into the signal window.
     * @param [in] bits: Number of low-order bits to read (1-64).
     * @param [in] order: Memory order, defaults to ain_memory_order_relax.
     * @return The masked signal value.
     */
    AIN_DEVICE uint64_t read_signal(
        asccomm_team_handle team, asccomm_window_handle signal_window, size_t signal_offset, uint32_t bits = 64,
        ain_memory_order order = ain_memory_order_relax) const;

    /*!
     * @brief Block until the local signal reaches at least `least` (masked to `bits` bits).
     * @param [in] team: The communication team handle.
     * @param [in] signal_window: Symmetric window containing the signal.
     * @param [in] signal_offset: Byte offset into the signal window.
     * @param [in] least: Threshold to wait for.
     * @param [in] bits: Number of low-order bits to compare (1-64).
     * @param [in] order: Memory order, defaults to ain_memory_order_relax.
     */
    AIN_DEVICE void wait_signal(
        asccomm_team_handle team, asccomm_window_handle signal_window, size_t signal_offset, uint64_t least, uint32_t bits = 64,
        ain_memory_order order = ain_memory_order_relax) const;

private:
    static constexpr comm_protocol protocol = mask_to_comm_protocol(comm_engine_mask);
    uint32_t context_index_ = 0;
    asccomm<protocol> asccomm_;
};

template <unsigned comm_engine_mask = ain_mask_default>
class ain_barrier_session {
public:
    /*!
     * @brief Construct a barrier session over the given team at the given barrier resource index.
     * @param [in] ain: The ain instance poiter.
     * @param [in] team: The communication team handle.
     * @param [in] index: Barrier resource index; each index isolates an independent barrier.
     */
    AIN_DEVICE ain_barrier_session(ain<comm_engine_mask>* ain, asccomm_team_handle team, uint32_t index);

    /*!
     * @brief Synchronize with all peers in the team, blocking until completion.
     * @param [in] order: Memory order, defaults to ain_memory_order_relax.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     * @note Implements a ring barrier: signal every other rank, then wait_signal from each.
     */
    template <typename descriptor_ubuf_t = ain_descriptor_ubuf>
    AIN_DEVICE void sync(ain_memory_order order, const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

    /*!
     * @brief Synchronize with all peers in the team with a timeout.
     * @param [in] order: Memory order, defaults to ain_memory_order_relax.
     * @param [in] timeout_cycles: Maximum cycles to wait.
     * @param [in] ubuf: UB workspace descriptor for the underlying asccomm engine.
     * @return 0 on success, -1 on timeout.
     */
    template <typename descriptor_ubuf_t = ain_descriptor_ubuf>
    AIN_DEVICE int32_t
    sync(ain_memory_order order, uint64_t timeout_cycles, const descriptor_ubuf_t& ubuf = descriptor_ubuf_t{});

private:
    ain_barrier_session_impl<comm_engine_mask> barrier_impl_;
};

} // namespace AscendC

#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl.h"

#endif // INCLUDE_COMM_API_AICORE_AIN_AIN_H

#if defined(UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H)
#undef ASCENDC_INCLUDE_INTERNAL_HEADERS
#undef UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H
#endif
