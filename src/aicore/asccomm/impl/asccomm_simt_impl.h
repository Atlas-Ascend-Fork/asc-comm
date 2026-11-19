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
 * \brief Hcomm asccomm SIMT interface implementation
 *
 * Defines the new asccomm* interfaces declared by the public asccomm/asccomm_simt.h header.
 * Each interface forwards to the same underlying asccomm_impl asccomm* implementation member that
 * the legacy Hcomm SIMT interface adapters in asccomm_simt_impl.h also use.
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_IMPL_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_SIMT_IMPL_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_SIMT_IMPL_H

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
#include "platform_v310/asccomm_simt_urma.h"
#endif

namespace asc::simt {

template <CommProtocol comm_protocol>
__simt_callee__ inline asccomm<comm_protocol>::asccomm()
{}

template <CommProtocol comm_protocol>
__simt_callee__ inline asccomm<comm_protocol>::~asccomm()
{}

template <CommProtocol comm_protocol>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl.asccomm_init(buff, len);
}

template <CommProtocol comm_protocol>
__simt_callee__ inline batch_handle<ChannelHandle> asccomm<comm_protocol>::asccomm_make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len)
{
    static_assert(comm_protocol == COMM_PROTOCOL_UBC_CTP, "SIMT batch only supports COMM_PROTOCOL_UBC_CTP");
    return impl.asccomm_make_batch_handle(channel, buff, buff_len);
}

template <CommProtocol comm_protocol>
template <typename coop_group>
__simt_callee__ inline batch_handle<ChannelHandle> asccomm<comm_protocol>::asccomm_make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, const coop_group& group)
{
    static_assert(comm_protocol == COMM_PROTOCOL_UBC_CTP, "SIMT batch only supports COMM_PROTOCOL_UBC_CTP");
    return impl.asccomm_make_batch_handle(channel, buff, buff_len, group);
}

template <CommProtocol comm_protocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl.template asccomm_write_nbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl.template asccomm_write_nbi<config>(batch_handle, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group)
{
    return impl.template asccomm_write_nbi<config>(batch_handle, dst, src, len, group);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_value_nbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return impl.template asccomm_write_value_nbi<T, commit, config>(channel, dst, value);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_value_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value)
{
    return impl.template asccomm_write_value_nbi<T, config>(batch_handle, dst, value);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_value_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value, const coop_group& group)
{
    return impl.template asccomm_write_value_nbi<T, config>(batch_handle, dst, value, group);
}

template <CommProtocol comm_protocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_read_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl.template asccomm_read_nbi<commit, config>(channel, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_read_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return impl.template asccomm_read_nbi<config>(batch_handle, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_read_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group)
{
    return impl.template asccomm_read_nbi<config>(batch_handle, dst, src, len, group);
}

template <CommProtocol comm_protocol>
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_with_notify_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return impl.template asccomm_write_with_notify_nbi<commit, config>(channel, dst, src, len, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return impl.template asccomm_write_with_notify_nbi<config>(batch_handle, dst, src, len, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val, const coop_group& group)
{
    return impl.template asccomm_write_with_notify_nbi<config>(
        batch_handle, dst, src, len, notify_addr, notify_val, group);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_faa_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
{
    return impl.template asccomm_atomic_faa_nbi<T, commit, config>(channel, dst, fetch_addr, add_val);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_faa_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
{
    return impl.template asccomm_atomic_faa_nbi<T, config>(batch_handle, dst, fetch_addr, add_val);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_faa_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val, const coop_group& group)
{
    return impl.template asccomm_atomic_faa_nbi<T, config>(batch_handle, dst, fetch_addr, add_val, group);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_cas_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
{
    return impl.template asccomm_atomic_cas_nbi<T, commit, config>(channel, dst, fetch_addr, compare_val, swap_val);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_cas_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
{
    return impl.template asccomm_atomic_cas_nbi<T, config>(batch_handle, dst, fetch_addr, compare_val, swap_val);
}

template <CommProtocol comm_protocol>
template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_atomic_cas_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val,
    const coop_group& group)
{
    return impl.template asccomm_atomic_cas_nbi<T, config>(batch_handle, dst, fetch_addr, compare_val, swap_val, group);
}

template <CommProtocol comm_protocol>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_batch_commit(ubc_ctp_batch_handle& batch_handle)
{
    return impl.asccomm_batch_commit(batch_handle);
}

template <CommProtocol comm_protocol>
template <typename coop_group>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_batch_commit(
    ubc_ctp_batch_handle& batch_handle, const coop_group& group)
{
    return impl.asccomm_batch_commit(batch_handle, group);
}

template <CommProtocol comm_protocol>
template <auto pipe>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_drain(ChannelHandle channel)
{
    return impl.template asccomm_drain<pipe>(channel);
}

template <CommProtocol comm_protocol>
template <auto pipe>
__simt_callee__ inline int32_t asccomm<comm_protocol>::asccomm_drain(ubc_ctp_batch_handle& batch_handle)
{
    return impl.template asccomm_drain<pipe>(batch_handle);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_local_buffer_addr(ChannelHandle channel, uint32_t buffer_idx)
{
    return asccomm_simt_local_buffer_addr(channel, buffer_idx);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_remote_buffer_addr(ChannelHandle channel, uint32_t buffer_idx)
{
    return asccomm_simt_remote_buffer_addr(channel, buffer_idx);
}

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_SIMT_IMPL_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_IMPL_H
#endif
