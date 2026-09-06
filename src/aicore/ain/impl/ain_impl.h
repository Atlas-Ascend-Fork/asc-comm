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
 * \brief Ain implementation
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message("This is an internal Ain header file and must not be used directly. " \
                "Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__
#endif

#ifndef IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H
#define IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#include "ain_impl_def.h"

namespace AscendC {

AIN_DEVICE HcommMemHandle GetPeerPointer(HcommTeamHandle team, uint32_t peer, HcclCommSymWindow window, size_t offset)
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    auto hcommWindow = reinterpret_cast<__gm__ HcommWindow*>(reinterpret_cast<uint64_t>(window));
    size_t peerOffset = hcommTeam->worldTeamIds[peer] * hcommWindow->lsaWin.stride + offset;
    HcommMemHandle ptr =
        reinterpret_cast<HcommMemHandle>(reinterpret_cast<uintptr_t>(hcommWindow->lsaWin.baseVa) + peerOffset);
    return ptr;
}

AIN_DEVICE ChannelHandle GetChannelHandle(const __gm__ HcommTeam* team, const uint32_t peer, const uint32_t index)
{
    const auto channelCntAccumulatePerMember = team->channelCntAccumulatePerMember;
    const auto channelIndexBase = ReadGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(
        reinterpret_cast<uintptr_t>(channelCntAccumulatePerMember) + peer * sizeof(uint32_t)));
    uint64_t channelIndex = index + channelIndexBase;
    ChannelHandle channel = team->channelsBaseAddr + channelIndex * sizeof(ChannelEntity);
    return channel;
}

AIN_DEVICE GM_ADDR
GetCommMemPtr(const __gm__ HcommTeam* team, const HcclCommSymWindow window, uint32_t memberId, uint64_t offset)
{
    auto hcommWin = reinterpret_cast<__gm__ HcommWindow*>(reinterpret_cast<uint64_t>(window));
    auto remoteMem = reinterpret_cast<uint64_t*>(hcommWin->netWin.baseRemoteMemAddr);
    auto worldTeamAccumulateId = hcommWin->netWin.worldTeamAccumulateId;
    uint64_t remoteMemId = team->worldTeamIds[memberId] + worldTeamAccumulateId[team->netLayer];
    uint64_t memHandle = remoteMem[remoteMemId] + offset;
    return reinterpret_cast<GM_ADDR>(memHandle);
}

// ===================== Ain =====================

template <unsigned CommEngineMask>
AIN_DEVICE Ain<CommEngineMask>::Ain(uint32_t contextIndex) : contextIndex_(contextIndex)
{}

template <unsigned CommEngineMask>
AIN_DEVICE Ain<CommEngineMask>::~Ain()
{}

template <unsigned CommEngineMask>
AIN_DEVICE void Ain<CommEngineMask>::Flush(HcommTeamHandle team)
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint32_t rankSize = hcommTeam->memberNum;
    uint32_t myRank = hcommTeam->selfMemberId;

    for (uint32_t i = 0; i < rankSize; i++) {
        if (i == myRank) {
            continue;
        }
        ChannelHandle channel = GetChannelHandle(hcommTeam, i, contextIndex_);
        hcomm_.Drain(channel);
    }
}

template <unsigned CommEngineMask>
AIN_DEVICE void Ain<CommEngineMask>::FlushAsync(HcommTeamHandle team, uint32_t peer, ChannelHandle* outChannelHandle)
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    *outChannelHandle = GetChannelHandle(hcommTeam, peer, contextIndex_);
}

template <unsigned CommEngineMask>
AIN_DEVICE void Ain<CommEngineMask>::Wait(ChannelHandle& channelHandle)
{
    hcomm_.Drain(channelHandle);
}

