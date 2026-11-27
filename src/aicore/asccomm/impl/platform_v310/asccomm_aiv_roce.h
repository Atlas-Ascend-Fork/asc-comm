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
 * \file asccomm_aiv_roce.h
 * \brief asccomm AIV implementation for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_ROCE_H

#include "asccomm_aiv_roce_def.h"
#include <c_api/vector_datamove/vector_datamove.h>
#include "../../common/asccomm_log.h"
#include "../../common/asccomm_utils.h"
#include "../../common/asccomm_inner_def.h"

namespace asc {

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff) & (ASCCOMM_ROCE_ADDR_ALIGNMENT - 1U)) != 0U,
        "Hcomm init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff)));
    ASCCOMM_DEBUG_TRAP_IF(len < ASCCOMM_UB_BUF_SIZE, "Hcomm init failed: len = %u\n", len);
    wqe_ub = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    return ASCCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::init(const LocalTensor<T>& buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        len < ASCCOMM_UB_BUF_SIZE || buff.GetSize() * sizeof(T) < ASCCOMM_UB_BUF_SIZE,
        "Hcomm init failed: len = %u, tensor_size = %llu\n", len, static_cast<uint64_t>(buff.GetSize()) * sizeof(T));
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff.GetPhyAddr()) & (ASCCOMM_ROCE_ADDR_ALIGNMENT - 1U)) != 0U,
        "Hcomm init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff.GetPhyAddr())));
    wqe_ub = reinterpret_cast<__ubuf__ uint32_t*>(buff.GetPhyAddr());
    return ASCCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_ctrl_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, uint32_t wqebb_index, uint32_t sq_depth)
{
    static_assert(config.cqe <= 1U, "fill_ctrl_seg only supports cqe values 0 and 1.");
    constexpr uint32_t en_cqe = config.cqe;
    uint8_t owner = (wqebb_index & sq_depth) == 0 ? 0 : 1;
    wqe_ptr->ctrl.owner_sl = (owner << ROCE_1825_WQE_OWNER_SHIFT) | ROCE_1825_WQE_CTRL_VALUE;

    constexpr uint8_t df_tsl = (en_cqe == 1U ? (1U << ROCE_1825_WQE_SQ_SIGNAL_SHIFT) : 0U) | ROCE_1825_WQE_SQ_VA_VALUE |
                               (sizeof(roce_wqe_task_seg) / ROCE_1825_WQE_TASK_SEG_ALIGN);
    wqe_ptr->ctrl.df_tsl = df_tsl;

    wqe_ptr->ctrl.wf_bdsl = hto_ns(static_cast<uint16_t>(0 << ROCE_1825_WQE_FAST_DMA_SHIFT));
    // Hardware confirms SSN is not required on this path. Keep the field encoding with zero
    // as a reference for restoring a WQE sequence number if a future mode requires it.
    wqe_ptr->ctrl.wf_bdsl |= hto_ns(static_cast<uint16_t>(0U & ROCE_1825_WQE_SSN_MASK) << ROCE_1825_WQE_SSN_SHIFT);
    wqe_ptr->ctrl.wf_bdsl |= hto_ns(static_cast<uint16_t>(
        static_cast<uint32_t>(1) << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));

    wqe_ptr->ctrl.cl_pi = hto_nl(ROCE_1825_WQE_CMP_TASK_LEN1 << ROCE_1825_WQE_CMP_TASK_LEN_SHIFT);
}

template <auto const& config, asccomm_roce_op_type op_type>
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_task_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR dst, uint64_t len, uint32_t r_key, uint32_t l_key)
{
    static_assert(config.cqe <= 1U, "fill_task_seg only supports cqe values 0 and 1.");
    static_assert(config.fence <= 1U, "fill_task_seg only supports fence values 0 and 1.");
    constexpr uint32_t en_cqe = config.cqe;
    constexpr uint32_t fence = config.fence;
    constexpr uint32_t task_ctrl = ((en_cqe & 1U) << ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT) |
                                   ((fence & 1U) << ROCE_1825_WQE_TASK_FENCE_SHIFT) |
                                   (static_cast<uint32_t>(op_type) << ROCE_1825_WQE_TASK_OPTYPE_SHIFT);
    wqe_ptr->task.com_task.value = hto_nl(task_ctrl);

    wqe_ptr->task.data_len = hto_nl((uint32_t)len);
    wqe_ptr->task.imm_data = 0;
    constexpr uint32_t dw3Value =
        (op_type == asccomm_roce_op_type::READ) ? hto_nl(ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN) : 0U;
    wqe_ptr->task.dw3.value = dw3Value;
    wqe_ptr->task.va_remote = hto_nll((uint64_t)dst);
    wqe_ptr->task.r_key = hto_nl(r_key);
    wqe_ptr->task.ulp = hto_nl(l_key & 0xffffff00U);
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_data_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR src, uint64_t len, uint32_t l_key)
{
    wqe_ptr->data.va_local = hto_nll((uint64_t)src);
    wqe_ptr->data.r_len = hto_nl((uint32_t)len);
    wqe_ptr->data.le_key = hto_nl((l_key & (~ROCE_1825_WQE_NEXT_SGE_INVALID)) | ROCE_1825_WQE_NEXT_SGE_INVALID);
}

// Pre-mark the next WQEBB owner byte as invalid so the hardware stops there until the following WQE is posted.
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::write_invalid_wqebb(
    __gm__ uint8_t* sq_addr, uint32_t sq_head, uint32_t sq_depth)
{
    const uint8_t owner_sl = ((sq_head & sq_depth) == 0) ? 0xff : 0x7f;
    // A device store bypasses the scalar cache and avoids a full cache-line
    // clean/invalidation for this one-byte producer marker.
    st_dev(owner_sl, sq_addr, 0);
}

