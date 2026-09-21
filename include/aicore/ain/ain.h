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
 * \brief Ain interface
 */
#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__
#endif

#ifndef INCLUDE_COMM_API_AICORE_AIN_AIN_H
#define INCLUDE_COMM_API_AICORE_AIN_AIN_H

#include "../hcomm/hcomm.h"
#include "ain_common.h"
#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl_def.h"

namespace AscendC {

/*!
 * @class Ain
 * @brief This class provides device-side one-sided communication primitives (put/get) layered on top
 *        of the Hcomm point-to-point engine. It resolves the per-peer communication channel from the
 *        device communication table, translates symmetric-window handles into remote/local addresses,
 *        and submits tasks immediately or defers submission until a subsequent immediate-commit task.
 *        Pending tasks can be awaited via Flush() or FlushAsync()/Wait().
 */
template <unsigned CommEngineMask = AIN_MASK_DEFAULT>
class Ain {
public:
    /*!
     * @brief Construct an Ain instance bound to a device communication context.
     * @param [in] contextIndex: Index of the communication context to operate on.
     */
    AIN_DEVICE Ain(uint32_t contextIndex = 0);
    AIN_DEVICE ~Ain();

    /*!
     * @brief Drain every peer channel in the team, blocking until all pending tasks complete.
     * @param [in] team: The communication team handle.
     */
    AIN_DEVICE void Flush(HcommTeamHandle team);

    /*!
     * @brief Get the channel handle of a given peer without blocking, for deferred wait.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [out] outChannelHandle: Output the resolved peer channel handle.
     * @note Each rank currently has only one channel; channel index 0 is used.
     */
    AIN_DEVICE void FlushAsync(HcommTeamHandle team, uint32_t peer, ChannelHandle* outChannelHandle);

    /*!
     * @brief Block until all tasks submitted on the given channel are complete.
     * @param [in] channelHandle: The channel handle returned by FlushAsync.
     */
    AIN_DEVICE void Wait(ChannelHandle& channelHandle);

    /*!
     * @brief Issue a one-sided write (put) from a local window to a remote peer's window.
     * @tparam RemoteAction: Optional remote-side action tag, AinRemoteNone by default.
     * @tparam DescriptorUbuf: UB workspace descriptor type, AinDescriptorUbuf by default.
     * @tparam CommitFlags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam Config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dstWin: Destination symmetric window of the peer.
     * @param [in] dstOffset: Byte offset into the destination window.
     * @param [in] srcWin: Source symmetric window of the local rank.
     * @param [in] srcOffset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] remoteAction: Remote signal action fired after write; AinRemoteNone by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename RemoteAction = AinRemoteNone, typename DescriptorUbuf = AinDescriptorUbuf,
        AinCommitFlags CommitFlags = AIN_COMMIT_IMMED, auto const& Config = URMA_DEFAULT_CFG>
    AIN_DEVICE void Put(
        HcommTeamHandle team, uint32_t peer, HcommWindowHandle dstWin, uint64_t dstOffset, HcommWindowHandle srcWin,
        uint64_t srcOffset, uint64_t bytes, RemoteAction remoteAction = RemoteAction{},
        const DescriptorUbuf& ubuf = DescriptorUbuf{});

    /*!
     * @brief Issue an inline one-sided write (put) of a typed value to a remote peer's window.
     * @tparam T: Value type to write.
     * @tparam RemoteAction: Signal action type, for example AinSignalInc, AinSignalAdd.
     * @tparam DescriptorUbuf: UB workspace descriptor type, AinDescriptorUbuf by default.
     * @tparam CommitFlags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam Config: URMA WQE control config, only used by URMA.
     *         Default: strongly ordered + fence + CQE + inline enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dstWin: Destination symmetric window of the peer.
     * @param [in] dstOffset: Byte offset into the destination window.
     * @param [in] value: Typed value to write inline.
     * @param [in] remoteAction: Remote signal action fired after write; AinRemoteNone by default.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename T, typename RemoteAction = AinRemoteNone, typename DescriptorUbuf = AinDescriptorUbuf,
        AinCommitFlags CommitFlags = AIN_COMMIT_IMMED, auto const& Config = URMA_INLINE_CFG>
    AIN_DEVICE void PutValue(
        HcommTeamHandle team, uint32_t peer, HcommWindowHandle dstWin, uint64_t dstOffset, T value,
        RemoteAction remoteAction = RemoteAction{}, const DescriptorUbuf& ubuf = DescriptorUbuf{});

    /*!
     * @brief Issue a one-sided read (get) from a remote peer's window into a local window.
     * @tparam DescriptorUbuf: UB workspace descriptor type, AinDescriptorUbuf by default.
     * @tparam CommitFlags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @tparam Config: URMA WQE control config, only used by URMA. Default: strongly ordered + fence + CQE enabled.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] dstWin: Destination symmetric window of the local rank.
     * @param [in] dstOffset: Byte offset into the destination window.
     * @param [in] srcWin: Source symmetric window of the peer.
     * @param [in] srcOffset: Byte offset into the source window.
     * @param [in] bytes: Number of bytes to transfer.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename DescriptorUbuf = AinDescriptorUbuf, AinCommitFlags CommitFlags = AIN_COMMIT_IMMED,
        auto const& Config = URMA_DEFAULT_CFG>
    AIN_DEVICE void Get(
        HcommTeamHandle team, uint32_t peer, HcommWindowHandle dstWin, uint64_t dstOffset, HcommWindowHandle srcWin,
        uint64_t srcOffset, uint64_t bytes, const DescriptorUbuf& ubuf = DescriptorUbuf{});

    /*!
     * @brief Atomically update a remote peer's signal via Fetch-and-Add.
     * @tparam RemoteAction: Signal action type, for example AinSignalInc, AinSignalAdd.
     * @tparam DescriptorUbuf: UB workspace descriptor type, AinDescriptorUbuf by default.
     * @tparam CommitFlags: AIN_COMMIT_IMMED rings the doorbell immediately;
     *                      AIN_COMMIT_DELAYED defers submission until a subsequent AIN_COMMIT_IMMED task.
     * @param [in] team: The communication team handle.
     * @param [in] peer: Peer rank id within the team.
     * @param [in] action: Signal action; AinSignalInc adds 1, AinSignalAdd adds a custom value.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     */
    template <
        typename RemoteAction, typename DescriptorUbuf = AinDescriptorUbuf,
        AinCommitFlags CommitFlags = AIN_COMMIT_IMMED, auto const& Config = URMA_DEFAULT_CFG>
    AIN_DEVICE void Signal(
        HcommTeamHandle team, uint32_t peer, RemoteAction action, const DescriptorUbuf& ubuf = DescriptorUbuf{});

