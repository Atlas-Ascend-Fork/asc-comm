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
 * \brief Hcomm AIV URMA implementation for V310
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include hcomm/hcomm.h instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_H

#include "asccomm_aiv_urma_def.h"

#include <c_api/vector_datamove/vector_datamove.h>

#include "../../common/asccomm_inner_def.h"
#include "../../common/asccomm_log.h"
#include "../../common/asccomm_utils.h"

typedef asc::asccomm_urma_sqe_ctx asccomm_urma_sqe_ctx;
typedef asc::asccomm_urma_sge_ctx asccomm_urma_sge_ctx;
typedef asc::asccomm_urma_notify_ctx asccomm_urma_notify_ctx;
typedef asc::asccomm_urma_jfc_cqe_ctx asccomm_urma_jfc_cqe_ctx;

namespace asc {
__aicore__ inline void asccomm_urma_fill_notify_ctx(
    __ubuf__ asccomm_urma_notify_ctx* notify_ctx, const RegedBufferEntity& remote_mem_info, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    uint64_t notify_addr_value = reinterpret_cast<uint64_t>(notify_addr);
    notify_ctx->notify_token_id = remote_mem_info.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU;
    notify_ctx->notify_token_value = remote_mem_info.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    notify_ctx->notify_addr_l = notify_addr_value & 0xFFFFFFFFU;
    notify_ctx->notify_addr_h = (notify_addr_value >> 32) & 0xFFFFFFFFU;
    notify_ctx->notify_data_l = notify_val & 0xFFFFFFFFU;
    notify_ctx->notify_data_h = (notify_val >> 32) & 0xFFFFFFFFU;
}

template <asccomm_urma_op_code op_code, auto const& config, typename T>
__aicore__ inline void asccomm_urma_fill_sqe_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __gm__ uint8_t* remote_addr, const SqContext& sq_ctx,
    const RegedBufferEntity& remote_mem_info, uint32_t cur_head, GM_ADDR notify_addr = nullptr,
    const udma_params<T>& params = udma_params<T>{});

template <asccomm_urma_op_code op_code, auto const& config, typename T>
__aicore__ inline void asccomm_urma_fill_sqe_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __gm__ uint8_t* remote_addr, const SqContext& sq_ctx,
    const RegedBufferEntity& remote_mem_info, uint32_t cur_head, uint32_t sge_num, GM_ADDR notify_addr = nullptr,
    const udma_params<T>& params = udma_params<T>{})
{
    sqe_ctx->opcode = static_cast<uint32_t>(
        op_code == asccomm_urma_op_code::WRITE_WITH_REDUCE ? asccomm_urma_op_code::WRITE : op_code);
    sqe_ctx->flag = (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
                    ((config.cqe & 0x1U) << 5U) | ((asccomm_config_inline_en(config) & 0x1U) << 6U) |
                    (op_code == asccomm_urma_op_code::WRITE_WITH_REDUCE ? ASCCOMM_URMA_UDF_FLAG : 0U);
    sqe_ctx->nf = 0;
    sqe_ctx->token_en = 1;
    sqe_ctx->rmt_jetty_type = 1;
    uint32_t base_block_count = sq_ctx.contextInfo.ubJfs.sqDepth;
    sqe_ctx->owner = (cur_head & base_block_count) == 0 ? 1 : 0;
    sqe_ctx->target_hint = 0;
    if constexpr (asccomm_config_inline_en(config) == 1) {
        sqe_ctx->inline_msg_len = sizeof(T);
        sqe_ctx->sge_num = 0;
        __ubuf__ T* inline_addr = (__ubuf__ T*)((__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx));
        *inline_addr = params.value;
    } else {
        sqe_ctx->inline_msg_len = 0;
        sqe_ctx->sge_num = sge_num;
    }
    sqe_ctx->tp_id = sq_ctx.contextInfo.ubJfs.tpID;
    sqe_ctx->rmt_jetty_or_seg_id = remote_mem_info.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    sqe_ctx->rmt_token_value = remote_mem_info.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    uint64_t remote_addr_value = reinterpret_cast<uint64_t>(remote_addr);
    sqe_ctx->rmt_addr_l_or_token_id = remote_addr_value & 0xFFFFFFFFU;
    sqe_ctx->rmt_addr_h_or_token_value = (remote_addr_value >> 32) & 0xFFFFFFFFU;
    auto rmt_eid = reinterpret_cast<const uint64_t*>(sq_ctx.contextInfo.ubJfs.remoteEID);
    sqe_ctx->rmt_eid_l = rmt_eid[0];
    sqe_ctx->rmt_eid_h = rmt_eid[1];
    sqe_ctx->udf_type = 0;
    sqe_ctx->reduce_data_type = 0;
    sqe_ctx->reduce_opcode = 0;
    if constexpr (op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx =
            (__ubuf__ asccomm_urma_notify_ctx*)((__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx));
        asccomm_urma_fill_notify_ctx(notify_ctx, remote_mem_info, notify_addr, params.value);
    } else if constexpr (op_code == asccomm_urma_op_code::WRITE_WITH_REDUCE) {
        sqe_ctx->reduce_data_type = params.reduce_data_type;
        sqe_ctx->reduce_opcode = params.reduce_opcode;
    }
}

template <asccomm_urma_op_code op_code, auto const& config, typename T>
__aicore__ inline void asccomm_urma_fill_sqe_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __gm__ uint8_t* remote_addr, const SqContext& sq_ctx,
    const RegedBufferEntity& remote_mem_info, uint32_t cur_head, GM_ADDR notify_addr, const udma_params<T>& params)
{
    asccomm_urma_fill_sqe_ctx<op_code, config>(
        sqe_ctx, remote_addr, sq_ctx, remote_mem_info, cur_head, 1U, notify_addr, params);
}

template <asccomm_urma_op_code op_code, auto const& config>
__aicore__ inline void asccomm_urma_fill_batch_sqe_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __gm__ uint8_t* remote_addr, const ubc_batch_handle& batch_handle,
    uint32_t sge_num)
{
    static_assert(
        op_code == asccomm_urma_op_code::WRITE || op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY ||
            op_code == asccomm_urma_op_code::READ,
        "Batch SQE fast fill only supports Write, WriteWithNotify and Read");
    static_assert(asccomm_config_inline_en(config) == 0, "Batch SQE fast fill does not support inline data");
    static_assert(
        sizeof(asccomm_urma_sqe_ctx) == 6U * sizeof(uint64_t),
        "Batch SQE fast fill requires the current 6-QW SQE layout");

    constexpr uint32_t flag =
        (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) | ((config.cqe & 0x1U) << 5U);
    constexpr uint32_t flag_shift = 16U;
    constexpr uint32_t token_en_shift = 28U;
    constexpr uint32_t rmt_jetty_type_shift = 29U;
    constexpr uint32_t opcode_shift = 8U;
    constexpr uint32_t sge_num_shift = 24U;
    constexpr uint32_t remote_token_id_mask = 0xFFFFFU;
    constexpr uint32_t tp_id_mask = 0xFFFFFFU;

    constexpr uint32_t first_dw = (flag << flag_shift) | (1U << token_en_shift) | (1U << rmt_jetty_type_shift);
    constexpr uint32_t second_dw = static_cast<uint32_t>(op_code) << opcode_shift;
    constexpr uint64_t sqe_word0 = static_cast<uint64_t>(first_dw) | (static_cast<uint64_t>(second_dw) << 32U);

    const uint32_t tp_id = batch_handle.remote_info.tp_id;
    const uint32_t token_id = batch_handle.remote_info.token_id;
    const uint32_t token_value = batch_handle.remote_info.token_value;
    const uint64_t remote_eid_low = batch_handle.remote_info.remote_eid_low;
    const uint64_t remote_eid_high = batch_handle.remote_info.remote_eid_high;
    const uint64_t remote_addr_value = reinterpret_cast<uint64_t>(remote_addr);
    __ubuf__ uint64_t* sqe_words = reinterpret_cast<__ubuf__ uint64_t*>(sqe_ctx);

    // Write complete QWs so all reserved fields and sqe_bb_idx are initialized without bit-field RMW stores.
    uint32_t third_dw = (tp_id & tp_id_mask) | (sge_num << sge_num_shift);
    uint32_t fourth_dw = token_id & remote_token_id_mask;

    sqe_words[0] = sqe_word0;
    sqe_words[1] = static_cast<uint64_t>(third_dw) | (static_cast<uint64_t>(fourth_dw) << 32U);
    sqe_words[2] = remote_eid_low;
    sqe_words[3] = remote_eid_high;
    sqe_words[4] = static_cast<uint64_t>(token_value);
    sqe_words[5] = remote_addr_value;
}

