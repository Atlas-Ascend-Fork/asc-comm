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
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H

#include "hcomm_aiv_roce_def.h"
#include "../../common/hcomm_utils.h"
#include "../../common/hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    if (len < asccomm_ub_buf_size) {
        return asccomm_failed;
    }
    __ubuf__ uint8_t* aligned_addr = align_addr_to_32_bytes(buff);
    TBuffAddr addr;
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.dataLen = len;
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(aligned_addr));
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t*>(aligned_addr);
#endif

    wqe_ub_.SetAddr(addr);
    cqe_ub_ = wqe_ub_[roce_cqe_pos];
    return asccomm_success;
}

template <typename t>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::init(const LocalTensor<t>& buff, uint32_t len)
{
    if (len < asccomm_ub_buf_size || buff.GetSize() < asccomm_ub_buf_size) {
        return asccomm_failed;
    }
    wqe_ub_ = buff.template ReinterpretCast<uint8_t>();
    cqe_ub_ = wqe_ub_[roce_cqe_pos];
    return asccomm_success;
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::fill_ctrl_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, uint32_t sq_head, uint32_t sq_depth)
{
    uint16_t wf_bdsl =
        (uint16_t)(roce_1825_wqe_data_seg_bdsl | ((sq_head & roce_1825_wqe_msn_mask) << roce_1825_wqe_msn_shift));
    wqe_ptr->ctrl.owner_sl =
        (((sq_head & sq_depth) == 0) ? 0 : (1U << roce_1825_wqe_owner_shift)) | roce_1825_wqe_ctrl_value;
    wqe_ptr->ctrl.df_tsl = (uint8_t)((1U << roce_1825_wqe_cqe_signal_shift) | roce_1825_wqe_va_value |
                                   (sizeof(roce_wqe_task_seg) / roce_1825_seg_len_unit));
    wqe_ptr->ctrl.wf_bdsl = hto_ns(wf_bdsl);
    wqe_ptr->ctrl.cl_pi = hto_nl(1U << roce_1825_wqe_cmp_task_len_shift);
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::fill_task_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR dst, uint64_t len, uint32_t op_type, uint32_t r_key, uint32_t l_key)
{
    wqe_ptr->task.com_task.value = 0;
    wqe_ptr->task.com_task.bs.signal = 1;
    wqe_ptr->task.com_task.bs.op_type = op_type;
    wqe_ptr->task.com_task.value = hto_nl(wqe_ptr->task.com_task.value);

    wqe_ptr->task.data_len = hto_nl((uint32_t)len);
    wqe_ptr->task.imm_data = 0;
    wqe_ptr->task.dw3.value = 0;
    if (op_type == (uint32_t)asccomm_roce_op_type::read) {
        wqe_ptr->task.dw3.bs.last_ext_len = roce_1825_rdma_read_last_ext_len;
        wqe_ptr->task.dw3.value = hto_nl(wqe_ptr->task.dw3.value);
    }
    wqe_ptr->task.va_remote = hto_nll((uint64_t)dst);
    wqe_ptr->task.r_key = hto_nl(r_key);
    wqe_ptr->task.ulp = hto_nl(l_key & 0xffffU);
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::fill_data_seg(
    __ubuf__ roce_wqe_entry* wqe_ptr, GM_ADDR src, uint64_t len, uint32_t l_key)
{
    wqe_ptr->data.va_local = hto_nll((uint64_t)src);
    wqe_ptr->data.r_len = hto_nl((uint32_t)len);
    wqe_ptr->data.le_key = hto_nl((l_key & (~roce_1825_wqe_next_sge_invalid)) | roce_1825_wqe_next_sge_invalid);
}

// Pre-mark the next WQEBB owner byte as invalid so the hardware stops there until the following WQE is posted.
__aicore__ inline void asccomm_impl<comm_protocol_roce>::write_invalid_wqebb(
    __gm__ uint8_t* sq_addr, uint32_t sq_head, uint32_t sq_depth)
{
    __gm__ roce_wqe_ctrl_seg* ctrl = (__gm__ roce_wqe_ctrl_seg*)sq_addr;
    ctrl->owner_sl = ((sq_head & sq_depth) == 0) ? 0xff : 0x7f;
    cache_write_through<uint8_t>(sq_addr, 1);
}

__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::make_wqe(
    __gm__ channel_entity* chnl_ptr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t op_type, uint32_t sq_head,
    uint32_t sq_depth)
{
    int32_t remote_idx = asccomm_find_buffer_idx(chnl_ptr->remote_buffer_addr, chnl_ptr->remote_buffer_num, dst, len);
    if (remote_idx < 0) {
        KERNEL_LOG(KERNEL_INFO, "hcomm make_wqe: failed with invalid remote buffer addr %llu.\n", dst);
        return asccomm_failed;
    }
    int32_t local_idx = asccomm_find_buffer_idx(chnl_ptr->local_buffer_addr, chnl_ptr->local_buffer_num, src, len);
    if (local_idx < 0) {
        KERNEL_LOG(KERNEL_INFO, "hcomm make_wqe: failed with invalid local buffer addr %llu.\n", src);
        return asccomm_failed;
    }
    __gm__ uint8_t* sq_base_addr = (__gm__ uint8_t*)(chnl_ptr->sq_context_addr->context_info.roce_sq.sq_va);
    uint32_t wqe_size = chnl_ptr->sq_context_addr->context_info.roce_sq.wqe_size;
    KERNEL_LOG(KERNEL_INFO, "hcomm make_wqe: get wqe_size from channel_entiry, wqe_size = %u.\n", wqe_size);
    __gm__ uint8_t* sq_addr = (__gm__ uint8_t*)(sq_base_addr + (sq_head & (sq_depth - 1)) * wqe_size);
    GlobalTensor<uint8_t> sq_global;
    sq_global.SetGlobalBuffer(sq_addr);

    __ubuf__ roce_wqe_entry* wqe_ptr = (__ubuf__ roce_wqe_entry*)(wqe_ub_.GetPhyAddr());
    fill_ctrl_seg(wqe_ptr, sq_head, sq_depth);
    uint32_t r_key = chnl_ptr->remote_buffer_addr[remote_idx].buffer_info.rma.protection_info_value.mem_info.roce.rkey;
    uint32_t l_key = chnl_ptr->local_buffer_addr[local_idx].buffer_info.rma.protection_info_value.mem_info.roce.lkey;
    fill_task_seg(wqe_ptr, dst, len, op_type, r_key, l_key);
    fill_data_seg(wqe_ptr, src, len, l_key);
    __gm__ uint8_t* sq_addr_next = (__gm__ uint8_t*)(sq_base_addr + ((sq_head + 1) & (sq_depth - 1)) * wqe_size);
    write_invalid_wqebb(sq_addr_next, (sq_head + 1), sq_depth);

    DataCopy(sq_global, wqe_ub_, sizeof(roce_wqe_entry));
    sync_action<HardEvent::MTE3_S>();

    KERNEL_LOG(KERNEL_INFO, "hcomm make_wqe: set wqe to qp ok.\n");
    return asccomm_success;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::post_send(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t op_type)
{
    __gm__ channel_entity* chnl_ptr = (__gm__ channel_entity*)(channel);
    if (chnl_ptr == nullptr || chnl_ptr->sq_num == 0 || chnl_ptr->cq_num == 0 || chnl_ptr->sq_context_addr == nullptr ||
        chnl_ptr->cq_context_addr == nullptr || dst == nullptr || src == nullptr) {
        KERNEL_LOG(
            KERNEL_INFO, "hcomm post_send: chnl_ptr = %llu, sq_num = %u, cq_num = %u, dst = %llu, src = %llu.\n", chnl_ptr,
            chnl_ptr->sq_num, chnl_ptr->cq_num, dst, src);
        return asccomm_failed;
    }
    uint32_t sq_depth = chnl_ptr->sq_context_addr[0].context_info.roce_sq.depth;
    uint32_t cq_depth = chnl_ptr->cq_context_addr[0].context_info.roce_cq.cq_depth;
    auto sq_pi_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr[0].context_info.roce_sq.head_addr);
    uint32_t sq_head = static_cast<uint32_t>(ld_dev(sq_pi_addr, 0));
    auto sq_ci_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr[0].context_info.roce_sq.tail_addr);
    uint32_t sq_tail = static_cast<uint32_t>(ld_dev(sq_ci_addr, 0));
    KERNEL_LOG(
        KERNEL_INFO, "hcomm post_send: op_type = %u, sq_head = %u, sq_tail = %u, sq_depth = %u, cq_depth = %u.\n", op_type,
        sq_head, sq_tail, sq_depth, cq_depth);

    constexpr uint32_t poll_cq_threshold = 10;
    constexpr uint32_t num_cqe_per_poll_cq = 100;
    uint32_t min_depth = (sq_depth <= cq_depth) ? sq_depth : cq_depth;
    if ((sq_head + poll_cq_threshold) % min_depth == sq_tail % min_depth) {
        uint32_t idx = (sq_tail + num_cqe_per_poll_cq) > sq_head ? sq_head : (sq_tail + num_cqe_per_poll_cq);
        KERNEL_LOG(
            KERNEL_INFO, "hcomm post_send: RoCE SQ overflow sq_head=%u sq_tail=%u idx=%u min_depth=%u\n", sq_head, sq_tail,
            idx, min_depth);
        if (poll_cq(chnl_ptr, idx) != asccomm_success) {
            KERNEL_LOG(KERNEL_INFO, "hcomm post_send: RoCE SQ overflow, poll_cq failed.\n");
            return asccomm_failed;
        }
    }

    if (make_wqe(chnl_ptr, dst, src, len, op_type, sq_head, sq_depth) != asccomm_success) {
        KERNEL_LOG(KERNEL_INFO, "hcomm post_send: make_wqe failed.\n");
        return asccomm_failed;
    }

    sq_head++;
    st_dev(sq_head, sq_pi_addr, 0);
    KERNEL_LOG(KERNEL_INFO, "hcomm post_send: update PI to GM sq_head = %u\n", sq_head);

    if constexpr (commit) {
        knock_door_bell(chnl_ptr, sq_head);
        KERNEL_LOG(KERNEL_INFO, "hcomm post_send: commit ok.\n");
    }
    return asccomm_success;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return post_send<commit, commit_pipe, req_pipe>(
        channel, dst, src, len, static_cast<uint32_t>(asccomm_roce_op_type::write));
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::read_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return post_send<commit, commit_pipe, req_pipe>(
        channel, src, dst, len, static_cast<uint32_t>(asccomm_roce_op_type::read));
}

