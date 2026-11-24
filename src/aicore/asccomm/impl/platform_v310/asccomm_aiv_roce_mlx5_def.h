/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file asccomm_aiv_roce_mlx5_def.h
 * \brief Asccomm AIV MLX5 RoCE definition for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Asccomm header. Please include public Asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_MLX5_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_MLX5_DEF_H

#include "../../common/asccomm_inner_def.h"

namespace asc {
constexpr uint32_t ASCCOMM_MLX5_WQEBB_SIZE = 64U;
constexpr uint32_t ASCCOMM_MLX5_CQE_SIZE = 64U;
constexpr uint32_t ASCCOMM_MLX5_WQEBB_U32_NUM = ASCCOMM_MLX5_WQEBB_SIZE / sizeof(uint32_t);
constexpr uint32_t ASCCOMM_MLX5_ADDR_ALIGNMENT = 32U;
constexpr uint32_t ASCCOMM_MLX5_SQ_INDEX_MASK = 0xffffU;
constexpr uint32_t ASCCOMM_MLX5_MAX_SGE_NUM = 24U;
constexpr uint64_t ASCCOMM_MLX5_MAX_DATA_LEN = 1ULL << 31U;

constexpr uint32_t ASCCOMM_MLX5_OPCODE_RDMA_WRITE = 0x08U;
constexpr uint32_t ASCCOMM_MLX5_OPCODE_RDMA_READ = 0x10U;
constexpr uint32_t ASCCOMM_MLX5_OPCODE_ATOMIC_CS = 0x11U;
constexpr uint32_t ASCCOMM_MLX5_OPCODE_ATOMIC_FA = 0x12U;

constexpr uint8_t ASCCOMM_MLX5_CTRL_CQ_UPDATE = 2U << 2U;
constexpr uint32_t ASCCOMM_MLX5_SND_DBR = 1U;
constexpr uint32_t ASCCOMM_MLX5_CQ_SET_CI = 0U;
constexpr uint32_t ASCCOMM_MLX5_CQ_UPDATE_CI_MASK = 0x00ffffffU;

constexpr uint8_t ASCCOMM_MLX5_CQE_INVALID = 0x0fU;
constexpr uint8_t ASCCOMM_MLX5_CQE_REQ_ERR = 0x0dU;
constexpr uint8_t ASCCOMM_MLX5_CQE_RESP_ERR = 0x0eU;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR = 0x01U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR = 0x02U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_PROT_ERR = 0x04U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_WR_FLUSH_ERR = 0x05U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_MW_BIND_ERR = 0x06U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_BAD_RESP_ERR = 0x10U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR = 0x11U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR = 0x12U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR = 0x13U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_OP_ERR = 0x14U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR = 0x15U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR = 0x16U;
constexpr uint8_t ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR = 0x22U;
constexpr uint32_t ASCCOMM_MLX5_CQE_OP_OWN_INDEX = 63U;
constexpr uint32_t ASCCOMM_MLX5_CQE_WQE_COUNTER_INDEX = 60U;
constexpr uint32_t ASCCOMM_MLX5_CQE_VENDOR_SYNDROME_INDEX = 54U;
constexpr uint32_t ASCCOMM_MLX5_CQE_SYNDROME_INDEX = 55U;

constexpr uint32_t ASCCOMM_MLX5_INLINE_SEG = 1U << 31U;
constexpr int64_t ASCCOMM_MLX5_SYSTEM_CYCLE_HZ = 1000ULL * 1000 * 1000;
constexpr int64_t ASCCOMM_MLX5_POLL_CQ_TIMEOUT_SECONDS = 60ULL;
constexpr int64_t ASCCOMM_MLX5_POLL_CQ_TIMEOUT_CYCLES =
    ASCCOMM_MLX5_POLL_CQ_TIMEOUT_SECONDS * ASCCOMM_MLX5_SYSTEM_CYCLE_HZ;

#pragma pack(push, 1)
struct roce_mlx5_wqe_ctrl_seg {
    uint32_t opmod_idx_opcode;
    uint32_t qpn_ds;
    uint8_t signature;
    uint16_t dci_stream_channel_id;
    uint8_t fm_ce_se;
    uint32_t imm;
};

struct roce_mlx5_wqe_raddr_seg {
    uint64_t raddr;
    uint32_t rkey;
    uint32_t reserved;
};

struct roce_mlx5_wqe_data_seg {
    uint32_t byte_count;
    uint32_t lkey;
    uint64_t addr;
};

struct roce_mlx5_wqe_atomic_seg {
    uint64_t swap_add;
    uint64_t compare;
};

struct roce_mlx5_wqe_inl_data_seg {
    uint32_t byte_count;
};
#pragma pack(pop)

static_assert(sizeof(roce_mlx5_wqe_ctrl_seg) == 16U);
static_assert(sizeof(roce_mlx5_wqe_raddr_seg) == 16U);
static_assert(sizeof(roce_mlx5_wqe_data_seg) == 16U);
static_assert(sizeof(roce_mlx5_wqe_atomic_seg) == 16U);
static_assert(sizeof(roce_mlx5_wqe_inl_data_seg) == 4U);

// Common RDMA WRITE/READ WQE prefix: control segment plus remote address segment.
constexpr uint32_t ASCCOMM_MLX5_PUBLIC_SEG_BYTES = sizeof(roce_mlx5_wqe_ctrl_seg) + sizeof(roce_mlx5_wqe_raddr_seg);
// One non-inline local buffer descriptor occupies one 16-byte data segment.
constexpr uint32_t ASCCOMM_MLX5_DATA_SEG_BYTES = sizeof(roce_mlx5_wqe_data_seg);

enum class asccomm_mlx5_op_type : uint32_t {
    WRITE = ASCCOMM_MLX5_OPCODE_RDMA_WRITE,
    READ = ASCCOMM_MLX5_OPCODE_RDMA_READ,
    ATOMIC_FAA = ASCCOMM_MLX5_OPCODE_ATOMIC_FA,
    ATOMIC_CAS = ASCCOMM_MLX5_OPCODE_ATOMIC_CS,
};

enum class asccomm_mlx5_cqe_status : int32_t {
    NOT_READY = 0,
    SUCCESS = 1,
    ERROR = 2,
    INVALID = 3,
};

enum class asccomm_mlx5_wc_status : int32_t {
    WC_SUCCESS = 0,
    WC_LOC_LEN_ERR,
    WC_LOC_QP_OP_ERR,
    WC_LOC_PROT_ERR,
    WC_WR_FLUSH_ERR,
    WC_MW_BIND_ERR,
    WC_BAD_RESP_ERR,
    WC_LOC_ACCESS_ERR,
    WC_REM_INV_REQ_ERR,
    WC_REM_ACCESS_ERR,
    WC_REM_OP_ERR,
    WC_RETRY_EXC_ERR,
    WC_RNR_RETRY_EXC_ERR,
    WC_REM_ABORT_ERR,
    WC_GENERAL_ERR,
};

enum class roce_sge_mode : uint32_t {
    SINGLE,
    MULTI,
};

struct roce_mlx5_batch_state {
    // WQEBB offset of the last request in the batch staging buffer. MLX5
    // BlueFlame/UAR requires the control qword from the last WQE, not from the
    // last WQEBB when a multi-SGE WQE spans multiple WQEBBs.
    uint32_t last_wqe_wqebb_idx;
    uint32_t reserved;
};

static_assert(
    sizeof(roce_mr_key) + sizeof(roce_mlx5_batch_state) <= sizeof(batch_remote_info),
    "RoCE MR key and MLX5 batch state must not grow ubc_batch_handle");

template <>
class asccomm_impl<COMM_PROTOCOL_ROCE> {
public:
    __aicore__ inline asccomm_impl() = default;
    __aicore__ inline ~asccomm_impl() = default;
    __aicore__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);
    template <typename T>
    __aicore__ inline int32_t init(const LocalTensor<T>& buff, uint32_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t write_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t read_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t write_nbi(
        ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t read_nbi(ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = URMA_INLINE_CFG>
    __aicore__ inline int32_t write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value);
    template <
        bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t write_with_notify_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t atomic_faa_nbi(ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val);
    template <
        typename T, bool commit = true, pipe_t commit_pipe = PIPE_S, pipe_t req_pipe = PIPE_MTE3,
        auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t atomic_cas_nbi(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val);
    template <pipe_t pipe = PIPE_S>
    __aicore__ inline int32_t commit(ChannelHandle channel);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(ChannelHandle channel);
    __aicore__ inline int32_t lock(ChannelHandle channel);
    __aicore__ inline int32_t unlock(ChannelHandle channel);
    __aicore__ inline roce_batch_handle make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    template <typename U>
    __aicore__ inline roce_batch_handle make_batch_handle(
        ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr);
    __aicore__ inline roce_batch_handle& get_handle_ref(
        roce_batch_handle& batch_handle, uint32_t channel_idx, GM_ADDR remote_addr);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t write_nbi(roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t write_nbi(
        roce_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t read_nbi(roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len);
    template <auto const& config = ROCE_DEFAULT_CFG>
    __aicore__ inline int32_t read_nbi(
        roce_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src);
    __aicore__ inline int32_t batch_commit(roce_batch_handle& batch_handle);
    template <pipe_t pipe = PIPE_MTE3>
    __aicore__ inline int32_t drain(roce_batch_handle& batch_handle);

private:
    __aicore__ inline bool check_channel_param(__gm__ ChannelEntity* chnl_ptr);
    __aicore__ inline uint32_t get_db_head(__gm__ ChannelEntity* chnl_ptr);
    __aicore__ inline uint32_t get_sq_cqe_delta(uint32_t sq_tail, uint16_t completed_counter);
    __aicore__ inline uint32_t get_ds(uint32_t segment_bytes);
    __aicore__ inline uint32_t get_wqebb_num(uint32_t ds);
    __aicore__ inline asccomm_mlx5_wc_status handle_error_cqe(uint8_t syndrome);
    template <auto const& config>
    __aicore__ inline void fill_ctrl_seg(
        __ubuf__ roce_mlx5_wqe_ctrl_seg* ctrl, asccomm_mlx5_op_type op_type, uint32_t wqebb_index, uint32_t qpn,
        uint32_t ds);
    __aicore__ inline void fill_raddr_seg(__ubuf__ roce_mlx5_wqe_raddr_seg* raddr, GM_ADDR remote_addr, uint32_t r_key);
    __aicore__ inline void fill_data_seg(
        __ubuf__ roce_mlx5_wqe_data_seg* data, GM_ADDR local_addr, uint64_t len, uint32_t l_key);
    template <auto const& config, asccomm_mlx5_op_type op_type, typename T = uint64_t>
    __aicore__ inline uint64_t make_wqe(
        __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, T value, uint64_t value2,
        uint32_t sq_head, uint32_t remote_idx, uint32_t local_key);
    template <auto const& config, asccomm_mlx5_op_type op_type>
    __aicore__ inline uint64_t make_sge_wqe(
        __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, uint32_t sq_head, uint32_t remote_idx, uint32_t local_key,
        const buf_desc* local_descs, uint32_t seg_num);
    template <
        bool commit, auto const& config, asccomm_mlx5_op_type op_type, typename T = uint64_t,
        roce_sge_mode sge_mode = roce_sge_mode::SINGLE>
    __aicore__ inline int32_t post_send(
        ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, T value, uint64_t value2,
        const buf_desc* local_descs = nullptr, uint32_t seg_num = 0U);
    template <bool sq_safe_mode = false>
    __aicore__ inline int32_t poll_cq(__gm__ ChannelEntity* chnl_ptr, uint32_t expect_idx, uint32_t threshold = 0);
    template <bool sq_safe_mode = false>
    __aicore__ inline bool en_continue(
        uint32_t expect_idx, uint32_t cq_tail, uint32_t sq_tail, uint32_t sq_depth, uint32_t threshold,
        uint32_t sq_db_head);
    __aicore__ inline asccomm_mlx5_cqe_status wait_cqe_and_update_sq_tail(
        __gm__ uint8_t* cqe_addr, uint32_t cq_tail, uint32_t cq_depth, uint32_t sq_depth, __gm__ uint8_t* sq_base,
        uint32_t sq_head, uint32_t& sq_tail);
    __aicore__ inline uint32_t get_completed_wqebb_num(
        __gm__ uint8_t* sq_base, uint32_t sq_depth, uint16_t wqe_counter, uint32_t& completed_ds);
    __aicore__ inline void knock_door_bell(
        __gm__ ChannelEntity* chnl_ptr, uint32_t sq_head, uint64_t db_value, __ubuf__ uint32_t* staging);
    __aicore__ inline bool has_batch_sq_capacity(
        uint32_t sq_depth, uint32_t sq_tail, uint32_t sq_db_head, uint32_t sq_head, uint32_t pre_sq_cnt);
    template <auto const& config>
    __aicore__ inline void update_batch_cursor(roce_batch_handle& batch_handle, uint32_t wqebb_advance);
    template <auto const& config, asccomm_mlx5_op_type op_type, roce_sge_mode sge_mode>
    __aicore__ inline int32_t batch_post_send(
        roce_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t seg_num);
    __aicore__ inline int32_t poll_batch_cq(roce_batch_handle& batch_handle, uint32_t expect_idx);

private:
    __ubuf__ uint32_t* wqe_ub = nullptr;
    bool pending_valid = false;
    ChannelHandle pending_channel = 0U;
    uint32_t pending_sq_head = 0U;
    uint64_t pending_db_value = 0U;
};
} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_DEF_H
#endif
