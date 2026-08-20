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
 * \file asccomm_simt_urma.h
 * \brief asccomm SIMT URMA implementation
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H

#include "hcomm_simt_urma_def.h"

#include "../../common/hcomm_simt_inner_def.h"
#include "../../common/hcomm_simt_utils.h"

namespace AscendC::simt {

// Bit layout of asccomm_urma_sqe_ctx::flag, matching the SIMD path's expression in
// asccomm_urma_fill_sqe_ctx. urma_default_cfg evaluates to 0b00101101.
template <auto const& config>
__simt_callee__ inline uint32_t asccomm_simt_urma_flag()
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
           ((config.cqe & 0x1U) << 5U) | ((config.inline_en & 0x1U) << 6U);
}

// A WQE that occupies a single basic block must not write past it: the next block may
// already hold another lane's WQE.
template <uint32_t byte_size>
__simt_callee__ inline void asccomm_simt_copy_ub_to_gm(__gm__ uint8_t* dst, __ubuf__ uint8_t* src)
{
    static_assert(byte_size % sizeof(uint64_t) == 0U, "UB WQE copy must use whole words");
    static_assert(byte_size <= asccomm_urma_dwqe_size, "UB WQE copy must stay inside the DWQE window");
#pragma unroll
    for (uint32_t i = 0; i < byte_size / sizeof(uint64_t); ++i) {
        asc_stwt(((__gm__ uint64_t*)dst) + i, ((__ubuf__ uint64_t*)src)[i]);
    }
}

template <uint32_t bb_cnt>
__simt_callee__ inline void asccomm_simt_copy_ub_wqe_to_sq(
    uint64_t sq_base_addr, __gm__ uint8_t* sqe_addr, uint32_t sq_depth, uint32_t cur_head, __ubuf__ uint8_t* src)
{
    static_assert(
        bb_cnt == asccomm_urma_wqe_bb_cnt || bb_cnt == asccomm_urma_dwqe_bb_cnt,
        "SIMT SQ copy supports one-BB or two-BB WQEs");
    if constexpr (bb_cnt == asccomm_urma_dwqe_bb_cnt) {
        // A logical 2-BB WQE remains contiguous across the physical SQ boundary.
        if ((cur_head % sq_depth) == sq_depth - 1U) {
            asccomm_simt_copy_ub_to_gm<asccomm_urma_wqe_bb_size>(sqe_addr, src);
            asccomm_simt_copy_ub_to_gm<asccomm_urma_wqe_bb_size>(
                reinterpret_cast<__gm__ uint8_t*>(sq_base_addr), src + asccomm_urma_wqe_bb_size);
            return;
        }
    }
    asccomm_simt_copy_ub_to_gm<bb_cnt * asccomm_urma_wqe_bb_size>(sqe_addr, src);
}

template <bool commit>
__simt_callee__ inline bool asccomm_simt_try_reserve_post(
    const asccomm_simt_ub_resolved_post& post, uint32_t bb_cnt, uint64_t& head_val)
{
    // SIMT has no standalone commit API, so a deferred post must leave enough SQ
    // space for the immediate 2-BB DWQE that will publish the accumulated batch.
    uint32_t required_free_bb_cnt = bb_cnt + (commit ? 0U : asccomm_urma_dwqe_bb_cnt);
    return asccomm_simt_try_reserve(
        post.head_addr, post.sq_tail_addr, post.meta.sq_depth, bb_cnt, required_free_bb_cnt, 1U, head_val);
}

template <asccomm_urma_op_code op_code, uint32_t sge_num, auto const& config>
__simt_callee__ inline void asccomm_simt_urma_fill_ub_sqe_ctx(
    __ubuf__ uint8_t* wqe_addr, __gm__ uint8_t* remote_addr, const asccomm_simt_ub_post_meta& meta, uint32_t cur_head,
    uint32_t next_head)
{
    __ubuf__ asccomm_urma_sqe_ctx* sqe_ctx = (__ubuf__ asccomm_urma_sqe_ctx*)wqe_addr;
    sqe_ctx->sqe_bb_idx = static_cast<uint16_t>(next_head);
    sqe_ctx->opcode = static_cast<uint32_t>(op_code);
    sqe_ctx->flag = asccomm_simt_urma_flag<config>();
    sqe_ctx->token_en = 1U;
    sqe_ctx->rmt_jetty_type = 1U;
    sqe_ctx->owner = (cur_head & meta.sq_depth) == 0U ? 1U : 0U;
    sqe_ctx->tp_id = meta.tp_id;
    sqe_ctx->sge_num = sge_num;
    sqe_ctx->rmt_jetty_or_seg_id = meta.remote_token_id;
    sqe_ctx->rmt_token_value = meta.remote_token_value;

    uint64_t remote_addr_value = reinterpret_cast<uint64_t>(remote_addr);
    sqe_ctx->rmt_addr_l_or_token_id = static_cast<uint32_t>(remote_addr_value & 0xFFFFFFFFU);
    sqe_ctx->rmt_addr_h_or_token_value = static_cast<uint32_t>((remote_addr_value >> 32U) & 0xFFFFFFFFU);
    sqe_ctx->rmt_eid_l = meta.remote_eid_l;
    sqe_ctx->rmt_eid_h = meta.remote_eid_h;
}