template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_value_nbi(channel_handle channel, GM_ADDR dst, t value)
{
    (void)commit;
    (void)commit_pipe;
    (void)req_pipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)value;
    KERNEL_LOG(KERNEL_ERROR, "hcomm ROCE write_value_nbi is not supported.");
    return asccomm_failed;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_with_notify_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
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
    KERNEL_LOG(KERNEL_ERROR, "hcomm ROCE write_with_notify_nbi is not supported.");
    return asccomm_failed;
}

__aicore__ inline uint64_t asccomm_impl<comm_protocol_roce>::get_db_value(uint32_t qpn, uint8_t mtu_shift)
{
    roce_db_entry db_entry;
    db_entry.dw0.value = 0;
    db_entry.dw0.bs.c = 0;
    db_entry.dw0.bs.r = 0;
    db_entry.dw0.bs.ctx_size = 1;
    db_entry.dw0.bs.qpn = qpn;
    db_entry.dw0.bs.sub_type = 0;
    db_entry.dw0.bs.resv = 0;
    db_entry.dw0.bs.pi = 0;
    db_entry.dw0.bs.sgid_idx = roce_1825_sq_db_sgit_idx;
    db_entry.dw0.bs.type = roce_1825_sq_db_type;
    db_entry.dw0.bs.mtu_shift = mtu_shift;
    db_entry.dw0.bs.cos = roce_1825_sq_db_cos;
    db_entry.dw0.bs.xrc_vld = 0;
    return db_entry.dw0.value;
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::knock_door_bell(__gm__ channel_entity* chnl_ptr, uint32_t sq_head)
{
    __gm__ uint32_t* db_sw_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr->context_info.roce_sq.db_sw_va);
    st_dev(hto_nl(sq_head), db_sw_addr, 0);
    KERNEL_LOG(KERNEL_INFO, "hcomm knock_door_bell: write sw db ok, sw_db_val = %u\n", sq_head);

    uint64_t db_value =
        get_db_value(chnl_ptr->sq_context_addr->context_info.roce_sq.qpn, chnl_ptr->sq_context_addr->context_info.roce_sq.mtu_shift);
    KERNEL_LOG(KERNEL_INFO, "hcomm knock_door_bell: db_value = %llu\n", db_value);
    __gm__ uint64_t* db_hw_addr = reinterpret_cast<__gm__ uint64_t*>(chnl_ptr->sq_context_addr->context_info.roce_sq.db_hw_va);
    uint64_t db_final_val =
        db_value | ((((uint64_t)(sq_head) >> roce_1825_sq_db_pi_high_shift) & 0xffULL) << roce_1825_sq_db_pi_field_shift);
    st_dev(db_final_val, db_hw_addr, 0);
    KERNEL_LOG(KERNEL_INFO, "hcomm knock_door_bell: write hw db ok, hw_db_val = %llu\n", db_final_val);
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::commit(channel_handle channel)
{
    KERNEL_LOG(KERNEL_INFO, "hcomm commit: Enter\n");
    __gm__ channel_entity* chnl_ptr = (__gm__ channel_entity*)(channel);
    auto sq_pi_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr->context_info.roce_sq.head_addr);
    uint32_t sq_head = static_cast<uint32_t>(ld_dev(sq_pi_addr, 0));
    KERNEL_LOG(KERNEL_INFO, "hcomm commit: sq_head = %u\n", sq_head);

    knock_door_bell(chnl_ptr, sq_head);
    KERNEL_LOG(KERNEL_INFO, "hcomm commit: Exit ok.\n");
    return asccomm_success;
}

