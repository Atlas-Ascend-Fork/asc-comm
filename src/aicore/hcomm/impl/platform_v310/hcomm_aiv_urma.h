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
 * \file asccomm_aiv_urma.h
 * \brief asccomm AIV URMA implementation for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include hcomm/hcomm.h instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_H

#include "hcomm_aiv_urma_def.h"

#include "../../common/hcomm_inner_def.h"
#include "../../common/hcomm_utils.h"

typedef AscendC::asccomm_urma_sqe_ctx asccomm_urma_sqe_ctx;
typedef AscendC::asccomm_urma_sge_ctx asccomm_urma_sge_ctx;
typedef AscendC::asccomm_urma_notify_ctx asccomm_urma_notify_ctx;
typedef AscendC::asccomm_urma_jfc_cqe_ctx asccomm_urma_jfc_cqe_ctx;

namespace AscendC {
__aicore__ inline void asccomm_urma_fill_notify_ctx(
    __ubuf__ asccomm_urma_notify_ctx* notify_ctx, const reged_buffer_entity& remote_mem_info, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    uint64_t notify_addr_value = reinterpret_cast<uint64_t>(notify_addr);
    notify_ctx->notify_token_id = remote_mem_info.buffer_info.rma.protection_info_value.mem_info.ub.token_id & 0xFFFFFU;
    notify_ctx->notify_token_value = remote_mem_info.buffer_info.rma.protection_info_value.mem_info.ub.token_value;
    notify_ctx->notify_addr_l = notify_addr_value & 0xFFFFFFFFU;
    notify_ctx->notify_addr_h = (notify_addr_value >> 32) & 0xFFFFFFFFU;
    notify_ctx->notify_data_l = notify_val & 0xFFFFFFFFU;
    notify_ctx->notify_data_h = (notify_val >> 32) & 0xFFFFFFFFU;
}

template <asccomm_urma_op_code op_code, auto const& config, typename t>
__aicore__ inline void asccomm_urma_fill_sqe_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __gm__ uint8_t* remote_addr, const sq_context& sq_ctx,
    const reged_buffer_entity& remote_mem_info, uint32_t cur_head, GM_ADDR notify_addr = nullptr,
    const udma_params<t>& params = udma_params<t>{})
{
    sqe_ctx->opcode =
        static_cast<uint32_t>(op_code == asccomm_urma_op_code::write_with_reduce ? asccomm_urma_op_code::write : op_code);
    sqe_ctx->flag = (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
                   ((config.cqe & 0x1U) << 5U) | ((config.inline_en & 0x1U) << 6U) |
                   (op_code == asccomm_urma_op_code::write_with_reduce ? asccomm_urma_udf_flag : 0U);
    sqe_ctx->nf = 0;
    sqe_ctx->token_en = 1;
    sqe_ctx->rmt_jetty_type = 1;
    uint32_t base_block_count = sq_ctx.context_info.ub_jfs.sq_depth;
    sqe_ctx->owner = (cur_head & base_block_count) == 0 ? 1 : 0;
    sqe_ctx->target_hint = 0;
    if constexpr (config.inline_en == 1) {
        sqe_ctx->inline_msg_len = sizeof(t);
        sqe_ctx->sge_num = 0;
        __ubuf__ t* inline_addr = (__ubuf__ t*)((__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx));
        *inline_addr = params.value;
    } else {
        sqe_ctx->inline_msg_len = 0;
        sqe_ctx->sge_num = 1;
    }
    sqe_ctx->tp_id = sq_ctx.context_info.ub_jfs.tp_id;
    sqe_ctx->rmt_jetty_or_seg_id = remote_mem_info.buffer_info.rma.protection_info_value.mem_info.ub.token_id;
    sqe_ctx->rmt_token_value = remote_mem_info.buffer_info.rma.protection_info_value.mem_info.ub.token_value;
    uint64_t remote_addr_value = reinterpret_cast<uint64_t>(remote_addr);
    sqe_ctx->rmt_addr_l_or_token_id = remote_addr_value & 0xFFFFFFFFU;
    sqe_ctx->rmt_addr_h_or_token_value = (remote_addr_value >> 32) & 0xFFFFFFFFU;
    auto rmt_eid = reinterpret_cast<const uint64_t*>(sq_ctx.context_info.ub_jfs.remote_eid);
    sqe_ctx->rmt_eid_l = rmt_eid[0];
    sqe_ctx->rmt_eid_h = rmt_eid[1];
    sqe_ctx->udf_type = 0;
    sqe_ctx->reduce_data_type = 0;
    sqe_ctx->reduce_opcode = 0;
    if constexpr (op_code == asccomm_urma_op_code::write_with_notify) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx =
            (__ubuf__ asccomm_urma_notify_ctx*)((__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx));
        asccomm_urma_fill_notify_ctx(notify_ctx, remote_mem_info, notify_addr, params.value);
    } else if constexpr (op_code == asccomm_urma_op_code::write_with_reduce) {
        sqe_ctx->reduce_data_type = params.reduce_data_type;
        sqe_ctx->reduce_opcode = params.reduce_opcode;
    }
}

