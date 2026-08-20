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
 * \brief asccomm SIMT base class
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H

#include "hcomm_simt_inner_def.h"

namespace AscendC::simt {

// Primary template: every protocol without a specialization fails. comm_protocol_ubc_ctp is
// specialized in impl/hcomm_simt_urma_def.h.
template <comm_protocol comm_protocol>
class asccomm_impl {
public:
    __simt_callee__ inline asccomm_impl() {}
    __simt_callee__ inline ~asccomm_impl() {}

    __simt_callee__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return asccomm_failed;
    }

    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t write_nbi(channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return asccomm_failed;
    }

    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t read_nbi(channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        return asccomm_failed;
    }

    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t write_with_notify_nbi(
        channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val)
    {
        (void)channel;
        (void)dst;
        (void)src;
        (void)len;
        (void)notify_addr;
        (void)notify_val;
        return asccomm_failed;
    }

    template <typename t, bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t atomic_faa(channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t add_val)
    {
        (void)channel;
        (void)dst;
        (void)fetch_addr;
        (void)add_val;
        return asccomm_failed;
    }

    template <typename t, bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t atomic_cas(
        channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t compare_val, t swap_val)
    {
        (void)channel;
        (void)dst;
        (void)fetch_addr;
        (void)compare_val;
        (void)swap_val;
        return asccomm_failed;
    }

    template <auto pipe = 0>
    __simt_callee__ inline int32_t drain(channel_handle channel)
    {
        (void)channel;
        return asccomm_failed;
    }
};

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_BASE_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_BASE_H
#endif
