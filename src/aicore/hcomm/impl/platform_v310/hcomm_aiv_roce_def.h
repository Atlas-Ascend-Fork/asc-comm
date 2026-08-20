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
 * \file asccomm_aiv_roce_def.h
 * \brief asccomm AIV RoCE definition for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_DEF_H

#include "../../common/hcomm_inner_def.h"

namespace AscendC {
constexpr uint32_t roce_cqe_pos = 128;
constexpr uint32_t roce_db_pos = 192;

constexpr uint32_t roce_1825_wqe_ctrl_value = 0x40;           // owner_sl fixed part
constexpr uint32_t roce_1825_wqe_va_value = 0x20;             // df_tsl VA bit
constexpr uint32_t roce_1825_wqe_cqe_signal_shift = 7;        // df_tsl CR bit
constexpr uint32_t roce_1825_wqe_owner_shift = 7;             // owner_sl owner bit
constexpr uint32_t roce_1825_wqe_cmp_task_len_shift = 28;     // cl_pi CL field
constexpr uint32_t roce_1825_wqe_msn_shift = 12;              // wf_bdsl wqe_msn field
constexpr uint32_t roce_1825_wqe_msn_mask = 0x3;              // low 2 bits of SQ WQE sequence number
constexpr uint32_t roce_1825_seg_len_unit = sizeof(uint64_t); // hardware section length unit = 8B
constexpr uint32_t roce_1825_wqe_data_seg_bdsl = sizeof(roce_wqe_data_seg) / roce_1825_seg_len_unit;
constexpr uint32_t roce_1825_rdma_read_last_ext_len = 4;
constexpr uint32_t roce_1825_wqe_next_sge_invalid = 1U << 31; // data seg le_key L bit

constexpr uint32_t roce_1825_sq_db_pi_high_shift = 8;   // high 8 bits of the SQ producer index
constexpr uint32_t roce_1825_sq_db_pi_field_shift = 32; // pi field offset in the 64-bit doorbell
constexpr uint32_t roce_1825_sq_db_type = 21;
constexpr uint32_t roce_1825_sq_db_sgit_idx = 1;
constexpr uint32_t roce_1825_sq_db_cos = 0x7;

constexpr uint32_t roce_1825_cqe_opcode_shift = 27;
constexpr uint32_t roce_1825_cqe_opcode_mask = 0x1f;
constexpr uint32_t roce_1825_cqe_optype_invalid = 0x1f;
constexpr uint32_t roce_1825_cqe_optype_error = 0x1e;       // op_type = error coding
constexpr uint32_t roce_1825_cqe_update_ci_mask = 0xffffff; // CQ consumer index is 24-bit
constexpr uint32_t roce_1825_cqe_owner_shift = 31;          // owner bit at dw1[31]

enum class asccomm_roce_op_type : uint32_t { write = 4U, read = 8U };

template <>
class asccomm_impl<comm_protocol_roce> {
public:
    __aicore__ inline asccomm_impl() {}
    __aicore__ inline ~asccomm_impl() {}
    __aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename t>
    __aicore__ inline int32_t init(const LocalTensor<t>& buff, uint32_t len);
    template <bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3, auto const& config>
    __aicore__ inline int32_t write_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3, auto const& config>
    __aicore__ inline int32_t read_nbi(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        typename t, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3, auto const& config>
    __aicore__ inline int32_t write_value_nbi(channel_handle channel, GM_ADDR dst, t value);
    template <bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3, auto const& config>
    __aicore__ inline int32_t write_with_notify_nbi(
        channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t commit(channel_handle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(channel_handle channel);

private:
    __aicore__ inline void fill_ctrl_seg(__ubuf__ roce_wqe_entry* wqe_ptr, uint32_t sq_head, uint32_t sq_depth);
    __aicore__ inline void fill_task_seg(
        __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR dst, uint64_t len, uint32_t op_type, uint32_t r_key, uint32_t l_key);
    __aicore__ inline void fill_data_seg(__ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR src, uint64_t len, uint32_t l_key);
    __aicore__ inline void write_invalid_wqebb(__gm__ uint8_t* sq_addr, uint32_t sq_head, uint32_t sq_depth);
    __aicore__ inline int32_t make_wqe(
        __gm__ channel_entity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t op_type, uint32_t sq_head,
        uint32_t sq_depth);
    template <bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3>
    __aicore__ inline int32_t post_send(channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t op_type);
    __aicore__ inline uint64_t get_db_value(uint32_t qpn, uint8_t mtu_shift);
    __aicore__ inline void knock_door_bell(__gm__ channel_entity* chnl_ptr, uint32_t sq_head);
    __aicore__ inline bool check_cqe_owner(__ubuf__ roce_cqe_entry* cqe_ptr, uint32_t cq_tail, uint32_t depth);
    __aicore__ inline int32_t poll_cq(__gm__ channel_entity* chnl_ptr, uint32_t expect_idx);

private:
    LocalTensor<uint8_t> wqe_ub_;
    LocalTensor<uint8_t> cqe_ub_;
};
} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_DEF_H
#endif
