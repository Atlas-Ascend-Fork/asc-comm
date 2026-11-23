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
 * \file asccomm_aiv_roce_mlx5.h
 * \brief Asccomm AIV MLX5 RoCE implementation for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Asccomm header. Please include public Asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_MLX5_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_MLX5_H

#include "asccomm_aiv_roce_mlx5_def.h"
#include <c_api/vector_datamove/vector_datamove.h>
#include "../../common/asccomm_log.h"
#include "../../common/asccomm_utils.h"
#include "../../common/asccomm_inner_def.h"

namespace asc {

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_init(__ubuf__ uint8_t* buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff) & (ASCCOMM_MLX5_ADDR_ALIGNMENT - 1U)) != 0U,
        "Asccomm MLX5 asccomm_init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff)));
    ASCCOMM_DEBUG_TRAP_IF(len < ASCCOMM_UB_BUF_SIZE, "Asccomm MLX5 asccomm_init failed: len = %u\n", len);
    wqe_ub = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    return ASCCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_init(const LocalTensor<T>& buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        len < ASCCOMM_UB_BUF_SIZE || buff.GetSize() * sizeof(T) < ASCCOMM_UB_BUF_SIZE,
        "Asccomm MLX5 asccomm_init failed: len = %u, tensor_size = %llu\n", len,
        static_cast<uint64_t>(buff.GetSize()) * sizeof(T));
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff.GetPhyAddr()) & (ASCCOMM_MLX5_ADDR_ALIGNMENT - 1U)) != 0U,
        "Asccomm MLX5 asccomm_init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff.GetPhyAddr())));
    wqe_ub = reinterpret_cast<__ubuf__ uint32_t*>(buff.GetPhyAddr());
    return ASCCOMM_SUCCESS;
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::check_channel_param(__gm__ ChannelEntity* chnl_ptr)
{
    if (chnl_ptr == nullptr) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "Asccomm MLX5 check_channel_param failed: chnl_ptr is nullptr.\n");
        return false;
    }
    if (chnl_ptr->sqNum == 0 || chnl_ptr->cqNum == 0 || chnl_ptr->sqContextAddr == 0 || chnl_ptr->cqContextAddr == 0) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR, "Asccomm MLX5 check_channel_param failed: empty SQ/CQ context. sqNum = %u cqNum = %u\n",
            chnl_ptr->sqNum, chnl_ptr->cqNum);
        return false;
    }
    const auto& sq = chnl_ptr->sqContextAddr[0].contextInfo.roceSq;
    const auto& cq = chnl_ptr->cqContextAddr[0].contextInfo.roceCq;
    const uint64_t db_hw_align_mask = sizeof(uint64_t) - 1U;
    if (chnl_ptr->protocol != COMM_PROTOCOL_ROCE || sq.sqVa == 0 || sq.dbHwVa == 0 || sq.dbSwVa == 0 || sq.qpn == 0 ||
        sq.wqeSize != ASCCOMM_MLX5_WQEBB_SIZE || sq.depth <= 1U || sq.depth > ASCCOMM_MLX5_SQ_INDEX_MASK ||
        (sq.depth & (sq.depth - 1U)) != 0 || (sq.dbHwVa & db_hw_align_mask) != 0 || cq.cqVa == 0 || cq.dbSwVa == 0 ||
        cq.cqeSize != ASCCOMM_MLX5_CQE_SIZE || cq.cqDepth == 0 || (cq.cqDepth & (cq.cqDepth - 1U)) != 0 ||
        cq.headAddr == 0) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "Asccomm MLX5 check_channel_param failed: invalid SQ/CQ context.\n");
        return false;
    }
    return true;
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_ds(uint32_t segment_bytes)
{
    return (segment_bytes + 15U) >> 4U;
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_sq_cqe_delta(
    uint32_t sq_tail, uint16_t completed_counter)
{
    // MLX5 CQE carries a 16-bit wqe_counter. Rebuild the forward delta against the
    // low 16 bits of the 32-bit SQ consumer serial, as mori does for doneIdx.
    return static_cast<uint32_t>(static_cast<uint16_t>(completed_counter - static_cast<uint16_t>(sq_tail)));
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_db_head(__gm__ ChannelEntity* chnl_ptr)
{
    // The send DBR record already separates "written WQEs" from "WQEs published to
    // the NIC". Rebuild the 32-bit serial from the 16-bit hardware producer index
    // using the software producer serial as the epoch anchor.
    const auto& sq = chnl_ptr->sqContextAddr[0].contextInfo.roceSq;
    __gm__ uint32_t* dbr_rec = reinterpret_cast<__gm__ uint32_t*>(sq.dbSwVa) + ASCCOMM_MLX5_SND_DBR;
    const uint16_t db_head16 = static_cast<uint16_t>(hto_nl(ld_dev(dbr_rec, 0)) & ASCCOMM_MLX5_SQ_INDEX_MASK);
    const uint32_t sq_head = chnl_ptr->sqHead;
    return sq_head - static_cast<uint32_t>(static_cast<uint16_t>(static_cast<uint16_t>(sq_head) - db_head16));
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_wqebb_num(uint32_t ds) { return (ds + 3U) >> 2U; }

__aicore__ inline asccomm_mlx5_wc_status asccomm_impl<COMM_PROTOCOL_ROCE>::handle_error_cqe(uint8_t syndrome)
{
    switch (syndrome) {
        case ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR:
            return asccomm_mlx5_wc_status::WC_LOC_LEN_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR:
            return asccomm_mlx5_wc_status::WC_LOC_QP_OP_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_PROT_ERR:
            return asccomm_mlx5_wc_status::WC_LOC_PROT_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_WR_FLUSH_ERR:
            return asccomm_mlx5_wc_status::WC_WR_FLUSH_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_MW_BIND_ERR:
            return asccomm_mlx5_wc_status::WC_MW_BIND_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_BAD_RESP_ERR:
            return asccomm_mlx5_wc_status::WC_BAD_RESP_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR:
            return asccomm_mlx5_wc_status::WC_LOC_ACCESS_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
            return asccomm_mlx5_wc_status::WC_REM_INV_REQ_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR:
            return asccomm_mlx5_wc_status::WC_REM_ACCESS_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_OP_ERR:
            return asccomm_mlx5_wc_status::WC_REM_OP_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
            return asccomm_mlx5_wc_status::WC_RETRY_EXC_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
            return asccomm_mlx5_wc_status::WC_RNR_RETRY_EXC_ERR;
        case ASCCOMM_MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR:
            return asccomm_mlx5_wc_status::WC_REM_ABORT_ERR;
        default:
            return asccomm_mlx5_wc_status::WC_GENERAL_ERR;
    }
}

template <auto const& config>
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_ctrl_seg(
    __ubuf__ roce_mlx5_wqe_ctrl_seg* ctrl, asccomm_mlx5_op_type op_type, uint32_t wqebb_index, uint32_t qpn,
    uint32_t ds)
{
    static_assert(config.cqe <= 1U, "MLX5 only supports cqe values 0 and 1.");
    ctrl->opmod_idx_opcode = hto_nl(((wqebb_index & 0xffffU) << 8U) | static_cast<uint32_t>(op_type));
    ctrl->qpn_ds = hto_nl((qpn << 8U) | ds);
    ctrl->signature = 0;
    ctrl->dci_stream_channel_id = 0;
    ctrl->fm_ce_se = static_cast<uint8_t>((config.cqe != 0U ? ASCCOMM_MLX5_CTRL_CQ_UPDATE : 0U));
    ctrl->imm = 0;
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_raddr_seg(
    __ubuf__ roce_mlx5_wqe_raddr_seg* raddr, GM_ADDR remote_addr, uint32_t r_key)
{
    raddr->raddr = hto_nll(reinterpret_cast<uint64_t>(remote_addr));
    raddr->rkey = hto_nl(r_key);
    raddr->reserved = 0;
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_data_seg(
    __ubuf__ roce_mlx5_wqe_data_seg* data, GM_ADDR local_addr, uint64_t len, uint32_t l_key)
{
    data->byte_count = hto_nl(static_cast<uint32_t>(len));
    data->lkey = hto_nl(l_key);
    data->addr = hto_nll(reinterpret_cast<uint64_t>(local_addr));
}

template <auto const& config, asccomm_mlx5_op_type op_type, typename T>
__aicore__ inline uint64_t asccomm_impl<COMM_PROTOCOL_ROCE>::make_wqe(
    __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, T value, uint64_t value2, uint32_t sq_head,
    uint32_t remote_idx, uint32_t local_key)
{
    constexpr bool is_atomic =
        op_type == asccomm_mlx5_op_type::ATOMIC_FAA || op_type == asccomm_mlx5_op_type::ATOMIC_CAS;
    constexpr uint32_t segment_bytes =
        is_atomic ? ASCCOMM_MLX5_PUBLIC_SEG_BYTES + sizeof(roce_mlx5_wqe_atomic_seg) + ASCCOMM_MLX5_DATA_SEG_BYTES :
                    (asccomm_config_inline_en(config) == 0U ?
                         ASCCOMM_MLX5_PUBLIC_SEG_BYTES + ASCCOMM_MLX5_DATA_SEG_BYTES :
                         ASCCOMM_MLX5_PUBLIC_SEG_BYTES + sizeof(roce_mlx5_wqe_inl_data_seg) + sizeof(T));

    // MLX5 stops WQE parsing at the DS boundary. Do not clear the full WQEBB on
    // every request; only inline data needs deterministic padding inside its
    // last 16-byte segment.
    __ubuf__ uint8_t* scratch = reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub);
    __ubuf__ roce_mlx5_wqe_ctrl_seg* ctrl = reinterpret_cast<__ubuf__ roce_mlx5_wqe_ctrl_seg*>(scratch);
    __ubuf__ roce_mlx5_wqe_raddr_seg* raddr =
        reinterpret_cast<__ubuf__ roce_mlx5_wqe_raddr_seg*>(scratch + sizeof(roce_mlx5_wqe_ctrl_seg));
    fill_ctrl_seg<config>(
        ctrl, op_type, sq_head, chnl_ptr->sqContextAddr[0].contextInfo.roceSq.qpn, get_ds(segment_bytes));
    fill_raddr_seg(raddr, dst, chnl_ptr->remoteBufferAddr[remote_idx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey);

    if constexpr (is_atomic) {
        __ubuf__ roce_mlx5_wqe_atomic_seg* atomic = reinterpret_cast<__ubuf__ roce_mlx5_wqe_atomic_seg*>(
            scratch + sizeof(roce_mlx5_wqe_ctrl_seg) + sizeof(roce_mlx5_wqe_raddr_seg));
        __ubuf__ roce_mlx5_wqe_data_seg* data = reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(
            scratch + sizeof(roce_mlx5_wqe_ctrl_seg) + sizeof(roce_mlx5_wqe_raddr_seg) +
            sizeof(roce_mlx5_wqe_atomic_seg));
        if constexpr (op_type == asccomm_mlx5_op_type::ATOMIC_FAA) {
            atomic->swap_add = hto_nll(value);
            atomic->compare = 0;
        } else {
            atomic->swap_add = hto_nll(value2);
            atomic->compare = hto_nll(value);
        }
        fill_data_seg(data, src, sizeof(uint64_t), local_key);
    } else if constexpr (asccomm_config_inline_en(config) == 0U) {
        __ubuf__ roce_mlx5_wqe_data_seg* data = reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(
            scratch + sizeof(roce_mlx5_wqe_ctrl_seg) + sizeof(roce_mlx5_wqe_raddr_seg));
        fill_data_seg(data, src, len, local_key);
    } else {
        __ubuf__ roce_mlx5_wqe_inl_data_seg* inline_header = reinterpret_cast<__ubuf__ roce_mlx5_wqe_inl_data_seg*>(
            scratch + sizeof(roce_mlx5_wqe_ctrl_seg) + sizeof(roce_mlx5_wqe_raddr_seg));
        inline_header->byte_count = hto_nl(static_cast<uint32_t>(len) | ASCCOMM_MLX5_INLINE_SEG);
        __ubuf__ uint8_t* payload = reinterpret_cast<__ubuf__ uint8_t*>(inline_header + 1);
        __ubuf__ uint32_t* payload_words = reinterpret_cast<__ubuf__ uint32_t*>(payload);
        if constexpr (sizeof(T) == sizeof(uint32_t)) {
            payload_words[0] = *reinterpret_cast<const uint32_t*>(&value);
        } else {
            const uint64_t value_bits = *reinterpret_cast<const uint64_t*>(&value);
            payload_words[0] = static_cast<uint32_t>(value_bits);
            payload_words[1] = static_cast<uint32_t>(value_bits >> 32U);
        }
        constexpr uint32_t inline_used_bytes = sizeof(roce_mlx5_wqe_inl_data_seg) + sizeof(T);
        constexpr uint32_t inline_tail_bytes = (16U - (inline_used_bytes % 16U)) % 16U;
        static_assert(inline_tail_bytes % sizeof(uint32_t) == 0U);
        constexpr uint32_t inline_tail_words = inline_tail_bytes / sizeof(uint32_t);
        for (uint32_t i = 0; i < inline_tail_words; ++i) {
            payload_words[sizeof(T) / sizeof(uint32_t) + i] = 0U;
        }
    }
    return *reinterpret_cast<__ubuf__ uint64_t*>(scratch);
}

template <auto const& config, asccomm_mlx5_op_type op_type>
__aicore__ inline uint64_t asccomm_impl<COMM_PROTOCOL_ROCE>::make_sge_wqe(
    __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, uint32_t sq_head, uint32_t remote_idx, uint32_t local_key,
    const buf_desc* local_descs, uint32_t seg_num)
{
    const uint32_t segment_bytes = ASCCOMM_MLX5_PUBLIC_SEG_BYTES + seg_num * ASCCOMM_MLX5_DATA_SEG_BYTES;

    __ubuf__ uint8_t* scratch = reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub);

    __ubuf__ roce_mlx5_wqe_ctrl_seg* ctrl = reinterpret_cast<__ubuf__ roce_mlx5_wqe_ctrl_seg*>(scratch);
    __ubuf__ roce_mlx5_wqe_raddr_seg* raddr =
        reinterpret_cast<__ubuf__ roce_mlx5_wqe_raddr_seg*>(scratch + sizeof(roce_mlx5_wqe_ctrl_seg));
    fill_ctrl_seg<config>(
        ctrl, op_type, sq_head, chnl_ptr->sqContextAddr[0].contextInfo.roceSq.qpn, get_ds(segment_bytes));
    fill_raddr_seg(raddr, dst, chnl_ptr->remoteBufferAddr[remote_idx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey);

    __ubuf__ roce_mlx5_wqe_data_seg* data =
        reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(scratch + ASCCOMM_MLX5_PUBLIC_SEG_BYTES);
    for (uint32_t i = 0; i < seg_num; ++i) {
        fill_data_seg(data, local_descs[i].addr, local_descs[i].len, local_key);
        data = reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(reinterpret_cast<__ubuf__ uint8_t*>(data + 1));
    }
    return *reinterpret_cast<__ubuf__ uint64_t*>(scratch);
}

template <bool commit, auto const& config, asccomm_mlx5_op_type op_type, typename T, roce_sge_mode sge_mode>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::post_send(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, T value, uint64_t value2,
    const buf_desc* local_descs, uint32_t seg_num)
{
    uint64_t total_len = 0U;
    if constexpr (sge_mode == roce_sge_mode::MULTI) {
        ASCCOMM_DEBUG_TRAP_IF(
            local_descs == nullptr || seg_num == 0U || seg_num > ASCCOMM_MLX5_MAX_SGE_NUM,
            "Asccomm MLX5 post_send failed: local_descs is null or invalid SGE number. seg_num = %u\n", seg_num);
        for (uint32_t i = 0; i < seg_num; ++i) {
            ASCCOMM_DEBUG_TRAP_IF(
                local_descs[i].addr == 0U || local_descs[i].len >= ASCCOMM_MLX5_MAX_DATA_LEN,
                "Asccomm MLX5 post_send failed: invalid SGE. index = %u addr = %llu len = %u\n", i,
                reinterpret_cast<uint64_t>(local_descs[i].addr), local_descs[i].len);
            total_len += local_descs[i].len;
        }
        ASCCOMM_DEBUG_TRAP_IF(
            dst == 0U || total_len >= ASCCOMM_MLX5_MAX_DATA_LEN,
            "Asccomm MLX5 post_send failed: dst = %llu total_len = %llu\n", reinterpret_cast<uint64_t>(dst), total_len);
    } else {
        ASCCOMM_DEBUG_TRAP_IF(
            dst == 0 || len >= ASCCOMM_MLX5_MAX_DATA_LEN,
            "Asccomm MLX5 post_send failed: dst = %llu, src = %llu, len = %llu.\n", reinterpret_cast<uint64_t>(dst),
            reinterpret_cast<uint64_t>(src), len);
    }

    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "Asccomm MLX5 post_send failed with invalid channel.");

    const uint64_t request_len = sge_mode == roce_sge_mode::MULTI ? total_len : len;
    const int32_t remote_idx =
        asccomm_find_buffer_idx(chnl_ptr->remoteBufferAddr, chnl_ptr->remoteBufferNum, dst, request_len);
    ASCCOMM_DEBUG_TRAP_IF(
        remote_idx < 0, "Asccomm MLX5 post_send failed with invalid remote address %llu.\n",
        reinterpret_cast<uint64_t>(dst));

    uint32_t local_key = 0U;
    if constexpr (asccomm_config_inline_en(config) == 0U) {
        const int32_t local_idx =
            sge_mode == roce_sge_mode::MULTI ?
                asccomm_find_buffer_idx(
                    chnl_ptr->localBufferAddr, chnl_ptr->localBufferNum, local_descs[0].addr, local_descs[0].len) :
                asccomm_find_buffer_idx(chnl_ptr->localBufferAddr, chnl_ptr->localBufferNum, src, len);
        ASCCOMM_DEBUG_TRAP_IF(
            local_idx < 0, "Asccomm MLX5 post_send failed with invalid local address %llu.\n",
            reinterpret_cast<uint64_t>(src));
        local_key = chnl_ptr->localBufferAddr[local_idx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    }

    uint32_t sq_head = chnl_ptr->sqHead;
    uint32_t sq_tail = chnl_ptr->sqTail;
    const uint32_t sq_db_head = get_db_head(chnl_ptr);
    const uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    const uint32_t deferred_wqebb_num = sq_head - sq_db_head;
    uint32_t total_wqebb_num = 1U;
    uint64_t db_value = 0U;
    if constexpr (sge_mode == roce_sge_mode::MULTI) {
        const uint32_t segment_bytes = ASCCOMM_MLX5_PUBLIC_SEG_BYTES + seg_num * ASCCOMM_MLX5_DATA_SEG_BYTES;
        total_wqebb_num = get_wqebb_num(get_ds(segment_bytes));
        ASCCOMM_DEBUG_TRAP_IF(
            static_cast<uint64_t>(total_wqebb_num) * ASCCOMM_MLX5_WQEBB_SIZE > ASCCOMM_UB_BUF_SIZE,
            "Asccomm MLX5 post_send failed: WQE with %u WQEBBs is larger than the init workspace.\n", total_wqebb_num);
    }

    // Stop reclaiming once the published WQEBBs plus deferred WQEBBs and the
    // current WQE fit in the SQ. The threshold is therefore one slot less than
    // the total space needed by this request.
    const uint32_t poll_threshold = total_wqebb_num + deferred_wqebb_num - 1U;
    const uint32_t active = sq_db_head - sq_tail;
    if (active >= sq_depth || sq_depth - active <= poll_threshold) {
        (void)poll_cq<true>(chnl_ptr, chnl_ptr->cqHead, poll_threshold);
        sq_tail = chnl_ptr->sqTail;
    }
    const uint32_t reserved = sq_head - sq_tail + total_wqebb_num;
    ASCCOMM_DEBUG_TRAP_IF(
        reserved > sq_depth,
        "Asccomm MLX5 post_send failed because SQ is full. sq_head = %u sq_tail = %u wqebb_num = %u\n", sq_head,
        sq_tail, total_wqebb_num);

    if constexpr (sge_mode == roce_sge_mode::MULTI) {
        const uint32_t start_slot = sq_head & (sq_depth - 1U);
        db_value = make_sge_wqe<config, op_type>(chnl_ptr, dst, sq_head, remote_idx, local_key, local_descs, seg_num);

        const uint64_t sq_base_addr = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.sqVa;
        const uint32_t tail_wqebb_count = sq_depth - start_slot;
        const uint32_t first_wqebb_count = total_wqebb_num < tail_wqebb_count ? total_wqebb_num : tail_wqebb_count;
        const uint32_t second_wqebb_count = total_wqebb_num - first_wqebb_count;

        __gm__ uint8_t* first_wqe_addr = reinterpret_cast<__gm__ uint8_t*>(
            sq_base_addr + static_cast<uint64_t>(start_slot) * ASCCOMM_MLX5_WQEBB_SIZE);

        sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
        asc_copy_ub2gm_align(
            first_wqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub), 1, first_wqebb_count * ASCCOMM_MLX5_WQEBB_SIZE,
            asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        if (second_wqebb_count != 0U) {
            asc_copy_ub2gm_align(
                reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
                reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub) + first_wqebb_count * ASCCOMM_MLX5_WQEBB_SIZE, 1,
                second_wqebb_count * ASCCOMM_MLX5_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        }
        sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    } else {
        db_value = make_wqe<config, op_type, T>(chnl_ptr, dst, src, len, value, value2, sq_head, remote_idx, local_key);
        __gm__ uint8_t* sq_addr = reinterpret_cast<__gm__ uint8_t*>(
            chnl_ptr->sqContextAddr[0].contextInfo.roceSq.sqVa +
            static_cast<uint64_t>(sq_head & (sq_depth - 1U)) * ASCCOMM_MLX5_WQEBB_SIZE);
        sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
        asc_copy_ub2gm_align(
            sq_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub), 1, ASCCOMM_MLX5_WQEBB_SIZE,
            asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    }

    const uint32_t next_sq_head = sq_head + total_wqebb_num;
    chnl_ptr->sqHead = next_sq_head;
    if constexpr (config.cqe == 1U) {
        chnl_ptr->cqHead += 1U;
    }
    if constexpr (commit) {
        knock_door_bell(chnl_ptr, next_sq_head, db_value, wqe_ub);
        if (!pending_valid || pending_channel == channel) {
            pending_valid = false;
        }
    } else {
        pending_valid = true;
        pending_channel = channel;
        pending_sq_head = next_sq_head;
        pending_db_value = db_value;
    }
    return ASCCOMM_SUCCESS;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 asccomm_write_nbi requires inline_en = 0.");
    return post_send<commit, config, asccomm_mlx5_op_type::WRITE, uint64_t, roce_sge_mode::SINGLE>(
        channel, dst, src, len, uint64_t{0}, 0);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_read_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 asccomm_read_nbi requires inline_en = 0.");
    return post_send<commit, config, asccomm_mlx5_op_type::READ, uint64_t, roce_sge_mode::SINGLE>(
        channel, src, dst, len, uint64_t{0}, 0);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_nbi(
    ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 multi-SGE asccomm_write_nbi requires inline_en = 0.");
    (void)commit_pipe;
    (void)req_pipe;
    return post_send<commit, config, asccomm_mlx5_op_type::WRITE, uint64_t, roce_sge_mode::MULTI>(
        channel, dst, 0, 0, uint64_t{0}, 0, src_descs, src_num);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_read_nbi(
    ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 multi-SGE asccomm_read_nbi requires inline_en = 0.");
    (void)commit_pipe;
    (void)req_pipe;
    return post_send<commit, config, asccomm_mlx5_op_type::READ, uint64_t, roce_sge_mode::MULTI>(
        channel, src, 0, 0, uint64_t{0}, 0, dst_descs, dst_num);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_value_nbi(
    ChannelHandle channel, GM_ADDR dst, T value)
{
    static_assert(
        sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t),
        "MLX5 asccomm_write_value_nbi only supports 4-byte and 8-byte scalar types.");
    static_assert(asccomm_config_inline_en(config) == 1U, "MLX5 asccomm_write_value_nbi requires inline_en = 1.");
    constexpr uint32_t value_bytes = static_cast<uint32_t>(sizeof(T));
    ASCCOMM_DEBUG_TRAP_IF(
        dst == 0, "Asccomm MLX5 asccomm_write_value_nbi failed: dst = %llu len = %u\n", reinterpret_cast<uint64_t>(dst),
        value_bytes);
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uint64_t>(dst) & (value_bytes - 1U)) != 0U,
        "Asccomm MLX5 asccomm_write_value_nbi failed: dst is not naturally aligned. dst = %llu len = %u\n",
        reinterpret_cast<uint64_t>(dst), value_bytes);
    return post_send<commit, config, asccomm_mlx5_op_type::WRITE, T, roce_sge_mode::SINGLE>(
        channel, dst, nullptr, value_bytes, value, 0);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_with_notify_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    (void)commit;
    (void)commit_pipe;
    (void)req_pipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)src;
    (void)len;
    (void)notify_addr;
    (void)notify_val;
    ASCCOMM_DEBUG_TRAP_IF(true, "Asccomm MLX5 asccomm_write_with_notify_nbi is not supported.\n");
    return ASCCOMM_FAILED;
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_atomic_faa_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val)
{
    static_assert(sizeof(T) == sizeof(uint64_t), "MLX5 asccomm_atomic_faa_nbi only supports 64-bit scalar types.");
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 asccomm_atomic_faa_nbi requires inline_en = 0.");
    static_assert(config.cqe == 1U, "MLX5 asccomm_atomic_faa_nbi requires cqe = 1.");
    ASCCOMM_DEBUG_TRAP_IF(
        dst == 0 || fetch_addr == 0, "Asccomm MLX5 asccomm_atomic_faa_nbi failed: dst = %llu fetch_addr = %llu\n",
        reinterpret_cast<uint64_t>(dst), reinterpret_cast<uint64_t>(fetch_addr));
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uint64_t>(dst) & (sizeof(uint64_t) - 1U)) != 0U ||
            (reinterpret_cast<uint64_t>(fetch_addr) & (sizeof(uint64_t) - 1U)) != 0U,
        "Asccomm MLX5 asccomm_atomic_faa_nbi failed: dst and fetch_addr must be 8-byte aligned.\n");
    return post_send<commit, config, asccomm_mlx5_op_type::ATOMIC_FAA, uint64_t, roce_sge_mode::SINGLE>(
        channel, dst, fetch_addr, sizeof(uint64_t), static_cast<uint64_t>(add_val), 0);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_atomic_cas_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val)
{
    static_assert(sizeof(T) == sizeof(uint64_t), "MLX5 asccomm_atomic_cas_nbi only supports 64-bit scalar types.");
    static_assert(asccomm_config_inline_en(config) == 0U, "MLX5 asccomm_atomic_cas_nbi requires inline_en = 0.");
    static_assert(config.cqe == 1U, "MLX5 asccomm_atomic_cas_nbi requires cqe = 1.");
    ASCCOMM_DEBUG_TRAP_IF(
        dst == 0 || fetch_addr == 0, "Asccomm MLX5 asccomm_atomic_cas_nbi failed: dst = %llu fetch_addr = %llu\n",
        reinterpret_cast<uint64_t>(dst), reinterpret_cast<uint64_t>(fetch_addr));
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uint64_t>(dst) & (sizeof(uint64_t) - 1U)) != 0U ||
            (reinterpret_cast<uint64_t>(fetch_addr) & (sizeof(uint64_t) - 1U)) != 0U,
        "Asccomm MLX5 asccomm_atomic_cas_nbi failed: dst and fetch_addr must be 8-byte aligned.\n");
    return post_send<commit, config, asccomm_mlx5_op_type::ATOMIC_CAS, uint64_t, roce_sge_mode::SINGLE>(
        channel, dst, fetch_addr, sizeof(uint64_t), static_cast<uint64_t>(compare_val),
        static_cast<uint64_t>(swap_val));
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::knock_door_bell(
    __gm__ ChannelEntity* chnl_ptr, uint32_t sq_head, uint64_t db_value, __ubuf__ uint32_t* staging)
{
    const auto& sq = chnl_ptr->sqContextAddr[0].contextInfo.roceSq;
    __gm__ uint32_t* dbr_rec = reinterpret_cast<__gm__ uint32_t*>(sq.dbSwVa) + ASCCOMM_MLX5_SND_DBR;
    st_dev(hto_nl(sq_head & ASCCOMM_MLX5_SQ_INDEX_MASK), dbr_rec, 0);
    asc_sync_data_barrier(DSB_DDR);

    *reinterpret_cast<__ubuf__ uint64_t*>(staging) = db_value;
    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    asc_copy_ub2gm_align(
        reinterpret_cast<__gm__ uint8_t*>(sq.dbHwVa), reinterpret_cast<__ubuf__ uint8_t*>(staging), 1, sizeof(uint64_t),
        asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_commit(ChannelHandle channel)
{
    ASCCOMM_DEBUG_TRAP_IF(
        !pending_valid || pending_channel != channel, "asccomm_commit failed: no pending request for the channel.\n");
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "asccomm_commit failed with invalid channel.\n");
    ASCCOMM_DEBUG_TRAP_IF(chnl_ptr->sqHead != pending_sq_head, "asccomm_commit failed: pending SQ head changed.\n");
    knock_door_bell(chnl_ptr, pending_sq_head, pending_db_value, wqe_ub);
    pending_valid = false;
    return ASCCOMM_SUCCESS;
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_completed_wqebb_num(
    __gm__ uint8_t* sq_base, uint32_t sq_depth, uint16_t wqe_counter, uint32_t& completed_ds)
{
    __gm__ uint32_t* qpn_ds_addr = reinterpret_cast<__gm__ uint32_t*>(
        sq_base + static_cast<uint64_t>(wqe_counter & (sq_depth - 1U)) * ASCCOMM_MLX5_WQEBB_SIZE +
        offsetof(roce_mlx5_wqe_ctrl_seg, qpn_ds));
    completed_ds = hto_nl(ld_dev(qpn_ds_addr, 0)) & 0xffU;
    return get_wqebb_num(completed_ds);
}

template <bool sq_safe_mode>
__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::en_continue(
    uint32_t expect_idx, uint32_t cq_tail, uint32_t sq_tail, uint32_t sq_depth, uint32_t threshold, uint32_t sq_db_head)
{
    bool is_continue = cq_tail != expect_idx;
    if constexpr (sq_safe_mode) {
        const uint32_t outstanding = sq_db_head - sq_tail;
        is_continue = (outstanding >= sq_depth || sq_depth - outstanding <= threshold) && is_continue;
    }
    return is_continue;
}

__aicore__ inline asccomm_mlx5_cqe_status asccomm_impl<COMM_PROTOCOL_ROCE>::wait_cqe_and_update_sq_tail(
    __gm__ uint8_t* cqe_addr, uint32_t cq_tail, uint32_t cq_depth, uint32_t sq_depth, __gm__ uint8_t* sq_base,
    uint32_t sq_head, uint32_t& sq_tail)
{
    const uint8_t op_own = ld_dev(cqe_addr + ASCCOMM_MLX5_CQE_OP_OWN_INDEX, 0);
    const uint8_t opcode = op_own >> 4U;
    const uint8_t owner = op_own & 1U;
    const uint8_t expected_owner = ((cq_tail & cq_depth) == 0U) ? 0U : 1U;
    if (opcode == ASCCOMM_MLX5_CQE_INVALID || owner != expected_owner) {
        return asccomm_mlx5_cqe_status::NOT_READY;
    }
    if (opcode == ASCCOMM_MLX5_CQE_REQ_ERR || opcode == ASCCOMM_MLX5_CQE_RESP_ERR) {
        const uint8_t syndrome = ld_dev(cqe_addr + ASCCOMM_MLX5_CQE_SYNDROME_INDEX, 0);
        const uint8_t vendor_syndrome = ld_dev(cqe_addr + ASCCOMM_MLX5_CQE_VENDOR_SYNDROME_INDEX, 0);
        const asccomm_mlx5_wc_status wc_status = handle_error_cqe(syndrome);
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR,
            "Asccomm MLX5 poll_cq failed: CQE error, opcode = %u, wc_status = %d, syndrome = %u, "
            "vendor_syndrome = %u\n",
            static_cast<uint32_t>(opcode), static_cast<int32_t>(wc_status), static_cast<uint32_t>(syndrome),
            static_cast<uint32_t>(vendor_syndrome));
        return asccomm_mlx5_cqe_status::ERROR;
    }

    const uint16_t wqe_counterBe =
        ld_dev(reinterpret_cast<__gm__ uint16_t*>(cqe_addr + ASCCOMM_MLX5_CQE_WQE_COUNTER_INDEX), 0);
    const uint16_t wqe_counter = static_cast<uint16_t>((wqe_counterBe >> 8U) | (wqe_counterBe << 8U));
    uint32_t completed_ds = 0U;
    const uint32_t wqebb_num = get_completed_wqebb_num(sq_base, sq_depth, wqe_counter, completed_ds);
    const uint32_t advance = get_sq_cqe_delta(sq_tail, wqe_counter) + wqebb_num;
    const uint32_t outstanding = sq_head - sq_tail;
    if (advance == 0U || advance > outstanding) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR,
            "Asccomm MLX5 poll_cq failed: invalid completion, wqe_counter = %u, ds = %u, wqebb_num = %u, "
            "advance = %u, outstanding = %u\n",
            static_cast<uint32_t>(wqe_counter), completed_ds, wqebb_num, advance, outstanding);
        return asccomm_mlx5_cqe_status::INVALID;
    }
    const uint32_t next_sq_tail = sq_tail + advance;
    sq_tail = next_sq_tail;
    const uint8_t consumed_op_own = static_cast<uint8_t>((ASCCOMM_MLX5_CQE_INVALID << 4U) | expected_owner);
    st_dev(consumed_op_own, cqe_addr + ASCCOMM_MLX5_CQE_OP_OWN_INDEX, 0);
    return asccomm_mlx5_cqe_status::SUCCESS;
}