__aicore__ inline bool asccomm_impl<comm_protocol_roce>::check_cqe_owner(
    __ubuf__ roce_cqe_entry* cqe_ptr, uint32_t cq_tail, uint32_t depth)
{
    uint32_t cur_owner = ((cqe_ptr->owner_id_qpn & (1U << roce_1825_cqe_owner_shift)) != 0);
    uint32_t expect_owner = (uint32_t)(((cq_tail & depth) == 0));
    return (expect_owner ^ cur_owner) != 0;
}

__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::poll_cq(__gm__ channel_entity* chnl_ptr, uint32_t expect_idx)
{
    if (expect_idx == 0) {
        return asccomm_success;
    }
    auto cq_context_info = chnl_ptr->cq_context_addr[0].context_info;
    uint32_t cqe_size = cq_context_info.roce_cq.cqe_size;
    uint32_t cq_depth = cq_context_info.roce_cq.cq_depth;
    auto cq_ci_addr = reinterpret_cast<__gm__ uint32_t*>(cq_context_info.roce_cq.tail_addr);
    uint32_t cq_tail = static_cast<uint32_t>(ld_dev(cq_ci_addr, 0));
    KERNEL_LOG(
        KERNEL_INFO, "hcomm poll_cq: cqe_size = %u cq_depth = %u cq_tail= %u expect_idx = %u\n", cqe_size, cq_depth, cq_tail,
        expect_idx);
    __ubuf__ roce_cqe_entry* cqe_ptr = (__ubuf__ roce_cqe_entry*)(cqe_ub_.GetPhyAddr());
    __gm__ uint8_t* cq_base_buf = (__gm__ uint8_t*)(cq_context_info.roce_cq.cq_va);
    AscendC::GlobalTensor<uint8_t> cqe_global_tensor;

    while (cq_tail < expect_idx) {
        __gm__ uint8_t* cqe_addr = (__gm__ uint8_t*)(cq_base_buf + cqe_size * (cq_tail & (cq_depth - 1)));
        cqe_global_tensor.SetGlobalBuffer(cqe_addr);
        uint32_t loop = 0;
        uint32_t cqe_type = roce_1825_cqe_optype_invalid;
        for (; loop < asccomm_pollcq_max_retry_times; loop++) {
            DataCopy(cqe_ub_, cqe_global_tensor, sizeof(roce_cqe_entry));
            sync_action<HardEvent::MTE2_S>();
#if defined(UT_TEST)
            cq_tail = expect_idx - 1;
            break;
#else
            cqe_type = (cqe_ptr->op_sr_wqebb >> roce_1825_cqe_opcode_shift) & roce_1825_cqe_opcode_mask;
            KERNEL_LOG(KERNEL_INFO, "hcomm poll_cq: cqe_type = %u\n", cqe_type);
            if (cqe_type != roce_1825_cqe_optype_invalid && check_cqe_owner(cqe_ptr, cq_tail, cq_depth)) {
                break;
            }
#endif
        }
        if (loop >= asccomm_pollcq_max_retry_times) {
            KERNEL_LOG(KERNEL_INFO, "hcomm poll_cq: failed, overtime and exit.\n");
            return asccomm_failed;
        }
        if (cqe_type == roce_1825_cqe_optype_error) {
            KERNEL_LOG(
                KERNEL_INFO, "hcomm poll_cq: failed, syndrome = 0x%x, qpn = %u, cq_tail = %u\n", cqe_ptr->syndrome,
                cqe_ptr->owner_id_qpn & 0xfffffU, cq_tail);
            return asccomm_failed;
        }
        cq_tail += 1;
        KERNEL_LOG(KERNEL_INFO, "hcomm poll_cq: cq_tail = %u\n", cq_tail);
    }
    st_dev(cq_tail, cq_ci_addr, 0);
    auto sq_ci_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr[0].context_info.roce_sq.tail_addr);
    st_dev(cq_tail, sq_ci_addr, 0);
    __gm__ uint32_t* db_sw_addr = reinterpret_cast<__gm__ uint32_t*>(cq_context_info.roce_cq.db_sw_va);
    st_dev(cq_tail & roce_1825_cqe_update_ci_mask, db_sw_addr, 0);
    KERNEL_LOG(KERNEL_INFO, "hcomm poll_cq: knock cq doorbell ok, cq_tail = %u, sq_tail = %u\n", cq_tail, cq_tail);
    return asccomm_success;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::drain(channel_handle channel)
{
    KERNEL_LOG(KERNEL_INFO, "hcomm drain: Enter\n");
    __gm__ channel_entity* chnl_ptr = (__gm__ channel_entity*)(channel);
    auto sq_pi_addr = reinterpret_cast<__gm__ uint32_t*>(chnl_ptr->sq_context_addr[0].context_info.roce_sq.head_addr);
    uint32_t sq_head = static_cast<uint32_t>(ld_dev(sq_pi_addr, 0));
    KERNEL_LOG(KERNEL_INFO, "hcomm drain: sq_head = %u\n", sq_head);
    if (poll_cq(chnl_ptr, sq_head) != asccomm_success) {
        KERNEL_LOG(KERNEL_INFO, "hcomm drain: poll_cq failed.\n");
        return asccomm_failed;
    }
    KERNEL_LOG(KERNEL_INFO, "hcomm drain: Exit ok.\n");
    return asccomm_success;
}
} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H
#endif
