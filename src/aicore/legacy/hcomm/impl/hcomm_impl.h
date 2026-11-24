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
 * \file hcomm_impl.h
 * \brief Hcomm legacy interface adapters (deprecated)
 *
 * Defines the legacy Hcomm interface names (Init, WriteNbi, ReadNbi and so on) declared by the
 * public hcomm/hcomm.h header. Deprecation is advertised by the ASC_DEPRECATED annotations there;
 * each legacy interface forwards to the same underlying AsccommImpl asccomm_* implementation
 * member used by the new asccomm_* interfaces (see hcomm_asccomm_impl.h).
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H

#if __NPU_ARCH__ == 2201
#include "platform_v220/asccomm_aiv.h"
#elif __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202
#include "platform_v310/asccomm_aiv_roce.h"
#include "platform_v310/asccomm_aiv_urma.h"
#endif

namespace AscendC {

template <CommProtocol commProtocol>
__aicore__ inline int32_t Hcomm<commProtocol>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.asccomm_init(buff, len);
}

template <CommProtocol commProtocol>
template <typename T>
__aicore__ inline int32_t Hcomm<commProtocol>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    return impl_.asccomm_init(buff, len);
}

template <CommProtocol commProtocol>
template <typename T, typename U>
__aicore__ inline BatchHandle<T> Hcomm<commProtocol>::MakeBatchHandle(
    T channel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr)
{
    static_assert(
        asc::protocol_traits<commProtocol>::backend == COMM_PROTOCOL_UB_CTP ||
            asc::protocol_traits<commProtocol>::backend == COMM_PROTOCOL_ROCE,
        "BatchHandle only supports RoCE, UB_CTP, UBOE or UB_RTP");
    auto batchHandle = impl_.asccomm_make_batch_handle(channel, buff, buffLen, remoteAddr, localAddr);
    using ExpectedType = BatchHandle<T>;
    static_assert(
        IsSameType<decltype(batchHandle), ExpectedType>::value, "Channel type and BatchHandle type do not match");
    return batchHandle;
}

template <CommProtocol commProtocol>
template <typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline BatchHandle<T>& Hcomm<commProtocol>::GetHandleRef(
    T& batchHandle, uint32_t channelIndex, GM_ADDR remoteAddr)
{
    return impl_.asccomm_get_handle_ref(batchHandle, channelIndex, remoteAddr);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteNbi(T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return impl_.template asccomm_write_nbi<config>(batchHandle, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteNbi(
    T& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum)
{
    return impl_.template asccomm_write_nbi<config>(batchHandle, dst, srcDescs, srcNum);
}

template <CommProtocol commProtocol>
template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl_.template asccomm_write_nbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    return impl_.template asccomm_write_value_nbi<T, commit, commitPipe, reqPipe, config>(channel, dst, value);
}

template <CommProtocol commProtocol>
template <typename T, HcommUrmaReduceOp reduceOp, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteReduceNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    return impl_.template asccomm_write_reduce_nbi<T, reduceOp, commit, commitPipe, reqPipe, config>(
        channel, dst, src, count);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    return impl_.template asccomm_write_with_notify_nbi<config>(batchHandle, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    T& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    return impl_.template asccomm_write_with_notify_nbi<config>(
        batchHandle, dst, srcDescs, srcNum, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol>
template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    return impl_.template asccomm_write_with_notify_nbi<commit, commitPipe, reqPipe, config>(
        channel, dst, src, len, notifyAddr, notifyVal);
}

template <CommProtocol commProtocol>
template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::ReadNbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl_.template asccomm_read_nbi<commit, commitPipe, reqPipe, config>(channel, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::ReadNbi(T& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return impl_.template asccomm_read_nbi<config>(batchHandle, dst, src, len);
}

template <CommProtocol commProtocol>
template <auto const& config, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::ReadNbi(
    T& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src)
{
    return impl_.template asccomm_read_nbi<config>(batchHandle, dstDescs, dstNum, src);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::AtomicFAA(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal)
{
    return impl_.template asccomm_atomic_faa_nbi<T, commit, commitPipe, reqPipe, config>(
        channel, dst, fetchAddr, addVal);
}

template <CommProtocol commProtocol>
template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t Hcomm<commProtocol>::AtomicCAS(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T compareVal, T swapVal)
{
    return impl_.template asccomm_atomic_cas_nbi<T, commit, commitPipe, reqPipe, config>(
        channel, dst, fetchAddr, compareVal, swapVal);
}

template <CommProtocol commProtocol>
template <pipe_t pipe>
__aicore__ inline int32_t Hcomm<commProtocol>::Commit(ChannelHandle channel)
{
    return impl_.template asccomm_commit<pipe>(channel);
}

template <CommProtocol commProtocol>
template <typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::BatchCommit(T& batchHandle)
{
    return impl_.asccomm_batch_commit(batchHandle);
}

template <CommProtocol commProtocol>
template <pipe_t pipe>
__aicore__ inline int32_t Hcomm<commProtocol>::Drain(ChannelHandle channel)
{
    return impl_.template asccomm_drain<pipe>(channel);
}

template <CommProtocol commProtocol>
template <pipe_t pipe, typename T, typename HandleTraits<T>::ChannelType*>
__aicore__ inline int32_t Hcomm<commProtocol>::Drain(T& batchHandle)
{
    return impl_.template asccomm_drain<pipe>(batchHandle);
}

template <CommProtocol commProtocol>
__aicore__ inline int32_t Hcomm<commProtocol>::Lock(ChannelHandle channel)
{
    static_assert(
        asc::protocol_traits<commProtocol>::backend == COMM_PROTOCOL_ROCE ||
            asc::protocol_traits<commProtocol>::uses_urma_backend,
        "Lock only supports RoCE, UB_CTP, UBOE or UB_RTP");
    return impl_.asccomm_lock(channel);
}

template <CommProtocol commProtocol>
__aicore__ inline int32_t Hcomm<commProtocol>::Unlock(ChannelHandle channel)
{
    static_assert(
        asc::protocol_traits<commProtocol>::backend == COMM_PROTOCOL_ROCE ||
            asc::protocol_traits<commProtocol>::uses_urma_backend,
        "Unlock only supports RoCE, UB_CTP, UBOE or UB_RTP");
    return impl_.asccomm_unlock(channel);
}
} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H
#endif
