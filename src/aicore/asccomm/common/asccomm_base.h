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
 * \file asccomm_base.h
 * \brief Hcomm base class
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_BASE_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_BASE_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_BASE_H

#include "asccomm_inner_def.h"

namespace asc {
using namespace AscendC;
template <CommProtocol comm_protocol>
class asccomm_impl {
public:
    __aicore__ inline asccomm_impl(){};
    __aicore__ inline ~asccomm_impl(){};
    __aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <typename T>
    __aicore__ inline int32_t init(const LocalTensor<T>& buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return ASCCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t write_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return ASCCOMM_FAILED;
    }

    template <
        typename T, asccomm_reduce_op reduce_op, bool commit = true, pipe_t commit_pipe = PIPE_S,
        pipe_t req_pipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t write_reduce_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
    {
        return ASCCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t read_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
    {
        return ASCCOMM_FAILED;
    }

    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value)
    {
        return ASCCOMM_FAILED;
    }

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
    {
        return ASCCOMM_FAILED;
    }

    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t commit(ChannelHandle channel)
    {
        return ASCCOMM_FAILED;
    }

    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(ChannelHandle channel)
    {
        return ASCCOMM_FAILED;
    }
};

// Backward-compatibility alias: the legacy public hcomm/hcomm.h header declares its impl member
// as HcommImpl. The legacy adapters and the new asccomm* interfaces share the same underlying
// asccomm_impl implementation.

} // namespace asc

// Compatibility bridge (implementation layer): exposes the shared engine type to the legacy
// AscendC surface. See the bridge block in asccomm_inner_def.h.
namespace AscendC {
using asc::asccomm_impl;
template <asc::CommProtocol comm_protocol>
using HcommImpl = asc::asccomm_impl<asc::protocol_traits<comm_protocol>::backend>;
} // namespace AscendC

#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_BASE_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_BASE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_BASE_H
#endif