__aicore__ inline void asccomm_urma_fill_batch_notify_ctx(
    __ubuf__ asccomm_urma_notify_ctx* notify_ctx, const ubc_batch_handle& batch_handle, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    const uint32_t token_id = batch_handle.remote_info.token_id;
    const uint32_t token_value = batch_handle.remote_info.token_value;
    const uint64_t notify_addr_value = reinterpret_cast<uint64_t>(notify_addr);
    notify_ctx->notify_token_id = token_id & 0xFFFFFU;
    notify_ctx->notify_token_value = token_value;
    notify_ctx->notify_addr_l = notify_addr_value & 0xFFFFFFFFU;
    notify_ctx->notify_addr_h = (notify_addr_value >> 32) & 0xFFFFFFFFU;
    notify_ctx->notify_data_l = notify_val & 0xFFFFFFFFU;
    notify_ctx->notify_data_h = (notify_val >> 32) & 0xFFFFFFFFU;
}

template <asccomm_urma_op_code op_code, typename T>
__aicore__ inline void asccomm_urma_fill_sge_ctx(
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx, uint64_t message_len, __gm__ uint8_t* local_addr, udma_params<T> params)
{
    sge_ctx->len = static_cast<uint32_t>(message_len);
    sge_ctx->va = reinterpret_cast<uint64_t>(local_addr);
    if constexpr (op_code == asccomm_urma_op_code::FAA) {
        __ubuf__ T* add_data_addr = (__ubuf__ T*)((__ubuf__ uint8_t*)sge_ctx + sizeof(asccomm_urma_sge_ctx));
        *add_data_addr = params.value;
    } else if constexpr (op_code == asccomm_urma_op_code::CAS) {
        __ubuf__ T* swap_data_addr = (__ubuf__ T*)((__ubuf__ uint8_t*)sge_ctx + sizeof(asccomm_urma_sge_ctx));
        *swap_data_addr = params.value;
        __ubuf__ T* cmp_data_addr = (__ubuf__ T*)((__ubuf__ uint8_t*)swap_data_addr + sizeof(T));
        *cmp_data_addr = params.cond;
    }
}

__aicore__ inline __gm__ uint32_t* asccomm_urma_get_lock_addr(__gm__ ChannelEntity* channel_entity)
{
    // cq_context_addr shares a cache line with the channel counters. Read it bypassing DataCache to avoid caching
    // stale counters before the lock is acquired, then use the default CQ context's ubJfc.headAddr as the lock address.
    __gm__ uint64_t* cq_contexts_addr_field = reinterpret_cast<__gm__ uint64_t*>(
        reinterpret_cast<__gm__ uint8_t*>(channel_entity) + offsetof(ChannelEntity, cqContextAddr));
    uint64_t cq_contexts_addr = ld_dev(cq_contexts_addr_field, 0);
    __gm__ CqContext* cq_context = reinterpret_cast<__gm__ CqContext*>(cq_contexts_addr) + ASCCOMM_URMA_DEFAULT_QP_IDX;
    uint64_t lock_addr_value = cq_context->contextInfo.ubJfc.headAddr;
    return reinterpret_cast<__gm__ uint32_t*>(lock_addr_value);
}

__aicore__ inline void asccomm_urma_dump_amo_ctx(__ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, uint32_t atomic_len)
{
    ASCCOMM_DEBUG_TRAP_IF(sqe_ctx == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    auto opcode = sqe_ctx->opcode;
    if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::FAA)) {
        __ubuf__ uint8_t* amo_data_addr =
            (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx);
        uint64_t add_value = (atomic_len == sizeof(uint32_t)) ?
                                 static_cast<uint64_t>(*(__ubuf__ uint32_t*)amo_data_addr) :
                                 *(__ubuf__ uint64_t*)amo_data_addr;
        ASCCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: add_value:0x%llx \n", add_value);
    } else if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::CAS)) {
        __ubuf__ uint8_t* amo_data_addr =
            (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx);
        uint64_t swap_value = (atomic_len == sizeof(uint32_t)) ?
                                  static_cast<uint64_t>(*(__ubuf__ uint32_t*)amo_data_addr) :
                                  *(__ubuf__ uint64_t*)amo_data_addr;
        uint64_t cond_value = (atomic_len == sizeof(uint32_t)) ?
                                  static_cast<uint64_t>(*(__ubuf__ uint32_t*)(amo_data_addr + atomic_len)) :
                                  *(__ubuf__ uint64_t*)(amo_data_addr + atomic_len);
        ASCCOMM_KERNEL_LOG(
            KERNEL_INFO, "Hcomm URMA SGE: cond_value:0x%llx, swap_value:0x%llx \n", cond_value, swap_value);
    }
}

__aicore__ inline void asccomm_urma_dump_sge_ctx(
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, __ubuf__ uint8_t* sge_addr, uint32_t atomic_len)
{
    ASCCOMM_DEBUG_TRAP_IF(sqe_ctx == nullptr || sge_addr == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)sge_addr;
    for (uint32_t i = 0; i < sqe_ctx->sge_num; i++) {
        auto sge_len = sge_ctx->len;
        auto sge_rmt_addr = sge_ctx->va;
        ASCCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: sge idx: %d, va: %p sge_len: %d \n", i, sge_rmt_addr, sge_len);
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
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA WQE: notify_token_id: %x notify_token_value: %x notify_addr_l: %x notify_addr_h: %x notify_data_l: "
        "%x "
        "notify_data_h: %x \n",
        notify_token_id, notify_token_value, notify_addr_l, notify_addr_h, notify_data_l, notify_data_h);
}

