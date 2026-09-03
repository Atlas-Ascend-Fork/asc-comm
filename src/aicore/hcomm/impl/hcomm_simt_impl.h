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

template <CommProtocol commProtocol>
__simt_callee__ inline UbcCtpBatchHandle HcommMakeBatchHandle(
    HcommImpl<commProtocol>& impl, ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen,
    __gm__ void* remoteBase, uint32_t itemBb, const detail::HcommUnboundGroup&)
{
    return impl.MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb);
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline UbcCtpBatchHandle HcommMakeBatchHandle(
    HcommImpl<commProtocol>& impl, ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen,
    __gm__ void* remoteBase, uint32_t itemBb, const Group& group)
{
    return group.size() == 1U ? impl.MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb) :
        impl.MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb, group);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t HcommBatchCommit(
    HcommImpl<commProtocol>& impl, UbcCtpBatchHandle& batchHandle, const detail::HcommUnboundGroup&)
{
    return impl.BatchCommit(batchHandle);
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline int32_t HcommBatchCommit(
    HcommImpl<commProtocol>& impl, UbcCtpBatchHandle& batchHandle, const Group& group)
{
    return group.size() == 1U ? impl.BatchCommit(batchHandle) : impl.BatchCommit(batchHandle, group);
}

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
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.Init(buff, len);
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline BatchHandle<ChannelHandle> Hcomm<commProtocol, Group>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase,
    uint32_t itemBb)
{
    static_assert(commProtocol == COMM_PROTOCOL_UBC_CTP, "SIMT batch only supports COMM_PROTOCOL_UBC_CTP");
    return HcommMakeBatchHandle(impl_, channel, buff, buffLen, remoteBase, itemBb, group_);
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template WriteNbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol, typename Group>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template WriteNbi<config>(batchHandle, dst, src, len);
    } else {
        return group_.size() == 1U ? impl_.template WriteNbi<config>(batchHandle, dst, src, len) :
            impl_.template WriteNbi<config>(batchHandle, dst, src, len, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return impl_.template WriteValueNbi<T, commit, config>(channel, dst, value);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template WriteValueNbi<T, config>(batchHandle, dst, value);
    } else {
        return group_.size() == 1U ? impl_.template WriteValueNbi<T, config>(batchHandle, dst, value) :
            impl_.template WriteValueNbi<T, config>(batchHandle, dst, value, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template ReadNbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol, typename Group>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template ReadNbi<config>(batchHandle, dst, src, len);
    } else {
        return group_.size() == 1U ? impl_.template ReadNbi<config>(batchHandle, dst, src, len) :
            impl_.template ReadNbi<config>(batchHandle, dst, src, len, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return impl_.template WriteWithNotifyNbi<commit, config>(channel, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol, typename Group>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal);
    } else {
        return group_.size() == 1U ?
            impl_.template WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal) :
            impl_.template WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return impl_.template AtomicFAA<T, commit, config>(channel, dst, fetchAddr, addVal);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template AtomicFAA<T, config>(batchHandle, dst, fetchAddr, addVal);
    } else {
        return group_.size() == 1U ? impl_.template AtomicFAA<T, config>(batchHandle, dst, fetchAddr, addVal) :
            impl_.template AtomicFAA<T, config>(batchHandle, dst, fetchAddr, addVal, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return impl_.template AtomicCAS<T, commit, config>(channel, dst, fetchAddr, compareVal, swapVal);
}

template <CommProtocol commProtocol, typename Group>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    if constexpr (std::is_same_v<Group, detail::HcommUnboundGroup>) {
        return impl_.template AtomicCAS<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal);
    } else {
        return group_.size() == 1U ?
            impl_.template AtomicCAS<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal) :
            impl_.template AtomicCAS<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal, group_);
    }
}

template <CommProtocol commProtocol, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::BatchCommit(UbcCtpBatchHandle& batchHandle)
{
    return HcommBatchCommit(impl_, batchHandle, group_);
}

template <CommProtocol commProtocol, typename Group>
template <auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::Drain(ChannelHandle channel)
{
    return impl_.template Drain<pipe>(channel);
}

template <CommProtocol commProtocol, typename Group>
template <auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol, Group>::Drain(UbcCtpBatchHandle& batchHandle)
{
    return impl_.template Drain<pipe>(batchHandle);
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