template <asccomm_urma_op_code op_code, typename t>
__aicore__ inline void asccomm_urma_fill_sge_ctx(
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx, uint64_t message_len, __gm__ uint8_t* local_addr, udma_params<t> params)
{
    sge_ctx->len = static_cast<uint32_t>(message_len);
    sge_ctx->va = reinterpret_cast<uint64_t>(local_addr);
    if constexpr (op_code == asccomm_urma_op_code::faa) {
        __ubuf__ t* add_data_addr = (__ubuf__ t*)((__ubuf__ uint8_t*)sge_ctx + sizeof(asccomm_urma_sge_ctx));
        *add_data_addr = params.value;
    } else if constexpr (op_code == asccomm_urma_op_code::cas) {
        __ubuf__ t* swap_data_addr = (__ubuf__ t*)((__ubuf__ uint8_t*)sge_ctx + sizeof(asccomm_urma_sge_ctx));
        *swap_data_addr = params.value;
        __ubuf__ t* cmp_data_addr = (__ubuf__ t*)((__ubuf__ uint8_t*)swap_data_addr + sizeof(t));
        *cmp_data_addr = params.cond;
    }
}

__aicore__ inline void asccomm_urma_dump_amo_ctx(__ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, uint32_t atomic_len)
{
    if (sqe_ctx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    auto opcode = sqe_ctx->opcode;
    if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::faa)) {
        __ubuf__ uint8_t* amo_data_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx);
        uint64_t add_value = (atomic_len == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amo_data_addr) :
                                                              *(__ubuf__ uint64_t*)amo_data_addr;
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA SGE: add_value:0x%llx \n", add_value);
    } else if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::cas)) {
        __ubuf__ uint8_t* amo_data_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx);
        uint64_t swap_value = (atomic_len == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amo_data_addr) :
                                                               *(__ubuf__ uint64_t*)amo_data_addr;
        uint64_t cond_value = (atomic_len == sizeof(uint32_t)) ?
                                 static_cast<uint64_t>(*(__ubuf__ uint32_t*)(amo_data_addr + atomic_len)) :
                                 *(__ubuf__ uint64_t*)(amo_data_addr + atomic_len);
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA SGE: cond_value:0x%llx, swap_value:0x%llx \n", cond_value, swap_value);
    }
}

__aicore__ inline void asccomm_urma_dump_sge_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __ubuf__ uint8_t* sge_addr, uint32_t atomic_len)
{
    if (sqe_ctx == nullptr || sge_addr == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)sge_addr;
    for (uint32_t i = 0; i < sqe_ctx->sge_num; i++) {
        auto sge_len = sge_ctx->len;
        auto sge_rmt_addr = sge_ctx->va;
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA SGE: sge idx: %d, va: %p sge_len: %d \n", i, sge_rmt_addr, sge_len);
        sge_ctx++;
    }
    asccomm_urma_dump_amo_ctx(sqe_ctx, atomic_len);
}

__aicore__ inline void asccomm_urma_dump_notify_ctx(__ubuf__ asccomm_urma_notify_ctx* notify_ctx)
{
    auto notify_token_id = notify_ctx->notify_token_id;
    auto notify_token_value = notify_ctx->notify_token_value;
    auto notify_addr_l = notify_ctx->notify_addr_l;
    auto notify_addr_h = notify_ctx->notify_addr_h;
    auto notify_data_l = notify_ctx->notify_data_l;
    auto notify_data_h = notify_ctx->notify_data_h;
    KERNEL_LOG(
        KERNEL_INFO,
        "hcomm URMA WQE: notify_token_id: %x notify_token_value: %x notify_addr_l: %x notify_addr_h: %x notify_data_l: %x "
        "notify_data_h: %x \n",
        notify_token_id, notify_token_value, notify_addr_l, notify_addr_h, notify_data_l, notify_data_h);
}