__aicore__ inline void asccomm_urma_dump_wqe_ctx(__ubuf__ asccomm_urma_sqe_ctx* sqe_ctx, uint32_t atomic_len)
{
    ASCCOMM_DEBUG_TRAP_IF(sqe_ctx == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    auto sqe_bb_idx = sqe_ctx->sqe_bb_idx;
    auto flag = sqe_ctx->flag;
    auto rsv0 = sqe_ctx->rsv0;
    auto nf = sqe_ctx->nf;
    auto token_en = sqe_ctx->token_en;
    auto rmt_jetty_type = sqe_ctx->rmt_jetty_type;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sqe_bb_idx: %x flag: %x rsv0: %x nf: %x token_en: %x rmt_jetty_type: %x \n",
        sqe_bb_idx, flag, rsv0, nf, token_en, rmt_jetty_type);
    auto owner = sqe_ctx->owner;
    auto target_hint = sqe_ctx->target_hint;
    auto opcode = sqe_ctx->opcode;
    auto rsv1 = sqe_ctx->rsv1;
    auto inline_msg_len = sqe_ctx->inline_msg_len;
    auto tp_id = sqe_ctx->tp_id;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: owner: %x target_hint: %x opcode: %x rsv1: %x inline_msg_len: %x tp_id: %x \n",
        owner, target_hint, opcode, rsv1, inline_msg_len, tp_id);
    auto sge_num = sqe_ctx->sge_num;
    auto rmt_jetty_or_seg_id = sqe_ctx->rmt_jetty_or_seg_id;
    auto rsv2 = sqe_ctx->rsv2;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sge_num: %x rmt_jetty_or_seg_id: %x rsv2: %x \n", sge_num, rmt_jetty_or_seg_id,
        rsv2);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: rmt_eid: %x, %x \n", sqe_ctx->rmt_eid_l, sqe_ctx->rmt_eid_h);
    auto rmt_token_value = sqe_ctx->rmt_token_value;
    auto udf_type = sqe_ctx->udf_type;
    auto reduce_data_type = sqe_ctx->reduce_data_type;
    auto reduce_opcode = sqe_ctx->reduce_opcode;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: rmt_token_value: %x udf_type: %x reduce_data_type: %x reduce_opcode: %x \n",
        rmt_token_value, udf_type, reduce_data_type, reduce_opcode);
    auto rmt_addr_l_or_token_id = sqe_ctx->rmt_addr_l_or_token_id;
    auto rmt_addr_h_or_token_value = sqe_ctx->rmt_addr_h_or_token_value;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: rmt_addr_l_or_token_id: %x rmt_addr_h_or_token_value: %x \n",
        rmt_addr_l_or_token_id, rmt_addr_h_or_token_value);
    __ubuf__ uint8_t* sge_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx);
    if (opcode == static_cast<uint32_t>(asccomm_urma_op_code::WRITE_WITH_NOTIFY)) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx = (__ubuf__ asccomm_urma_notify_ctx*)sge_addr;
        asccomm_urma_dump_notify_ctx(notify_ctx);
        sge_addr += sizeof(asccomm_urma_notify_ctx);
    }
    asccomm_urma_dump_sge_ctx(sqe_ctx, sge_addr, atomic_len);
}

__aicore__ inline void asccomm_urma_dump_cqe_ctx(__gm__ uint32_t* cqe_words)
{
    ASCCOMM_DEBUG_TRAP_IF(cqe_words == nullptr, "Hcomm URMA CQE: nullptr pointer \n");
    uint32_t dw0 = ld_dev(cqe_words, 0);
    uint32_t dw1 = ld_dev(cqe_words + 1, 0);
    uint32_t dw2 = ld_dev(cqe_words + 2, 0);
    uint32_t dw3 = ld_dev(cqe_words + 3, 0);
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA CQE: DW0 - s_r: %d, is_jetty: %d, owner: %d, inlineEn: %d, "
        "opcode: %d, fd: %d, substatus: %d, status: %d \n",
        dw0 & 1U, (dw0 >> 1U) & 1U, (dw0 >> 2U) & 1U, (dw0 >> 3U) & 1U, (dw0 >> 4U) & 0x7U, (dw0 >> 7U) & 1U,
        (dw0 >> 16U) & 0xFFU, (dw0 >> 24U) & 0xFFU);
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW1 - entry_idx: %d, local_num_l: %d \n", dw1 & 0xFFFFU, dw1 >> 16U);
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW2 - local_num_h: %d, rmt_idx: %d \n", dw2 & 0xFU, (dw2 >> 4U) & 0xFFFFFU);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW3 - tpn: %d \n", dw3 & 0xFFFFFFU);
    ASCCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW4 - byte_cnt: %d \n", ld_dev(cqe_words + 4, 0));
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW5-DW6 - user_data: 0x%x%x \n", ld_dev(cqe_words + 6, 0),
        ld_dev(cqe_words + 5, 0));
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW7-DW10 - rmt_eid: [0x%x, 0x%x, 0x%x, 0x%x] \n", ld_dev(cqe_words + 7, 0),
        ld_dev(cqe_words + 8, 0), ld_dev(cqe_words + 9, 0), ld_dev(cqe_words + 10, 0));
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW11-DW12 - data: 0x%x%x \n", ld_dev(cqe_words + 12, 0),
        ld_dev(cqe_words + 11, 0));
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW13-DW15 - inline_data: [0x%x, 0x%x, 0x%x] \n", ld_dev(cqe_words + 13, 0),
        ld_dev(cqe_words + 14, 0), ld_dev(cqe_words + 15, 0));
}

__aicore__ inline asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_impl() {}

__aicore__ inline asccomm_impl<COMM_PROTOCOL_UB_CTP>::~asccomm_impl() {}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_init(__ubuf__ uint8_t* buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff) & (ASCCOMM_URMA_ADDR_ALIGNMENT - 1U)) != 0U,
        "Hcomm asccomm_init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff)));
    ASCCOMM_DEBUG_TRAP_IF(len < ASCCOMM_URMA_TMP_BUF_SIZE, "Hcomm asccomm_init failed: len = %u\n", len);

    wqe_item = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    return ASCCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_init(const LocalTensor<T>& buff, uint32_t len)
{
    ASCCOMM_DEBUG_TRAP_IF(
        len > buff.GetSize() * static_cast<uint32_t>(sizeof(T)),
        "Hcomm asccomm_init failed: len = %u, tensor_size = %llu\n", len,
        static_cast<uint64_t>(buff.GetSize()) * sizeof(T));
    return asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), len);
}

__aicore__ inline void asccomm_urma_resolve_batch_remote(
    const multi_channel_remote_info& remote_info, GM_ADDR, batch_remote_info& resolved_remote_info)
{
    auto* remote_buffers = reinterpret_cast<RegedBufferEntity*>(remote_info.remote_buffer_addr);
    ASCCOMM_DEBUG_TRAP_IF(remote_buffers == nullptr || remote_info.remote_buffer_num == 0U, "Invalid remote buffers\n");

    resolved_remote_info.token_id =
        remote_buffers[ASCCOMM_URMA_DEFAULT_BUFFER_IDX].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    resolved_remote_info.token_value =
        remote_buffers[ASCCOMM_URMA_DEFAULT_BUFFER_IDX].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    resolved_remote_info.tp_id = remote_info.tp_id;
    resolved_remote_info.remote_eid_low = remote_info.remote_eid_low;
    resolved_remote_info.remote_eid_high = remote_info.remote_eid_high;
}

__aicore__ inline ubc_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    (void)local_addr;
    __gm__ ChannelEntity* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    auto sq_ctx = channel_entity->sqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    auto remote_eid = reinterpret_cast<const uint64_t*>(sq_ctx.contextInfo.ubJfs.remoteEID);
    multi_channel_remote_info remote_source{};
    remote_source.remote_buffer_addr = reinterpret_cast<uint64_t>(channel_entity->remoteBufferAddr);
    remote_source.remote_buffer_num = channel_entity->remoteBufferNum;
    remote_source.tp_id = sq_ctx.contextInfo.ubJfs.tpID;
    remote_source.remote_eid_low = remote_eid[0];
    remote_source.remote_eid_high = remote_eid[1];

    ubc_batch_handle batch_handle{};
    asccomm_urma_resolve_batch_remote(remote_source, remote_addr, batch_handle.remote_info);
    batch_handle.channel_handle = channel;
    batch_handle.sq_context = sq_ctx;
    batch_handle.cq_context = cq_ctx;
    batch_handle.cursor.sq_head = channel_entity->sqHead;
    batch_handle.cursor.sq_tail = channel_entity->sqTail;
    batch_handle.cursor.cq_head = channel_entity->cqHead;
    batch_handle.cursor.cq_tail = channel_entity->cqTail;
    batch_handle.cursor.pre_sq_cnt = 0U;
    batch_handle.buffer.raw_buffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    batch_handle.buffer.buffer_capacity = buff_len / ASCCOMM_URMA_WQEBB_SIZE;
    return batch_handle;
}