    /*!
     * @brief Read the local signal value, masked to the lower `bits` bits.
     * @param [in] team: The communication team handle.
     * @param [in] signalWindow: Symmetric window containing the signal.
     * @param [in] signalOffset: Byte offset into the signal window.
     * @param [in] bits: Number of low-order bits to read (1-64).
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     * @return The masked signal value.
     */
    AIN_DEVICE uint64_t ReadSignal(
        HcommTeamHandle team, HcommWindowHandle signalWindow, size_t signalOffset, uint32_t bits = 64,
        AinMemoryOrder order = AIN_MEMORY_ORDER_RELAX) const;

    /*!
     * @brief Block until the local signal reaches at least `least` (masked to `bits` bits).
     * @param [in] team: The communication team handle.
     * @param [in] signalWindow: Symmetric window containing the signal.
     * @param [in] signalOffset: Byte offset into the signal window.
     * @param [in] least: Threshold to wait for.
     * @param [in] bits: Number of low-order bits to compare (1-64).
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     */
    AIN_DEVICE void WaitSignal(
        HcommTeamHandle team, HcommWindowHandle signalWindow, size_t signalOffset, uint64_t least, uint32_t bits = 64,
        AinMemoryOrder order = AIN_MEMORY_ORDER_RELAX) const;

private:
    static constexpr CommProtocol commProtocol = MaskToCommProtocol(CommEngineMask);
    uint32_t contextIndex_ = 0;
    Hcomm<commProtocol> hcomm_;
};

template <unsigned CommEngineMask = AIN_MASK_DEFAULT>
class AinBarrierSession {
public:
    /*!
     * @brief Construct a barrier session over the given team at the given barrier resource index.
     * @param [in] ain: The Ain instance poiter.
     * @param [in] team: The communication team handle.
     * @param [in] index: Barrier resource index; each index isolates an independent barrier.
     */
    AIN_DEVICE AinBarrierSession(Ain<CommEngineMask>* ain, HcommTeamHandle team, uint32_t index);

    /*!
     * @brief Synchronize with all peers in the team, blocking until completion.
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     * @note Implements a ring barrier: Signal every other rank, then WaitSignal from each.
     */
    template <typename DescriptorUbuf = AinDescriptorUbuf>
    AIN_DEVICE void Sync(AinMemoryOrder order, const DescriptorUbuf& ubuf = DescriptorUbuf{});

    /*!
     * @brief Synchronize with all peers in the team with a timeout.
     * @param [in] order: Memory order, defaults to AIN_MEMORY_ORDER_RELAX.
     * @param [in] timeoutCycles: Maximum cycles to wait.
     * @param [in] ubuf: UB workspace descriptor for the underlying Hcomm engine.
     * @return 0 on success, -1 on timeout.
     */
    template <typename DescriptorUbuf = AinDescriptorUbuf>
    AIN_DEVICE int32_t
    Sync(AinMemoryOrder order, uint64_t timeoutCycles, const DescriptorUbuf& ubuf = DescriptorUbuf{});

private:
    AinBarrierSessionImpl<CommEngineMask> barrierImpl_;
};

} // namespace AscendC

#include "../../../../impl/comm_api/aicore/ain/impl/ain_impl.h"

#endif // INCLUDE_COMM_API_AICORE_AIN_AIN_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_H__
#endif
