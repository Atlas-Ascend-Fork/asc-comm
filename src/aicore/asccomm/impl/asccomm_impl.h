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
 * \file asccomm_impl.h
 * \brief Hcomm asccomm interface implementation
 *
 * Defines the new asccomm* interfaces declared by the public asccomm/asccomm.h header. The legacy
 * Hcomm interface adapters in hcomm/hcomm.h forward to these asccomm* interfaces, so both sets
 * share the same underlying asccomm_impl implementation member.
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_IMPL_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_IMPL_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_IMPL_H

#if __NPU_ARCH__ == 2201
#include "platform_v220/asccomm_aiv.h"
#elif __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202
#if defined(ASCCOMM_ROCE_BACKEND_MLX5)
#include "platform_v310/asccomm_aiv_roce_mlx5.h"
#else
#include "platform_v310/asccomm_aiv_roce.h"
#endif
#include "platform_v310/asccomm_aiv_urma.h"
#endif

namespace asc {

template <CommProtocol comm_protocol>
__aicore__ inline int32_t asccomm<comm_protocol>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl.init(buff, len);
}

template <CommProtocol comm_protocol>
template <typename T>
__aicore__ inline batch_handle<T> asccomm<comm_protocol>::make_batch_handle(
    T channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    static_assert(
        protocol_traits<comm_protocol>::backend == COMM_PROTOCOL_UB_CTP ||
            protocol_traits<comm_protocol>::backend == COMM_PROTOCOL_ROCE,
        "batch_handle only supports UB_CTP, UBOE, UB_RTP and RoCE");
    auto handle = impl.make_batch_handle(channel, buff, buff_len, remote_addr, local_addr);
    using ExpectedType = batch_handle<T>;
    static_assert(IsSameType<decltype(handle), ExpectedType>::value, "Channel type and batch_handle type do not match");
    return handle;
}

template <CommProtocol comm_protocol>
template <typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline batch_handle<T>& asccomm<comm_protocol>::get_handle_ref(
    T& batch_handle_pool, uint32_t channel_idx, GM_ADDR remote_addr)
{
    return impl.get_handle_ref(batch_handle_pool, channel_idx, remote_addr);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::write_nbi(T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return impl.template write_nbi<config>(batch_handle, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::write_nbi(
    T& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return impl.template write_nbi<config>(batch_handle, dst, src_descs, src_num);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl.template write_nbi<commit, commit_pipe, req_pipe, config>(channel, dst, src, len);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_nbi(
    ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return impl.template write_nbi<commit, commit_pipe, req_pipe, config>(channel, dst, src_descs, src_num);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    return impl.template write_value_nbi<T, commit, commit_pipe, req_pipe, config>(channel, dst, value);
}

template <CommProtocol comm_protocol>
template <typename T, asccomm_reduce_op reduce_op, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_reduce_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    return impl.template write_reduce_nbi<T, reduce_op, commit, commit_pipe, req_pipe, config>(
        channel, dst, src, count);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::write_with_notify_nbi(
    T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    return impl.template write_with_notify_nbi<config>(batch_handle, dst, src, len, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::write_with_notify_nbi(
    T& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr, uint64_t notify_val)
{
    return impl.template write_with_notify_nbi<config>(batch_handle, dst, src_descs, src_num, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_with_notify_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    return impl.template write_with_notify_nbi<commit, commit_pipe, req_pipe, config>(
        channel, dst, src, len, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_with_notify_nbi(
    ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    return impl.template write_with_notify_nbi<commit, commit_pipe, req_pipe, config>(
        channel, dst, src_descs, src_num, notify_addr, notify_val);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::read_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl.template read_nbi<commit, commit_pipe, req_pipe, config>(channel, dst, src, len);
}

template <CommProtocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::read_nbi(
    ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return impl.template read_nbi<commit, commit_pipe, req_pipe, config>(channel, dst_descs, dst_num, src);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::read_nbi(T& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return impl.template read_nbi<config>(batch_handle, dst, src, len);
}

template <CommProtocol comm_protocol>
template <auto const& config, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::read_nbi(
    T& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return impl.template read_nbi<config>(batch_handle, dst_descs, dst_num, src);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::atomic_faa_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val)
{
    return impl.template atomic_faa_nbi<T, commit, commit_pipe, req_pipe, config>(channel, dst, fetch_addr, add_val);
}

template <CommProtocol comm_protocol>
template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::atomic_cas_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val)
{
    return impl.template atomic_cas_nbi<T, commit, commit_pipe, req_pipe, config>(
        channel, dst, fetch_addr, compare_val, swap_val);
}

template <CommProtocol comm_protocol>
template <pipe_t pipe>
__aicore__ inline int32_t asccomm<comm_protocol>::commit(ChannelHandle channel)
{
    return impl.template commit<pipe>(channel);
}

template <CommProtocol comm_protocol>
template <typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::batch_commit(T& batch_handle)
{
    return impl.batch_commit(batch_handle);
}

template <CommProtocol comm_protocol>
template <pipe_t pipe>
__aicore__ inline int32_t asccomm<comm_protocol>::drain(ChannelHandle channel)
{
    return impl.template drain<pipe>(channel);
}

template <CommProtocol comm_protocol>
template <pipe_t pipe, typename T, typename handle_traits<T>::ChannelType*>
__aicore__ inline int32_t asccomm<comm_protocol>::drain(T& batch_handle)
{
    return impl.template drain<pipe>(batch_handle);
}

template <CommProtocol comm_protocol>
__aicore__ inline int32_t asccomm<comm_protocol>::lock(ChannelHandle channel)
{
    static_assert(
        protocol_traits<comm_protocol>::backend == COMM_PROTOCOL_ROCE ||
            protocol_traits<comm_protocol>::uses_urma_backend,
        "lock only supports RoCE, UB_CTP, UBOE or UB_RTP");
    return impl.lock(channel);
}

template <CommProtocol comm_protocol>
__aicore__ inline int32_t asccomm<comm_protocol>::unlock(ChannelHandle channel)
{
    static_assert(
        protocol_traits<comm_protocol>::backend == COMM_PROTOCOL_ROCE ||
            protocol_traits<comm_protocol>::uses_urma_backend,
        "unlock only supports RoCE, UB_CTP, UBOE or UB_RTP");
    return impl.unlock(channel);
}
} // namespace asc

#endif // IMPL_COMM_API_AICORE_ASCCOMM_IMPL_ASCCOMM_IMPL_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_IMPL_H
#endif
