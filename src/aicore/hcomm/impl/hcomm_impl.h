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
 * \brief asccomm implementation
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H

#if __NPU_ARCH__ == 2201
#include "platform_v220/hcomm_aiv.h"
#elif __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_aiv_roce.h"
#include "platform_v310/hcomm_aiv_urma.h"
#endif

namespace AscendC {

template <comm_protocol comm_protocol>
__aicore__ inline int32_t asccomm<comm_protocol>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    return impl_.init(buff, len);
}

template <comm_protocol comm_protocol>
template <typename t>
__aicore__ inline int32_t asccomm<comm_protocol>::init(const LocalTensor<t>& buff, uint32_t len)
{
    return impl_.init(buff, len);
}

template <comm_protocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl_.template write_nbi<commit, commit_pipe, req_pipe, config>(channel, dst, src, len);
}

template <comm_protocol comm_protocol>
template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_value_nbi(channel_handle channel, GM_ADDR dst, t value)
{
    return impl_.template write_value_nbi<t, commit, commit_pipe, req_pipe, config>(channel, dst, value);
}

template <comm_protocol comm_protocol>
template <typename t, asccomm_urma_reduce_op reduce_op, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_reduce_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    return impl_.template write_reduce_nbi<t, reduce_op, commit, commit_pipe, req_pipe, config>(channel, dst, src, count);
}

template <comm_protocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::write_with_notify_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    return impl_.template write_with_notify_nbi<commit, commit_pipe, req_pipe, config>(
        channel, dst, src, len, notify_addr, notify_val);
}

template <comm_protocol comm_protocol>
template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::read_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return impl_.template read_nbi<commit, commit_pipe, req_pipe, config>(channel, dst, src, len);
}

template <comm_protocol comm_protocol>
template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::atomic_faa(
    channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t add_val)
{
    return impl_.template atomic_faa<t, commit, commit_pipe, req_pipe, config>(channel, dst, fetch_addr, add_val);
}

template <comm_protocol comm_protocol>
template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm<comm_protocol>::atomic_cas(
    channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t compare_val, t swap_val)
{
    return impl_.template atomic_cas<t, commit, commit_pipe, req_pipe, config>(
        channel, dst, fetch_addr, compare_val, swap_val);
}

template <comm_protocol comm_protocol>
template <pipe_t pipe>
__aicore__ inline int32_t asccomm<comm_protocol>::commit(channel_handle channel)
{
    return impl_.template commit<pipe>(channel);
}

template <comm_protocol comm_protocol>
template <pipe_t pipe>
__aicore__ inline int32_t asccomm<comm_protocol>::drain(channel_handle channel)
{
    return impl_.template drain<pipe>(channel);
}
} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_IMPL_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_IMPL_H
#endif