__aicore__ inline ubc_multi_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_make_batch_handle(
    multi_channel_handle multi_channel, __ubuf__ uint8_t* buff, uint32_t buff_len, GM_ADDR remote_addr,
    GM_ADDR local_addr)
{
    (void)remote_addr;
    (void)local_addr;
    ASCCOMM_DEBUG_TRAP_IF(
        multi_channel == multi_channel_handle{}, "Hcomm asccomm_make_batch_handle failed with empty handle\n");

    auto* multi_channel_entity_ptr =
        reinterpret_cast<__gm__ multi_channel_entity*>(static_cast<uint64_t>(multi_channel));
    ChannelHandle channel = multi_channel_entity_ptr->channel_handle;
    ASCCOMM_DEBUG_TRAP_IF(
        channel == 0U || multi_channel_entity_ptr->channel_num == 0U ||
            multi_channel_entity_ptr->remote_info_addr == 0U,
        "Hcomm asccomm_make_batch_handle failed with invalid multi channel entity\n");
    auto* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    auto sq_ctx = channel_entity->sqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];

    ubc_multi_batch_handle multi_batch_handle{};
    multi_batch_handle.handle.sq_context = sq_ctx;
    multi_batch_handle.handle.cq_context = cq_ctx;
    multi_batch_handle.handle.cursor.sq_head = channel_entity->sqHead;
    multi_batch_handle.handle.cursor.sq_tail = channel_entity->sqTail;
    multi_batch_handle.handle.cursor.cq_head = channel_entity->cqHead;
    multi_batch_handle.handle.cursor.cq_tail = channel_entity->cqTail;
    multi_batch_handle.handle.buffer.raw_buffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    multi_batch_handle.handle.buffer.buffer_capacity = buff_len / ASCCOMM_URMA_WQEBB_SIZE;
    multi_batch_handle.channel_handle = channel;
    multi_batch_handle.channel_num = multi_channel_entity_ptr->channel_num;
    multi_batch_handle.remote_info_addr = multi_channel_entity_ptr->remote_info_addr;
    return multi_batch_handle;
}

template <typename U>
__aicore__ inline ubc_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_make_batch_handle(
    ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr, GM_ADDR local_addr)
{
    return asccomm_make_batch_handle(
        channel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buff_len, remote_addr, local_addr);
}

template <typename U>
__aicore__ inline ubc_multi_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_make_batch_handle(
    multi_channel_handle multi_channel, const LocalTensor<U>& buff, uint32_t buff_len, GM_ADDR remote_addr,
    GM_ADDR local_addr)
{
    return asccomm_make_batch_handle(
        multi_channel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buff_len, remote_addr, local_addr);
}

__aicore__ inline ubc_batch_handle& asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_get_handle_ref(
    ubc_batch_handle& batch_handle, uint32_t channel_index, GM_ADDR remote_addr)
{
    (void)channel_index;
    (void)remote_addr;
    return batch_handle;
}

__aicore__ inline ubc_batch_handle& asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_get_handle_ref(
    ubc_multi_batch_handle& multi_batch_handle, uint32_t channel_index, GM_ADDR remote_addr)
{
    multi_batch_handle.handle.remote_info = batch_remote_info{};
    multi_batch_handle.handle.channel_handle = 0U;
    ASCCOMM_DEBUG_TRAP_IF(
        channel_index >= multi_batch_handle.channel_num,
        "Hcomm asccomm_get_handle_ref failed with invalid channel index\n");

    auto* remote_infos = reinterpret_cast<__gm__ multi_channel_remote_info*>(multi_batch_handle.remote_info_addr);
    multi_channel_remote_info remote_source{};
    remote_source.remote_buffer_addr = remote_infos[channel_index].remote_buffer_addr;
    remote_source.remote_buffer_num = remote_infos[channel_index].remote_buffer_num;
    remote_source.remote_eid_low = remote_infos[channel_index].remote_eid_low;
    remote_source.remote_eid_high = remote_infos[channel_index].remote_eid_high;
    remote_source.tp_id = remote_infos[channel_index].tp_id;
    asccomm_urma_resolve_batch_remote(remote_source, remote_addr, multi_batch_handle.handle.remote_info);
    multi_batch_handle.handle.channel_handle = multi_batch_handle.channel_handle;
    return multi_batch_handle.handle;
}

template <asccomm_urma_op_code op_code, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_post_send(
    ubc_batch_handle& batch_handle, GM_ADDR remote_addr, GM_ADDR local_addr, uint32_t len, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    static_assert(
        op_code == asccomm_urma_op_code::WRITE || op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY ||
            op_code == asccomm_urma_op_code::READ,
        "batch_post_send only supports Write, WriteWithNotify and Read");
    static_assert(config.cqe == 0 || config.cqe == 1, "batch_post_send supports cqe values 0 and 1 only");
    static_assert(asccomm_config_inline_en(config) == 0, "batch_post_send does not support inline data");

    constexpr bool with_notify = op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY;
    constexpr uint32_t wqe_bytes = sizeof(asccomm_urma_sqe_ctx) + (with_notify ? sizeof(asccomm_urma_notify_ctx) : 0U) +
                                   sizeof(asccomm_urma_sge_ctx);
    constexpr uint32_t wqebb_count = (wqe_bytes + ASCCOMM_URMA_WQEBB_SIZE - 1U) / ASCCOMM_URMA_WQEBB_SIZE;

    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt > batch_handle.buffer.buffer_capacity ||
            wqebb_count > batch_handle.buffer.buffer_capacity - pre_sq_cnt,
        "Hcomm batch_post_send failed with insufficient buffer\n");

    // Contract: descriptors and remote addresses are valid for the channel/MR selected by batch_handle.
    // These caller guarantees are not revalidated here.
    __ubuf__ uint8_t* current_wqe_addr =
        reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer) + pre_sq_cnt * ASCCOMM_URMA_WQEBB_SIZE;
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = reinterpret_cast<__ubuf__ asccomm_urma_sqe_ctx*>(current_wqe_addr);

    asccomm_urma_fill_batch_sqe_ctx<op_code, config>(
        sqe_ctx, reinterpret_cast<__gm__ uint8_t*>(remote_addr), batch_handle, 1U);

    __ubuf__ uint8_t* sge_addr = current_wqe_addr + sizeof(asccomm_urma_sqe_ctx);
    if constexpr (with_notify) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx = reinterpret_cast<__ubuf__ asccomm_urma_notify_ctx*>(sge_addr);
        asccomm_urma_fill_batch_notify_ctx(notify_ctx, batch_handle, notify_addr, notify_val);
        sge_addr += sizeof(asccomm_urma_notify_ctx);
    }
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = reinterpret_cast<__ubuf__ asccomm_urma_sge_ctx*>(sge_addr);
    asccomm_urma_fill_sge_ctx<op_code>(
        sge_ctx, len, reinterpret_cast<__gm__ uint8_t*>(local_addr), udma_params<uint64_t>{});

    batch_handle.cursor.pre_sq_cnt = pre_sq_cnt + wqebb_count;
    if constexpr (config.cqe == 1) {
        batch_handle.cursor.cq_head++;
    }
    ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_wqe_ctx, sqe_ctx, sizeof(uint64_t));
    return ASCCOMM_SUCCESS;
}