__aicore__ inline void asccomm_urma_dump_wqe_ctx(__ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, uint32_t atomic_len)
{
    if (sqe_ctx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    auto sqe_bb_idx = sqe_ctx->sqe_bb_idx;
    auto flag = sqe_ctx->flag;
    auto rsv0 = sqe_ctx->rsv0;
    auto nf = sqe_ctx->nf;
    auto token_en = sqe_ctx->token_en;
    auto rmt_jetty_type = sqe_ctx->rmt_jetty_type;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA WQE: sqe_bb_idx: %x flag: %x rsv0: %x nf: %x token_en: %x rmt_jetty_type: %x \n",
        sqe_bb_idx, flag, rsv0, nf, token_en, rmt_jetty_type);
    auto owner = sqe_ctx->owner;
    auto target_hint = sqe_ctx->target_hint;
    auto opcode = sqe_ctx->opcode;
    auto rsv1 = sqe_ctx->rsv1;
    auto inline_msg_len = sqe_ctx->inline_msg_len;
    auto tp_id = sqe_ctx->tp_id;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA WQE: owner: %x target_hint: %x opcode: %x rsv1: %x inline_msg_len: %x tp_id: %x \n",
        owner, target_hint, opcode, rsv1, inline_msg_len, tp_id);
    auto sge_num = sqe_ctx->sge_num;
    auto rmt_jetty_or_seg_id = sqe_ctx->rmt_jetty_or_seg_id;
    auto rsv2 = sqe_ctx->rsv2;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA WQE: sge_num: %x rmt_jetty_or_seg_id: %x rsv2: %x \n", sge_num, rmt_jetty_or_seg_id, rsv2);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA WQE: rmt_eid: %x, %x \n", sqe_ctx->rmt_eid_l, sqe_ctx->rmt_eid_h);
    auto rmt_token_value = sqe_ctx->rmt_token_value;
    auto udf_type = sqe_ctx->udf_type;
    auto reduce_data_type = sqe_ctx->reduce_data_type;
    auto reduce_opcode = sqe_ctx->reduce_opcode;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA WQE: rmt_token_value: %x udf_type: %x reduce_data_type: %x reduce_opcode: %x \n",
        rmt_token_value, udf_type, reduce_data_type, reduce_opcode);
    auto rmt_addr_l_or_token_id = sqe_ctx->rmt_addr_l_or_token_id;
    auto rmt_addr_h_or_token_value = sqe_ctx->rmt_addr_h_or_token_value;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA WQE: rmt_addr_l_or_token_id: %x rmt_addr_h_or_token_value: %x \n", rmt_addr_l_or_token_id,
        rmt_addr_h_or_token_value);
    __ubuf__ uint8_t* sge_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx);
    if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::write_with_notify)) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx = (__ubuf__ asccomm_urma_notify_ctx*)sge_addr;
        asccomm_urma_dump_notify_ctx(notify_ctx);
        sge_addr += sizeof(asccomm_urma_notify_ctx);
    }
    asccomm_urma_dump_sge_ctx(sqe_ctx, sge_addr, atomic_len);
}