template <auto const& config, asccomm_roce_op_type op_type>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::make_wqe(
    __gm__ ChannelEntity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t sq_head, uint32_t sq_depth)
{
    static_assert(config.cqe <= 1U, "make_wqe only supports cqe values 0 and 1.");
    static_assert(config.fence <= 1U, "make_wqe only supports fence values 0 and 1.");
    int32_t remote_idx = asccomm_find_buffer_idx(chnl_ptr->remoteBufferAddr, chnl_ptr->remoteBufferNum, dst, len);
    ASCCOMM_DEBUG_TRAP_IF(remote_idx < 0, "Hcomm make_wqe: failed with invalid remote buffer addr %llu.\n", dst);
    int32_t local_idx = asccomm_find_buffer_idx(chnl_ptr->localBufferAddr, chnl_ptr->localBufferNum, src, len);
    ASCCOMM_DEBUG_TRAP_IF(local_idx < 0, "Hcomm make_wqe: failed with invalid local buffer addr %llu.\n", src);
    __gm__ uint8_t* sq_base_addr = (__gm__ uint8_t*)(chnl_ptr->sqContextAddr->contextInfo.roceSq.sqVa);
    __gm__ uint8_t* sq_addr = (__gm__ uint8_t*)(sq_base_addr + (sq_head & (sq_depth - 1)) * ASCCOMM_ROCE_WQEBB_SIZE);

    __ubuf__ roce_wqe_entry* wqe_ptr = (__ubuf__ roce_wqe_entry*)(wqe_ub);
    uint32_t r_key = chnl_ptr->remoteBufferAddr[remote_idx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    uint32_t l_key = chnl_ptr->localBufferAddr[local_idx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    uint32_t next_wqebb_index = (sq_head + 1U) & ROCE_SQ_INDEX_MASK;
    __gm__ uint8_t* sq_addr_next =
        (__gm__ uint8_t*)(sq_base_addr + (next_wqebb_index & (sq_depth - 1)) * ASCCOMM_ROCE_WQEBB_SIZE);
    fill_ctrl_seg<config>(wqe_ptr, sq_head, sq_depth);
    fill_task_seg<config, op_type>(wqe_ptr, dst, len, r_key, l_key);
    fill_data_seg(wqe_ptr, src, len, l_key);
    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    asc_copy_ub2gm_align(
        sq_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_ub), 1, sizeof(roce_wqe_entry),
        asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    // The next-slot marker only needs to be visible before the eventual doorbell. Issue it after the
    // WQE copy so the device store overlaps MTE3 instead of blocking the scalar pipe first.
    write_invalid_wqebb(sq_addr_next, next_wqebb_index, sq_depth);
    // Wait until MTE3 has finished reading wqe_ub before Scalar can reuse it.
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM make_wqe: set wqe to qp ok.\n");
    return ASCCOMM_SUCCESS;
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::check_channel_param(__gm__ ChannelEntity* chnl_ptr)
{
    if (chnl_ptr == nullptr) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "ASCCOMM check_channel_param failed: chnl_ptr is nullptr.\n");
        return false;
    }
    if (chnl_ptr->sqNum == 0 || chnl_ptr->cqNum == 0 || chnl_ptr->sqContextAddr == 0 || chnl_ptr->cqContextAddr == 0) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR,
            "ASCCOMM check_channel_param failed: sqNum = %u cqNum = %u sqContextAddr = %llu  cq_context_addr = %llu\n",
            chnl_ptr->sqNum, chnl_ptr->cqNum, chnl_ptr->sqContextAddr, chnl_ptr->cqContextAddr);
        return false;
    }
    uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cq_depth = chnl_ptr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t cqe_size = chnl_ptr->cqContextAddr[0].contextInfo.roceCq.cqeSize;
    if (chnl_ptr->protocol != COMM_PROTOCOL_ROCE || cq_depth == 0 ||
        chnl_ptr->sqContextAddr[0].contextInfo.roceSq.sqVa == 0 || sq_depth <= 1U || sq_depth > ROCE_SQ_INDEX_MASK ||
        (sq_depth & (sq_depth - 1U)) != 0 || chnl_ptr->sqContextAddr[0].contextInfo.roceSq.dbHwVa == 0 ||
        chnl_ptr->cqContextAddr[0].contextInfo.roceCq.cqVa == 0 ||
        chnl_ptr->cqContextAddr[0].contextInfo.roceCq.dbSwVa == 0 || cqe_size < sizeof(roce_cqe_entry) ||
        (cq_depth & (cq_depth - 1U)) != 0) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "ASCCOMM check_channel_param failed: invalid SQ/CQ context.\n");
        return false;
    }
    return true;
}

