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
 * \file asccomm_aiv_urma_def.h
 * \brief asccomm AIV URMA definition for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include hcomm/hcomm.h instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {

constexpr uint32_t asccomm_urma_max_retry_times = 1000000;
constexpr uint32_t asccomm_urma_tmp_buf_size = 512;
constexpr uint32_t asccomm_urma_wqe_u32_num = 32;
constexpr uint32_t asccomm_urma_cqe_u32_num = 16;
constexpr uint32_t asccomm_urma_udf_flag = 0x80U;
constexpr uint32_t poll_cq_threshold = 10;
constexpr uint32_t num_cqe_per_poll_cq = 100;
#if defined(UT_TEST)
constexpr uint32_t asccomm_urma_mutex_id = 27U;
#else
constexpr uint32_t asccomm_urma_mutex_id = 29U;
#endif

constexpr uint32_t asccomm_urma_invalid_reduce_data_type = 0xFFFFFFFFU;
template <typename t>
constexpr uint32_t asccomm_urma_reduce_data_type =
    std::is_same<t, int8_t>::value     ? 0x0U :
    std::is_same<t, int16_t>::value    ? 0x1U :
    std::is_same<t, int32_t>::value    ? 0x2U :
    std::is_same<t, uint32_t>::value   ? 0x5U :
    std::is_same<t, half>::value       ? 0x6U :
    std::is_same<t, float>::value      ? 0x7U :
    std::is_same<t, bfloat16_t>::value ? 0x8U :
                                         asccomm_urma_invalid_reduce_data_type;

template <typename t>
struct udma_params {
    t value;
    t cond;
    uint32_t reduce_data_type;
    uint32_t reduce_opcode;
};

template <>
class asccomm_impl<comm_protocol_ubc_ctp> {
public:
    __aicore__ inline asccomm_impl();
    __aicore__ inline ~asccomm_impl();
    __aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename t>
    __aicore__ inline int32_t init(const LocalTensor<t>& buff, uint32_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        typename t, asccomm_urma_reduce_op reduce_op, bool commit = true, pipe_t commit_pipe = PIPE_S,
        pipe_t req_pipe = PIPE_MTE3, auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_reduce_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t read_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        typename t, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_inline_cfg>
    __aicore__ inline int32_t write_value_nbi(channel_handle channel, GM_ADDR dst, t value);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t write_with_notify_nbi(
        channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);
    template <
        typename t, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t atomic_faa(channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t add_val);
    template <
        typename t, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = urma_default_cfg>
    __aicore__ inline int32_t atomic_cas(channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t compare_val, t swap_val);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t commit(channel_handle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(channel_handle channel);

private:
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        asccomm_urma_op_code op_code = asccomm_urma_op_code::write, auto const& config = urma_default_cfg, typename t = uint64_t>
    __aicore__ inline int32_t post_send(
        channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr = nullptr,
        const udma_params<t>& params = udma_params<t>{});
    __aicore__ inline void commit_impl(channel_handle channel, const sq_context& sq_ctx, uint32_t sq_head, uint32_t cqe_cnt);
    __aicore__ inline void poll_cq_when_sq_overflow(
        channel_handle channel, const sq_context& sq_ctx, const cq_context& cq_ctx, uint32_t sq_head, uint32_t cqe_cnt);
    __aicore__ inline uint32_t poll_cq(channel_handle channel, uint32_t expect_tail);

private:
    LocalTensor<uint32_t> wqe_item_;
    LocalTensor<uint32_t> cqe_item_;
};
} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_DEF_H
#endif