__aicore__ inline void asccomm_urma_dump_cqe_ctx(__ubuf__ asccomm_urma_jfc_cqe_ctx* cqe_ctx)
{
    if (cqe_ctx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: nullptr pointer \n");
        return;
    }
    uint32_t s_r = cqe_ctx->s_r;
    uint32_t is_jetty = cqe_ctx->is_jetty;
    uint32_t owner = cqe_ctx->owner;
    uint32_t inline_en = cqe_ctx->inline_en;
    uint32_t opcode = cqe_ctx->opcode;
    uint32_t fd = cqe_ctx->fd;
    uint32_t substatus = cqe_ctx->substatus;
    uint32_t status = cqe_ctx->status;
    uint32_t entry_idx = cqe_ctx->entry_idx;
    uint32_t local_num_l = cqe_ctx->local_num_l;
    uint32_t local_num_h = cqe_ctx->local_num_h;
    uint32_t rmt_idx = cqe_ctx->rmt_idx;
    uint32_t tpn = cqe_ctx->tpn;
    KERNEL_LOG(
        KERNEL_INFO,
        "hcomm URMA CQE: DW0 - s_r: %d, is_jetty: %d, owner: %d, inline_en: %d, "
        "opcode: %d, fd: %d, substatus: %d, status: %d \n",
        s_r, is_jetty, owner, inline_en, opcode, fd, substatus, status);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW1 - entry_idx: %d, local_num_l: %d \n", entry_idx, local_num_l);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW2 - local_num_h: %d, rmt_idx: %d \n", local_num_h, rmt_idx);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW3 - tpn: %d \n", tpn);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW4 - byte_cnt: %d \n", cqe_ctx->byte_cnt);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW5-DW6 - user_data: 0x%x%x \n", cqe_ctx->user_data_h, cqe_ctx->user_data_l);
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA CQE: DW7-DW10 - rmt_eid: [0x%x, 0x%x, 0x%x, 0x%x] \n", cqe_ctx->rmt_eid[0],
        cqe_ctx->rmt_eid[1], cqe_ctx->rmt_eid[2], cqe_ctx->rmt_eid[3]);
    KERNEL_LOG(KERNEL_INFO, "hcomm URMA CQE: DW11-DW12 - data: 0x%x%x \n", cqe_ctx->data_h, cqe_ctx->data_l);
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA CQE: DW13-DW15 - inline_data: [0x%x, 0x%x, 0x%x] \n", cqe_ctx->inline_data[0],
        cqe_ctx->inline_data[1], cqe_ctx->inline_data[2]);
}

__aicore__ inline asccomm_impl<comm_protocol_ubc_ctp>::asccomm_impl() {}

__aicore__ inline asccomm_impl<comm_protocol_ubc_ctp>::~asccomm_impl() {}

__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    if (len < asccomm_urma_tmp_buf_size) {
        return asccomm_failed;
    }

    __ubuf__ uint8_t* aligned_buff = (__ubuf__ uint8_t*)align_addr_to_32_bytes(buff);
    TBuffAddr addr;
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(aligned_buff));
    addr.dataLen = len;
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t*>(aligned_buff);
#endif
    wqe_item_.SetAddr(addr);
    cqe_item_ = wqe_item_[asccomm_urma_wqe_u32_num];
    return asccomm_success;
}

template <typename t>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::init(const LocalTensor<t>& buff, uint32_t len)
{
    if (len < asccomm_urma_tmp_buf_size || len > buff.GetSize()) {
        return asccomm_failed;
    }

    wqe_item_ = buff.template ReinterpretCast<uint32_t>();
    cqe_item_ = wqe_item_[asccomm_urma_wqe_u32_num];
    return asccomm_success;
}

__aicore__ inline void asccomm_impl<comm_protocol_ubc_ctp>::poll_cq_when_sq_overflow(
    channel_handle channel, const sq_context& sq_ctx, const cq_context& cq_ctx, uint32_t sq_head, uint32_t cqe_cnt)
{
    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    uint32_t cq_tail = channel_entity_ptr->cq_tail;
    constexpr uint32_t poll_cq_threshold = 10;
    constexpr uint32_t num_cqe_per_poll_cq = 100;
    uint32_t cq_depth = cq_ctx.context_info.ub_jfc.cq_depth;
    uint32_t sq_depth = sq_ctx.context_info.ub_jfs.sq_depth;
    if ((cqe_cnt + poll_cq_threshold) % cq_depth == cq_tail % cq_depth ||
        (sq_head % sq_depth) + poll_cq_threshold >= sq_depth) {
        uint32_t idx = (cq_tail + num_cqe_per_poll_cq) > cqe_cnt ? cqe_cnt : cq_tail + num_cqe_per_poll_cq;
        KERNEL_LOG(
            KERNEL_INFO, "hcomm URMA queue overflow sq_head=%u cqe_cnt=%u cq_tail=%u idx=%u sq_depth=%u cq_depth=%u \n",
            sq_head, cqe_cnt, cq_tail, idx, sq_depth, cq_depth);
        (void)poll_cq(channel, idx);
    }
}

