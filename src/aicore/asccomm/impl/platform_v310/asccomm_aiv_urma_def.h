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
 * \brief Hcomm AIV URMA definition for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include hcomm/hcomm.h instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_DEF_H

#include "../../common/asccomm_inner_def.h"

namespace asc {

constexpr uint32_t ASCCOMM_URMA_MAX_RETRY_TIMES = 100000000;
constexpr uint32_t ASCCOMM_URMA_POLL_CQ_HEARTBEAT_TIMES = 1000000;
constexpr uint32_t ASCCOMM_URMA_ADDR_ALIGNMENT = 32U;
constexpr uint32_t ASCCOMM_URMA_TMP_BUF_SIZE = 512;
// Deprecated pre-rename spelling, kept for source compatibility of 9.2.0-era code; removed after 2027/12/31.
constexpr auto HCOMM_URMA_TMP_BUF_SIZE = ASCCOMM_URMA_TMP_BUF_SIZE;
constexpr uint32_t ASCCOMM_URMA_WQE_U32_NUM = 32;
constexpr uint32_t ASCCOMM_URMA_CQE_U32_NUM = 16;
constexpr uint32_t ASCCOMM_URMA_UDF_FLAG = 0x80U;
constexpr uint32_t POLL_CQ_THRESHOLD = 10;
constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
#if defined(UT_TEST)
constexpr uint32_t ASCCOMM_URMA_MUTEX_ID = 27U;
#else
constexpr uint32_t ASCCOMM_URMA_MUTEX_ID = 29U;
#endif
constexpr uint32_t ASCCOMM_URMA_WQEBB_SIZE = 64U;
constexpr uint32_t ASCCOMM_URMA_WQEBB_U32_NUM = ASCCOMM_URMA_WQEBB_SIZE / sizeof(uint32_t);
constexpr uint32_t ASCCOMM_URMA_BATCH_MIN_SGE_NUM = 1U;
constexpr uint32_t ASCCOMM_URMA_BATCH_MAX_WQE_BYTES = 256U;
constexpr uint32_t ASCCOMM_URMA_BATCH_MAX_WQEBB_NUM = ASCCOMM_URMA_BATCH_MAX_WQE_BYTES / ASCCOMM_URMA_WQEBB_SIZE;
constexpr uint32_t ASCCOMM_URMA_BATCH_DATA_MAX_SGE_NUM =
    (ASCCOMM_URMA_BATCH_MAX_WQE_BYTES - sizeof(asccomm_urma_sqe_ctx)) / sizeof(asccomm_urma_sge_ctx);
constexpr uint32_t ASCCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM =
    (ASCCOMM_URMA_BATCH_MAX_WQE_BYTES - sizeof(asccomm_urma_sqe_ctx) - sizeof(asccomm_urma_notify_ctx)) /
    sizeof(asccomm_urma_sge_ctx);

constexpr uint32_t ASCCOMM_URMA_INVALID_REDUCE_DATA_TYPE = 0xFFFFFFFFU;
template <typename T>
constexpr uint32_t ASCCOMM_URMA_REDUCE_DATA_TYPE =
    std::is_same<T, int8_t>::value     ? 0x0U :
    std::is_same<T, int16_t>::value    ? 0x1U :
    std::is_same<T, int32_t>::value    ? 0x2U :
    std::is_same<T, uint32_t>::value   ? 0x5U :
    std::is_same<T, half>::value       ? 0x6U :
    std::is_same<T, float>::value      ? 0x7U :
    std::is_same<T, bfloat16_t>::value ? 0x8U :
                                         ASCCOMM_URMA_INVALID_REDUCE_DATA_TYPE;

template <typename T>
struct udma_params {
    T value;
    T cond;
    uint32_t reduce_data_type;
    uint32_t reduce_opcode;
};

template <>
class asccomm_impl<COMM_PROTOCOL_UB_CTP> {
public:
    __aicore__ inline asccomm_impl();
    __aicore__ inline ~asccomm_impl();
    __aicore__ inline int32_t asccomm_init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename T>
    __aicore__ inline int32_t asccomm_init(const LocalTensor<T>& buff, uint32_t len);
    __aicore__ inline ubc_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    __aicore__ inline ubc_multi_batch_handle asccomm_make_batch_handle(
        multi_channel_handle multi_channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr,
        GM_ADDR local_addr);
    template <typename U>
    __aicore__ inline ubc_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    template <typename U>
    __aicore__ inline ubc_multi_batch_handle asccomm_make_batch_handle(
        multi_channel_handle multi_channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr,
        GM_ADDR local_addr);
    __aicore__ inline ubc_batch_handle& asccomm_get_handle_ref(
        ubc_batch_handle& batch_handle, uint32_t channel_index, GM_ADDR remote_addr);
    __aicore__ inline ubc_batch_handle& asccomm_get_handle_ref(
        ubc_multi_batch_handle& multi_batch_handle, uint32_t channel_index, GM_ADDR remote_addr);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(
        ubc_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);
    template <
        typename T, asccomm_urma_reduce_op reduce_op, bool commit = true, pipe_t commit_pipe = PIPE_S,
        pipe_t req_pipe = PIPE_MTE3, auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_reduce_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(
        ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(
        ubc_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t asccomm_write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
        uint64_t notify_val);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notify_addr,
        uint64_t notify_val);
    template <auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ubc_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
        uint64_t notify_val);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_atomic_faa_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_atomic_cas_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t asccomm_commit(ChannelHandle channel);
    __aicore__ inline int32_t asccomm_batch_commit(ubc_batch_handle& batch_handle);
    __aicore__ inline int32_t asccomm_batch_commit(ubc_multi_batch_handle& batch_handle);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t asccomm_drain(ChannelHandle channel);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t asccomm_drain(ubc_batch_handle& batch_handle);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t asccomm_drain(ubc_multi_batch_handle& batch_handle);
    __aicore__ inline int32_t asccomm_lock(ChannelHandle channel);
    __aicore__ inline int32_t asccomm_unlock(ChannelHandle channel);

private:
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        asccomm_urma_op_code op_code = asccomm_urma_op_code::WRITE, auto const& config = URMA_DEFAULT_CFG,
        typename T = uint64_t>
    __aicore__ inline int32_t post_send(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr = nullptr,
        const udma_params<T>& params = udma_params<T>{});
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        asccomm_urma_op_code op_code = asccomm_urma_op_code::WRITE, auto const& config = URMA_DEFAULT_CFG,
        typename T = uint64_t>
    __aicore__ inline int32_t post_send(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* local_descs, uint32_t sge_num,
        GM_ADDR notify_addr = nullptr, const udma_params<T>& params = udma_params<T>{});
    template <asccomm_urma_op_code op_code, auto const& config>
    __aicore__ inline int32_t batch_post_send(
        ubc_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t sge_num,
        GM_ADDR notify_addr = nullptr, uint64_t notify_val = 0);
    template <asccomm_urma_op_code op_code, auto const& config>
    __aicore__ inline int32_t batch_post_send(
        ubc_batch_handle& batch_handle, GM_ADDR remote_addr, GM_ADDR local_addr, uint32_t len,
        GM_ADDR notify_addr = nullptr, uint64_t notify_val = 0);
    __aicore__ inline void commit_impl(
        ChannelHandle channel, const SqContext& sq_ctx, uint32_t sq_head, uint32_t cqe_cnt);
    __aicore__ inline void poll_cq_when_cq_overflow(
        ChannelHandle channel, const SqContext& sq_ctx, const CqContext& cq_ctx, uint32_t sq_head, uint32_t cqe_cnt);
    __aicore__ inline void poll_cq_when_sq_overflow(ChannelHandle channel, const SqContext& sq_ctx, uint32_t sq_head);
    template <bool sq_safe_mode = false>
    __aicore__ inline uint32_t poll_cq_impl(
        uint64_t cq_base_addr, uint32_t cqe_size, uint32_t cq_depth, uint32_t expect_tail, uint32_t& cur_tail,
        uint32_t& sq_tail, uint32_t sq_head = 0, uint32_t sq_depth = 0, uint32_t threshold = 0);
    template <bool sq_safe_mode = false>
    __aicore__ inline uint32_t poll_cq(
        ChannelHandle channel, uint32_t expect_idx, uint32_t sq_head = 0, uint32_t sq_depth = 0,
        uint32_t threshold = 0);
    __aicore__ inline uint32_t poll_batch_cq(ubc_batch_handle& batch_handle, uint32_t expect_tail);

private:
    __ubuf__ uint32_t* wqe_item = nullptr;
};
} // namespace asc

// Compatibility bridge for pre-rename AscendC-namespace spellings. Declared next to the
// definitions so the aliases share the same platform gating as the entities; 9.2.0-era
// code built against the asccomm/jetty public headers resolves the old spellings here
// instead of through the legacy hcomm header. Removed together with the legacy API.
namespace AscendC {
using asc::HCOMM_URMA_TMP_BUF_SIZE;
} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_DEF_H
#endif