template <unsigned CommEngineMask>
template <typename RemoteAction, typename DescriptorUbuf, AinCommitFlags CommitFlags, auto const& Config>
AIN_DEVICE void Ain<CommEngineMask>::Put(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, HcclCommSymWindow srcWin,
    uint64_t srcOffset, uint64_t bytes, RemoteAction remoteAction, const DescriptorUbuf& ubuf)
{
    static_assert(
        !IsSameType<DescriptorUbuf, AinDescriptorUbufNone>::value,
        "put requires a valid DescriptorUbuf; AinDescriptorUbufNone is not allowed");
    hcomm_.Init(ubuf.addr, ubuf.bytes);

    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remoteAddr = GetCommMemPtr(hcommTeam, dstWin, peer, dstOffset);
    auto localAddr = GetCommMemPtr(hcommTeam, srcWin, hcommTeam->selfMemberId, srcOffset);

    ChannelHandle channel = GetChannelHandle(hcommTeam, peer, contextIndex_);

    if constexpr (CommitFlags == AIN_COMMIT_DELAYED) {
        hcomm_.template WriteNbi<false, PIPE_S, PIPE_MTE3, Config>(channel, remoteAddr, localAddr, bytes);
    } else {
        hcomm_.template WriteNbi<true, PIPE_S, PIPE_MTE3, Config>(channel, remoteAddr, localAddr, bytes);
    }

    if constexpr (IsSameType<RemoteAction, AinSignalInc>::value || IsSameType<RemoteAction, AinSignalAdd>::value) {
        this->template Signal<RemoteAction, DescriptorUbuf, CommitFlags, SIGNAL_WQE_CONFIG>(
            team, peer, remoteAction, ubuf);
    }
}

template <unsigned CommEngineMask>
template <typename T, typename RemoteAction, typename DescriptorUbuf, AinCommitFlags CommitFlags, auto const& Config>
AIN_DEVICE void Ain<CommEngineMask>::PutValue(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, T value,
    RemoteAction remoteAction, const DescriptorUbuf& ubuf)
{
    static_assert(
        !IsSameType<DescriptorUbuf, AinDescriptorUbufNone>::value,
        "putValue requires a valid DescriptorUbuf; AinDescriptorUbufNone is not allowed");
    hcomm_.Init(ubuf.addr, ubuf.bytes);

    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto remoteAddr = GetCommMemPtr(hcommTeam, dstWin, peer, dstOffset);

    ChannelHandle channel = GetChannelHandle(hcommTeam, peer, contextIndex_);

    if constexpr (CommitFlags == AIN_COMMIT_DELAYED) {
        hcomm_.template WriteValueNbi<T, false, PIPE_S, PIPE_MTE3, Config>(channel, remoteAddr, value);
    } else {
        hcomm_.template WriteValueNbi<T, true, PIPE_S, PIPE_MTE3, Config>(channel, remoteAddr, value);
    }

    if constexpr (IsSameType<RemoteAction, AinSignalInc>::value || IsSameType<RemoteAction, AinSignalAdd>::value) {
        this->template Signal<RemoteAction, DescriptorUbuf, CommitFlags, SIGNAL_WQE_CONFIG>(
            team, peer, remoteAction, ubuf);
    }
}

template <unsigned CommEngineMask>
template <typename DescriptorUbuf, AinCommitFlags CommitFlags, auto const& Config>
AIN_DEVICE void Ain<CommEngineMask>::Get(
    HcommTeamHandle team, uint32_t peer, HcclCommSymWindow dstWin, uint64_t dstOffset, HcclCommSymWindow srcWin,
    uint64_t srcOffset, uint64_t bytes, const DescriptorUbuf& ubuf)
{
    static_assert(
        !IsSameType<DescriptorUbuf, AinDescriptorUbufNone>::value,
        "get requires a valid DescriptorUbuf; AinDescriptorUbufNone is not allowed");
    hcomm_.Init(ubuf.addr, ubuf.bytes);

    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));

    auto localAddr = GetCommMemPtr(hcommTeam, dstWin, hcommTeam->selfMemberId, dstOffset);
    auto remoteAddr = GetCommMemPtr(hcommTeam, srcWin, peer, srcOffset);

    ChannelHandle channel = GetChannelHandle(hcommTeam, peer, contextIndex_);

    if constexpr (CommitFlags == AIN_COMMIT_DELAYED) {
        hcomm_.template ReadNbi<false, PIPE_S, PIPE_MTE3, Config>(channel, localAddr, remoteAddr, bytes);
    } else {
        hcomm_.template ReadNbi<true, PIPE_S, PIPE_MTE3, Config>(channel, localAddr, remoteAddr, bytes);
    }
}

template <typename T>
AIN_DEVICE uint64_t GetSignalOpArg(T action)
{
    if constexpr (IsSameType<T, AinSignalInc>::value) {
        return 1;
    } else {
        return action.value;
    }
}

