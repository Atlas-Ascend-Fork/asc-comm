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
 * \file asccomm_aiv_def.h
 * \brief asccomm AIV definition for V220
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {

enum class asccomm_op_type : uint32_t { write = 3U, read = 5U };

template <>
class asccomm_impl<comm_protocol_roce> {
public:
    __aicore__ inline asccomm_impl();

    __aicore__ inline ~asccomm_impl();

    __aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len)
    {
        (void)buff;
        (void)len;
        return asccomm_failed;
    }

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t read_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);

    template <
        typename t, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_value_nbi(channel_handle channel, GM_ADDR dst, t value);

    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_with_notify_nbi(
        channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);

    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t commit(channel_handle channel)
    {
        return asccomm_failed;
    }

    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(channel_handle channel)
    {
        return asccomm_failed;
    }

private:
    __aicore__ inline void post_send(channel_handle channel_handle, GM_ADDR dst, GM_ADDR src, uint64_t len, bool is_read);

    __aicore__ inline void door_bell(__gm__ channel_entity* channel, uint64_t cur_head);

private:
    LocalTensor<uint64_t> ub_local_;
    LocalTensor<uint32_t> ub_local_head_;
};

} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_DEF_H
#endif