template <auto const& config>
__simt_callee__ inline void asccomm_simt_urma_fill_ub_write_with_notify_wqe(
    __ubuf__ uint8_t* wqe_addr, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
    __gm__ uint8_t* notify_addr, uint64_t notify_val, const asccomm_simt_ub_post_meta& meta, uint32_t cur_head,
    uint32_t next_head)
{
    asccomm_simt_urma_fill_ub_sqe_ctx<asccomm_urma_op_code::write_with_notify, 1U, config>(
        wqe_addr, remote_addr, meta, cur_head, next_head);

    __ubuf__ asccomm_urma_notify_ctx* notify_ctx = (__ubuf__ asccomm_urma_notify_ctx*)(wqe_addr + sizeof(asccomm_urma_sqe_ctx));
    uint64_t notify_addr_value = reinterpret_cast<uint64_t>(notify_addr);
    notify_ctx->notify_token_id = meta.remote_token_id & 0xFFFFFU;
    notify_ctx->notify_token_value = meta.remote_token_value;
    notify_ctx->notify_addr_l = static_cast<uint32_t>(notify_addr_value & 0xFFFFFFFFU);
    notify_ctx->notify_addr_h = static_cast<uint32_t>((notify_addr_value >> 32U) & 0xFFFFFFFFU);
    notify_ctx->notify_data_l = static_cast<uint32_t>(notify_val & 0xFFFFFFFFU);
    notify_ctx->notify_data_h = static_cast<uint32_t>((notify_val >> 32U) & 0xFFFFFFFFU);

    __ubuf__ asccomm_urma_sge_ctx* sge_ctx =
        (__ubuf__ asccomm_urma_sge_ctx*)((__ubuf__ uint8_t*)notify_ctx + sizeof(asccomm_urma_notify_ctx));
    sge_ctx->len = static_cast<uint32_t>(len);
    sge_ctx->token_id = 0U;
    sge_ctx->va = reinterpret_cast<uint64_t>(local_addr);
}

// Write/Read carry no notify or AMO tail, so the SGEs directly follow the SQE header.
// sge_num == 2 splits the payload so an immediate post fills the whole 128B DWQE.
template <asccomm_urma_op_code op_code, uint32_t sge_num, auto const& config>
__simt_callee__ inline void asccomm_simt_urma_fill_ub_data_wqe(
    __ubuf__ uint8_t* wqe_addr, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
    const asccomm_simt_ub_post_meta& meta, uint32_t cur_head, uint32_t next_head)
{
    static_assert(sge_num == 1U || sge_num == 2U, "SIMT URMA supports one or two SGEs per WQE");
    asccomm_simt_urma_fill_ub_sqe_ctx<op_code, sge_num, config>(wqe_addr, remote_addr, meta, cur_head, next_head);

    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)(wqe_addr + sizeof(asccomm_urma_sqe_ctx));
    if constexpr (sge_num == 1U) {
        sge_ctx->len = static_cast<uint32_t>(len);
        sge_ctx->token_id = 0U;
        sge_ctx->va = reinterpret_cast<uint64_t>(local_addr);
    } else {
        uint32_t first_len = static_cast<uint32_t>(len >> 1);
        uint32_t last_len = static_cast<uint32_t>(len - first_len);
        __ubuf__ asccomm_urma_sge_ctx* last_sge_ctx = sge_ctx + 1;

        sge_ctx->len = first_len;
        sge_ctx->token_id = 0U;
        sge_ctx->va = reinterpret_cast<uint64_t>(local_addr);

        last_sge_ctx->len = last_len;
        last_sge_ctx->token_id = 0U;
        last_sge_ctx->va = reinterpret_cast<uint64_t>(local_addr + first_len);

        // An immediate data post copies the entire 128-byte DWQE window. The
        // unused tail must not retain fields from an earlier WQE.
#pragma unroll
        for (uint32_t i = 10U; i < asccomm_urma_dwqe_size / sizeof(uint64_t); ++i) {
            reinterpret_cast<__ubuf__ uint64_t*>(wqe_addr)[i] = 0U;
        }
    }
}

