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
 * \file asccomm_simt_impl.h
 * \brief asccomm SIMT implementation
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_simt_urma.h"
#endif

namespace AscendC::simt {

template <comm_protocol comm_protocol, typename group_t>
__simt_callee__ inline asccomm<comm_protocol, group_t>::asccomm()
{}

template <comm_protocol comm_protocol, typename group_t>
__simt_callee__ inline asccomm<comm_protocol, group_t>::asccomm(const group_t& group) : group_(group)
{}

template <comm_protocol comm_protocol, typename group_t>
__simt_callee__ inline asccomm<comm_protocol, group_t>::~asccomm()
{}

template <comm_protocol comm_protocol, typename group_t>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.init(buff, len);
}

template <comm_protocol comm_protocol, typename group_t>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::write_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template write_nbi<commit, config>(channel, dst, src, len);
}

template <comm_protocol comm_protocol, typename group_t>
template <typename t, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::write_value_nbi(
    channel_handle channel, __gm__ void* dst, t value)
{
    static_assert(sizeof(t) == 0U, "SIMT write_value_nbi is not supported");
    (void)channel;
    (void)dst;
    (void)value;
    return asccomm_failed;
}

template <comm_protocol comm_protocol, typename group_t>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::read_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl_.template read_nbi<commit, config>(channel, dst, src, len);
}

template <comm_protocol comm_protocol, typename group_t>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::write_with_notify_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return impl_.template write_with_notify_nbi<commit, config>(channel, dst, src, len, notify_addr, notify_val);
}

template <comm_protocol comm_protocol, typename group_t>
template <typename t, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::atomic_faa(
    channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t add_val)
{
    return impl_.template atomic_faa<t, commit, config>(channel, dst, fetch_addr, add_val);
}

template <comm_protocol comm_protocol, typename group_t>
template <typename t, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::atomic_cas(
    channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t compare_val, t swap_val)
{
    return impl_.template atomic_cas<t, commit, config>(channel, dst, fetch_addr, compare_val, swap_val);
}

template <comm_protocol comm_protocol, typename group_t>
template <auto pipe>
__simt_callee__ inline int32_t asccomm<comm_protocol, group_t>::drain(channel_handle channel)
{
    return impl_.template drain<pipe>(channel);
}

__simt_callee__ inline __gm__ uint8_t* local_buffer_addr(channel_handle channel, uint32_t buffer_idx)
{
    return asccomm_simt_local_buffer_addr(channel, buffer_idx);
}

__simt_callee__ inline __gm__ uint8_t* remote_buffer_addr(channel_handle channel, uint32_t buffer_idx)
{
    return asccomm_simt_remote_buffer_addr(channel, buffer_idx);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_SIMT_IMPL_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_IMPL_H
#endif
