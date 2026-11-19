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
 * \file asccomm_simt_base.h
 * \brief Hcomm SIMT base class
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_BASE_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_BASE_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_BASE_H

#include "asccomm_simt_inner_def.h"

namespace asc::simt {
using namespace AscendC;

// Primary template: every protocol without a specialization fails. COMM_PROTOCOL_UB_CTP is
// specialized in impl/asccomm_simt_urma_def.h.
template <CommProtocol comm_protocol>
class asccomm_impl {
public:
    __simt_callee__ inline asccomm_impl() {}
    __simt_callee__ inline ~asccomm_impl() {}

    __simt_callee__ inline int32_t asccomm_init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return ASCCOMM_FAILED;
    }

    __simt_callee__ inline ubc_ctp_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len)
    {
        (void)channel;
        (void)buff;
        (void)buff_len;
        return {};
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_value_nbi(ChannelHandle channel, __gm__ void* dst, T value)
    {
        (void)channel;
        (void)dst;
        (void)value;
        return ASCCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)batch_handle;
        (void)dst;
        (void)src;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t asccomm_write_value_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value)
    {
        (void)batch_handle;
        (void)dst;
        (void)value;
        return ASCCOMM_FAILED;
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_read_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        (void)notify_addr;
        (void)notify_val;
        return ASCCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_read_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)batch_handle;
        (void)dst;
        (void)src;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_with_notify_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val)
    {
        (void)batch_handle;
        (void)dst;
        (void)src;
        (void)len;
        (void)notify_addr;
        (void)notify_val;
        return ASCCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_faa_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
    {
        (void)channel;
        (void)dst;
        (void)fetch_addr;
        (void)add_val;
        return ASCCOMM_FAILED;
    }

    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_cas_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
    {
        (void)channel;
        (void)dst;
        (void)fetch_addr;
        (void)compare_val;
        (void)swap_val;
        return ASCCOMM_FAILED;
    }

    template <auto pipe = 0>
    __simt_callee__ inline int32_t asccomm_drain(ChannelHandle channel)
    {
        (void)channel;
        return ASCCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_faa_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
    {
        (void)batch_handle;
        (void)dst;
        (void)fetch_addr;
        (void)add_val;
        return ASCCOMM_FAILED;
    }

    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_cas_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
    {
        (void)batch_handle;
        (void)dst;
        (void)fetch_addr;
        (void)compare_val;
        (void)swap_val;
        return ASCCOMM_FAILED;
    }

    __simt_callee__ inline int32_t asccomm_batch_commit(ubc_ctp_batch_handle& batch_handle)
    {
        (void)batch_handle;
        return ASCCOMM_FAILED;
    }

    template <auto pipe = 0>
    __simt_callee__ inline int32_t asccomm_drain(ubc_ctp_batch_handle& batch_handle)
    {
        (void)batch_handle;
        return ASCCOMM_FAILED;
    }
};

// Backward-compatibility alias: the legacy public hcomm/hcomm_simt.h header declares its impl
// member as HcommImpl. The legacy adapters and the new asccomm* interfaces share the same
// underlying asccomm_impl implementation.

} // namespace asc::simt

// Compatibility bridge (implementation layer) for the legacy AscendC::simt surface. See the
// bridge block in asccomm_inner_def.h.
namespace AscendC::simt {
using asc::simt::ubc_ctp_batch_handle;
template <asc::CommProtocol comm_protocol>
using HcommImpl = asc::simt::asccomm_impl<comm_protocol>;
} // namespace AscendC::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_BASE_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_BASE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_BASE_H
#endif
