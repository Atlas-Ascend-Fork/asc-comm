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
 * \file hcomm_simt_impl.h
 * \brief Hcomm SIMT implementation
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_simt_urma.h"
#endif

namespace AscendC::simt {

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline Hcomm<commProtocol, Group>::Hcomm()
{}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline Hcomm<commProtocol, Group>::Hcomm(const Group& group) : group_(group)
{}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline Hcomm<commProtocol, Group>::~Hcomm()
{}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline bool Hcomm<commProtocol, Group>::IsGroupLeader() const
{
    return group_.thread_rank() == detail::HCOMM_SIMT_GROUP_LEADER_RANK;
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline void Hcomm<commProtocol, Group>::SyncGroup() const
{
    group_.sync();
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.Init(buff, len);
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::ReserveDwqeBurst(
    ChannelHandle channel, __gm__ void* targetAddr, uint64_t len, uint32_t wqeCnt)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.ReserveDwqeBurst(channel, targetAddr, len, wqeCnt);
        }
        SyncGroup();
        return ret;
    }
    return impl_.ReserveDwqeBurst(channel, targetAddr, len, wqeCnt);
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::ReserveDwqeBatch(
    ChannelHandle channel, __ubuf__ uint32_t* state, bool active,
    uint32_t activeWqeCnt, uint32_t commitLane)
{
    return impl_.ReserveDwqeBatch(channel, state, active, activeWqeCnt, commitLane);
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template WriteNbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template WriteNbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template WriteValueNbi<T, commit, commitPipe, reqPipe, config>(channel, dst, value);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template WriteValueNbi<T, commit, commitPipe, reqPipe, config>(channel, dst, value);
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template ReadNbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template ReadNbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template WriteWithNotifyNbi<commit, commitPipe, reqPipe, config>(
                channel, dst, src, len, notifyAddr, notifyVal);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template WriteWithNotifyNbi<commit, commitPipe, reqPipe, config>(
        channel, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template AtomicFAA<T, commit, commitPipe, reqPipe, config>(
                channel, dst, fetchAddr, addVal);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template AtomicFAA<T, commit, commitPipe, reqPipe, config>(channel, dst, fetchAddr, addVal);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template AtomicCAS<T, commit, commitPipe, reqPipe, config>(
                channel, dst, fetchAddr, compareVal, swapVal);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template AtomicCAS<T, commit, commitPipe, reqPipe, config>(
        channel, dst, fetchAddr, compareVal, swapVal);
}

template <CommProtocol commProtocol, typename Group>
template <auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::Drain(ChannelHandle channel)
{
    if constexpr (detail::HcommGroupTraits<Group>::IS_BOUND) {
        int32_t ret = HCOMM_SUCCESS;
        if (IsGroupLeader()) {
            ret = impl_.template Drain<pipe>(channel);
        }
        SyncGroup();
        return ret;
    }
    return impl_.template Drain<pipe>(channel);
}

__simt_callee__ inline __gm__ uint8_t* LocalBufferAddr(ChannelHandle channel, uint32_t bufferIdx)
{
    return HcommSimtLocalBufferAddr(channel, bufferIdx);
}

__simt_callee__ inline __gm__ uint8_t* RemoteBufferAddr(ChannelHandle channel, uint32_t bufferIdx)
{
    return HcommSimtRemoteBufferAddr(channel, bufferIdx);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H
#endif