template <bool commit, auto const& config, asccomm_roce_op_type op_type>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::post_send(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        dst == 0 || src == 0 || len >= ROCE_1825_WQE_MAX_DATA_LEN,
        "Hcomm post_send failed: dst = %llu, src = %llu, len = %llu.\n", dst, src, len);
    __gm__ ChannelEntity* chnl_ptr = (__gm__ ChannelEntity*)(channel);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "Hcomm post_send failed with invalid channel.\n");
    uint32_t wqebb_head = chnl_ptr->sqHead;
    uint32_t wqebb_tail = chnl_ptr->sqTail;
    uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "ASCCOMM post_send: op_type = %u, sq_head = 0x%x, sq_tail = 0x%x, sq_depth = %u.\n",
        static_cast<uint32_t>(op_type), chnl_ptr->sqHead, chnl_ptr->sqTail, sq_depth);
    uint16_t outstanding = static_cast<uint16_t>(wqebb_head - wqebb_tail);
    if (outstanding >= sq_depth || sq_depth - outstanding <= ASCCOMM_POLL_CQ_THRESHOLD) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_INFO, "ASCCOMM post_send: RoCE SQ overflow sq_head=%u sq_tail=%u sq_depth=%u\n", wqebb_head,
            wqebb_tail, sq_depth);
        (void)poll_cq<true>(chnl_ptr, chnl_ptr->cqHead, ASCCOMM_POLL_CQ_THRESHOLD);
        wqebb_tail = chnl_ptr->sqTail;
        outstanding = static_cast<uint16_t>(wqebb_head - wqebb_tail);
    }
    // Keep one WQEBB free for the next-slot invalid marker.
    ASCCOMM_DEBUG_TRAP_IF(
        outstanding >= sq_depth - 1U, "Hcomm post_send: RoCE SQ has no free WQE. sq_head=%u sq_tail=%u sq_depth=%u\n",
        wqebb_head, wqebb_tail, sq_depth);

    (void)make_wqe<config, op_type>(chnl_ptr, dst, src, len, wqebb_head, sq_depth);

    if constexpr (config.cqe != 0) {
        uint32_t cq_head = chnl_ptr->cqHead + 1U;
        chnl_ptr->cqHead = cq_head;
        ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM post_send: update CQ PI cq_head = %u\n", cq_head);
    }
    uint32_t next_wqebb_head = (wqebb_head + 1U) & ROCE_SQ_INDEX_MASK;
    chnl_ptr->sqHead = next_wqebb_head;
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM post_send: update SQ PI sq_head = %u\n", chnl_ptr->sqHead);

    if constexpr (commit) {
        knock_door_bell(chnl_ptr, next_wqebb_head);
        ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM post_send: knock_door_bell ok.\n");
    }
    return ASCCOMM_SUCCESS;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::write_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, config, asccomm_roce_op_type::WRITE>(channel, dst, src, len);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::read_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, config, asccomm_roce_op_type::READ>(channel, src, dst, len);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::write_value_nbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    (void)commit;
    (void)commit_pipe;
    (void)req_pipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)value;
    ASCCOMM_DEBUG_TRAP_IF(true, "Hcomm ROCE write_value_nbi is not supported.\n");
    return ASCCOMM_SUCCESS;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::write_with_notify_nbi(
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
    ASCCOMM_DEBUG_TRAP_IF(true, "Hcomm ROCE write_with_notify_nbi is not supported.\n");
    return ASCCOMM_SUCCESS;
}