__aicore__ inline void asccomm_impl<comm_protocol_ubc_ctp>::commit_impl(
    channel_handle channel, const sq_context& sq_ctx, uint32_t sq_head, uint32_t cqe_cnt)
{
    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    auto cq_ctx = channel_entity_ptr->cq_context_addr[asccomm_urma_default_qp_idx];
    uint32_t cq_depth = cq_ctx.context_info.ub_jfc.cq_depth;
    uint32_t cq_tail = channel_entity_ptr->cq_tail;
    uint32_t cq_left = (cqe_cnt - cq_tail) % cq_depth;
    if (cq_left >= 0) {
        Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
        st_dev(sq_head, reinterpret_cast<__gm__ uint32_t*>(sq_ctx.context_info.ub_jfs.db_va), 0);
        Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);
    } else {
        auto commit_cnt = sq_head - cq_left;
        Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
        st_dev(commit_cnt, reinterpret_cast<__gm__ uint32_t*>(sq_ctx.context_info.ub_jfs.db_va), 0);
        Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);
        poll_cq(channel, commit_cnt);
        Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
        st_dev(sq_head, reinterpret_cast<__gm__ uint32_t*>(sq_ctx.context_info.ub_jfs.db_va), 0);
        Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);
    }
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, asccomm_urma_op_code op_code, auto const& config, typename t>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::post_send(
    channel_handle channel, GM_ADDR remote_addr, GM_ADDR local_addr, uint64_t len, GM_ADDR notify_addr,
    const udma_params<t>& params)
{
    (void)commit_pipe;
    (void)req_pipe;

    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    int32_t remote_idx =
        asccomm_find_buffer_idx(channel_entity_ptr->remote_buffer_addr, channel_entity_ptr->remote_buffer_num, remote_addr, len);
    if (remote_idx == asccomm_failed) {
        KERNEL_LOG(KERNEL_ERROR, "hcomm URMA post_send failed with invalid remote buffer \n");
        return asccomm_failed;
    }

    auto sq_ctx = channel_entity_ptr->sq_context_addr[asccomm_urma_default_qp_idx];
    uint32_t cur_head = channel_entity_ptr->sq_head;
    uint32_t cqe_cnt = channel_entity_ptr->cq_head;
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA post_send resolved remote_idx=%d cur_head=%u sq_depth=%u \n", remote_idx, cur_head,
        sq_ctx.context_info.ub_jfs.sq_depth);

    // write SQE
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = (__ubuf__ asccomm_urma_sqe_ctx*)wqe_item_.GetPhyAddr();
    auto remote_mem_info = channel_entity_ptr->remote_buffer_addr[remote_idx];
    Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
    asccomm_urma_fill_sqe_ctx<op_code, config>(
        sqe_ctx, (__gm__ uint8_t*)remote_addr, sq_ctx, remote_mem_info, cur_head, notify_addr, params);

    if constexpr (config.inline_en == 0) {
        // write SGE
        __ubuf__ uint8_t* sge_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx);
        if constexpr (op_code == asccomm_urma_op_code::write_with_notify) {
            sge_addr += sizeof(asccomm_urma_notify_ctx);
        }
        __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)sge_addr;
        asccomm_urma_fill_sge_ctx<op_code>(sge_ctx, len, (__gm__ uint8_t*)local_addr, params);
    }
    Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);

    // SQE & SGE cache flush
    uint64_t sq_base_addr = sq_ctx.context_info.ub_jfs.sq_va;
    uint32_t wqe_size = sq_ctx.context_info.ub_jfs.wqe_size;
    uint32_t base_block_count = sq_ctx.context_info.ub_jfs.sq_depth;
    __gm__ uint8_t* sqe_addr = (__gm__ uint8_t*)(sq_base_addr + wqe_size * (cur_head % base_block_count));
    AscendC::GlobalTensor<uint32_t> sqe_global;
    sqe_global.SetGlobalBuffer((__gm__ uint32_t*)sqe_addr);

    constexpr uint32_t wqe_bb_cnt = (op_code == asccomm_urma_op_code::write_with_notify || op_code == asccomm_urma_op_code::faa ||
                                   op_code == asccomm_urma_op_code::cas) ?
                                      2U :
                                      1U;
    Mutex::Lock<PIPE_MTE3>(asccomm_urma_mutex_id);
    if constexpr (wqe_bb_cnt == 2) {
        if (unlikely((cur_head % base_block_count) == base_block_count - 1)) {
            AscendC::GlobalTensor<uint32_t> sqe_base_global;
            sqe_base_global.SetGlobalBuffer((__gm__ uint32_t*)sq_base_addr);

            uint32_t wqe_size_per_u32 = wqe_size / sizeof(uint32_t);
            DataCopy(sqe_global, wqe_item_, wqe_size_per_u32);
            DataCopy(sqe_base_global, wqe_item_[wqe_size_per_u32], wqe_size_per_u32);
        } else {
            DataCopy(sqe_global, wqe_item_, wqe_size * wqe_bb_cnt / sizeof(uint32_t));
        }
    } else {
        DataCopy(sqe_global, wqe_item_, wqe_size * wqe_bb_cnt / sizeof(uint32_t));
    }
    Mutex::Unlock<PIPE_MTE3>(asccomm_urma_mutex_id);

    if constexpr (config.cqe != 0) {
        cqe_cnt++;
        channel_entity_ptr->cq_head = cqe_cnt;
    }
    cur_head += wqe_bb_cnt;
    channel_entity_ptr->sq_head = cur_head;

    if constexpr (commit) {
        commit_impl(channel, sq_ctx, cur_head, cqe_cnt);
    }
    asccomm_urma_dump_wqe_ctx(sqe_ctx, sizeof(t));
    return asccomm_success;
}