template <asccomm_urma_op_code op_code, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_post_send(
    ubc_batch_handle& batch_handle, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t sge_num,
    GM_ADDR notify_addr, uint64_t notify_val)
{
    static_assert(
        op_code == asccomm_urma_op_code::WRITE || op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY ||
            op_code == asccomm_urma_op_code::READ,
        "batch_post_send only supports Write, WriteWithNotify and Read");
    static_assert(config.cqe == 0 || config.cqe == 1, "batch_post_send supports cqe values 0 and 1 only");
    static_assert(asccomm_config_inline_en(config) == 0, "batch_post_send does not support inline data");

    constexpr bool with_notify = op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY;
    ASCCOMM_DEBUG_TRAP_IF(
        sge_num < ASCCOMM_URMA_BATCH_MIN_SGE_NUM ||
            sge_num > (with_notify ? ASCCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM : ASCCOMM_URMA_BATCH_DATA_MAX_SGE_NUM),
        "Hcomm batch_post_send failed with invalid SGE count sge_num=%u valid_range=[%u, %u]\n", sge_num,
        ASCCOMM_URMA_BATCH_MIN_SGE_NUM,
        with_notify ? ASCCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM : ASCCOMM_URMA_BATCH_DATA_MAX_SGE_NUM);

    constexpr uint32_t fixed_bytes =
        sizeof(asccomm_urma_sqe_ctx) + (with_notify ? sizeof(asccomm_urma_notify_ctx) : 0U);
    uint32_t wqe_bytes = fixed_bytes + sge_num * sizeof(asccomm_urma_sge_ctx);
    uint32_t wqebb_count = (wqe_bytes + ASCCOMM_URMA_WQEBB_SIZE - 1U) / ASCCOMM_URMA_WQEBB_SIZE;
    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt > batch_handle.buffer.buffer_capacity ||
            wqebb_count > batch_handle.buffer.buffer_capacity - pre_sq_cnt,
        "Hcomm batch_post_send failed with insufficient buffer\n");

    // Contract: descriptors and remote addresses are valid for the channel/MR selected by batch_handle.
    // These caller guarantees are not revalidated here.
    __ubuf__ uint8_t* current_wqe_addr =
        reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer) + pre_sq_cnt * ASCCOMM_URMA_WQEBB_SIZE;
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = reinterpret_cast<__ubuf__ asccomm_urma_sqe_ctx*>(current_wqe_addr);

    asccomm_urma_fill_batch_sqe_ctx<op_code, config>(
        sqe_ctx, reinterpret_cast<__gm__ uint8_t*>(remote_addr), batch_handle, sge_num);

    __ubuf__ uint8_t* sge_addr = current_wqe_addr + sizeof(asccomm_urma_sqe_ctx);
    if constexpr (with_notify) {
        __ubuf__ asccomm_urma_notify_ctx* notify_ctx = reinterpret_cast<__ubuf__ asccomm_urma_notify_ctx*>(sge_addr);
        asccomm_urma_fill_batch_notify_ctx(notify_ctx, batch_handle, notify_addr, notify_val);
        sge_addr += sizeof(asccomm_urma_notify_ctx);
    }
    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = reinterpret_cast<__ubuf__ asccomm_urma_sge_ctx*>(sge_addr);
    for (uint32_t i = 0U; i < sge_num; ++i) {
        asccomm_urma_fill_sge_ctx<op_code>(
            &sge_ctx[i], local_descs[i].len, reinterpret_cast<__gm__ uint8_t*>(local_descs[i].addr),
            udma_params<uint64_t>{});
    }

    batch_handle.cursor.pre_sq_cnt = pre_sq_cnt + wqebb_count;
    if constexpr (config.cqe == 1) {
        batch_handle.cursor.cq_head++;
    }
    ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_wqe_ctx, sqe_ctx, sizeof(uint64_t));
    return ASCCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_nbi(
    ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return batch_post_send<asccomm_urma_op_code::WRITE, config>(batch_handle, dst, src, len);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_nbi(
    ubc_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return batch_post_send<asccomm_urma_op_code::WRITE, config>(batch_handle, dst, src_descs, src_num);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_read_nbi(
    ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return batch_post_send<asccomm_urma_op_code::READ, config>(batch_handle, src, dst, len);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_read_nbi(
    ubc_batch_handle& batch_handle, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return batch_post_send<asccomm_urma_op_code::READ, config>(batch_handle, src, dst_descs, dst_num);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_with_notify_nbi(
    ubc_batch_handle& batch_handle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    return batch_post_send<asccomm_urma_op_code::WRITE_WITH_NOTIFY, config>(
        batch_handle, dst, src, len, notify_addr, notify_val);
}

template <auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_with_notify_nbi(
    ubc_batch_handle& batch_handle, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    return batch_post_send<asccomm_urma_op_code::WRITE_WITH_NOTIFY, config>(
        batch_handle, dst, src_descs, src_num, notify_addr, notify_val);
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_cq_when_cq_overflow(
    ChannelHandle channel, const SqContext& sq_ctx, const CqContext& cq_ctx, uint32_t sq_head, uint32_t cqe_cnt)
{
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    uint32_t cq_tail = channel_entity->cqTail;
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
    uint32_t cq_depth = cq_ctx.contextInfo.ubJfc.cqDepth;
    uint32_t sq_depth = sq_ctx.contextInfo.ubJfs.sqDepth;
    if (((cqe_cnt + POLL_CQ_THRESHOLD) & (cq_depth - 1U)) == (cq_tail & (cq_depth - 1U))) {
        uint32_t idx = (cq_tail + NUM_CQE_PER_POLL_CQ) > cqe_cnt ? cqe_cnt : cq_tail + NUM_CQE_PER_POLL_CQ;
        ASCCOMM_KERNEL_LOG(
            KERNEL_INFO, "Hcomm URMA queue overflow sq_head=%u cqe_cnt=%u cq_tail=%u idx=%u sq_depth=%u cq_depth=%u \n",
            sq_head, cqe_cnt, cq_tail, idx, sq_depth, cq_depth);
        (void)poll_cq(channel, idx);
    }
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_cq_when_sq_overflow(
    ChannelHandle channel, const SqContext& sq_ctx, uint32_t sq_head)
{
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    uint32_t sq_tail = channel_entity->sqTail;
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    uint32_t sq_depth = sq_ctx.contextInfo.ubJfs.sqDepth;
    uint16_t outstanding = (uint16_t)((uint16_t)(sq_head & 0xFFFFU) - (uint16_t)(sq_tail & 0xFFFFU));
    if ((uint32_t)outstanding + POLL_CQ_THRESHOLD >= sq_depth) {
        (void)poll_cq<true>(channel, channel_entity->cqHead, sq_head, sq_depth, POLL_CQ_THRESHOLD);
    }
}

__aicore__ inline void asccomm_impl<COMM_PROTOCOL_UB_CTP>::commit_impl(
    ChannelHandle channel, const SqContext& sq_ctx, uint32_t sq_head, uint32_t cqe_cnt)
{
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    st_dev(sq_head, reinterpret_cast<__gm__ uint32_t*>(sq_ctx.contextInfo.ubJfs.dbVa), 0);
    poll_cq_when_cq_overflow(channel, sq_ctx, cq_ctx, sq_head, cqe_cnt);
}

template <
    bool commit, pipe_t commit_pipe, pipe_t req_pipe, asccomm_urma_op_code op_code, auto const& config, typename T>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::post_send(
    ChannelHandle channel, GM_ADDR remote_addr, GM_ADDR local_addr, uint64_t len, GM_ADDR notify_addr,
    const udma_params<T>& params)
{
    (void)commit_pipe;
    (void)req_pipe;

    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    auto sq_ctx = channel_entity->sqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t cur_head = channel_entity->sqHead;
    uint32_t cqe_cnt = channel_entity->cqHead;
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA post_send resolved remote buffer cur_head=%u sq_depth=%u \n", cur_head,
        sq_ctx.contextInfo.ubJfs.sqDepth);
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    poll_cq_when_sq_overflow(channel, sq_ctx, cur_head);

    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = (__ubuf__ asccomm_urma_sqe_ctx*)wqe_item;
    auto remote_mem_info = channel_entity->remoteBufferAddr[ASCCOMM_URMA_DEFAULT_BUFFER_IDX];
    asccomm_urma_fill_sqe_ctx<op_code, config>(
        sqe_ctx, (__gm__ uint8_t*)remote_addr, sq_ctx, remote_mem_info, cur_head, notify_addr, params);

    if constexpr (asccomm_config_inline_en(config) == 0) {
        __ubuf__ uint8_t* sge_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx);
        if constexpr (op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY) {
            sge_addr += sizeof(asccomm_urma_notify_ctx);
        }
        __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)sge_addr;
        asccomm_urma_fill_sge_ctx<op_code>(sge_ctx, len, (__gm__ uint8_t*)local_addr, params);
    }

    uint64_t sq_base_addr = sq_ctx.contextInfo.ubJfs.sqVa;
    uint32_t wqe_size = sq_ctx.contextInfo.ubJfs.wqeSize;
    uint32_t base_block_count = sq_ctx.contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqe_addr = (__gm__ uint8_t*)(sq_base_addr + wqe_size * (cur_head & (base_block_count - 1U)));

    constexpr uint32_t wqe_bb_cnt = (op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY ||
                                     op_code == asccomm_urma_op_code::FAA || op_code == asccomm_urma_op_code::CAS) ?
                                        2U :
                                        1U;
    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    if constexpr (wqe_bb_cnt == 2) {
        if (unlikely((cur_head & (base_block_count - 1U)) == base_block_count - 1U)) {
            uint32_t wqe_size_per_u32 = wqe_size / sizeof(uint32_t);
            asc_copy_ub2gm_align(
                sqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_item), 1, wqe_size,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
            asc_copy_ub2gm_align(
                reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
                reinterpret_cast<__ubuf__ uint8_t*>(wqe_item + wqe_size_per_u32), 1, wqe_size,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        } else {
            asc_copy_ub2gm_align(
                sqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_item), 1, wqe_size * wqe_bb_cnt,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        }
    } else {
        asc_copy_ub2gm_align(
            sqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_item), 1, wqe_size,
            asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }

    if constexpr (config.cqe != 0) {
        cqe_cnt++;
        channel_entity->cqHead = cqe_cnt;
    }
    cur_head += wqe_bb_cnt;
    channel_entity->sqHead = cur_head;
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    if constexpr (commit) {
        commit_impl(channel, sq_ctx, cur_head, cqe_cnt);
    }
    ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_wqe_ctx, sqe_ctx, sizeof(T));
    return ASCCOMM_SUCCESS;
}

template <
    bool commit, pipe_t commit_pipe, pipe_t req_pipe, asccomm_urma_op_code op_code, auto const& config, typename T>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::post_send(
    ChannelHandle channel, GM_ADDR remote_addr, const buf_desc* local_descs, uint32_t sge_num, GM_ADDR notify_addr,
    const udma_params<T>& params)
{
    (void)commit_pipe;
    (void)req_pipe;

    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    auto sq_ctx = channel_entity->sqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t cur_head = channel_entity->sqHead;
    uint32_t cqe_cnt = channel_entity->cqHead;
    constexpr bool with_notify = op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY;
    constexpr uint32_t max_sge_num =
        with_notify ? ASCCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM : ASCCOMM_URMA_BATCH_DATA_MAX_SGE_NUM;
    if constexpr (asccomm_config_inline_en(config) == 0) {
        ASCCOMM_DEBUG_TRAP_IF(
            local_descs == nullptr || sge_num < ASCCOMM_URMA_BATCH_MIN_SGE_NUM || sge_num > max_sge_num,
            "asccomm post_send failed with invalid SGE count sge_num=%u valid_range=[%u, %u]\n", sge_num,
            ASCCOMM_URMA_BATCH_MIN_SGE_NUM, max_sge_num);
    }
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA post_send resolved remote buffer cur_head=%u sq_depth=%u \n", cur_head,
        sq_ctx.contextInfo.ubJfs.sqDepth);
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    poll_cq_when_sq_overflow(channel, sq_ctx, cur_head);

    // Write SQE.
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = (__ubuf__ asccomm_urma_sqe_ctx*)wqe_item;
    auto remote_mem_info = channel_entity->remoteBufferAddr[ASCCOMM_URMA_DEFAULT_BUFFER_IDX];
    asccomm_urma_fill_sqe_ctx<op_code, config>(
        sqe_ctx, (__gm__ uint8_t*)remote_addr, sq_ctx, remote_mem_info, cur_head,
        asccomm_config_inline_en(config) == 0 ? sge_num : 0U, notify_addr, params);

    if constexpr (asccomm_config_inline_en(config) == 0) {
        // write SGE
        __ubuf__ uint8_t* sge_addr = (__ubuf__ uint8_t*)sqe_ctx + sizeof(asccomm_urma_sqe_ctx);
        if constexpr (op_code == asccomm_urma_op_code::WRITE_WITH_NOTIFY) {
            sge_addr += sizeof(asccomm_urma_notify_ctx);
        }
        __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)sge_addr;
        for (uint32_t i = 0U; i < sge_num; ++i) {
            asccomm_urma_fill_sge_ctx<op_code>(
                &sge_ctx[i], local_descs[i].len, (__gm__ uint8_t*)local_descs[i].addr, params);
        }
    }

    uint64_t sq_base_addr = sq_ctx.contextInfo.ubJfs.sqVa;
    uint32_t wqe_size = sq_ctx.contextInfo.ubJfs.wqeSize;
    uint32_t base_block_count = sq_ctx.contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqe_addr = (__gm__ uint8_t*)(sq_base_addr + wqe_size * (cur_head & (base_block_count - 1U)));

    uint32_t wqe_bb_cnt = 0U;
    if constexpr (op_code == asccomm_urma_op_code::FAA || op_code == asccomm_urma_op_code::CAS) {
        // FAA/CAS use the two-WQEBB atomic format even when the operand is represented by a single
        // non-inline SGE. Keep this rule shared by inline and non-inline paths so SQ head/doorbell
        // accounting remains compatible with the original atomic submission path.
        wqe_bb_cnt = 2U;
    } else if constexpr (asccomm_config_inline_en(config) == 1) {
        wqe_bb_cnt = 1U;
    } else {
        uint32_t fixed_bytes = sizeof(asccomm_urma_sqe_ctx) + (with_notify ? sizeof(asccomm_urma_notify_ctx) : 0U);
        uint32_t wqe_bytes = fixed_bytes + sge_num * sizeof(asccomm_urma_sge_ctx);
        wqe_bb_cnt = (wqe_bytes + ASCCOMM_URMA_WQEBB_SIZE - 1U) / ASCCOMM_URMA_WQEBB_SIZE;
    }
    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    uint32_t start_slot = cur_head % base_block_count;
    uint32_t first_wqebb_count =
        wqe_bb_cnt < base_block_count - start_slot ? wqe_bb_cnt : base_block_count - start_slot;
    asc_copy_ub2gm_align(
        sqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(wqe_item), 1, wqe_size * first_wqebb_count,
        asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (first_wqebb_count < wqe_bb_cnt) {
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
            reinterpret_cast<__ubuf__ uint8_t*>(wqe_item) + wqe_size * first_wqebb_count, 1,
            wqe_size * (wqe_bb_cnt - first_wqebb_count), asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    if constexpr (config.cqe != 0) {
        cqe_cnt++;
        channel_entity->cqHead = cqe_cnt;
    }
    cur_head += wqe_bb_cnt;
    channel_entity->sqHead = cur_head;
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    if constexpr (commit) {
        commit_impl(channel, sq_ctx, cur_head, cqe_cnt);
    }
    ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_wqe_ctx, sqe_ctx, sizeof(T));
    return ASCCOMM_SUCCESS;
}

template <bool sq_safe_mode>
__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_cq_impl(
    uint64_t cq_base_addr, uint32_t cqe_size, uint32_t cq_depth, uint32_t expect_idx, uint32_t& cur_tail,
    uint32_t& sq_tail, uint32_t sq_head, uint32_t sq_depth, uint32_t threshold)
{
    ASCCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA poll_cq enter expect_idx=%u cur_tail=%u cq_depth=%u \n", expect_idx, cur_tail,
        cq_depth);

#if defined(UT_TEST)
    cur_tail = expect_idx;
    sq_tail = expect_idx;
#else
    uint32_t sq_mask = sq_depth - 1U;
    while (true) {
        bool should_continue = false;
        if constexpr (sq_safe_mode) {
            uint16_t outstanding = (uint16_t)((uint16_t)(sq_head & 0xFFFFU) - (uint16_t)(sq_tail & 0xFFFFU));
            // Poll early when SQ is near-full, but only while CQEs remain un-consumed (cur_tail != expect_idx).
            should_continue = (uint32_t)outstanding + threshold >= sq_depth && cur_tail != expect_idx;
        } else {
            // Keep polling while CQEs remain, until reaching expect_idx.
            should_continue = cur_tail != expect_idx;
        }
        if (!should_continue) {
            break;
        }

        __gm__ uint32_t* cqe =
            (__gm__ uint32_t*)(cq_base_addr + static_cast<uint64_t>(cqe_size) * (cur_tail & (cq_depth - 1U)));
        uint32_t valid_owner = (cur_tail / cq_depth) & 1U;
        uint32_t times = 0;
        uint32_t ret = ASCCOMM_SUCCESS;
        // Read DW0 and DW1 in one 64-bit ld_dev; owner is DW0 bit 2, entry_idx is in the upper word.
        uint64_t cqe_words = ld_dev(reinterpret_cast<__gm__ uint64_t*>(cqe), 0);
        uint32_t cqe_flags = static_cast<uint32_t>(cqe_words);
        while (((cqe_flags >> 2U) & 1U) == valid_owner && times < ASCCOMM_URMA_MAX_RETRY_TIMES) {
            if (times != 0 && times % ASCCOMM_URMA_POLL_CQ_HEARTBEAT_TIMES == 0) {
                ASCCOMM_KERNEL_LOG(
                    KERNEL_WARN, "Hcomm URMA Poll CQ heartbeat times=%u cur_tail=%u expect_idx=%u \n", times, cur_tail,
                    expect_idx);
            }
            cqe_words = ld_dev(reinterpret_cast<__gm__ uint64_t*>(cqe), 0);
            cqe_flags = static_cast<uint32_t>(cqe_words);
            times++;
        }
        if (times >= ASCCOMM_URMA_MAX_RETRY_TIMES) {
            ASCCOMM_KERNEL_LOG(
                KERNEL_ERROR, "Hcomm URMA Poll CQ timeout cur_tail=%u expect_idx=%u \n", cur_tail, expect_idx);
            ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_cqe_ctx, cqe);
            ret = 0xFFU;
        } else {
            // check CQE status
            uint32_t status = (cqe_flags >> 24U) & 0xFFU;
            uint32_t sub_status = (cqe_flags >> 16U) & 0xFFU;
            constexpr uint8_t status_shift = 8;
            if (status != 0 || sub_status != 0) {
                ASCCOMM_KERNEL_LOG(
                    KERNEL_ERROR, "Hcomm URMA CQE failed status=%u sub_status=%u \n", status, sub_status);
                ASCCOMM_DEBUG_FUNC(asccomm_urma_dump_cqe_ctx, cqe);
                ret = (status << status_shift) | sub_status;
            }
        }
        if (ret != ASCCOMM_SUCCESS) {
            ASCCOMM_KERNEL_LOG(KERNEL_ERROR, "Poll CQ error, status=%u.\n", ret);
            Trap();
            return ret;
        }
        uint32_t entry_idx = static_cast<uint32_t>(cqe_words >> 32U) & 0xFFFFU;
        sq_tail += ((entry_idx - sq_tail) & sq_mask) + 1U;
        cur_tail++;
    }
#endif
    return ASCCOMM_SUCCESS;
}