template <asccomm_urma_op_code op_code, typename t, auto const& config>
__simt_callee__ inline void asccomm_simt_urma_fill_ub_atomic_wqe(
    __ubuf__ uint8_t* wqe_addr, __gm__ uint8_t* remote_addr, __gm__ uint8_t* fetch_addr, t value, t cond,
    const asccomm_simt_ub_post_meta& meta, uint32_t cur_head, uint32_t next_head)
{
    asccomm_simt_urma_fill_ub_sqe_ctx<op_code, 1U, config>(wqe_addr, remote_addr, meta, cur_head, next_head);

    __ubuf__ asccomm_urma_sge_ctx* sge_ctx = (__ubuf__ asccomm_urma_sge_ctx*)(wqe_addr + sizeof(asccomm_urma_sqe_ctx));
    sge_ctx->len = sizeof(t);
    sge_ctx->token_id = 0U;
    sge_ctx->va = reinterpret_cast<uint64_t>(fetch_addr);

    // Always overwrite 16B: faa/cas and uint32_t/uint64_t can reuse one slot safely.
    __ubuf__ t* amo_data = (__ubuf__ t*)((__ubuf__ uint8_t*)sge_ctx + sizeof(asccomm_urma_sge_ctx));
    amo_data[0] = value;
    amo_data[1] = op_code == asccomm_urma_op_code::cas ? cond : static_cast<t>(0);
    if constexpr (sizeof(t) == sizeof(uint32_t)) {
        amo_data[2] = 0U;
        amo_data[3] = 0U;
    }
}

union alignas(sizeof(uint64_t)) asccomm_simt_local_wqe_stage {
    uint64_t words[asccomm_urma_dwqe_size / sizeof(uint64_t)];
};
static_assert(
    sizeof(asccomm_simt_local_wqe_stage) == asccomm_urma_dwqe_size,
    "SIMT lane-private WQE stage must match the 128-byte DWQE window");

__simt_callee__ inline void asccomm_simt_store_local_wqe(__gm__ uint8_t* dst, const asccomm_simt_local_wqe_stage& stage)
{
#pragma unroll
    for (uint32_t i = 0; i < asccomm_urma_dwqe_size / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[i];
    }
}

__simt_callee__ inline void asccomm_simt_store_local_wqe_bb(
    __gm__ uint8_t* dst, const asccomm_simt_local_wqe_stage& stage, uint32_t src_word_offset)
{
#pragma unroll
    for (uint32_t i = 0; i < asccomm_urma_wqe_bb_size / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[src_word_offset + i];
    }
}

__simt_callee__ inline void asccomm_simt_store_local_wqe_to_sq(
    uint64_t sq_base_addr, __gm__ uint8_t* sqe_addr, uint32_t sq_depth, uint32_t cur_head,
    const asccomm_simt_local_wqe_stage& stage)
{
    if ((cur_head % sq_depth) != sq_depth - 1U) {
        asccomm_simt_store_local_wqe(sqe_addr, stage);
        return;
    }

    constexpr uint32_t words_per_bb = asccomm_urma_wqe_bb_size / sizeof(uint64_t);
    asccomm_simt_store_local_wqe_bb(sqe_addr, stage, 0U);
    asccomm_simt_store_local_wqe_bb(reinterpret_cast<__gm__ uint8_t*>(sq_base_addr), stage, words_per_bb);
}

__simt_callee__ inline void asccomm_simt_store_local_dwqe_stwt16(__gm__ uint8_t* dst, const asccomm_simt_local_wqe_stage& stage)
{
    __gm__ ulonglong2* dst_words = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < asccomm_urma_dwqe_size / sizeof(ulonglong2); ++i) {
        uint32_t word_idx = i * 2U;
        asc_stwt(&dst_words[i], make_ulonglong2(stage.words[word_idx], stage.words[word_idx + 1U]));
    }
}

__simt_callee__ inline void asccomm_simt_copy_ub128_to_dwqe_stwt16(__gm__ uint8_t* dst, __ubuf__ uint8_t* src)
{
    __ubuf__ uint64_t* src_words = reinterpret_cast<__ubuf__ uint64_t*>(src);
    __gm__ ulonglong2* dst_words = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < asccomm_urma_dwqe_size / sizeof(ulonglong2); ++i) {
        uint32_t word_idx = i * 2U;
        asc_stwt(&dst_words[i], make_ulonglong2(src_words[word_idx], src_words[word_idx + 1U]));
    }
}

template <uint32_t sge_num>
__simt_callee__ inline void asccomm_simt_build_packed_sqe_template(
    const asccomm_simt_ub_post_meta& meta, asccomm_simt_packed_sqe_template& sqe_template)
{
    static_assert(sge_num == 1U || sge_num == 2U, "SIMT URMA supports one or two SGEs per WQE");
    sqe_template.word1 = static_cast<uint64_t>(meta.tp_id & 0xFFFFFFU) | (static_cast<uint64_t>(sge_num) << 24U) |
                        (static_cast<uint64_t>(meta.remote_token_id & 0xFFFFFU) << 32U);
    sqe_template.remote_eid_l = meta.remote_eid_l;
    sqe_template.remote_eid_h = meta.remote_eid_h;
    sqe_template.token_word = meta.remote_token_value;
    sqe_template.sq_depth = meta.sq_depth;
}