__aicore__ inline uint32_t asccomm_impl<comm_protocol_ubc_ctp>::poll_cq(channel_handle channel, uint32_t expect_idx)
{
    if (expect_idx == 0) {
        return asccomm_success;
    }
    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    auto cq_ctx = channel_entity_ptr->cq_context_addr[asccomm_urma_default_qp_idx];
    uint32_t cur_tail = channel_entity_ptr->cq_tail;

    uint64_t cq_base_addr = cq_ctx.context_info.ub_jfc.scq_va;
    uint32_t cqe_size = cq_ctx.context_info.ub_jfc.cqe_size;
    uint32_t cq_depth = cq_ctx.context_info.ub_jfc.cq_depth;
    __ubuf__ asccomm_urma_jfc_cqe_ctx* cqe_ub = (__ubuf__ asccomm_urma_jfc_cqe_ctx*)cqe_item_.GetPhyAddr();
    KERNEL_LOG(
        KERNEL_INFO, "hcomm URMA poll_cq enter expect_idx=%u cur_tail=%u cq_depth=%u \n", expect_idx, cur_tail, cq_depth);
#if defined(UT_TEST)
    cur_tail = expect_idx;
#else
    while (cur_tail != expect_idx) {
        __gm__ uint8_t* cqe_addr = (__gm__ uint8_t*)(cq_base_addr + cqe_size * (cur_tail & (cq_depth - 1)));
        AscendC::GlobalTensor<uint32_t> cqe_global;
        cqe_global.SetGlobalBuffer((__gm__ uint32_t*)cqe_addr);
        Mutex::Lock<PIPE_MTE2>(asccomm_urma_mutex_id);
        DataCopy(cqe_item_, cqe_global, cqe_size / sizeof(uint32_t));
        Mutex::Unlock<PIPE_MTE2>(asccomm_urma_mutex_id);
        Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
        bool valid_owner = (cur_tail / cq_depth) & 1;
        uint32_t times = 0;
        uint32_t ret = asccomm_success;
        while ((valid_owner ^ cqe_ub->owner) == 0 && times < asccomm_urma_max_retry_times) {
            Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);
            Mutex::Lock<PIPE_MTE2>(asccomm_urma_mutex_id);
            DataCopy(cqe_item_, cqe_global, cqe_size / sizeof(uint32_t));
            Mutex::Unlock<PIPE_MTE2>(asccomm_urma_mutex_id);
            Mutex::Lock<PIPE_S>(asccomm_urma_mutex_id);
            times++;
        }
        if (times >= asccomm_urma_max_retry_times) {
            KERNEL_LOG(KERNEL_ERROR, "hcomm URMA Poll CQ timeout cur_tail=%u expect_idx=%u \n", cur_tail, expect_idx);
            asccomm_urma_dump_cqe_ctx(cqe_ub);
            ret = 0xFFU;
        } else {
            // check CQE status
            uint8_t status = cqe_ub->status & 0xFFU;
            uint8_t sub_status = cqe_ub->substatus & 0xFFU;
            constexpr uint8_t status_shift = 8;
            if (status != 0 || sub_status != 0) {
                KERNEL_LOG(KERNEL_ERROR, "hcomm URMA CQE failed status=%u sub_status=%u \n", status, sub_status);
                asccomm_urma_dump_cqe_ctx(cqe_ub);
                ret = (status << status_shift) | sub_status;
            }
        }
        Mutex::Unlock<PIPE_S>(asccomm_urma_mutex_id);
        if (ret != asccomm_success) {
            return ret;
        }
        cur_tail++;
    }
