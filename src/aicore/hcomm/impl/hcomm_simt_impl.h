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
__simt_callee__ inline Hcomm<commProtocol>::Hcomm()
{}

template <CommProtocol commProtocol>
__simt_callee__ inline Hcomm<commProtocol>::~Hcomm()
{}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.Init(buff, len);
}

template <CommProtocol commProtocol>
__simt_callee__ inline BatchHandle<ChannelHandle> Hcomm<commProtocol>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb)
{
    static_assert(commProtocol == COMM_PROTOCOL_UBC_CTP, "SIMT batch only supports COMM_PROTOCOL_UBC_CTP");
    return impl_.MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb);
}

template <CommProtocol commProtocol>
template <typename Group>
__simt_callee__ inline BatchHandle<ChannelHandle> Hcomm<commProtocol>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb,
    const Group& group)
{
    static_assert(commProtocol == COMM_PROTOCOL_UBC_CTP, "SIMT batch only supports COMM_PROTOCOL_UBC_CTP");
    return impl_.MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb, group);
}

template <CommProtocol commProtocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template WriteNbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template WriteNbi<config>(batchHandle, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
{
    return impl_.template WriteNbi<config>(batchHandle, dst, src, len, group);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteValueNbi(ChannelHandle channel, __gm__ void* dst, T value)
{
    return impl_.template WriteValueNbi<T, commit, config>(channel, dst, value);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value)
{
    return impl_.template WriteValueNbi<T, config>(batchHandle, dst, value);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value, const Group& group)
{
    return impl_.template WriteValueNbi<T, config>(batchHandle, dst, value, group);
}

template <CommProtocol commProtocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template ReadNbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template ReadNbi<config>(batchHandle, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
{
    return impl_.template ReadNbi<config>(batchHandle, dst, src, len, group);
}

template <CommProtocol commProtocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return impl_.template WriteWithNotifyNbi<commit, config>(channel, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol>
template <auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return impl_.template WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol>
template <auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal, const Group& group)
{
    return impl_.template WriteWithNotifyNbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal, group);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return impl_.template AtomicFAA<T, commit, config>(channel, dst, fetchAddr, addVal);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return impl_.template AtomicFAA<T, config>(batchHandle, dst, fetchAddr, addVal);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal, const Group& group)
{
    return impl_.template AtomicFAA<T, config>(batchHandle, dst, fetchAddr, addVal, group);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return impl_.template AtomicCAS<T, commit, config>(channel, dst, fetchAddr, compareVal, swapVal);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return impl_.template AtomicCAS<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal);
}

template <CommProtocol commProtocol>
template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
    const Group& group)
{
    return impl_.template AtomicCAS<T, config>(batchHandle, dst, fetchAddr, compareVal, swapVal, group);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::BatchCommit(UbcCtpBatchHandle& batchHandle)
{
    return impl_.BatchCommit(batchHandle);
}

template <CommProtocol commProtocol>
template <typename Group>
__simt_callee__ inline int32_t Hcomm<commProtocol>::BatchCommit(UbcCtpBatchHandle& batchHandle, const Group& group)
{
    return impl_.BatchCommit(batchHandle, group);
}

template <CommProtocol commProtocol>
template <auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Drain(ChannelHandle channel)
{
    return impl_.template Drain<pipe>(channel);
}

template <CommProtocol commProtocol>
template <auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Drain(UbcCtpBatchHandle& batchHandle)
{
    return impl_.template Drain<pipe>(batchHandle);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::InitCompletionSet(
    UbcCtpCompletionSet& set, __gm__ ChannelHandle* channels, uint32_t count, __gm__ uint8_t* workspace, uint64_t bytes)
{
    return impl_.InitCompletionSet(set, channels, count, workspace, bytes);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::PrepareCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount)
{
    return impl_.PrepareCompletion(set, channelIndex, wqeCount, bbCount);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Progress(
    UbcCtpCompletionSet& set, uint32_t budget, uint32_t& processed)
{
    return impl_.Progress(set, budget, processed);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WaitCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t maxIdlePolls)
{
    return impl_.WaitCompletion(set, channelIndex, maxIdlePolls);
}

template <CommProtocol commProtocol>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Drain(UbcCtpCompletionSet& set)
{
    return impl_.Drain(set);
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
