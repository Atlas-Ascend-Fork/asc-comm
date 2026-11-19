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
 * \brief Hcomm AIV RoCE definition for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_DEF_H

#include "../../common/asccomm_inner_def.h"

namespace asc {
constexpr uint32_t ASCCOMM_ROCE_WQEBB_SIZE = 64U;
constexpr uint32_t ASCCOMM_ROCE_ADDR_ALIGNMENT = 32U;
constexpr uint32_t ASCCOMM_ROCE_WQEBB_U32_NUM = ASCCOMM_ROCE_WQEBB_SIZE / sizeof(uint32_t);
constexpr uint32_t ASCCOMM_ROCE_WQEBB_U64_SIZE = 8U;
constexpr uint32_t ASCCOMM_ROCE_WQEBB_U64_NUM = ASCCOMM_ROCE_WQEBB_SIZE / ASCCOMM_ROCE_WQEBB_U64_SIZE;
constexpr uint32_t ROCE_SQ_INDEX_MASK = 0xffffU;

constexpr uint32_t ROCE_1825_WQE_BYTE_BITS = 8U;
constexpr uint32_t ROCE_1825_WQE_HALFWORD_BITS = 16U;
constexpr uint32_t ROCE_1825_WQE_WORD_BITS = 32U;
constexpr uint32_t ROCE_1825_WQE_OWNER_SHIFT = 7U;   // owner_sl owner bit
constexpr uint32_t ROCE_1825_WQE_CTRL_VALUE = 0x40;  // owner_sl fixed part
constexpr uint32_t ROCE_1825_WQE_SQ_VA_VALUE = 0x20; // df_tsl VA bit
constexpr uint32_t ROCE_1825_WQE_SQ_SIGNAL_SHIFT = 7U;
constexpr uint32_t ROCE_1825_WQE_TASK_OPTYPE_SHIFT = 24U;
constexpr uint32_t ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT = 29U;
constexpr uint32_t ROCE_1825_WQE_TASK_FENCE_SHIFT = 30U;
constexpr uint32_t ROCE_1825_WQE_CMP_TASK_LEN1 = 1U;
constexpr uint32_t ROCE_1825_WQE_CMP_TASK_LEN_SHIFT = 28U; // cl_pi CL field
constexpr uint32_t ROCE_1825_WQE_TASK_SEG_ALIGN = ASCCOMM_ROCE_WQEBB_U64_SIZE;
constexpr uint32_t ROCE_1825_WQE_FAST_DMA_SHIFT = 10U;
constexpr uint32_t ROCE_1825_WQE_SSN_MASK = 0x3;  // low 2 bits of SQ WQE sequence number
constexpr uint32_t ROCE_1825_WQE_SSN_SHIFT = 12U; // wf_bdsl wqe_ssn field
constexpr uint32_t ROCE_1825_WQE_DATA_SEG_SHIFT = 4U;
constexpr uint32_t ROCE_1825_WQE_SECTION_ALIGN_SHIFT = 3U;
constexpr uint32_t ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN = 4U;
constexpr uint32_t ROCE_1825_WQE_NEXT_SGE_INVALID = 1U << 31U; // data seg le_key L bit
constexpr uint64_t ROCE_1825_WQE_MAX_DATA_LEN = 1ULL << 31U;

constexpr uint32_t ROCE_1825_SQ_DB_PI_HIGH_SHIFT = 8U; // high 8 bits of the SQ producer index
constexpr uint32_t ROCE_1825_SQ_DB_TYPE = 21U;
constexpr uint32_t ROCE_1825_SQ_DB_SGIT_IDX = 1U;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_COS_SHIFT = 24U;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_MTUSHIFT_SHIFT = 50U;
constexpr uint32_t ROCE_1825_SQ_DB_VENDOR_FIELD_MASK = 0x7;

constexpr uint32_t ROCE_1825_CQE_OPCODE_SHIFT = 27U;
constexpr uint32_t ROCE_1825_CQE_OPCODE_MASK = 0x1f;
constexpr uint32_t ROCE_1825_CQE_OPTYPE_INVALID = 0x1f;
constexpr uint32_t ROCE_1825_CQE_OPTYPE_ERROR = 0x1e;       // op_type = error coding
constexpr uint32_t ROCE_1825_CQE_UPDATE_CI_MASK = 0xffffff; // CQ consumer index is 24-bit
constexpr uint32_t ROCE_1825_CQE_OWNER_SHIFT = 31U;         // owner bit at dw1[31]
constexpr uint32_t ROCE_1825_CQE_QPN_BITS = 20U;
constexpr uint32_t ROCE_1825_CQE_QPN_MASK = (1U << ROCE_1825_CQE_QPN_BITS) - 1U;
constexpr uint32_t ROCE_1825_CQE_SYNDROME_WORD_INDEX = 7U; // dw8, zero-based word index
constexpr uint32_t ROCE_1825_CQE_SYNDROME_MASK = 0xffU;
constexpr uint32_t ROCE_1825_CQE_WQE_COUNTER_SHIFT = 16U;
constexpr uint32_t ROCE_1825_CQE_ERR_CODE_MASK = 0xfU;
constexpr uint32_t ROCE_1825_CQE_OP_SR_WQEBB_SHIFT = ROCE_1825_WQE_WORD_BITS; // dw1 in the dw0/dw1 pair

constexpr uint32_t ROCE_1825_MAX_SGE_NUM = 24U;
constexpr int64_t ROCE_1825_SYSTEM_CYCLE_HZ = 1000ULL * 1000 * 1000;
constexpr int64_t ROCE_1825_POLL_CQ_TIMEOUT_SECONDS = 60ULL;
constexpr int64_t ROCE_1825_POLL_CQ_TIMEOUT_CYCLES = ROCE_1825_POLL_CQ_TIMEOUT_SECONDS * ROCE_1825_SYSTEM_CYCLE_HZ;

enum class asccomm_roce_op_type : uint32_t { WRITE = 4U, READ = 8U };

// Compatibility alias for the pre-rename spelling.
using HcommRoceOpType = asccomm_roce_op_type;

enum class asccomm_cqe_status : int32_t {
    NOT_READY = 0,
    SUCCESS = 1,
    ERROR = 2,
    INVALID = 3,
};

template <>
class asccomm_impl<COMM_PROTOCOL_ROCE> {
public:
    __aicore__ inline asccomm_impl() {}
    __aicore__ inline ~asccomm_impl() {}
    __aicore__ inline int32_t asccomm_init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename T>
    __aicore__ inline int32_t asccomm_init(const LocalTensor<T>& buff, uint32_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t asccomm_commit(ChannelHandle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t asccomm_drain(ChannelHandle channel);
    __aicore__ inline int32_t asccomm_lock(ChannelHandle channel);
    __aicore__ inline int32_t asccomm_unlock(ChannelHandle channel);
    __aicore__ inline roce_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    template <typename U>
    __aicore__ inline roce_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    __aicore__ inline roce_batch_handle& asccomm_get_handle_ref(
        roce_batch_handle& batch_handle, uint32_t channel_index, GM_ADDR remote_addr);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(
        roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_write_nbi(
        roce_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t asccomm_read_nbi(
        roce_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
    __aicore__ inline int32_t asccomm_batch_commit(roce_batch_handle& batch_handle);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t asccomm_drain(roce_batch_handle& batch_handle);

private:
    template <auto const& config>
    __aicore__ inline void fill_ctrl_seg(__ubuf__ roce_wqe_entry* wqe_ptr, uint32_t wqebb_index, uint32_t sq_depth);
    template <auto const& config, asccomm_roce_op_type op_type>
    __aicore__ inline void fill_task_seg(
        __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR dst, uint64_t len, uint32_t r_key, uint32_t l_key);
    __aicore__ inline void fill_data_seg(__ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR src, uint64_t len, uint32_t l_key);
    __aicore__ inline void write_invalid_wqebb(__gm__ uint8_t* sq_addr, uint32_t sq_head, uint32_t sq_depth);
    template <auto const& config, asccomm_roce_op_type op_type>
    __aicore__ inline int32_t make_wqe(
        __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t sq_head, uint32_t sq_depth);
    template <bool commit, auto const& config, asccomm_roce_op_type op_type>
    __aicore__ inline int32_t post_send(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    __aicore__ inline uint64_t get_db_value(uint32_t sq_head, uint32_t qpn, uint64_t vendor);
    __aicore__ inline void knock_door_bell(__gm__ ChannelEntity* chnl_ptr, uint32_t sq_head);
    __aicore__ inline bool check_cqe_owner(uint32_t owner_id_qpn, uint32_t cq_tail, uint32_t depth);
    template <bool sq_safe_mode = false>
    __aicore__ inline int32_t poll_cq(__gm__ ChannelEntity* chnl_ptr, uint32_t expect_idx, uint32_t threshold = 0);
    template <bool sq_safe_mode = false>
    __aicore__ inline bool en_continue(
        uint32_t expect_idx, uint32_t cq_tail, uint16_t sq_head, uint16_t sq_tail, uint32_t sq_depth,
        uint32_t threshold);
    __aicore__ inline asccomm_cqe_status wait_cqe_and_update_sq_tail(
        __gm__ uint32_t* cqe_words, uint32_t cq_tail, uint32_t cq_depth, uint32_t expected_qpn, uint16_t wqebb_head,
        uint16_t& wqebb_tail, uint32_t& cqe_type);
    __aicore__ inline bool check_channel_param(__gm__ ChannelEntity* chnl_ptr);
    template <auto const& config, asccomm_roce_op_type op_type>
    __aicore__ inline __ubuf__ uint64_t* fill_batch_wqe_header(
        __ubuf__ uint64_t* wqe, GM_ADDR remote_addr, uint64_t total_len, uint32_t base_wqebb_index, uint32_t sq_depth,
        uint16_t wf_bdsl_be, uint64_t key_word);
    __aicore__ inline void fill_batch_data_seg(
        __ubuf__ uint64_t* sge, GM_ADDR addr, uint32_t len, uint32_t data_key_be, bool last_sge);
    template <auto const& config>
    __aicore__ inline void update_batch_cursor(roce_batch_handle& batch_handle, uint32_t wqebb_advance);
    __aicore__ inline bool has_batch_sq_capacity(
        __gm__ ChannelEntity* chnl_ptr, const roce_batch_handle& batch_handle, uint32_t additional_wqebb_num = 0U);
    __aicore__ inline bool is_batch_sq_near_full(__gm__ ChannelEntity* chnl_ptr, const roce_batch_handle& batch_handle);
    template <asccomm_roce_op_type op_type, auto const& config>
    __aicore__ inline int32_t batch_post_send(roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <asccomm_roce_op_type op_type, auto const& config>
    __aicore__ inline int32_t batch_post_send(
        roce_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t seg_num);
    __aicore__ inline int32_t poll_batch_cq(roce_batch_handle& batch_handle, uint32_t expect_idx);

private:
    __ubuf__ uint32_t* wqe_ub = nullptr;
};
} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_DEF_H
#endif