__aicore__ inline uint64_t asccomm_impl<COMM_PROTOCOL_ROCE>::get_db_value(
    uint32_t sq_head, uint32_t qpn, uint64_t vendor)
{
    roce_db_entry db_entry;
    db_entry.dw0.value = 0;
    db_entry.dw0.bs.c = 0;
    db_entry.dw0.bs.r = 0;
    db_entry.dw0.bs.ctx_size = 1;
    db_entry.dw0.bs.qpn = qpn;
    db_entry.dw0.bs.sub_type = 0;
    db_entry.dw0.bs.resv = 0;
    db_entry.dw0.bs.pi = ((sq_head >> ROCE_1825_SQ_DB_PI_HIGH_SHIFT) & 0xff);
    db_entry.dw0.bs.sgid_idx = ROCE_1825_SQ_DB_SGIT_IDX;
    db_entry.dw0.bs.type = ROCE_1825_SQ_DB_TYPE;
    db_entry.dw0.bs.mtu_shift =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_MTUSHIFT_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    db_entry.dw0.bs.cos =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_COS_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    db_entry.dw0.bs.xrc_vld = 0;
    return db_entry.dw0.value;
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::knock_door_bell(
    __gm__ ChannelEntity* chnl_ptr, uint32_t sq_head)
{
    uint64_t db_value = get_db_value(
        sq_head, chnl_ptr->sqContextAddr->contextInfo.roceSq.qpn,
        chnl_ptr->sqContextAddr->contextInfo.roceSq.dbVendorSpecified);
    __gm__ uint64_t* db_hw_addr =
        reinterpret_cast<__gm__ uint64_t*>(chnl_ptr->sqContextAddr->contextInfo.roceSq.dbHwVa);
    // SQ WQEBBs and the invalid owner marker must reach DDR before the doorbell notifies the NIC.
    asc_sync_data_barrier(DSB_DDR);
    st_dev(db_value, db_hw_addr + (sq_head & 0xff), 0);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM knock_door_bell: write hw db ok, hw_db_val = %llu\n", db_value);

    uint32_t cq_head = chnl_ptr->cqHead;
    uint32_t cq_tail = chnl_ptr->cqTail;
    uint32_t cq_depth = chnl_ptr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cq_outstanding = cq_head - cq_tail;
    if (cq_outstanding != 0 && cq_depth - cq_outstanding <= ASCCOMM_POLL_CQ_THRESHOLD) {
        uint32_t poll_count =
            cq_outstanding < ASCCOMM_NUM_CQE_PER_POLL_CQ ? cq_outstanding : ASCCOMM_NUM_CQE_PER_POLL_CQ;
        uint32_t idx = cq_tail + poll_count;
        ASCCOMM_KERNEL_LOG(
            KERNEL_INFO,
            "ASCCOMM knock_door_bell: cq overflow sq_head=%u cq_head=%u cq_tail=%u idx=%u sq_depth=%u cq_depth=%u\n",
            sq_head, cq_head, cq_tail, idx, sq_depth, cq_depth);
        (void)poll_cq(chnl_ptr, idx);
    }
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::commit(ChannelHandle channel)
{
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM commit: Enter\n");
    __gm__ ChannelEntity* chnl_ptr = (__gm__ ChannelEntity*)(channel);
    // post_send updates the software PI; the hardware head is not a staging location for deferred posts.
    uint32_t sq_head = chnl_ptr->sqHead;
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM commit: sq_head = %u\n", sq_head);

    knock_door_bell(chnl_ptr, sq_head);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM commit: Exit ok.\n");
    return ASCCOMM_SUCCESS;
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::check_cqe_owner(
    uint32_t owner_id_qpn, uint32_t cq_tail, uint32_t depth)
{
    uint32_t cur_owner = ((owner_id_qpn & (1U << ROCE_1825_CQE_OWNER_SHIFT)) != 0);
    uint32_t expect_owner = (uint32_t)(((cq_tail & depth) == 0));
    return (expect_owner ^ cur_owner) != 0;
}

template <bool sq_safe_mode>
__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::en_continue(
    uint32_t expect_idx, uint32_t cq_tail, uint16_t sq_head, uint16_t sq_tail, uint32_t sq_depth, uint32_t threshold)
{
    bool is_continue = false;
    if constexpr (sq_safe_mode) {
        uint16_t outstanding = static_cast<uint16_t>(sq_head - sq_tail);
        is_continue = (outstanding >= sq_depth || sq_depth - outstanding <= threshold) && (cq_tail != expect_idx);
    } else {
        is_continue = (cq_tail != expect_idx);
    }
    return is_continue;
}

__aicore__ inline asccomm_cqe_status asccomm_impl<COMM_PROTOCOL_ROCE>::wait_cqe_and_update_sq_tail(
    __gm__ uint32_t* cqe_words, uint32_t cq_tail, uint32_t cq_depth, uint32_t expected_qpn, uint16_t wqebb_head,
    uint16_t& wqebb_tail, uint32_t& cqe_type)
{
    // DW0 and DW1 are contiguous: read them as one 64-bit device load.
    uint64_t cqe_pair = ld_dev(reinterpret_cast<__gm__ uint64_t*>(cqe_words), 0);
    uint32_t owner_id_qpn = static_cast<uint32_t>(cqe_pair);
    uint32_t op_sr_wqebb = static_cast<uint32_t>(cqe_pair >> ROCE_1825_CQE_OP_SR_WQEBB_SHIFT);
    cqe_type = (op_sr_wqebb >> ROCE_1825_CQE_OPCODE_SHIFT) & ROCE_1825_CQE_OPCODE_MASK;
    uint32_t cqe_qpn = owner_id_qpn & ROCE_1825_CQE_QPN_MASK;
    bool cqe_ready = cqe_type != ROCE_1825_CQE_OPTYPE_INVALID && check_cqe_owner(owner_id_qpn, cq_tail, cq_depth) &&
                     cqe_qpn == expected_qpn;
    if (!cqe_ready) {
        return asccomm_cqe_status::NOT_READY;
    }
    if (cqe_type == ROCE_1825_CQE_OPTYPE_ERROR) {
        return asccomm_cqe_status::ERROR;
    }

    uint16_t completed_wqebb_index = static_cast<uint16_t>(op_sr_wqebb & ROCE_SQ_INDEX_MASK);
    uint16_t next_wqebb_tail = static_cast<uint16_t>(completed_wqebb_index + 1U);
    uint16_t wqebb_advance = static_cast<uint16_t>(next_wqebb_tail - wqebb_tail);
    uint16_t wqebb_outstanding = static_cast<uint16_t>(wqebb_head - wqebb_tail);
    if (wqebb_advance == 0U || wqebb_advance > wqebb_outstanding) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR, "ASCCOMM poll_cq: invalid SQ completion, wqebb_index = %u.\n", completed_wqebb_index);
        return asccomm_cqe_status::INVALID;
    }
    wqebb_tail = next_wqebb_tail;
    return asccomm_cqe_status::SUCCESS;
}

template <bool sq_safe_mode>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::poll_cq(
    __gm__ ChannelEntity* chnl_ptr, uint32_t expect_idx, uint32_t threshold)
{
    if (expect_idx == chnl_ptr->cqTail) {
        return ASCCOMM_SUCCESS;
    }
    auto cq_context_info = chnl_ptr->cqContextAddr[0].contextInfo;
    uint32_t cqe_size = cq_context_info.roceCq.cqeSize;
    uint32_t cq_depth = cq_context_info.roceCq.cqDepth;
    uint32_t cq_tail = chnl_ptr->cqTail;
    uint16_t wqebb_head = static_cast<uint16_t>(chnl_ptr->sqHead);
    uint16_t wqebb_tail = static_cast<uint16_t>(chnl_ptr->sqTail);
    uint32_t sq_depth = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t expected_qpn = chnl_ptr->sqContextAddr[0].contextInfo.roceSq.qpn & ROCE_1825_CQE_QPN_MASK;
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM poll_cq: cq_tail= %u expect_idx = %u\n", cq_tail, expect_idx);
    __gm__ uint8_t* cq_base_buf = (__gm__ uint8_t*)(cq_context_info.roceCq.cqVa);
    while (true) {
        if (!en_continue<sq_safe_mode>(expect_idx, cq_tail, wqebb_head, wqebb_tail, sq_depth, threshold)) {
            break;
        }
        __gm__ uint8_t* cqe_addr = (__gm__ uint8_t*)(cq_base_buf + cqe_size * (cq_tail & (cq_depth - 1)));
        __gm__ uint32_t* cqe_words = reinterpret_cast<__gm__ uint32_t*>(cqe_addr);
        uint32_t cqe_type = ROCE_1825_CQE_OPTYPE_INVALID;
        asccomm_cqe_status cqe_status = asccomm_cqe_status::NOT_READY;
        const int64_t start_cycle = AscendC::GetSystemCycle();
        while (cqe_status == asccomm_cqe_status::NOT_READY &&
               AscendC::GetSystemCycle() - start_cycle < ROCE_1825_POLL_CQ_TIMEOUT_CYCLES) {
            cqe_status = wait_cqe_and_update_sq_tail(
                cqe_words, cq_tail, cq_depth, expected_qpn, wqebb_head, wqebb_tail, cqe_type);
        }
        if (cqe_status == asccomm_cqe_status::NOT_READY) {
            ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "ASCCOMM poll_cq: failed, overtime and exit.\n");
            AscendC::Trap();
            return ASCCOMM_FAILED;
        }
        if (cqe_status == asccomm_cqe_status::ERROR) {
            uint32_t owner_id_qpn = ld_dev(cqe_words, 0);
            uint32_t syndrome_word = ld_dev(cqe_words + ROCE_1825_CQE_SYNDROME_WORD_INDEX, 0);
            uint32_t syndrome = syndrome_word & ROCE_1825_CQE_SYNDROME_MASK;
            uint32_t wqe_counter = syndrome_word >> ROCE_1825_CQE_WQE_COUNTER_SHIFT;
            ASCCOMM_KERNEL_LOG(
                KERNEL_ERROR, "ASCCOMM poll_cq: failed, syndrome = 0x%x, err_code = %u, wqe_counter = %u\n", syndrome,
                (owner_id_qpn >> ROCE_1825_CQE_QPN_BITS) & ROCE_1825_CQE_ERR_CODE_MASK, wqe_counter);
            AscendC::Trap();
            return ASCCOMM_FAILED;
        }
        if (cqe_status == asccomm_cqe_status::INVALID) {
            AscendC::Trap();
            return ASCCOMM_FAILED;
        }
        cq_tail += 1;
        ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM poll_cq: cq_tail = %u\n", cq_tail);
    }
    chnl_ptr->cqTail = cq_tail;
    chnl_ptr->sqTail = wqebb_tail;
    __gm__ uint32_t* db_sw_addr = reinterpret_cast<__gm__ uint32_t*>(cq_context_info.roceCq.dbSwVa);
    st_dev(hto_nl(cq_tail & ROCE_1825_CQE_UPDATE_CI_MASK), db_sw_addr, 0);
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "ASCCOMM poll_cq: knock cq_db ok, cq_tail = %u, sq_tail = %u\n", cq_tail, chnl_ptr->sqTail);
    return ASCCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::drain(ChannelHandle channel)
{
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM drain: Enter\n");
    __gm__ ChannelEntity* chnl_ptr = (__gm__ ChannelEntity*)(channel);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "Hcomm drain failed with invalid channel.\n");
    uint32_t cq_head = chnl_ptr->cqHead;
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM drain: cq_head = %u\n", cq_head);
    if (poll_cq(chnl_ptr, cq_head) != ASCCOMM_SUCCESS) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "ASCCOMM drain: poll_cq failed.\n");
        return ASCCOMM_FAILED;
    }
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "ASCCOMM drain: Exit ok.\n");
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::lock(ChannelHandle channel)
{
    return asccomm_channel_lock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::unlock(ChannelHandle channel)
{
    return asccomm_channel_unlock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline roce_batch_handle& asccomm_impl<COMM_PROTOCOL_ROCE>::get_handle_ref(
    roce_batch_handle& batch_handle, uint32_t channel_idx, GM_ADDR remote_addr)
{
    (void)channel_idx;
    (void)remote_addr;
    return batch_handle;
}

__aicore__ inline roce_batch_handle asccomm_impl<COMM_PROTOCOL_ROCE>::make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    roce_batch_handle batch_handle{};
    __gm__ ChannelEntity* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    uintptr_t buffer_addr = reinterpret_cast<uintptr_t>(buff);
    ASCCOMM_DEBUG_TRAP_IF(
        !check_channel_param(chnl_ptr) || buff_len < ASCCOMM_ROCE_WQEBB_SIZE ||
            (buffer_addr & (ASCCOMM_ROCE_ADDR_ALIGNMENT - 1U)) != 0U,
        "Hcomm make_batch_handle: failed with invalid channel or buffer.\n");
    int32_t remote_idx = asccomm_find_buffer_idx(chnl_ptr->remoteBufferAddr, chnl_ptr->remoteBufferNum, remote_addr, 1);
    ASCCOMM_DEBUG_TRAP_IF(
        remote_idx < 0, "Hcomm make_batch_handle: failed with invalid remote addr %llu.\n", remote_addr);
    int32_t local_idx = asccomm_find_buffer_idx(chnl_ptr->localBufferAddr, chnl_ptr->localBufferNum, local_addr, 1);
    ASCCOMM_DEBUG_TRAP_IF(local_idx < 0, "Hcomm make_batch_handle: failed with invalid local addr %llu.\n", local_addr);
    auto sq_ctx = chnl_ptr->sqContextAddr[ASCCOMM_DEFAULT_QP_IDX];
    auto cq_ctx = chnl_ptr->cqContextAddr[ASCCOMM_DEFAULT_QP_IDX];
    batch_handle.channel_handle = channel;
    batch_handle.sq_context = sq_ctx;
    batch_handle.cq_context = cq_ctx;
    batch_handle.cursor.sq_head = chnl_ptr->sqHead;
    batch_handle.cursor.sq_tail = chnl_ptr->sqTail;
    batch_handle.cursor.cq_head = chnl_ptr->cqHead;
    batch_handle.cursor.cq_tail = chnl_ptr->cqTail;
    batch_handle.cursor.pre_sq_cnt = 0U;
    batch_handle.buffer.raw_buffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    batch_handle.buffer.buffer_capacity = buff_len / ASCCOMM_ROCE_WQEBB_SIZE;
    batch_handle.mr_key.l_key = chnl_ptr->localBufferAddr[local_idx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    batch_handle.mr_key.r_key = chnl_ptr->remoteBufferAddr[remote_idx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    const uint32_t remote_key_be = hto_nl(batch_handle.mr_key.r_key);
    const uint32_t local_key_be = hto_nl(batch_handle.mr_key.l_key & 0xffffff00U);
    const uint32_t data_key_be =
        hto_nl((batch_handle.mr_key.l_key & (~ROCE_1825_WQE_NEXT_SGE_INVALID)) | ROCE_1825_WQE_NEXT_SGE_INVALID);
    batch_handle.mr_key.key_word =
        static_cast<uint64_t>(remote_key_be) | (static_cast<uint64_t>(local_key_be) << ROCE_1825_WQE_WORD_BITS);
    batch_handle.mr_key.data_key_high_word = static_cast<uint64_t>(data_key_be) << ROCE_1825_WQE_WORD_BITS;

    return batch_handle;
}

template <typename U>
__aicore__ inline roce_batch_handle asccomm_impl<COMM_PROTOCOL_ROCE>::make_batch_handle(
    ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    ASCCOMM_DEBUG_TRAP_IF(
        buff.GetSize() * sizeof(U) < buff_len, "Hcomm make_batch_handle: failed with invalid buffer size.\n");
    return make_batch_handle(
        channel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buff_len, remote_addr, local_addr);
}

template <auto const& config, asccomm_roce_op_type op_type>
__aicore__ inline __ubuf__ uint64_t* asccomm_impl<COMM_PROTOCOL_ROCE>::fill_batch_wqe_header(
    __ubuf__ uint64_t* wqe, GM_ADDR remote_addr, uint64_t total_len, uint32_t base_wqebb_index, uint32_t sq_depth,
    uint16_t wf_bdsl_be, uint64_t key_word)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "fill_batch_wqe_header only supports cqe values 0 and 1.");
    static_assert(config.fence == 0 || config.fence == 1, "fill_batch_wqe_header only supports fence values 0 and 1.");
    constexpr uint32_t task_ctrl_be = hto_nl(
        ((config.cqe & 1U) << ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT) |
        ((config.fence & 1U) << ROCE_1825_WQE_TASK_FENCE_SHIFT) |
        ((static_cast<uint32_t>(op_type) & 0x1fU) << ROCE_1825_WQE_TASK_OPTYPE_SHIFT));
    constexpr uint32_t dw3Be =
        op_type == asccomm_roce_op_type::READ ? hto_nl(ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN) : 0U;
    constexpr uint32_t cl_pi_be = hto_nl(ROCE_1825_WQE_CMP_TASK_LEN1 << ROCE_1825_WQE_CMP_TASK_LEN_SHIFT);
    constexpr uint8_t df_tsl = static_cast<uint8_t>(
        (config.cqe == 1 ? (1U << ROCE_1825_WQE_SQ_SIGNAL_SHIFT) : 0U) | ROCE_1825_WQE_SQ_VA_VALUE |
        (sizeof(roce_wqe_task_seg) / ROCE_1825_WQE_TASK_SEG_ALIGN));
    constexpr uint64_t dw3Word = static_cast<uint64_t>(dw3Be) << ROCE_1825_WQE_WORD_BITS;
    constexpr uint64_t ctrl_base_word = (static_cast<uint64_t>(df_tsl) << ROCE_1825_WQE_BYTE_BITS) |
                                        (static_cast<uint64_t>(cl_pi_be) << ROCE_1825_WQE_WORD_BITS) |
                                        ROCE_1825_WQE_CTRL_VALUE;
    const uint32_t total_len_be = hto_nl(static_cast<uint32_t>(total_len));
    const uint64_t owner_word = static_cast<uint64_t>((base_wqebb_index & sq_depth) != 0U) << ROCE_1825_WQE_OWNER_SHIFT;

    wqe[0] = ctrl_base_word | (static_cast<uint64_t>(wf_bdsl_be) << ROCE_1825_WQE_HALFWORD_BITS) | owner_word;
    wqe[1] = 0;
    wqe[2] = static_cast<uint64_t>(task_ctrl_be) | (static_cast<uint64_t>(total_len_be) << ROCE_1825_WQE_WORD_BITS);
    wqe[3] = dw3Word;
    wqe[4] = hto_nll(reinterpret_cast<uint64_t>(remote_addr));
    wqe[5] = key_word;
    constexpr uint32_t public_seg_size = sizeof(roce_wqe_ctrl_seg) + sizeof(roce_wqe_task_seg);
    return wqe + public_seg_size / ASCCOMM_ROCE_WQEBB_U64_SIZE;
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::fill_batch_data_seg(
    __ubuf__ uint64_t* sge, GM_ADDR addr, uint32_t len, uint32_t data_key_be, bool last_sge)
{
    constexpr uint32_t last_sge_be_mask = ROCE_1825_WQE_NEXT_SGE_INVALID >> 24;
    const uint32_t seg_key_be = last_sge ? data_key_be : data_key_be & ~last_sge_be_mask;
    sge[0] = hto_nll(reinterpret_cast<uint64_t>(addr));
    sge[1] = static_cast<uint64_t>(hto_nl(len)) | (static_cast<uint64_t>(seg_key_be) << ROCE_1825_WQE_WORD_BITS);
}

template <auto const& config>
__aicore__ inline void asccomm_impl<COMM_PROTOCOL_ROCE>::update_batch_cursor(
    roce_batch_handle& batch_handle, uint32_t wqebb_advance)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "update_batch_cursor only supports cqe values 0 and 1.");
    batch_handle.cursor.pre_sq_cnt += wqebb_advance;
    if constexpr (config.cqe == 1) {
        batch_handle.cursor.cq_head += 1U;
    }
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::has_batch_sq_capacity(
    __gm__ ChannelEntity* chnl_ptr, const roce_batch_handle& batch_handle, uint32_t additional_wqebb_num)
{
    uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    uint16_t sq_tail = static_cast<uint16_t>(chnl_ptr->sqTail);
    uint32_t end_wqebb_index =
        (batch_handle.cursor.sq_head + batch_handle.cursor.pre_sq_cnt + additional_wqebb_num) & ROCE_SQ_INDEX_MASK;
    uint16_t outstanding = static_cast<uint16_t>(end_wqebb_index - sq_tail);
    return outstanding <= static_cast<uint16_t>(sq_depth - 1U);
}

__aicore__ inline bool asccomm_impl<COMM_PROTOCOL_ROCE>::is_batch_sq_near_full(
    __gm__ ChannelEntity* chnl_ptr, const roce_batch_handle& batch_handle)
{
    uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    uint16_t sq_tail = static_cast<uint16_t>(chnl_ptr->sqTail);
    uint32_t end_wqebb_index = (batch_handle.cursor.sq_head + batch_handle.cursor.pre_sq_cnt) & ROCE_SQ_INDEX_MASK;
    uint16_t outstanding = static_cast<uint16_t>(end_wqebb_index - sq_tail);
    return outstanding >= sq_depth || sq_depth - outstanding <= ASCCOMM_POLL_CQ_THRESHOLD;
}

template <asccomm_roce_op_type op_type, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::batch_post_send(
    roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "batch_post_send only supports cqe values 0 and 1.");
    auto* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || dst == 0 || src == 0 || len >= ROCE_1825_WQE_MAX_DATA_LEN ||
            !check_channel_param(chnl_ptr),
        "Hcomm batch_post_send: failed with invalid handle or request.\n");

    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt >= batch_handle.buffer.buffer_capacity, "Hcomm batch_post_send: failed with insufficient buffer\n");
    constexpr uint16_t single_sge_bdsl_be = hto_ns(static_cast<uint16_t>(
        static_cast<uint32_t>(1) << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    __ubuf__ uint64_t* wqe_ptr =
        reinterpret_cast<__ubuf__ uint64_t*>(batch_handle.buffer.raw_buffer + pre_sq_cnt * ASCCOMM_ROCE_WQEBB_U32_NUM);
    uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    uint32_t base_wqebb_index = (batch_handle.cursor.sq_head + batch_handle.cursor.pre_sq_cnt) & ROCE_SQ_INDEX_MASK;
    const uint32_t data_key_be =
        static_cast<uint32_t>(batch_handle.mr_key.data_key_high_word >> ROCE_1825_WQE_WORD_BITS);

    __ubuf__ uint64_t* data_seg = fill_batch_wqe_header<config, op_type>(
        wqe_ptr, dst, len, base_wqebb_index, sq_depth, single_sge_bdsl_be, batch_handle.mr_key.key_word);
    fill_batch_data_seg(data_seg, src, len, data_key_be, true);

    update_batch_cursor<config>(batch_handle, 1U);
    return ASCCOMM_SUCCESS;
}

template <asccomm_roce_op_type op_type, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::batch_post_send(
    roce_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t seg_num)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "batch_post_send only supports cqe values 0 and 1.");
    auto* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || remote_addr == 0 || local_descs == nullptr || seg_num == 0 ||
            seg_num > ROCE_1825_MAX_SGE_NUM || !check_channel_param(chnl_ptr),
        "Hcomm batch_post_send: failed with invalid handle or request.\n");

    uint64_t total_len = 0U;
    for (uint32_t i = 0U; i < seg_num; i++) {
        total_len += local_descs[i].len;
    }
    ASCCOMM_DEBUG_TRAP_IF(
        total_len >= ROCE_1825_WQE_MAX_DATA_LEN, "Hcomm batch_post_send: failed with invalid message length\n");

    constexpr uint32_t public_seg_size = sizeof(roce_wqe_ctrl_seg) + sizeof(roce_wqe_task_seg);
    const uint32_t wqe_bytes = public_seg_size + seg_num * sizeof(roce_wqe_data_seg);
    uint32_t wqebb_num = (wqe_bytes + ASCCOMM_ROCE_WQEBB_SIZE - 1U) / ASCCOMM_ROCE_WQEBB_SIZE;
    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt >= batch_handle.buffer.buffer_capacity ||
            wqebb_num > batch_handle.buffer.buffer_capacity - pre_sq_cnt,
        "Hcomm batch_post_send: failed with insufficient buffer\n");
    __ubuf__ uint64_t* wqe =
        reinterpret_cast<__ubuf__ uint64_t*>(batch_handle.buffer.raw_buffer + pre_sq_cnt * ASCCOMM_ROCE_WQEBB_U32_NUM);
    uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    uint32_t base_wqebb_index = (batch_handle.cursor.sq_head + batch_handle.cursor.pre_sq_cnt) & ROCE_SQ_INDEX_MASK;

    const uint16_t wf_bdsl_be =
        hto_ns(static_cast<uint16_t>(seg_num << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    const uint32_t data_key_be =
        static_cast<uint32_t>(batch_handle.mr_key.data_key_high_word >> ROCE_1825_WQE_WORD_BITS);

    __ubuf__ uint64_t* sge = fill_batch_wqe_header<config, op_type>(
        wqe, remote_addr, total_len, base_wqebb_index, sq_depth, wf_bdsl_be, batch_handle.mr_key.key_word);
    for (uint32_t i = 0U; i < seg_num; i++) {
        fill_batch_data_seg(sge, local_descs[i].addr, local_descs[i].len, data_key_be, i + 1U == seg_num);
        sge += sizeof(roce_wqe_data_seg) / ASCCOMM_ROCE_WQEBB_U64_SIZE;
    }

    update_batch_cursor<config>(batch_handle, wqebb_num);
    return ASCCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::write_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return batch_post_send<asccomm_roce_op_type::WRITE, config>(batch_handle, dst, src, len);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::read_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return batch_post_send<asccomm_roce_op_type::READ, config>(batch_handle, src, dst, len);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::write_nbi(
    roce_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return batch_post_send<asccomm_roce_op_type::WRITE, config>(batch_handle, dst, src_descs, src_num);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::read_nbi(
    roce_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return batch_post_send<asccomm_roce_op_type::READ, config>(batch_handle, src, dst_descs, dst_num);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::batch_commit(roce_batch_handle& batch_handle)
{
    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt == 0 || pre_sq_cnt > ROCE_SQ_INDEX_MASK || pre_sq_cnt > batch_handle.buffer.buffer_capacity,
        "Hcomm batch_commit: failed with invalid WQEBB count\n");
    auto* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    ASCCOMM_DEBUG_TRAP_IF(!check_channel_param(chnl_ptr), "Hcomm batch_commit failed with invalid channel.\n");
    if (is_batch_sq_near_full(chnl_ptr, batch_handle)) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_INFO, "ASCCOMM batch_commit: RoCE SQ near full, poll CQ first. sq_head=%u sq_tail=%u sq_depth=%u\n",
            (batch_handle.cursor.sq_head + pre_sq_cnt) & ROCE_SQ_INDEX_MASK, chnl_ptr->sqTail,
            batch_handle.sq_context.contextInfo.roceSq.depth);
        (void)poll_cq<true>(chnl_ptr, chnl_ptr->cqHead, ASCCOMM_POLL_CQ_THRESHOLD);
    }
    ASCCOMM_DEBUG_TRAP_IF(
        !has_batch_sq_capacity(chnl_ptr, batch_handle),
        "Hcomm batch_commit: failed because batch num larger than SQ capacity\n");
    uint32_t sq_depth = batch_handle.sq_context.contextInfo.roceSq.depth;
    uint32_t start_wqebb_index = batch_handle.cursor.sq_head;
    uint32_t new_sq_head = (start_wqebb_index + pre_sq_cnt) & ROCE_SQ_INDEX_MASK;
    uint32_t sq_head = start_wqebb_index;
    uint64_t sq_base_addr = batch_handle.sq_context.contextInfo.roceSq.sqVa;

    uint32_t start_slot = sq_head % sq_depth;
    uint32_t tail_wqebb_count = sq_depth - start_slot;
    uint32_t first_wqebb_count = pre_sq_cnt < tail_wqebb_count ? pre_sq_cnt : tail_wqebb_count;
    uint32_t second_wqebb_count = pre_sq_cnt - first_wqebb_count;
    __gm__ uint8_t* first_wqe_addr =
        reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + static_cast<uint64_t>(start_slot) * ASCCOMM_ROCE_WQEBB_SIZE);

    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    // Copy the first segment from start_slot to the end of the circular SQ, or the whole batch if it does not wrap.
    asc_copy_ub2gm_align(
        first_wqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer), 1,
        first_wqebb_count * ASCCOMM_ROCE_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (second_wqebb_count != 0) {
        // The batch crosses the SQ boundary; copy the remaining WQEBBs to the beginning of the circular SQ.
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
            reinterpret_cast<__ubuf__ uint8_t*>(
                batch_handle.buffer.raw_buffer + first_wqebb_count * ASCCOMM_ROCE_WQEBB_U32_NUM),
            1, second_wqebb_count * ASCCOMM_ROCE_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    __gm__ uint8_t* sq_addr_next =
        (__gm__ uint8_t*)(sq_base_addr + ((new_sq_head) & (sq_depth - 1)) * ASCCOMM_ROCE_WQEBB_SIZE);
    write_invalid_wqebb(sq_addr_next, new_sq_head, sq_depth);
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    chnl_ptr->sqHead = new_sq_head;
    chnl_ptr->cqHead = batch_handle.cursor.cq_head;
    knock_door_bell(chnl_ptr, new_sq_head);
    batch_handle.cursor.sq_head = new_sq_head;
    batch_handle.cursor.pre_sq_cnt = 0;
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::poll_batch_cq(
    roce_batch_handle& batch_handle, uint32_t expect_idx)
{
    auto* chnl_ptr = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    int32_t poll_ret = poll_cq(chnl_ptr, expect_idx);
    if (poll_ret != ASCCOMM_SUCCESS) {
        return poll_ret;
    }
    batch_handle.cursor.cq_tail = chnl_ptr->cqTail;
    batch_handle.cursor.sq_tail = chnl_ptr->sqTail;
    return ASCCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_ROCE>::drain(roce_batch_handle& batch_handle)
{
    (void)pipe;
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || batch_handle.buffer.buffer_capacity == 0U ||
            batch_handle.cursor.pre_sq_cnt != 0U,
        "Hcomm Batch drain failed with invalid batch handle\n");

    int32_t poll_ret = poll_batch_cq(batch_handle, batch_handle.cursor.cq_head);
    if (poll_ret != ASCCOMM_SUCCESS) {
        ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "ASCCOMM RoCE drain by batch handle failed.\n");
        return poll_ret;
    }
    return ASCCOMM_SUCCESS;
}
} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_ROCE_H
#endif