template <asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline void asccomm_simt_store_local_sqe_header(
    uint64_t* words, __gm__ uint8_t* remote_addr, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head,
    uint32_t next_head)
{
    uint64_t owner = (cur_head & sqe_template.sq_depth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(next_head & 0xFFFFU) |
               (static_cast<uint64_t>(asccomm_simt_urma_flag<config>() & 0xFFU) << 16U) | (1ULL << 28U) | (1ULL << 29U) |
               (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(op_code) & 0xFFU) << 40U);
    words[1] = sqe_template.word1;
    words[2] = sqe_template.remote_eid_l;
    words[3] = sqe_template.remote_eid_h;
    words[4] = sqe_template.token_word;
    words[5] = reinterpret_cast<uint64_t>(remote_addr);
}

template <asccomm_urma_op_code op_code, uint32_t sge_num, auto const& config>
__simt_callee__ inline void asccomm_simt_build_local_data_wqe(
    asccomm_simt_local_wqe_stage& stage, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
    const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head, uint32_t next_head)
{
    static_assert(sge_num == 1U || sge_num == 2U, "SIMT URMA supports one or two SGEs per WQE");
    asccomm_simt_store_local_sqe_header<op_code, config>(stage.words, remote_addr, sqe_template, cur_head, next_head);
    if constexpr (sge_num == 1U) {
        stage.words[6] = static_cast<uint64_t>(static_cast<uint32_t>(len));
        stage.words[7] = reinterpret_cast<uint64_t>(local_addr);
#pragma unroll
        for (uint32_t i = 8U; i < asccomm_urma_dwqe_size / sizeof(uint64_t); ++i) {
            stage.words[i] = 0U;
        }
    } else {
        uint32_t first_len = static_cast<uint32_t>(len >> 1);
        uint32_t last_len = static_cast<uint32_t>(len - first_len);
        stage.words[6] = static_cast<uint64_t>(first_len);
        stage.words[7] = reinterpret_cast<uint64_t>(local_addr);
        stage.words[8] = static_cast<uint64_t>(last_len);
        stage.words[9] = reinterpret_cast<uint64_t>(local_addr + first_len);
#pragma unroll
        for (uint32_t i = 10U; i < asccomm_urma_dwqe_size / sizeof(uint64_t); ++i) {
            stage.words[i] = 0U;
        }
    }
}

__simt_callee__ inline void asccomm_simt_publish_local_wqe(
    uint64_t sq_base_addr, __gm__ uint8_t* sqe_addr, uint32_t sq_depth, uint32_t cur_head, __gm__ uint8_t* dwqe_addr,
    const asccomm_simt_local_wqe_stage& stage)
{
    asccomm_simt_store_local_wqe_to_sq(sq_base_addr, sqe_addr, sq_depth, cur_head, stage);
    asc_threadfence();
    asccomm_simt_store_local_dwqe_stwt16(dwqe_addr, stage);
    asc_dcci_single(dwqe_addr);
    asc_dcci_single(dwqe_addr + asccomm_urma_wqe_bb_size);
}

__simt_callee__ inline void asccomm_simt_ring_dwqe_from_ub_stwt16(__gm__ uint8_t* dwqe_addr, __ubuf__ uint8_t* wqe_addr)
{
    asc_threadfence();
    asccomm_simt_copy_ub128_to_dwqe_stwt16(dwqe_addr, wqe_addr);
    asc_dcci_single(dwqe_addr);
    asc_dcci_single(dwqe_addr + asccomm_urma_wqe_bb_size);
}

__simt_callee__ inline asccomm_impl<comm_protocol_ubc_ctp>::asccomm_impl() {}

__simt_callee__ inline asccomm_impl<comm_protocol_ubc_ctp>::~asccomm_impl() {}

__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    uint32_t linear_lane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    uint64_t lane_count = static_cast<uint64_t>(blockDim.x) * blockDim.y * blockDim.z;
    uint64_t wqe_bytes = lane_count * asccomm_urma_dwqe_size;
    uint64_t cache_required = ((wqe_bytes + lane_count * sizeof(asccomm_simt_ub_remote_cache) + asccomm_urma_dwqe_size - 1U) &
                              ~(static_cast<uint64_t>(asccomm_urma_dwqe_size) - 1U)) +
                             asccomm_simt_ub_post_context_bytes;

    wqe_item_ = nullptr;
    ub_post_context_ = nullptr;
    ub_remote_cache_ = nullptr;
    ub_post_context_ready_ = false;
    if (buff == nullptr || wqe_bytes > len) {
        return asccomm_failed;
    }
    wqe_item_ = buff + linear_lane * asccomm_urma_dwqe_size;

    if (cache_required <= len) {
        __ubuf__ uint8_t* remote_cache_base = buff + wqe_bytes;
        ub_remote_cache_ = reinterpret_cast<__ubuf__ asccomm_simt_ub_remote_cache*>(remote_cache_base) + linear_lane;
        ub_remote_cache_->valid = 0U;

        uint64_t context_offset = (wqe_bytes + lane_count * sizeof(asccomm_simt_ub_remote_cache) + asccomm_urma_dwqe_size - 1U) &
                                 ~(static_cast<uint64_t>(asccomm_urma_dwqe_size) - 1U);
        ub_post_context_ = reinterpret_cast<__ubuf__ asccomm_simt_ub_post_context*>(buff + context_offset);
        if (linear_lane == 0U) {
            ub_post_context_->channel = 0U;
            ub_post_context_->state = asccomm_simt_ub_context_empty;
        }
        asc_syncthreads();
    }
    return asccomm_success;
}

