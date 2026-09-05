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
template <typename Coop, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteNbi(
    Coop coop, ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template WriteNbi<commit, config>(channel, dst, src, len);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteValueNbi(
    Coop coop, ChannelHandle channel, __gm__ void* dst, T value)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template WriteValueNbi<T, commit, config>(channel, dst, value);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::ReadNbi(
    Coop coop, ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template ReadNbi<commit, config>(channel, dst, src, len);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::WriteWithNotifyNbi(
    Coop coop, ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template WriteWithNotifyNbi<commit, config>(channel, dst, src, len, notifyAddr, notifyVal);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicFAA(
    Coop coop, ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template AtomicFAA<T, commit, config>(channel, dst, fetchAddr, addVal);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t Hcomm<commProtocol>::AtomicCAS(
    Coop coop, ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    coop.sync();
    int32_t ret = -1;
    if (coop.thread_rank() == 0) {
        ret = impl_.template AtomicCAS<T, commit, config>(channel, dst, fetchAddr, compareVal, swapVal);
    }
    coop.sync();
    return ret;
}

template <CommProtocol commProtocol>
template <typename Coop, auto pipe>
__simt_callee__ inline int32_t Hcomm<commProtocol>::Drain(Coop coop, ChannelHandle channel)
{
    coop.sync();
    int32_t ret = 0;
    if (coop.thread_rank() == 0) {
        ret = impl_.template Drain<pipe>(channel);
    }
    coop.sync();
    return ret;
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