template <unsigned CommEngineMask>
template <typename RemoteAction, typename DescriptorUbuf, AinCommitFlags CommitFlags, auto const& Config>
AIN_DEVICE void Ain<CommEngineMask>::Signal(
    HcommTeamHandle team, uint32_t peer, RemoteAction action, const DescriptorUbuf& ubuf)
{
    static_assert(
        !IsSameType<DescriptorUbuf, AinDescriptorUbufNone>::value,
        "signal requires a valid DescriptorUbuf; AinDescriptorUbufNone is not allowed");
    hcomm_.Init(ubuf.addr, ubuf.bytes);

    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    ChannelHandle channel = GetChannelHandle(hcommTeam, peer, contextIndex_);

    uint64_t contextBaseId = hcommTeam->memberNum * contextIndex_;

    uint64_t signalOpArg = GetSignalOpArg(action);

    auto remoteAddr = GetCommMemPtr(hcommTeam, action.signalWindow, peer, action.signalOffset);

    // amoAddr: the address to store the fetched old value required by AtomicFAA.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amoAddr.
    auto localBase = hcommTeam->syncMem.shadowMem.addr;
    uint64_t amoOffset = (contextBaseId + hcommTeam->selfMemberId) * sizeof(uint64_t);
    auto amoAddr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(localBase) + amoOffset);

    if constexpr (CommitFlags == AIN_COMMIT_DELAYED) {
        hcomm_.template AtomicFAA<uint64_t, false, PIPE_S, PIPE_MTE3, Config>(
            channel, remoteAddr, amoAddr, signalOpArg);
    } else {
        hcomm_.template AtomicFAA<uint64_t, true, PIPE_S, PIPE_MTE3, Config>(channel, remoteAddr, amoAddr, signalOpArg);
    }
}

template <unsigned CommEngineMask>
AIN_DEVICE uint64_t Ain<CommEngineMask>::ReadSignal(
    HcommTeamHandle team, HcclCommSymWindow signalWindow, size_t signalOffset, uint32_t bits,
    AinMemoryOrder order) const
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signalAddr = reinterpret_cast<__gm__ uint64_t*>(
        GetCommMemPtr(hcommTeam, signalWindow, hcommTeam->selfMemberId, signalOffset));
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signalAddr, 0);
}

template <unsigned CommEngineMask>
AIN_DEVICE void Ain<CommEngineMask>::WaitSignal(
    HcommTeamHandle team, HcclCommSymWindow signalWindow, size_t signalOffset, uint64_t least, uint32_t bits,
    AinMemoryOrder order) const
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    __gm__ uint64_t* signalAddr = reinterpret_cast<__gm__ uint64_t*>(
        GetCommMemPtr(hcommTeam, signalWindow, hcommTeam->selfMemberId, signalOffset));
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    while (true) {
        if (least <= (ld_dev(signalAddr, 0) & mask)) {
            break;
        }
    }
}

// ===================== AinBarrierSession =====================

template <unsigned CommEngineMask>
AIN_DEVICE AinBarrierSession<CommEngineMask>::AinBarrierSession(
    Ain<CommEngineMask>* ain, HcommTeamHandle team, uint32_t index)
{
    barrierImpl_.ain_ = ain;
    barrierImpl_.team_ = team;
    barrierImpl_.index_ = index;
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    barrierImpl_.signalBaseId_ = index * hcommTeam->memberNum;
}

template <unsigned CommEngineMask>
template <typename DescriptorUbuf>
AIN_DEVICE void AinBarrierSession<CommEngineMask>::Sync(AinMemoryOrder order, const DescriptorUbuf& ubuf)
{
    barrierImpl_.template SyncImpl<false>(order, 0, ubuf);
}

template <unsigned CommEngineMask>
template <typename DescriptorUbuf>
AIN_DEVICE int32_t
AinBarrierSession<CommEngineMask>::Sync(AinMemoryOrder order, uint64_t timeoutCycles, const DescriptorUbuf& ubuf)
{
    return barrierImpl_.template SyncImpl<true>(order, timeoutCycles, ubuf);
}