template <bool sq_safe_mode>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::poll_cq(
    __gm__ ChannelEntity* chnl_ptr, uint32_t expect_idx, uint32_t threshold)
{
    uint32_t local_cq_tail = chnl_ptr->cqTail;
    if (expect_idx == local_cq_tail) {
        return ASCCOMM_SUCCESS;
    }
    const auto& cq = chnl_ptr->cqContextAddr[0].contextInfo.roceCq;
    const uint32_t cq_depth = cq.cqDepth;
    const uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    __gm__ uint8_t* sq_base = reinterpret_cast<__gm__ uint8_t*>(chnl_ptr->sqContextAddr[0].contextInfo.roceSq.sqVa);
    uint32_t sq_head = chnl_ptr->sqHead;
    uint32_t sq_tail = chnl_ptr->sqTail;
    // poll_cq does not ring the SQ doorbell, so the producer index is stable for
    // the whole polling pass. Read it once instead of once per consumed CQE.
    const uint32_t sq_db_head = sq_safe_mode ? get_db_head(chnl_ptr) : 0U;
    while (en_continue<sq_safe_mode>(expect_idx, local_cq_tail, sq_tail, sq_depth, threshold, sq_db_head)) {
        __gm__ uint8_t* cqe_addr = reinterpret_cast<__gm__ uint8_t*>(
            cq.cqVa + static_cast<uint64_t>(local_cq_tail & (cq_depth - 1U)) * ASCCOMM_MLX5_CQE_SIZE);
        asccomm_mlx5_cqe_status status = asccomm_mlx5_cqe_status::NOT_READY;
        const int64_t start_cycle = AscendC::GetSystemCycle();
        while (status == asccomm_mlx5_cqe_status::NOT_READY &&
               AscendC::GetSystemCycle() - start_cycle < ASCCOMM_MLX5_POLL_CQ_TIMEOUT_CYCLES) {
            status =
                wait_cqe_and_update_sq_tail(cqe_addr, local_cq_tail, cq_depth, sq_depth, sq_base, sq_head, sq_tail);
        }
        if (status == asccomm_mlx5_cqe_status::NOT_READY) {
            ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "Asccomm MLX5 poll_cq failed: timeout.\n");
            AscendC::Trap();
            return ASCCOMM_FAILED;
        }
        if (status != asccomm_mlx5_cqe_status::SUCCESS) {
            AscendC::Trap();
            return ASCCOMM_FAILED;
        }
        local_cq_tail += 1U;
    }
    chnl_ptr->cqTail = local_cq_tail;
    chnl_ptr->sqTail = sq_tail;
    __gm__ uint32_t* cq_dbr_rec = reinterpret_cast<__gm__ uint32_t*>(cq.dbSwVa) + ASCCOMM_MLX5_CQ_SET_CI;
    st_dev(hto_nl(local_cq_tail & ASCCOMM_MLX5_CQ_UPDATE_CI_MASK), cq_dbr_rec, 0);
    return ASCCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_drain(ChannelHandle channel)
{
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "Asccomm MLX5 asccomm_drain failed with invalid channel.\n");
    return poll_cq(chnl_ptr, chnl_ptr->cqHead);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_lock(ChannelHandle channel)
{
    return asccomm_channel_lock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_unlock(ChannelHandle channel)
{
    return asccomm_channel_unlock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline roce_batch_handle& asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_get_handle_ref(
    roce_batch_handle& batch_handle, uint32_t channel_index, GM_ADDR remote_addr)
{
    (void)channel_index;
    (void)remote_addr;
    return batch_handle;
}

__aicore__ inline roce_batch_handle asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    roce_batch_handle batch_handle{};
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    const uintptr_t buffer_addr = reinterpret_cast<uintptr_t>(buff);
    ASCCOMM_DEBUG_TRAP_IF(
        !check_channel_param(chnl_ptr) || buff_len < ASCCOMM_MLX5_WQEBB_SIZE ||
            (buffer_addr & (ASCCOMM_MLX5_ADDR_ALIGNMENT - 1U)) != 0U,
        "Asccomm MLX5 asccomm_make_batch_handle failed with invalid channel or buffer.\n");
    const int32_t remote_idx =
        asccomm_find_buffer_idx(chnl_ptr->remoteBufferAddr, chnl_ptr->remoteBufferNum, remote_addr, 1);
    ASCCOMM_DEBUG_TRAP_IF(
        remote_idx < 0, "Asccomm MLX5 asccomm_make_batch_handle failed with invalid remote address %llu.\n",
        reinterpret_cast<uint64_t>(remote_addr));
    const int32_t local_idx =
        asccomm_find_buffer_idx(chnl_ptr->localBufferAddr, chnl_ptr->localBufferNum, local_addr, 1);
    ASCCOMM_DEBUG_TRAP_IF(
        local_idx < 0, "Asccomm MLX5 asccomm_make_batch_handle failed with invalid local address %llu.\n",
        reinterpret_cast<uint64_t>(local_addr));

    batch_handle.channel_handle = channel;
    batch_handle.sq_context = chnl_ptr->sqContextAddr[ASCCOMM_DEFAULT_QP_IDX];
    batch_handle.cq_context = chnl_ptr->cqContextAddr[ASCCOMM_DEFAULT_QP_IDX];
    batch_handle.cursor.sq_head = chnl_ptr->sqHead;
    batch_handle.cursor.sq_tail = chnl_ptr->sqTail;
    batch_handle.cursor.cq_head = chnl_ptr->cqHead;
    batch_handle.cursor.cq_tail = chnl_ptr->cqTail;
    batch_handle.cursor.pre_sq_cnt = 0U;
    batch_handle.buffer.raw_buffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    batch_handle.buffer.buffer_capacity = buff_len / ASCCOMM_MLX5_WQEBB_SIZE;
    batch_handle.mr_key.l_key = chnl_ptr->localBufferAddr[local_idx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    batch_handle.mr_key.r_key = chnl_ptr->remoteBufferAddr[remote_idx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    batch_handle.mr_key.key_word = 0U;
    batch_handle.mr_key.data_key_high_word = 0U;
    return batch_handle;
}

template <typename U>
__aicore__ inline roce_batch_handle asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_make_batch_handle(
    ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    ASCCOMM_DEBUG_TRAP_IF(
        buff.GetSize() * sizeof(U) < buff_len,
        "Asccomm MLX5 asccomm_make_batch_handle failed with invalid buffer size.\n");
    return asccomm_make_batch_handle(
        channel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buff_len, remote_addr, local_addr);
}

template <auto const& config>
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::update_batch_cursor(
    roce_batch_handle& batch_handle, uint32_t wqebb_advance)
{
    static_assert(config.cqe <= 1U, "MLX5 batch only supports cqe values 0 and 1.");
    batch_handle.cursor.pre_sq_cnt += wqebb_advance;
    if constexpr (config.cqe == 1U) {
        batch_handle.cursor.cq_head += 1U;
    }
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::has_batch_sq_capacity(
    uint32_t sq_depth, uint32_t sq_tail, uint32_t sq_db_head, uint32_t sq_head, uint32_t pre_sq_cnt)
{
    const uint32_t active = sq_db_head - sq_tail;
    const uint32_t reserved = sq_head + pre_sq_cnt - sq_tail;
    const uint32_t outstanding = active > reserved ? active : reserved;
    return outstanding <= sq_depth;
}

template <auto const& config, asccomm_mlx5_op_type op_type, roce_sge_mode sge_mode>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::batch_post_send(
    roce_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t seg_num)
{
    auto* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || remote_addr == 0U || !check_channel_param(chnl_ptr),
        "Asccomm MLX5 batch_post_send failed with invalid handle or request.\n");

    if constexpr (sge_mode == roce_sge_mode::SINGLE) {
        ASCCOMM_DEBUG_TRAP_IF(
            local_descs == nullptr || local_descs[0].addr == 0U || local_descs[0].len >= ASCCOMM_MLX5_MAX_DATA_LEN,
            "Asccomm MLX5 batch_post_send failed with invalid single request.\n");
    } else {
        ASCCOMM_DEBUG_TRAP_IF(
            local_descs == nullptr || seg_num == 0U || seg_num > ASCCOMM_MLX5_MAX_SGE_NUM,
            "Asccomm MLX5 batch_post_send failed with invalid SGE request.\n");
        uint64_t total_len = 0U;
        for (uint32_t i = 0; i < seg_num; ++i) {
            total_len += local_descs[i].len;
        }
        ASCCOMM_DEBUG_TRAP_IF(
            total_len >= ASCCOMM_MLX5_MAX_DATA_LEN, "Asccomm MLX5 batch_post_send failed with invalid total length.\n");
    }

    const uint32_t segment_bytes = sge_mode == roce_sge_mode::SINGLE ?
                                       ASCCOMM_MLX5_PUBLIC_SEG_BYTES + ASCCOMM_MLX5_DATA_SEG_BYTES :
                                       ASCCOMM_MLX5_PUBLIC_SEG_BYTES + seg_num * ASCCOMM_MLX5_DATA_SEG_BYTES;
    const uint32_t wqebb_num = get_wqebb_num(get_ds(segment_bytes));
    const uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        wqebb_num > batch_handle.buffer.buffer_capacity - pre_sq_cnt,
        "Asccomm MLX5 batch_post_send failed with insufficient staging buffer.\n");

    __ubuf__ uint8_t* scratch =
        reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer + pre_sq_cnt * ASCCOMM_MLX5_WQEBB_U32_NUM);
    auto* ctrl = reinterpret_cast<__ubuf__ roce_mlx5_wqe_ctrl_seg*>(scratch);
    auto* raddr = reinterpret_cast<__ubuf__ roce_mlx5_wqe_raddr_seg*>(scratch + sizeof(roce_mlx5_wqe_ctrl_seg));
    const uint32_t base_wqebb_idx = batch_handle.cursor.sq_head + pre_sq_cnt;
    fill_ctrl_seg<config>(
        ctrl, op_type, base_wqebb_idx, batch_handle.sq_context.contextInfo.roceSq.qpn, get_ds(segment_bytes));
    fill_raddr_seg(raddr, remote_addr, batch_handle.mr_key.r_key);
    auto& batch_state = *reinterpret_cast<roce_mlx5_batch_state*>(
        reinterpret_cast<uint8_t*>(&batch_handle.mr_key) + sizeof(roce_mr_key));
    batch_state.last_wqe_wqebb_idx = pre_sq_cnt;

    auto* data = reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(scratch + ASCCOMM_MLX5_PUBLIC_SEG_BYTES);
    if constexpr (sge_mode == roce_sge_mode::SINGLE) {
        fill_data_seg(data, local_descs[0].addr, local_descs[0].len, batch_handle.mr_key.l_key);
    } else {
        for (uint32_t i = 0; i < seg_num; ++i) {
            fill_data_seg(data, local_descs[i].addr, local_descs[i].len, batch_handle.mr_key.l_key);
            data = reinterpret_cast<__ubuf__ roce_mlx5_wqe_data_seg*>(reinterpret_cast<__ubuf__ uint8_t*>(data + 1));
        }
    }

    update_batch_cursor<config>(batch_handle, wqebb_num);
    return ASCCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    buf_desc src_desc;
    src_desc.addr = src;
    src_desc.len = len;
    return batch_post_send<config, asccomm_mlx5_op_type::WRITE, roce_sge_mode::SINGLE>(
        batch_handle, dst, &src_desc, 1U);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_read_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    buf_desc dst_desc;
    dst_desc.addr = dst;
    dst_desc.len = len;
    return batch_post_send<config, asccomm_mlx5_op_type::READ, roce_sge_mode::SINGLE>(batch_handle, src, &dst_desc, 1U);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_write_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return batch_post_send<config, asccomm_mlx5_op_type::WRITE, roce_sge_mode::MULTI>(
        batch_handle, dst, src_descs, src_num);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_read_nbi(
    roce_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return batch_post_send<config, asccomm_mlx5_op_type::READ, roce_sge_mode::MULTI>(
        batch_handle, src, dst_descs, dst_num);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_batch_commit(roce_batch_handle& batch_handle)
{
    const uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt == 0 || pre_sq_cnt > ASCCOMM_MLX5_SQ_INDEX_MASK || pre_sq_cnt > batch_handle.buffer.buffer_capacity,
        "Asccomm MLX5 asccomm_batch_commit failed with invalid WQEBB count.\n");
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    ASCCOMM_DEBUG_TRAP_IF(
        !check_channel_param(chnl_ptr), "Asccomm MLX5 asccomm_batch_commit failed with invalid channel.\n");

    const uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    const uint32_t sq_tail = chnl_ptr->sqTail;
    const uint32_t sq_head = batch_handle.cursor.sq_head;
    if (!has_batch_sq_capacity(sq_depth, sq_tail, get_db_head(chnl_ptr), sq_head, pre_sq_cnt)) {
        // Reclaim only enough published WQEBBs for this batch to fit. The
        // pending batch itself cannot complete before this commit rings.
        const uint32_t poll_threshold = pre_sq_cnt == 0U ? 0U : pre_sq_cnt - 1U;
        (void)poll_cq<true>(chnl_ptr, chnl_ptr->cqHead, poll_threshold);
    }
    ASCCOMM_DEBUG_TRAP_IF(
        !has_batch_sq_capacity(sq_depth, chnl_ptr->sqTail, get_db_head(chnl_ptr), sq_head, pre_sq_cnt),
        "Asccomm MLX5 asccomm_batch_commit failed because batch is larger than SQ capacity.\n");

    const uint32_t start_wqebb_index = sq_head;
    const uint32_t new_sq_head = start_wqebb_index + pre_sq_cnt;
    const uint64_t sq_base_addr = batch_handle.sq_context.contextInfo.roceSq.sqVa;
    const uint32_t start_slot = start_wqebb_index & (sq_depth - 1U);
    const uint32_t tail_wqebb_count = sq_depth - start_slot;
    const uint32_t first_wqebb_count = pre_sq_cnt < tail_wqebb_count ? pre_sq_cnt : tail_wqebb_count;
    const uint32_t second_wqebb_count = pre_sq_cnt - first_wqebb_count;
    __gm__ uint8_t* first_wqe_addr =
        reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + static_cast<uint64_t>(start_slot) * ASCCOMM_MLX5_WQEBB_SIZE);
    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    asc_copy_ub2gm_align(
        first_wqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer), 1,
        first_wqebb_count * ASCCOMM_MLX5_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (second_wqebb_count != 0U) {
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
            reinterpret_cast<__ubuf__ uint8_t*>(
                batch_handle.buffer.raw_buffer + first_wqebb_count * ASCCOMM_MLX5_WQEBB_U32_NUM),
            1, second_wqebb_count * ASCCOMM_MLX5_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);

    chnl_ptr->sqHead = new_sq_head;
    chnl_ptr->cqHead = batch_handle.cursor.cq_head;
    const auto& batch_state = *reinterpret_cast<const roce_mlx5_batch_state*>(
        reinterpret_cast<const uint8_t*>(&batch_handle.mr_key) + sizeof(roce_mr_key));
    const auto* last_wqe = reinterpret_cast<const __ubuf__ uint64_t*>(
        batch_handle.buffer.raw_buffer + batch_state.last_wqe_wqebb_idx * ASCCOMM_MLX5_WQEBB_U32_NUM);
    const uint64_t db_value = *last_wqe;
    knock_door_bell(chnl_ptr, new_sq_head, db_value, batch_handle.buffer.raw_buffer);
    batch_handle.cursor.sq_head = new_sq_head;
    batch_handle.cursor.pre_sq_cnt = 0U;
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::poll_batch_cq(
    roce_batch_handle& batch_handle, uint32_t expect_idx)
{
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    const int32_t poll_ret = poll_cq(chnl_ptr, expect_idx);
    if (poll_ret != ASCCOMM_SUCCESS) {
        return poll_ret;
    }
    batch_handle.cursor.cq_tail = chnl_ptr->cqTail;
    batch_handle.cursor.sq_tail = chnl_ptr->sqTail;
    return ASCCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::asccomm_drain(roce_batch_handle& batch_handle)
{
    (void)pipe;
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || batch_handle.buffer.buffer_capacity == 0U ||
            batch_handle.cursor.pre_sq_cnt != 0U,
        "Asccomm MLX5 batch asccomm_drain failed with invalid batch handle.\n");
    return poll_batch_cq(batch_handle, batch_handle.cursor.cq_head);
}
} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_MLX5_H
#endif