__simt_callee__ inline void asccomm_impl<comm_protocol_ubc_ctp>::init_ub_post_context(channel_handle channel)
{
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    ub_post_context_->remote_buffers_addr = channel_entity_ptr->remote_buffer_addr;
    ub_post_context_->remote_buffer_num = channel_entity_ptr->remote_buffer_num;

    __gm__ sq_context* sq_contexts = reinterpret_cast<__gm__ sq_context*>(channel_entity_ptr->sq_context_addr);
    __gm__ sq_context* sq_ctx = &sq_contexts[asccomm_urma_default_qp_idx];
    ub_post_context_->head_addr = sq_ctx->context_info.ub_jfs.head_addr;
    ub_post_context_->sq_tail_addr = sq_ctx->context_info.ub_jfs.tail_addr;
    ub_post_context_->sq_base_addr = sq_ctx->context_info.ub_jfs.sq_va;
    ub_post_context_->sq_wqe_size = sq_ctx->context_info.ub_jfs.wqe_size;
    ub_post_context_->sq_depth = sq_ctx->context_info.ub_jfs.sq_depth;
    ub_post_context_->tp_id = sq_ctx->context_info.ub_jfs.tp_id;
    ub_post_context_->remote_eid_l = asccomm_simt_load_le64(&sq_ctx->context_info.ub_jfs.remote_eid[0]);
    ub_post_context_->remote_eid_h = asccomm_simt_load_le64(&sq_ctx->context_info.ub_jfs.remote_eid[sizeof(uint64_t)]);
    ub_post_context_->dwqe_addr = sq_ctx->context_info.ub_jfs.db_va - asccomm_urma_dwqe_db_offset;
    ub_post_context_->channel = channel;
}

__simt_callee__ inline bool asccomm_impl<comm_protocol_ubc_ctp>::ensure_ub_post_context(channel_handle channel)
{
    if (ub_post_context_ == nullptr) {
        return false;
    }
    if (ub_post_context_ready_) {
        return ub_post_context_->channel == channel;
    }

    __ubuf__ uint32_t* state_addr = &ub_post_context_->state;
    uint32_t state = asc_atomic_cas(state_addr, asccomm_simt_ub_context_ready, asccomm_simt_ub_context_ready);
    if (state == asccomm_simt_ub_context_ready) {
        ub_post_context_ready_ = ub_post_context_->channel == channel;
        return ub_post_context_ready_;
    }

    uint32_t previous = asc_atomic_cas(state_addr, asccomm_simt_ub_context_empty, asccomm_simt_ub_context_initializing);
    if (previous == asccomm_simt_ub_context_empty) {
        init_ub_post_context(channel);
        asc_threadfence_block();
        (void)asc_atomic_exch(state_addr, asccomm_simt_ub_context_ready);
        ub_post_context_ready_ = true;
        return true;
    }

    // Avoid spinning in a potentially diverged branch; this post can use the GM fallback.
    return false;
}