template <unsigned CommEngineMask>
template <bool EnableTimeout, typename DescriptorUbuf>
AIN_DEVICE int32_t AinBarrierSessionImpl<CommEngineMask>::SyncImpl(
    AinMemoryOrder order, uint64_t timeoutCycles, const DescriptorUbuf& ubuf)
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(this->team_));

    // Phase 1: signal all peers first.
    for (int32_t i = 0; i + 1 < hcommTeam->memberNum; ++i) {
        int peer = 1 + hcommTeam->selfMemberId + i;
        if (hcommTeam->memberNum <= peer) {
            peer -= hcommTeam->memberNum;
        }
        this->Signal(this->team_, peer, ubuf);
    }

    // Phase 2: wait for all peer signals.
    for (int32_t i = 0; i + 1 < hcommTeam->memberNum; ++i) {
        int peer = 1 + hcommTeam->selfMemberId + i;
        if (hcommTeam->memberNum <= peer) {
            peer -= hcommTeam->memberNum;
        }

        size_t signalOffset = (this->signalBaseId_ + peer) * sizeof(uint64_t);
        auto signalShadow = hcommTeam->syncMem.shadowMem.addr;
        __gm__ uint64_t* shadowPtr =
            reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signalShadow) + signalOffset);
        uint64_t waitVal = ++(*shadowPtr);

        if constexpr (EnableTimeout) {
            uint64_t retry = 0;
            while (true) {
                uint64_t got = this->ReadSignal(this->team_, peer, 64);
                if (got >= waitVal) {
                    break;
                }
                if (retry++ >= timeoutCycles) {
                    return -1;
                }
            }
        } else {
            this->WaitSignal(this->team_, peer, waitVal, 64);
        }
    }

    return 0;
}

template <unsigned CommEngineMask>
template <typename DescriptorUbuf>
AIN_DEVICE void AinBarrierSessionImpl<CommEngineMask>::Signal(
    HcommTeamHandle team, uint32_t peer, const DescriptorUbuf& ubuf)
{
    static_assert(
        !IsSameType<DescriptorUbuf, AinDescriptorUbufNone>::value,
        "signal requires a valid DescriptorUbuf; AinDescriptorUbufNone is not allowed");
    hcomm_.Init(ubuf.addr, ubuf.bytes);

    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    ChannelHandle channel = GetChannelHandle(hcommTeam, peer, index_);

    uint64_t signalOffset = (this->signalBaseId_ + hcommTeam->selfMemberId) * sizeof(uint64_t);
    auto signalBase = hcommTeam->syncMem.remoteMems[peer].addr;
    auto remoteAddr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(signalBase) + signalOffset);

    // amoAddr: the address to store the fetched old value required by AtomicFAA.
    // reuse the slot corresponding to the selfMemberId (which is idle) in barrier region of shadow memory as amoAddr.
    auto localBase = hcommTeam->syncMem.shadowMem.addr;
    auto amoAddr = reinterpret_cast<__gm__ uint8_t*>(reinterpret_cast<uintptr_t>(localBase) + signalOffset);

    hcomm_.template AtomicFAA<>(channel, remoteAddr, amoAddr, (uint64_t)1);
}

template <unsigned CommEngineMask>
AIN_DEVICE uint64_t AinBarrierSessionImpl<CommEngineMask>::ReadSignal(
    HcommTeamHandle team, uint32_t signalId, uint32_t bits, AinMemoryOrder order) const
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signalOffset = (signalBaseId_ + signalId) * sizeof(uint64_t);
    auto signalBase = hcommTeam->syncMem.remoteMems[hcommTeam->selfMemberId].addr;
    __gm__ uint64_t* signalAddr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signalBase) + signalOffset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    return mask & ld_dev(signalAddr, 0);
}

template <unsigned CommEngineMask>
AIN_DEVICE void AinBarrierSessionImpl<CommEngineMask>::WaitSignal(
    HcommTeamHandle team, uint32_t signalId, uint64_t least, uint32_t bits, AinMemoryOrder order) const
{
    auto hcommTeam = reinterpret_cast<__gm__ HcommTeam*>(reinterpret_cast<uint64_t>(team));
    uint64_t signalOffset = (signalBaseId_ + signalId) * sizeof(uint64_t);
    auto signalBase = hcommTeam->syncMem.remoteMems[hcommTeam->selfMemberId].addr;
    __gm__ uint64_t* signalAddr =
        reinterpret_cast<__gm__ uint64_t*>(reinterpret_cast<uintptr_t>(signalBase) + signalOffset);
    uint64_t mask = bits >= 64U ? UINT64_MAX : ((1ULL << bits) - 1ULL);
    while (true) {
        if (least <= (ld_dev(signalAddr, 0) & mask)) {
            break;
        }
    }
}

} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_AIN_IMPL_AIN_IMPL_H

#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_AIN_IMPL_H__
#endif