template <bool sq_safe_mode>
__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_cq(
    ChannelHandle channel, uint32_t expect_idx, uint32_t sq_head, uint32_t sq_depth, uint32_t threshold)
{
    if (expect_idx == 0) {
        return ASCCOMM_SUCCESS;
    }
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    auto cq_ctx = channel_entity->cqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t cur_tail = channel_entity->cqTail;
    uint32_t sq_tail = channel_entity->sqTail;

    uint32_t ret = poll_cq_impl<sq_safe_mode>(
        cq_ctx.contextInfo.ubJfc.scqVa, cq_ctx.contextInfo.ubJfc.cqeSize, cq_ctx.contextInfo.ubJfc.cqDepth, expect_idx,
        cur_tail, sq_tail, sq_head, sq_depth, threshold);
    if (ret != ASCCOMM_SUCCESS) {
        return ret;
    }

    // update CQ tail
    channel_entity->cqTail = cur_tail;
    channel_entity->sqTail = sq_tail;

    // ring CQ doorbell
    st_dev(cur_tail & 0xFFFFFFU, (__gm__ uint32_t*)cq_ctx.contextInfo.ubJfc.dbVa, 0);

    return ASCCOMM_SUCCESS;
}

__aicore__ inline uint32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_batch_cq(
    ubc_batch_handle& batch_handle, uint32_t expect_idx)
{
    if (expect_idx == 0) {
        return ASCCOMM_SUCCESS;
    }

    uint32_t cur_tail = batch_handle.cursor.cq_tail;
    uint32_t ret = poll_cq_impl<false>(
        batch_handle.cq_context.contextInfo.ubJfc.scqVa, batch_handle.cq_context.contextInfo.ubJfc.cqeSize,
        batch_handle.cq_context.contextInfo.ubJfc.cqDepth, expect_idx, cur_tail, batch_handle.cursor.sq_tail, 0U,
        batch_handle.sq_context.contextInfo.ubJfs.sqDepth, 0U);
    if (ret != ASCCOMM_SUCCESS) {
        return ret;
    }

    batch_handle.cursor.cq_tail = cur_tail;
    auto* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    channel_entity->sqTail = batch_handle.cursor.sq_tail;
    channel_entity->cqTail = cur_tail;
    st_dev(cur_tail & 0xFFFFFFU, reinterpret_cast<__gm__ uint32_t*>(batch_handle.cq_context.contextInfo.ubJfc.dbVa), 0);
    return ASCCOMM_SUCCESS;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE, config>(channel, dst, src, len);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_nbi(
    ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE, config>(
        channel, dst, src_descs, src_num);
}

template <
    typename T, asccomm_urma_reduce_op reduce_op, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_reduce_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    constexpr uint32_t reduce_data_type = ASCCOMM_URMA_REDUCE_DATA_TYPE<T>;
    static_assert(
        reduce_data_type != ASCCOMM_URMA_INVALID_REDUCE_DATA_TYPE,
        "asccomm_write_reduce_nbi only supports int8_t, int16_t, int32_t, uint32_t, half, float and bfloat16_t");
    static_assert(
        reduce_op == asccomm_urma_reduce_op::MAX || reduce_op == asccomm_urma_reduce_op::MIN ||
            reduce_op == asccomm_urma_reduce_op::SUM,
        "asccomm_write_reduce_nbi only supports MAX, MIN and SUM");
    static_assert(
        asccomm_config_inline_en(config) == 0, "asccomm_write_reduce_nbi does not support inline data in WQE");
    udma_params<uint32_t> params{};
    params.reduce_data_type = reduce_data_type;
    params.reduce_opcode = static_cast<uint32_t>(reduce_op);
    uint64_t len = count * sizeof(T);
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE_WITH_REDUCE, config>(
        channel, dst, src, len, nullptr, params);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_read_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::READ, config>(channel, src, dst, len);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_read_nbi(
    ChannelHandle channel, const buf_desc* dst_descs, uint32_t dst_num, GM_ADDR src)
{
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::READ, config>(
        channel, src, dst_descs, dst_num);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_value_nbi(
    ChannelHandle channel, GM_ADDR dst, T value)
{
    static_assert(asccomm_config_inline_en(config) == 1, "asccomm_write_value_nbi requires inline data in WQE");
    udma_params<T> params{value, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE, config>(
        channel, dst, nullptr, 0U, nullptr, params);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_with_notify_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    udma_params<uint64_t> params{notify_val, 0, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE_WITH_NOTIFY, config>(
        channel, dst, src, len, notify_addr, params);
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_write_with_notify_nbi(
    ChannelHandle channel, GM_ADDR dst, const buf_desc* src_descs, uint32_t src_num, GM_ADDR notify_addr,
    uint64_t notify_val)
{
    udma_params<uint64_t> params{notify_val, 0, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::WRITE_WITH_NOTIFY, config>(
        channel, dst, src_descs, src_num, notify_addr, params);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_atomic_faa_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T add_val)
{
    static_assert(
        std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value || std::is_same<T, int64_t>::value ||
            std::is_same<T, uint64_t>::value,
        "asccomm_atomic_faa_nbi only supports int32_t, uint32_t, int64_t, uint64_t");
    udma_params<T> params{add_val, 0, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::FAA, config>(
        channel, dst, fetch_addr, sizeof(T), nullptr, params);
}

template <typename T, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_atomic_cas_nbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetch_addr, T compare_val, T swap_val)
{
    static_assert(
        std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value || std::is_same<T, int64_t>::value ||
            std::is_same<T, uint64_t>::value,
        "asccomm_atomic_cas_nbi only supports int32_t, uint32_t, int64_t, uint64_t");
    udma_params<T> params{swap_val, compare_val, 0, 0};
    return post_send<commit, commit_pipe, req_pipe, asccomm_urma_op_code::CAS, config>(
        channel, dst, fetch_addr, sizeof(T), nullptr, params);
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_commit(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    auto sq_ctx = channel_entity->sqContextAddr[ASCCOMM_URMA_DEFAULT_QP_IDX];
    commit_impl(channel, sq_ctx, channel_entity->sqHead, channel_entity->cqHead);
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_batch_commit(ubc_batch_handle& batch_handle)
{
    uint32_t pre_sq_cnt = batch_handle.cursor.pre_sq_cnt;
    ASCCOMM_DEBUG_TRAP_IF(
        pre_sq_cnt == 0 || pre_sq_cnt > batch_handle.buffer.buffer_capacity,
        "Hcomm asccomm_batch_commit failed with invalid WQEBB count\n");

    uint32_t sq_depth = batch_handle.sq_context.contextInfo.ubJfs.sqDepth;
    auto* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(batch_handle.channel_handle);
    uint32_t cq_depth = batch_handle.cq_context.contextInfo.ubJfc.cqDepth;
    uint32_t sq_outstanding = batch_handle.cursor.sq_head - batch_handle.cursor.sq_tail;
    bool sq_overflow = sq_outstanding > sq_depth || pre_sq_cnt > sq_depth - sq_outstanding;
    uint32_t cq_after_commit = batch_handle.cursor.cq_head - batch_handle.cursor.cq_tail;
    bool cq_overflow = cq_after_commit > cq_depth;

    if (sq_overflow || cq_overflow) {
        uint32_t committed_cq_head = channel_entity->cqHead;
        poll_batch_cq(batch_handle, committed_cq_head);
    }
    uint32_t sq_head = batch_handle.cursor.sq_head;
    uint32_t start_slot = sq_head & (sq_depth - 1);
    uint32_t tail_wqebb_count = sq_depth - start_slot;
    uint32_t first_wqebb_count = pre_sq_cnt < tail_wqebb_count ? pre_sq_cnt : tail_wqebb_count;
    uint64_t sq_base_addr = batch_handle.sq_context.contextInfo.ubJfs.sqVa;
    __gm__ uint8_t* first_wqe_addr =
        reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + static_cast<uint64_t>(start_slot) * ASCCOMM_URMA_WQEBB_SIZE);

    sync_action<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    // Copy the first segment from start_slot to the end of the circular SQ, or the whole batch if it does not wrap.
    asc_copy_ub2gm_align(
        first_wqe_addr, reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer), 1,
        first_wqebb_count * ASCCOMM_URMA_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);

    uint32_t second_wqebb_count = pre_sq_cnt - first_wqebb_count;
    if (second_wqebb_count != 0) {
        // The batch crosses the SQ boundary; copy the remaining WQEBBs to the beginning of the circular SQ.
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(sq_base_addr),
            reinterpret_cast<__ubuf__ uint8_t*>(batch_handle.buffer.raw_buffer) +
                first_wqebb_count * ASCCOMM_URMA_WQEBB_SIZE,
            1, second_wqebb_count * ASCCOMM_URMA_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }

    uint32_t new_sq_head = sq_head + pre_sq_cnt;
    batch_handle.cursor.sq_head = new_sq_head;
    channel_entity->sqHead = new_sq_head;
    channel_entity->cqHead = batch_handle.cursor.cq_head;
    sync_action<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    st_dev(new_sq_head, reinterpret_cast<__gm__ uint32_t*>(batch_handle.sq_context.contextInfo.ubJfs.dbVa), 0);

    batch_handle.cursor.pre_sq_cnt = 0;
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_batch_commit(ubc_multi_batch_handle& batch_handle)
{
    return asccomm_batch_commit(batch_handle.handle);
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_drain(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channel_entity = (__gm__ ChannelEntity*)channel;
    uint32_t ret = poll_cq(channel, channel_entity->cqHead);
    ASCCOMM_DEBUG_TRAP_IF(
        ret != ASCCOMM_SUCCESS, "Hcomm URMA asccomm_drain by channel failed channel=%lu poll_ret=%u \n", channel, ret);
    return ret;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_drain(ubc_batch_handle& batch_handle)
{
    (void)pipe;
    ASCCOMM_DEBUG_TRAP_IF(
        batch_handle.channel_handle == 0U || batch_handle.buffer.buffer_capacity == 0U ||
            batch_handle.cursor.pre_sq_cnt != 0U,
        "Hcomm Batch asccomm_drain failed with invalid batch handle\n");

    uint32_t ret = poll_batch_cq(batch_handle, batch_handle.cursor.cq_head);
    ASCCOMM_DEBUG_TRAP_IF(
        ret != ASCCOMM_SUCCESS, "Hcomm URMA asccomm_drain by batch handle failed poll_ret=%u \n", ret);
    return ret;
}

template <pipe_t pipe>
__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_drain(ubc_multi_batch_handle& batch_handle)
{
    return asccomm_drain<pipe>(batch_handle.handle);
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_lock(ChannelHandle channel)
{
    ASCCOMM_DEBUG_TRAP_IF(
        channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U,
        "Hcomm asccomm_lock failed with invalid channel\n");

    __gm__ ChannelEntity* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint32_t* lock_addr = asccomm_urma_get_lock_addr(channel_entity);
    while (AtomicCas(lock_addr, ASCCOMM_LOCK_FREE, ASCCOMM_LOCK_HELD) != ASCCOMM_LOCK_FREE) {
        // Back off for 800 cycles before retrying to reduce atomic contention.
        Nop<800>();
    }
    return ASCCOMM_SUCCESS;
}

__aicore__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_unlock(ChannelHandle channel)
{
    ASCCOMM_DEBUG_TRAP_IF(
        channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U,
        "Hcomm asccomm_unlock failed with invalid channel\n");

    __gm__ ChannelEntity* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint8_t* counter_addr = reinterpret_cast<__gm__ uint8_t*>(channel_entity) + offsetof(ChannelEntity, sqHead);
    // Flush the four contiguous channel counters: sq_head, sqTail, cqHead, and cq_tail.
    cache_write_through<uint8_t>(counter_addr, 4U * sizeof(uint32_t));

    __gm__ uint32_t* lock_addr = asccomm_urma_get_lock_addr(channel_entity);
    uint32_t old_value = AtomicCas(lock_addr, ASCCOMM_LOCK_HELD, ASCCOMM_LOCK_FREE);
    return old_value == ASCCOMM_LOCK_HELD ? ASCCOMM_SUCCESS : ASCCOMM_FAILED;
}

} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_AIV_URMA_H
#endif