__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::resolve_ub_post_context(
    channel_handle channel, __gm__ uint8_t* remote_addr, uint64_t len, asccomm_simt_ub_resolved_post& post)
{
    __gm__ reged_buffer_entity* remote_buffers = nullptr;
    uint32_t remote_buffer_num = 0U;
    __gm__ sq_context* sq_ctx = nullptr;
    bool use_ub_post_context = ensure_ub_post_context(channel);
    if (use_ub_post_context) {
        remote_buffers = reinterpret_cast<__gm__ reged_buffer_entity*>(ub_post_context_->remote_buffers_addr);
        remote_buffer_num = ub_post_context_->remote_buffer_num;
    } else {
        __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
        remote_buffers = reinterpret_cast<__gm__ reged_buffer_entity*>(channel_entity_ptr->remote_buffer_addr);
        remote_buffer_num = channel_entity_ptr->remote_buffer_num;
        __gm__ sq_context* sq_contexts = reinterpret_cast<__gm__ sq_context*>(channel_entity_ptr->sq_context_addr);
        sq_ctx = &sq_contexts[asccomm_urma_default_qp_idx];
    }

    uint32_t remote_token_id = 0U;
    uint32_t remote_token_value = 0U;
    bool token_resolved = false;
    uint64_t target_addr = reinterpret_cast<uint64_t>(remote_addr);
    if (use_ub_post_context && ub_remote_cache_ != nullptr && ub_remote_cache_->valid != 0U &&
        target_addr >= ub_remote_cache_->base_addr) {
        uint64_t offset = target_addr - ub_remote_cache_->base_addr;
        if (offset <= ub_remote_cache_->size && len <= ub_remote_cache_->size - offset) {
            remote_token_id = ub_remote_cache_->token_id;
            remote_token_value = ub_remote_cache_->token_value;
            token_resolved = true;
        }
    }

    if (!token_resolved) {
        int32_t remote_idx = asccomm_simt_find_buffer_idx(remote_buffers, remote_buffer_num, remote_addr, len);
        if (remote_idx == asccomm_failed) {
            return asccomm_failed;
        }

        __gm__ reged_buffer_entity* remote_mem_info = &remote_buffers[remote_idx];
        remote_token_id = remote_mem_info->buffer_info.rma.protection_info_value.mem_info.ub.token_id;
        remote_token_value = remote_mem_info->buffer_info.rma.protection_info_value.mem_info.ub.token_value;
        if (use_ub_post_context && ub_remote_cache_ != nullptr) {
            ub_remote_cache_->base_addr = remote_mem_info->buffer_info.rma.addr;
            ub_remote_cache_->size = remote_mem_info->buffer_info.rma.size;
            ub_remote_cache_->token_id = remote_token_id;
            ub_remote_cache_->token_value = remote_token_value;
            ub_remote_cache_->valid = 1U;
        }
    }

    if (use_ub_post_context) {
        post.head_addr = reinterpret_cast<__gm__ uint64_t*>(ub_post_context_->head_addr);
        post.sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(ub_post_context_->sq_tail_addr);
        post.dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(ub_post_context_->dwqe_addr);
        post.sq_base_addr = ub_post_context_->sq_base_addr;
        post.sq_wqe_size = ub_post_context_->sq_wqe_size;
        post.meta.sq_depth = ub_post_context_->sq_depth;
        post.meta.tp_id = ub_post_context_->tp_id;
        post.meta.remote_eid_l = ub_post_context_->remote_eid_l;
        post.meta.remote_eid_h = ub_post_context_->remote_eid_h;
    } else {
        post.head_addr = reinterpret_cast<__gm__ uint64_t*>(sq_ctx->context_info.ub_jfs.head_addr);
        post.sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(sq_ctx->context_info.ub_jfs.tail_addr);
        post.dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(sq_ctx->context_info.ub_jfs.db_va - asccomm_urma_dwqe_db_offset);
        post.sq_base_addr = sq_ctx->context_info.ub_jfs.sq_va;
        post.sq_wqe_size = sq_ctx->context_info.ub_jfs.wqe_size;
        post.meta.sq_depth = sq_ctx->context_info.ub_jfs.sq_depth;
        post.meta.tp_id = sq_ctx->context_info.ub_jfs.tp_id;
        post.meta.remote_eid_l = asccomm_simt_load_le64(&sq_ctx->context_info.ub_jfs.remote_eid[0]);
        post.meta.remote_eid_h = asccomm_simt_load_le64(&sq_ctx->context_info.ub_jfs.remote_eid[sizeof(uint64_t)]);
    }
    post.meta.remote_token_id = remote_token_id;
    post.meta.remote_token_value = remote_token_value;
    return asccomm_success;
}

template <bool commit>
__simt_callee__ inline bool asccomm_impl<comm_protocol_ubc_ctp>::reserve_post(
    channel_handle channel, const asccomm_simt_ub_resolved_post& post, uint32_t bb_cnt, uint64_t& head_val)
{
    uint32_t required_free_bb_cnt = bb_cnt + (commit ? 0U : asccomm_urma_dwqe_bb_cnt);
    if (post.meta.sq_depth == 0U || required_free_bb_cnt > post.meta.sq_depth) {
        return false;
    }

    if (asccomm_simt_try_reserve_post<commit>(post, bb_cnt, head_val)) {
        return true;
    }

    // The fast path found no SQ space. Consume one completed CQE at a time so
    // poll_cq can advance sq_tail, then retry the reservation. Waiting for all
    // submitted WQEs is unsafe because wqe_cnt may include deferred WQEs that a
    // later immediate DWQE has not published yet.
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ cq_context* cq_contexts = reinterpret_cast<__gm__ cq_context*>(channel_entity_ptr->cq_context_addr);
    __gm__ cq_context* cq_ctx = &cq_contexts[asccomm_urma_default_qp_idx];
    __gm__ uint32_t* cq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(cq_ctx->context_info.ub_jfc.tail_addr);
    while (true) {
        uint32_t cq_tail = *cq_tail_addr;
        if (cq_tail == asccomm_simt_wqe_cnt(head_val) || poll_cq(channel, cq_tail + 1U) != asccomm_success) {
            return false;
        }
        if (asccomm_simt_try_reserve_post<commit>(post, bb_cnt, head_val)) {
            return true;
        }
    }
}