#endif

    // update CQ tail
    channel_entity_ptr->cq_tail = cur_tail;

    // ring CQ doorbell
    st_dev(cur_tail & 0xFFFFFFU, (__gm__ uint32_t*)cq_ctx.context_info.ub_jfc.db_va, 0);

    return asccomm_success;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::write, config>(channel, dst, src, len);
}

template <typename t, asccomm_urma_reduce_op reduce_op, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_reduce_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    constexpr uint32_t reduce_data_type = asccomm_urma_reduce_data_type<t>;
    static_assert(
        reduce_data_type != asccomm_urma_invalid_reduce_data_type,
        "write_reduce_nbi only supports int8_t, int16_t, int32_t, uint32_t, half, float and bfloat16_t");
    static_assert(
        reduce_op == asccomm_urma_reduce_op::max || reduce_op == asccomm_urma_reduce_op::min || reduce_op == asccomm_urma_reduce_op::sum,
        "write_reduce_nbi only supports max, min and sum");
    static_assert(config.inline_en == 0, "write_reduce_nbi does not support inline data in WQE");
    udma_params<uint32_t> params{};
    params.reduce_data_type = reduce_data_type;
    params.reduce_opcode = static_cast<uint32_t>(reduce_op);
    uint64_t len = count * sizeof(t);
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::write_with_reduce, config>(
        channel, dst, src, len, nullptr, params);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::read_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::read, config>(channel, src, dst, len);
}

template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_value_nbi(channel_handle channel, GM_ADDR dst, t value)
{
    static_assert(config.inline_en == 1, "write_value_nbi requires inline data in WQE");
    udma_params<t> params{value, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::write, config>(
        channel, dst, nullptr, sizeof(t), nullptr, params);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_with_notify_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    udma_params<uint64_t> params{notify_val, 0, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::write_with_notify, config>(
        channel, dst, src, len, notify_addr, params);
}

template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::atomic_faa(
    channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t add_val)
{
    static_assert(
        std::is_same<t, int32_t>::value || std::is_same<t, uint32_t>::value || std::is_same<t, int64_t>::value ||
            std::is_same<t, uint64_t>::value,
        "atomic_faa only supports int32_t, uint32_t, int64_t, uint64_t");
    udma_params<t> params{add_val, 0, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::faa, config>(
        channel, dst, fetch_addr, sizeof(t), nullptr, params);
}

template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::atomic_cas(
    channel_handle channel, GM_ADDR dst, GM_ADDR fetch_addr, t compare_val, t swap_val)
{
    static_assert(
        std::is_same<t, int32_t>::value || std::is_same<t, uint32_t>::value || std::is_same<t, int64_t>::value ||
            std::is_same<t, uint64_t>::value,
        "atomic_cas only supports int32_t, uint32_t, int64_t, uint64_t");
    udma_params<t> params{swap_val, compare_val, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::cas, config>(
        channel, dst, fetch_addr, sizeof(t), nullptr, params);
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::commit(channel_handle channel)
{
    (void)pipe;
    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    auto sq_ctx = channel_entity_ptr->sq_context_addr[asccomm_urma_default_qp_idx];
    commit_impl(channel, sq_ctx, channel_entity_ptr->sq_head, channel_entity_ptr->cq_head);
    return asccomm_success;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::drain(channel_handle channel)
{
    (void)pipe;
    __gm__ channel_entity* channel_entity_ptr = (__gm__ channel_entity*)channel;
    uint32_t ret = poll_cq(channel, channel_entity_ptr->cq_head);
    if (ret != asccomm_success) {
        KERNEL_LOG(KERNEL_ERROR, "hcomm URMA drain by channel failed channel=%lu poll_ret=%u \n", channel, ret);
        return ret;
    }
    return asccomm_success;
}

} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H
#endif