template <bool commit, asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::post_send(
    channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len)
{
    if (wqe_item_ == nullptr) {
        return asccomm_failed;
    }

    asccomm_simt_ub_resolved_post post;
    if (resolve_ub_post_context(channel, remote_addr, len, post) != asccomm_success) {
        return asccomm_failed;
    }
    // A deferred Write/Read needs a single SGE in one BB. An immediate one is published
    // through the 128B DWQE window, so the payload is split across two SGEs to fill it.
    constexpr uint32_t sge_num = commit ? 2U : 1U;
    constexpr uint32_t bb_cnt = commit ? asccomm_urma_dwqe_bb_cnt : asccomm_urma_wqe_bb_cnt;

    uint64_t head_val = 0U;
    if (!reserve_post<commit>(channel, post, bb_cnt, head_val)) {
        return asccomm_failed;
    }
    uint32_t cur_head = asccomm_simt_head_idx(head_val);
    uint32_t next_head = cur_head + bb_cnt;

    uint64_t sq_base_addr = post.sq_base_addr;
    uint32_t wqe_size = post.sq_wqe_size;
    uint32_t sq_depth = post.meta.sq_depth;
    __gm__ uint8_t* sqe_addr = reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + wqe_size * (cur_head % sq_depth));

    asccomm_simt_urma_fill_ub_data_wqe<op_code, sge_num, config>(
        wqe_item_, remote_addr, local_addr, len, post.meta, cur_head, next_head);
    asccomm_simt_copy_ub_wqe_to_sq<bb_cnt>(sq_base_addr, sqe_addr, sq_depth, cur_head, wqe_item_);
    if constexpr (commit) {
        asccomm_simt_ring_dwqe_from_ub_stwt16(post.dwqe_addr, wqe_item_);
    }
    asc_threadfence();
    return asccomm_success;
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::post_write_with_notify(
    channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
    __gm__ uint8_t* notify_addr, uint64_t notify_val)
{
    if (wqe_item_ == nullptr) {
        return asccomm_failed;
    }

    asccomm_simt_ub_resolved_post post;
    if (resolve_ub_post_context(channel, remote_addr, len, post) != asccomm_success) {
        return asccomm_failed;
    }
    uint64_t head_val = 0U;
    if (!reserve_post<commit>(channel, post, asccomm_urma_dwqe_bb_cnt, head_val)) {
        return asccomm_failed;
    }
    uint32_t cur_head = asccomm_simt_head_idx(head_val);
    uint32_t next_head = cur_head + asccomm_urma_dwqe_bb_cnt;

    uint64_t sq_base_addr = post.sq_base_addr;
    uint32_t wqe_size = post.sq_wqe_size;
    uint32_t sq_depth = post.meta.sq_depth;
    __gm__ uint8_t* sqe_addr = reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + wqe_size * (cur_head % sq_depth));
    asccomm_simt_urma_fill_ub_write_with_notify_wqe<config>(
        wqe_item_, remote_addr, local_addr, len, notify_addr, notify_val, post.meta, cur_head, next_head);
    asccomm_simt_copy_ub_wqe_to_sq<asccomm_urma_dwqe_bb_cnt>(sq_base_addr, sqe_addr, sq_depth, cur_head, wqe_item_);
    if constexpr (commit) {
        asccomm_simt_ring_dwqe_from_ub_stwt16(post.dwqe_addr, wqe_item_);
    }
    asc_threadfence();
    return asccomm_success;
}

template <typename t, bool commit, asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::post_atomic(
    channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* fetch_addr, t value, t cond)
{
    static_assert(
        sizeof(t) == sizeof(uint32_t) || sizeof(t) == sizeof(uint64_t),
        "Atomic operation only supports 32-bit or 64-bit data types");
    if (wqe_item_ == nullptr) {
        return asccomm_failed;
    }
    asccomm_simt_ub_resolved_post post;
    if (resolve_ub_post_context(channel, remote_addr, sizeof(t), post) != asccomm_success) {
        return asccomm_failed;
    }
    uint64_t head_val = 0U;
    if (!reserve_post<commit>(channel, post, asccomm_urma_dwqe_bb_cnt, head_val)) {
        return asccomm_failed;
    }
    uint32_t cur_head = asccomm_simt_head_idx(head_val);
    uint32_t next_head = cur_head + asccomm_urma_dwqe_bb_cnt;

    uint64_t sq_base_addr = post.sq_base_addr;
    uint32_t wqe_size = post.sq_wqe_size;
    uint32_t sq_depth = post.meta.sq_depth;
    __gm__ uint8_t* sqe_addr = reinterpret_cast<__gm__ uint8_t*>(sq_base_addr + wqe_size * (cur_head % sq_depth));
    // Keep the complete Atomic WQE in the explicit per-lane UB slot. Publishing a
    // lane-local C++ object with ordinary stores can lose the operand words even
    // though the WQE header and fetch SGE are consumed successfully.
    asccomm_simt_urma_fill_ub_atomic_wqe<op_code, t, config>(
        wqe_item_, remote_addr, fetch_addr, value, cond, post.meta, cur_head, next_head);
    asccomm_simt_copy_ub_wqe_to_sq<asccomm_urma_dwqe_bb_cnt>(sq_base_addr, sqe_addr, sq_depth, cur_head, wqe_item_);
    if constexpr (commit) {
        asccomm_simt_ring_dwqe_from_ub_stwt16(post.dwqe_addr, wqe_item_);
    }
    asc_threadfence();
    return asccomm_success;
}

__simt_callee__ inline uint32_t asccomm_impl<comm_protocol_ubc_ctp>::poll_cq(channel_handle channel, uint32_t expect_idx)
{
    if (expect_idx == 0U) {
        return asccomm_success;
    }
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ cq_context* cq_contexts = reinterpret_cast<__gm__ cq_context*>(channel_entity_ptr->cq_context_addr);
    __gm__ cq_context* cq_ctx = &cq_contexts[asccomm_urma_default_qp_idx];
    __gm__ uint32_t* tail_addr = (__gm__ uint32_t*)cq_ctx->context_info.ub_jfc.tail_addr;
    uint32_t cur_tail = *tail_addr;

    __gm__ sq_context* sq_contexts = reinterpret_cast<__gm__ sq_context*>(channel_entity_ptr->sq_context_addr);
    __gm__ sq_context* sq_ctx = &sq_contexts[asccomm_urma_default_qp_idx];
    __gm__ uint32_t* sq_tail_addr = (__gm__ uint32_t*)sq_ctx->context_info.ub_jfs.tail_addr;
    uint32_t sq_tail = *sq_tail_addr;
    uint32_t sq_depth = sq_ctx->context_info.ub_jfs.sq_depth;

    uint64_t cq_base_addr = cq_ctx->context_info.ub_jfc.scq_va;
    uint32_t cqe_size = cq_ctx->context_info.ub_jfc.cqe_size;
    uint32_t cq_depth = cq_ctx->context_info.ub_jfc.cq_depth;
    while (cur_tail != expect_idx) {
        __gm__ asccomm_urma_jfc_cqe_ctx* cqe_addr = (__gm__ asccomm_urma_jfc_cqe_ctx*)(cq_base_addr + cqe_size * (cur_tail % cq_depth));
        uint32_t valid_owner = (cur_tail / cq_depth) & 1U;
        uint32_t retry = 0U;
        while (((valid_owner ^ cqe_addr->owner) == 0U) && retry < asccomm_simt_max_cq_retry) {
            asc_threadfence();
            asc_dcci_single(cqe_addr);
            ++retry;
        }
        if (retry >= asccomm_simt_max_cq_retry) {
            // No CQE arrived: the WQE was never consumed.
            return 0xFFU;
        }
        uint8_t status = cqe_addr->status & 0xFFU;
        uint8_t sub_status = cqe_addr->substatus & 0xFFU;
        if (status != 0U || sub_status != 0U) {
            constexpr uint8_t status_shift = 8U;
            return (static_cast<uint32_t>(status) << status_shift) | sub_status;
        }
        if (sq_depth == 0U) {
            return 0xFFU;
        }
        uint32_t sq_tail_slot = sq_tail % sq_depth;
        uint32_t completed_slot = cqe_addr->entry_idx % sq_depth;
        sq_tail += (completed_slot + sq_depth - sq_tail_slot) % sq_depth + 1U;
        ++cur_tail;
    }

    *tail_addr = cur_tail;
    __gm__ uint32_t* cq_db_addr = (__gm__ uint32_t*)cq_ctx->context_info.ub_jfc.db_va;
    *cq_db_addr = cur_tail & 0xFFFFFFU;

    *sq_tail_addr = sq_tail;
    return asccomm_success;
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return post_send<commit, asccomm_urma_op_code::write, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len);
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::read_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return post_send<commit, asccomm_urma_op_code::read, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len);
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::write_with_notify_nbi(
    channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return post_write_with_notify<commit, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
        reinterpret_cast<__gm__ uint8_t*>(notify_addr), notify_val);
}

template <typename t, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::atomic_faa(
    channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t add_val)
{
    return post_atomic<t, commit, asccomm_urma_op_code::faa, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), add_val,
        static_cast<t>(0));
}

template <typename t, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::atomic_cas(
    channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t compare_val, t swap_val)
{
    return post_atomic<t, commit, asccomm_urma_op_code::cas, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), swap_val,
        compare_val);
}

template <auto pipe>
__simt_callee__ inline int32_t asccomm_impl<comm_protocol_ubc_ctp>::drain(channel_handle channel)
{
    (void)pipe;
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ sq_context* sq_contexts = reinterpret_cast<__gm__ sq_context*>(channel_entity_ptr->sq_context_addr);
    __gm__ sq_context* sq_ctx = &sq_contexts[asccomm_urma_default_qp_idx];
    __gm__ uint64_t* head_addr = (__gm__ uint64_t*)sq_ctx->context_info.ub_jfs.head_addr;

    // wqe_cnt counts only CQE-producing WQEs, so it is directly comparable with the CQ tail.
    uint64_t head_val = *head_addr;
    uint32_t wqe_cnt = asccomm_simt_wqe_cnt(head_val);
    uint32_t poll_ret = poll_cq(channel, wqe_cnt);
    return poll_ret == asccomm_success ? asccomm_success : asccomm_failed;
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif
