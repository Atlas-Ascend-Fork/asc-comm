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
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_H

#include "asccomm_simt_urma_def.h"

#include "../../common/asccomm_simt_inner_def.h"
#include "../../common/asccomm_simt_utils.h"

namespace asc::simt {

// Bit layout of asccomm_urma_sqe_ctx::flag, matching the SIMD path's expression in
// asccomm_urma_fill_sqe_ctx.
template <auto const& config>
__simt_callee__ inline uint32_t asccomm_simt_urma_flag()
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
           ((config.cqe & 0x1U) << 5U) | ((asccomm_config_inline_en(config) & 0x1U) << 6U);
}

// Every post stages its WQE here, in lane-private storage that lives only for the duration of
// the call. The window is always a full DWQE (two basic blocks) so that a committed post can
// ring the doorbell straight out of it; a deferred one-BB post simply leaves the upper half
// untouched and never copies it.
struct alignas(sizeof(ulonglong2)) asccomm_simt_wqe_stage {
    uint64_t words[ASCCOMM_URMA_DWQE_SIZE / sizeof(uint64_t)];
};
static_assert(
    sizeof(asccomm_simt_wqe_stage) == ASCCOMM_URMA_DWQE_SIZE, "The WQE stage must match the 128-byte DWQE window");

// A WQE that occupies a single basic block must not write past it: the next block may
// already hold another lane's WQE.
template <uint32_t byte_size>
__simt_callee__ inline void asccomm_simt_copy_stage_to_sq(__gm__ uint8_t* dst, const asccomm_simt_wqe_stage& stage)
{
    static_assert(byte_size % sizeof(uint64_t) == 0U, "WQE copy must use whole words");
    static_assert(byte_size <= ASCCOMM_URMA_DWQE_SIZE, "WQE copy must stay inside the DWQE window");

    __gm__ ulonglong2* dst_words = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < byte_size / sizeof(ulonglong2); ++i) {
        uint32_t word_idx = i * 2U;
        asc_stwt(&dst_words[i], make_ulonglong2(stage.words[word_idx], stage.words[word_idx + 1U]));
    }
}

static_assert(sizeof(asccomm_urma_sqe_ctx) == 48U, "Packed SIMT SQE assumes a 48-byte SQE context");
static_assert(sizeof(asccomm_urma_notify_ctx) == 32U, "Packed SIMT Notify assumes a 32-byte notify context");
static_assert(sizeof(asccomm_urma_sge_ctx) == 16U, "Packed SIMT SQE assumes a 16-byte SGE context");

// Word offsets of the URMA WQE areas. Every layout below writes whole 64-bit words rather
// than struct bit-fields, so these offsets are the single description of the WQE layout;
// the structs above only pin down the sizes they are derived from.
constexpr uint32_t ASCCOMM_SIMT_WQE_WORDS = ASCCOMM_URMA_DWQE_SIZE / sizeof(uint64_t);
constexpr uint32_t ASCCOMM_SIMT_BB_WORDS = ASCCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t);
constexpr uint32_t ASCCOMM_SIMT_SQE_WORDS = sizeof(asccomm_urma_sqe_ctx) / sizeof(uint64_t);
constexpr uint32_t ASCCOMM_SIMT_NOTIFY_WORDS = sizeof(asccomm_urma_notify_ctx) / sizeof(uint64_t);
constexpr uint32_t ASCCOMM_SIMT_SGE_WORDS = sizeof(asccomm_urma_sge_ctx) / sizeof(uint64_t);
// The inline payload and the first SGE both start right after the SQE header.
constexpr uint32_t ASCCOMM_SIMT_INLINE_FIRST_WORD = ASCCOMM_SIMT_SQE_WORDS;

// Static queue/Jetty fields and the exclusive producer cursor are shared by all lanes. The
// second 128-byte workspace block holds the final publisher WQE used to ring DWQE.
struct alignas(sizeof(ulonglong2)) asccomm_simt_batch_static_context {
    uint64_t head_addr;
    uint64_t sq_tail_addr;
    uint64_t dwqe_addr;
    uint64_t sq_base_addr;
    uint64_t word1Base;
    uint64_t remote_eid_l;
    uint64_t remote_eid_h;
    uint64_t token_word;
    uint64_t reserved_head;
    uint32_t sq_wqe_size;
    uint32_t sq_depth;
    uint32_t cached_sq_tail;
    uint32_t item_bb; // WQE width in basic blocks, re-declared by each appended request; 0 until the first append.
    uint32_t group_size;
    int32_t reservation_status;
};
static_assert(
    sizeof(asccomm_simt_batch_static_context) <= ASCCOMM_URMA_DWQE_SIZE,
    "The shared SIMT batch context must fit in the public UB workspace");

// Copies a staged WQE into its reserved SQ slot. A 2-BB WQE landing on the last slot of the ring
// is split: the first basic block goes to the slot, the second wraps to the base of the SQ. The
// WQE stays logically contiguous for the NIC, which reads the ring modulo its depth, but the
// bytes must not run off the end of the SQ allocation.
template <uint32_t bb_cnt>
__simt_callee__ inline void asccomm_simt_copy_stage_wqe_to_sq(
    uint64_t sq_base_addr, __gm__ uint8_t* sqe_addr, uint32_t sq_depth, uint32_t cur_head,
    const asccomm_simt_wqe_stage& stage)
{
    static_assert(
        bb_cnt == ASCCOMM_URMA_WQE_BB_CNT || bb_cnt == ASCCOMM_URMA_DWQE_BB_CNT,
        "SIMT SQ copy supports one-BB or two-BB WQEs");
    if constexpr (bb_cnt == ASCCOMM_URMA_DWQE_BB_CNT) {
        if ((cur_head % sq_depth) == sq_depth - 1U) {
            asccomm_simt_wqe_stage second_bb;
#pragma unroll
            for (uint32_t i = 0; i < ASCCOMM_SIMT_BB_WORDS; ++i) {
                second_bb.words[i] = stage.words[ASCCOMM_SIMT_BB_WORDS + i];
            }
            asccomm_simt_copy_stage_to_sq<ASCCOMM_URMA_WQE_BB_SIZE>(sqe_addr, stage);
            asccomm_simt_copy_stage_to_sq<ASCCOMM_URMA_WQE_BB_SIZE>(
                reinterpret_cast<__gm__ uint8_t*>(sq_base_addr), second_bb);
            return;
        }
    }
    asccomm_simt_copy_stage_to_sq<bb_cnt * ASCCOMM_URMA_WQE_BB_SIZE>(sqe_addr, stage);
}

// Words a layout leaves unused are not cleared: the NIC reads only as far as the fields the
// SQE header describes, so stale bytes from an earlier post on the same slot are never consumed.
// Each writer below instead asserts that its own fields fit the window.
template <uint32_t used_words, uint32_t word_cnt>
__simt_callee__ inline constexpr void asccomm_simt_assert_wqe_fits()
{
    static_assert(used_words <= word_cnt, "WQE layout must not overflow its basic-block window");
}

// The SQE header, identical for every opcode. Bit positions match asccomm_urma_sqe_ctx:
// word 0 holds sqe_bb_idx/flag/token_en/rmt_jetty_type/owner/opcode/inline_msg_len, word 1 holds
// tp_id/sge_num/rmt_jetty_or_seg_id, words 2..3 the remote EID, word 4 the token value and
// word 5 the 64-bit remote address. inline_msg_len stays 0 unless the payload rides inline.
template <asccomm_urma_op_code op_code, uint32_t sge_num, auto const& config, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_sqe_header(
    word_ptr words, __gm__ uint8_t* remote_addr, const asccomm_simt_packed_sqe_template& sqe_template,
    uint32_t cur_head, uint32_t next_head, uint32_t inline_msg_len = 0U)
{
    static_assert(sge_num <= 2U, "SIMT URMA supports at most two SGEs per WQE");
    // The owner bit flips each lap of the ring so the NIC can tell a freshly written slot from
    // the previous lap's. `head & sq_depth` isolates the lap parity only because sq_depth is a power
    // of two -- it is `(head / sq_depth) & 1` -- and a non-power-of-two depth would silently yield
    // the wrong owner, letting the NIC consume stale WQEs. poll_cq masks on the same assumption.
    uint64_t owner = (cur_head & sqe_template.sq_depth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(next_head & 0xFFFFU) |
               (static_cast<uint64_t>(asccomm_simt_urma_flag<config>() & 0xFFU) << 16U) | (1ULL << 28U) |
               (1ULL << 29U) | (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(op_code) & 0xFFU) << 40U) |
               (static_cast<uint64_t>(inline_msg_len & 0x3FFU) << 54U);
    words[1] = sqe_template.word1;
    words[2] = sqe_template.remote_eid_l;
    words[3] = sqe_template.remote_eid_h;
    words[4] = sqe_template.token_word;
    words[5] = reinterpret_cast<uint64_t>(remote_addr);
}

// One SGE: the 32-bit length in the low half of the first word (tokenId, the high half,
// stays 0) and the local address in the second.
template <uint32_t first_word, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_sge(word_ptr words, __gm__ uint8_t* local_addr, uint32_t len)
{
    words[first_word] = static_cast<uint64_t>(len);
    words[first_word + 1U] = reinterpret_cast<uint64_t>(local_addr);
}

// Write/Read. sge_num == 2 splits the payload so an immediate post fills the whole 128B
// DWQE window; sge_num == 1 keeps the WQE inside one basic block, which is what a deferred
// Write/Read needs because the next block may already hold another lane's WQE.
template <asccomm_urma_op_code op_code, uint32_t sge_num, uint32_t word_cnt, auto const& config, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_data_wqe(
    word_ptr words, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
    const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head, uint32_t next_head)
{
    static_assert(sge_num == 1U || sge_num == 2U, "SIMT URMA supports one or two SGEs per WQE");
    asccomm_simt_write_sqe_header<op_code, sge_num, config>(words, remote_addr, sqe_template, cur_head, next_head);
    if constexpr (sge_num == 1U) {
        asccomm_simt_write_sge<ASCCOMM_SIMT_SQE_WORDS>(words, local_addr, static_cast<uint32_t>(len));
        asccomm_simt_assert_wqe_fits<ASCCOMM_SIMT_SQE_WORDS + ASCCOMM_SIMT_SGE_WORDS, word_cnt>();
    } else {
        uint32_t first_len = static_cast<uint32_t>(len >> 1);
        uint32_t last_len = static_cast<uint32_t>(len - first_len);
        asccomm_simt_write_sge<ASCCOMM_SIMT_SQE_WORDS>(words, local_addr, first_len);
        asccomm_simt_write_sge<ASCCOMM_SIMT_SQE_WORDS + ASCCOMM_SIMT_SGE_WORDS>(
            words, local_addr + first_len, last_len);
        asccomm_simt_assert_wqe_fits<ASCCOMM_SIMT_SQE_WORDS + 2U * ASCCOMM_SIMT_SGE_WORDS, word_cnt>();
    }
}

// Write-with-notify: a 32-byte notify context between the SQE header and the single SGE.
// The notify token repeats the remote token, so both fields come from the SQE template.
template <uint32_t word_cnt, auto const& config, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_notify_wqe(
    word_ptr words, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len, __gm__ uint8_t* notify_addr,
    uint64_t notify_val, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head, uint32_t next_head)
{
    constexpr uint32_t notify_word = ASCCOMM_SIMT_SQE_WORDS;
    constexpr uint32_t sge_word = notify_word + ASCCOMM_SIMT_NOTIFY_WORDS;
    asccomm_simt_write_sqe_header<asccomm_urma_op_code::WRITE_WITH_NOTIFY, 1U, config>(
        words, remote_addr, sqe_template, cur_head, next_head);
    words[notify_word] = static_cast<uint64_t>((sqe_template.word1 >> 32U) & 0xFFFFFU) |
                         ((sqe_template.token_word & 0xFFFFFFFFULL) << 32U);
    words[notify_word + 1U] = reinterpret_cast<uint64_t>(notify_addr);
    words[notify_word + 2U] = notify_val;
    words[notify_word + 3U] = 0U;
    asccomm_simt_write_sge<sge_word>(words, local_addr, static_cast<uint32_t>(len));
    asccomm_simt_assert_wqe_fits<sge_word + ASCCOMM_SIMT_SGE_WORDS, word_cnt>();
}

// Writes a NOP WQE into the second basic block (words 8..15) of a 2BB staging buffer. A NOP
// occupies one BB and produces no CQE; it pads the DWQE window when a 1BB inline Write needs to be
// published through the doorbell.
template <uint32_t word_cnt, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_nop_wqe(
    word_ptr words, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t next_head)
{
    static_assert(word_cnt == ASCCOMM_SIMT_WQE_WORDS, "NOP padding requires a 2BB staging buffer");
    constexpr uint32_t nop_first_word = ASCCOMM_SIMT_BB_WORDS; // Second BB starts at word 8

    uint64_t owner = (next_head & sqe_template.sq_depth) == 0U ? 1U : 0U;
    words[nop_first_word] = static_cast<uint64_t>((next_head + 1U) & 0xFFFFU) | (1ULL << 28U) | (1ULL << 29U) |
                            (owner << 31U) |
                            (static_cast<uint64_t>(static_cast<uint32_t>(asccomm_urma_op_code::NOP)) << 40U);
}

// Packs the inline payload into the whole 64-bit words that follow the SQE header. A type
// shorter than a word occupies the low bytes of the first payload word, because the SQE
// header is a whole number of words long.
//
// When commit is true the WQE spans two basic blocks: the inline Write, then a NOP padding the
// DWQE window to 128B, because a 1BB WQE cannot be published through the doorbell on its own.
template <typename T, uint32_t word_cnt, auto const& config, bool commit, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_inline_wqe(
    word_ptr words, __gm__ uint8_t* remote_addr, T value, const asccomm_simt_packed_sqe_template& sqe_template,
    uint32_t cur_head, uint32_t next_head)
{
    constexpr uint32_t payload_words = (sizeof(T) + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    asccomm_simt_assert_wqe_fits<ASCCOMM_SIMT_INLINE_FIRST_WORD + payload_words, word_cnt>();
    asccomm_simt_write_sqe_header<asccomm_urma_op_code::WRITE, 0U, config>(
        words, remote_addr, sqe_template, cur_head, next_head, sizeof(T));

    // Each payload word is assembled in a register and then assigned, so this does not depend
    // on the target words having been zeroed first. A type shorter than the payload area leaves
    // the bytes above it zero, because the accumulator starts at zero.
    const uint8_t* value_addr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
    for (uint32_t word = 0; word < payload_words; ++word) {
        uint64_t packed = 0U;
#pragma unroll
        for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
            uint32_t idx = word * sizeof(uint64_t) + byte;
            if (idx < sizeof(T)) {
                packed |= static_cast<uint64_t>(value_addr[idx]) << (byte * 8U);
            }
        }
        words[ASCCOMM_SIMT_INLINE_FIRST_WORD + word] = packed;
    }

    if constexpr (commit) {
        asccomm_simt_write_nop_wqe<word_cnt>(words, sqe_template, next_head);
    }
}

// FAA/CAS: one SGE pointing at the local fetch buffer, then the 16-byte AMO operand area.
// A 32-bit type packs both operands into one word; CAS puts the compare value second.
template <asccomm_urma_op_code op_code, typename T, uint32_t word_cnt, auto const& config, typename word_ptr>
__simt_callee__ inline void asccomm_simt_write_atomic_wqe(
    word_ptr words, __gm__ uint8_t* remote_addr, __gm__ uint8_t* fetch_addr, T value, T cond,
    const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head, uint32_t next_head)
{
    static_assert(
        sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t),
        "Atomic operation only supports 32-bit or 64-bit data types");
    constexpr uint32_t amo_word = ASCCOMM_SIMT_SQE_WORDS + ASCCOMM_SIMT_SGE_WORDS;
    asccomm_simt_write_sqe_header<op_code, 1U, config>(words, remote_addr, sqe_template, cur_head, next_head);
    asccomm_simt_write_sge<ASCCOMM_SIMT_SQE_WORDS>(words, fetch_addr, sizeof(T));
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        words[amo_word] = static_cast<uint64_t>(static_cast<uint32_t>(value)) |
                          (static_cast<uint64_t>(
                               static_cast<uint32_t>(op_code == asccomm_urma_op_code::CAS ? cond : static_cast<T>(0)))
                           << 32U);
        words[amo_word + 1U] = 0U;
    } else {
        words[amo_word] = static_cast<uint64_t>(value);
        words[amo_word + 1U] = op_code == asccomm_urma_op_code::CAS ? static_cast<uint64_t>(cond) : 0U;
    }
    asccomm_simt_assert_wqe_fits<amo_word + 2U, word_cnt>();
}

// The doorbell window must be written with 128-bit stores, and the leading header words must
// land last: the NIC can start consuming the window once the header is in place (with plain
// 64-bit stores it has been observed to consume the header and the fetch SGE while losing the
// atomic operand words behind them), so the payload goes first, mirroring the header-last
// publish order of asccomm_simt_ring_batch_dwqe.
__simt_callee__ inline void asccomm_simt_copy_stage_to_dwqe(__gm__ uint8_t* dst, const asccomm_simt_wqe_stage& stage)
{
    __gm__ ulonglong2* dst_words = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 1; i < ASCCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        uint32_t word_idx = i * 2U;
        asc_stwt(&dst_words[i], make_ulonglong2(stage.words[word_idx], stage.words[word_idx + 1U]));
    }
    asc_stwt(&dst_words[0], make_ulonglong2(stage.words[0], stage.words[1U]));
}

template <uint32_t sge_num>
__simt_callee__ inline void asccomm_simt_build_packed_sqe_template(
    const asccomm_simt_post_meta& meta, asccomm_simt_packed_sqe_template& sqe_template)
{
    // sge_num == 0 is the inline case: the payload rides in the WQE, so there is no SGE.
    static_assert(sge_num <= 2U, "SIMT URMA supports at most two SGEs per WQE");
    sqe_template.word1 = static_cast<uint64_t>(meta.tp_id & 0xFFFFFFU) | (static_cast<uint64_t>(sge_num) << 24U) |
                         (static_cast<uint64_t>(meta.remote_token_id & 0xFFFFFU) << 32U);
    sqe_template.remote_eid_l = meta.remote_eid_l;
    sqe_template.remote_eid_h = meta.remote_eid_h;
    sqe_template.token_word = meta.remote_token_value;
    sqe_template.sq_depth = meta.sq_depth;
}

__simt_callee__ inline void asccomm_simt_load_batch_static_post(
    __ubuf__ asccomm_simt_batch_static_context* context, asccomm_simt_resolved_post& post)
{
    post.head_addr = reinterpret_cast<__gm__ uint64_t*>(context->head_addr);
    post.sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(context->sq_tail_addr);
    post.dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(context->dwqe_addr);
    post.sq_base_addr = context->sq_base_addr;
    post.sq_wqe_size = context->sq_wqe_size;
    post.meta.sq_depth = context->sq_depth;
}

__simt_callee__ inline __ubuf__ uint64_t* asccomm_simt_batch_publisher_words(
    __ubuf__ asccomm_simt_batch_static_context* context)
{
    return reinterpret_cast<__ubuf__ uint64_t*>(
        reinterpret_cast<__ubuf__ uint8_t*>(context) + ASCCOMM_SIMT_BATCH_CONTEXT_BYTES);
}

template <asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline uint64_t asccomm_simt_batch_sqe_word0(
    const __ubuf__ asccomm_simt_batch_static_context* context, uint32_t cur_head, uint32_t next_head,
    uint32_t inline_msg_len = 0U)
{
    const uint64_t owner = (cur_head & context->sq_depth) == 0U ? 1U : 0U;
    return static_cast<uint64_t>(next_head & 0xFFFFU) |
           (static_cast<uint64_t>(asccomm_simt_urma_flag<config>() & 0xFFU) << 16U) | (1ULL << 28U) | (1ULL << 29U) |
           (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(op_code) & 0xFFU) << 40U) |
           (static_cast<uint64_t>(inline_msg_len & 0x3FFU) << 54U);
}

// Batch Emit constructs each final 32-byte SQ vector directly. Publisher vectors are mirrored
// into the shared UB image as they are produced, avoiding a lane-local 128-byte staging array and
// a second pass over it. Only the first three vectors can contain valid fields in the supported
// layouts; the unused final 32 bytes of the DWQE image stay zero after handle creation.
template <bool publisher>
__simt_callee__ inline void asccomm_simt_store_batch_vector(
    __ubuf__ asccomm_simt_batch_static_context* context, __gm__ uint8_t* first_bb, __gm__ uint8_t* second_bb,
    uint32_t vector_idx, uint64_t word0, uint64_t word1, uint64_t word2, uint64_t word3)
{
    __gm__ uint8_t* vector_addr = vector_idx < 2U ? first_bb + vector_idx * sizeof(ulonglong4) : second_bb;
    asc_stwt(reinterpret_cast<__gm__ ulonglong4*>(vector_addr), make_ulonglong4(word0, word1, word2, word3));
    if constexpr (publisher) {
        __ubuf__ uint64_t* publisher_words = asccomm_simt_batch_publisher_words(context) + vector_idx * 4U;
        publisher_words[0] = word0;
        publisher_words[1] = word1;
        publisher_words[2] = word2;
        publisher_words[3] = word3;
    }
}

__simt_callee__ inline uint32_t asccomm_simt_batch_total_bb(uint32_t group_size, uint32_t item_bb)
{
    return group_size * item_bb + (item_bb == ASCCOMM_URMA_WQE_BB_CNT ? ASCCOMM_URMA_WQE_BB_CNT : 0U);
}

// Advance the private producer cursor without publishing it. The cached tail is sufficient on the
// normal path; it is refreshed from GM only when the cached free range has been exhausted.
// item_bb overrides context->item_bb for this planning pass without storing it, so make_batch_handle can
// pre-check capacity with the widest request layout before the first request fixes the actual width.
__simt_callee__ inline bool asccomm_simt_plan_batch_from_head(
    __ubuf__ asccomm_simt_batch_static_context* context, uint64_t head_val, uint32_t item_bb = 0U)
{
    const uint32_t planned_item_bb = item_bb == 0U ? context->item_bb : item_bb;
    if (planned_item_bb == 0U) {
        // A zero width means no request has fixed the WQE size yet; a zero-width plan would pass every
        // capacity check, so fail the reservation instead.
        context->reservation_status = ASCCOMM_FAILED;
        return false;
    }
    const uint32_t total_bb = asccomm_simt_batch_total_bb(context->group_size, planned_item_bb);
    const uint32_t head = asccomm_simt_head_idx(head_val);
    uint32_t used_bb = head - context->cached_sq_tail;
    if (used_bb > context->sq_depth || total_bb > context->sq_depth - used_bb) {
        asc_dcci_single(reinterpret_cast<__gm__ void*>(context->sq_tail_addr));
        context->cached_sq_tail = *reinterpret_cast<__gm__ uint32_t*>(context->sq_tail_addr);
        used_bb = head - context->cached_sq_tail;
    }
    context->reserved_head = head_val;
    context->reservation_status =
        used_bb <= context->sq_depth && total_bb <= context->sq_depth - used_bb ? ASCCOMM_SUCCESS : ASCCOMM_FAILED;
    return context->reservation_status == ASCCOMM_SUCCESS;
}

// Rings the doorbell by copying the staged 128B WQE into the DWQE window. The fence and
// invalidation sequence matches asccomm_simt_ring_batch_dwqe: the first fence orders the
// preceding SQ write against the doorbell, the second one orders the window stores before the
// two cache invalidations (which cover both basic blocks of the window), and the trailing one
// keeps the invalidations ahead of whatever this lane issues next. A channel is driven by a
// single lane, so every step is lane-local.
__simt_callee__ inline void asccomm_simt_ring_dwqe(__gm__ uint8_t* dwqe_addr, const asccomm_simt_wqe_stage& stage)
{
    asc_threadfence();
    asccomm_simt_copy_stage_to_dwqe(dwqe_addr, stage);
    asc_threadfence();
    asc_dcci_single(dwqe_addr);
    asc_dcci_single(dwqe_addr + ASCCOMM_URMA_WQE_BB_SIZE);
    asc_threadfence();
}

__simt_callee__ inline void asccomm_simt_ring_batch_dwqe(__gm__ uint8_t* dwqe_addr, const __ubuf__ uint64_t* words)
{
    asc_threadfence();
    __gm__ ulonglong2* dst_words = reinterpret_cast<__gm__ ulonglong2*>(dwqe_addr);
#pragma unroll
    for (uint32_t i = 1; i < ASCCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        const uint32_t word = i * 2U;
        asc_stwt(&dst_words[i], make_ulonglong2(words[word], words[word + 1U]));
    }
    asc_stwt(&dst_words[0], make_ulonglong2(words[0], words[1U]));
    asc_threadfence();
    asc_dcci_single(dwqe_addr);
    asc_dcci_single(dwqe_addr + ASCCOMM_URMA_WQE_BB_SIZE);
    asc_threadfence();
}

__simt_callee__ inline asccomm_impl<COMM_PROTOCOL_UB_CTP>::asccomm_impl() {}

__simt_callee__ inline asccomm_impl<COMM_PROTOCOL_UB_CTP>::~asccomm_impl() {}

__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::init(__ubuf__ uint8_t* buff, uint32_t len)
{
    (void)buff;
    (void)len;
    initialized = true;
    return ASCCOMM_SUCCESS;
}

__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::resolve_post(
    ChannelHandle channel, asccomm_simt_resolved_post& post)
{
    __gm__ asccomm_simt_channel_entity* channel_entity = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ RegedBufferEntity* remote_buffers =
        reinterpret_cast<__gm__ RegedBufferEntity*>(channel_entity->remoteBufferAddr);
    __gm__ RegedBufferEntity* remote_mem_info = &remote_buffers[ASCCOMM_URMA_DEFAULT_BUFFER_IDX];

    // The 64-byte used range (offset 8..71) crosses a cache line, and bulk-reading it as an
    // indexed array costs more instructions than direct field access because the layout has holes.
    // The per-field reads are grouped tightly so the compiler can see the locality.
    __gm__ SqContext* sq_contexts = reinterpret_cast<__gm__ SqContext*>(channel_entity->sqContextAddr);
    __gm__ SqContext* sq_ctx = &sq_contexts[ASCCOMM_URMA_DEFAULT_QP_IDX];
    uint64_t sqVa = sq_ctx->contextInfo.ubJfs.sqVa;
    uint64_t head_addr = sq_ctx->contextInfo.ubJfs.headAddr;
    uint64_t sq_tail_addr = sq_ctx->contextInfo.ubJfs.tailAddr;
    uint64_t dbVa = sq_ctx->contextInfo.ubJfs.dbVa;
    uint32_t wqeSize = sq_ctx->contextInfo.ubJfs.wqeSize;
    uint32_t sq_depth = sq_ctx->contextInfo.ubJfs.sqDepth;
    uint32_t tpID = sq_ctx->contextInfo.ubJfs.tpID;
    __gm__ uint64_t* remote_eid = reinterpret_cast<__gm__ uint64_t*>(&sq_ctx->contextInfo.ubJfs.remoteEID[0]);
    uint64_t remote_eid_l = remote_eid[0];
    uint64_t remote_eid_h = remote_eid[1];

    post.head_addr = reinterpret_cast<__gm__ uint64_t*>(head_addr);
    post.sq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(sq_tail_addr);
    post.dwqe_addr = reinterpret_cast<__gm__ uint8_t*>(dbVa - ASCCOMM_URMA_DWQE_DB_OFFSET);
    post.sq_base_addr = sqVa;
    post.sq_wqe_size = wqeSize;
    post.meta.sq_depth = sq_depth;
    post.meta.tp_id = tpID;
    post.meta.remote_eid_l = remote_eid_l;
    post.meta.remote_eid_h = remote_eid_h;
    post.meta.remote_token_id = remote_mem_info->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    post.meta.remote_token_value = remote_mem_info->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    return ASCCOMM_SUCCESS;
}

__simt_callee__ inline ubc_ctp_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len)
{
    return make_batch_handle(channel, buff, buff_len, 0U, 1U);
}

__simt_callee__ inline ubc_ctp_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, uint32_t group_rank, uint32_t group_size)
{
    ubc_ctp_batch_handle batch_handle;
    constexpr uint32_t workspace_bytes = ASCCOMM_SIMT_BATCH_CONTEXT_BYTES + ASCCOMM_SIMT_BATCH_ITEM_BYTES;
    if (buff == nullptr || buff_len < workspace_bytes) {
        return batch_handle;
    }
    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(buff);
    // Publisher WQEs fill the first three vectors; the full DWQE copy also includes the fourth.
    __ubuf__ uint64_t* publisher_words = asccomm_simt_batch_publisher_words(context);
#pragma unroll
    for (uint32_t word = 3U * sizeof(ulonglong4) / sizeof(uint64_t);
         word < ASCCOMM_SIMT_BATCH_ITEM_BYTES / sizeof(uint64_t); ++word) {
        publisher_words[word] = 0U;
    }
    context->head_addr = 0U;
    asccomm_simt_resolved_post post;
    if (resolve_post(channel, post) != ASCCOMM_SUCCESS) {
        return batch_handle;
    }

    context->head_addr = reinterpret_cast<uint64_t>(post.head_addr);
    context->sq_tail_addr = reinterpret_cast<uint64_t>(post.sq_tail_addr);
    context->dwqe_addr = reinterpret_cast<uint64_t>(post.dwqe_addr);
    context->sq_base_addr = post.sq_base_addr;
    context->word1Base = static_cast<uint64_t>(post.meta.tp_id & 0xFFFFFFU) |
                         (static_cast<uint64_t>(post.meta.remote_token_id & 0xFFFFFU) << 32U);
    context->remote_eid_l = post.meta.remote_eid_l;
    context->remote_eid_h = post.meta.remote_eid_h;
    context->token_word = post.meta.remote_token_value;
    context->sq_wqe_size = post.sq_wqe_size;
    context->sq_depth = post.meta.sq_depth;
    asc_dcci_single(post.sq_tail_addr);
    asc_dcci_single(post.head_addr);
    context->cached_sq_tail = *post.sq_tail_addr;
    context->item_bb = 0U;
    context->group_size = group_size;
    // Pre-check capacity with the widest request layout; every appended request re-plans with its
    // actual width.
    asccomm_simt_plan_batch_from_head(context, *post.head_addr, ASCCOMM_URMA_DWQE_BB_CNT);

    batch_handle.channel = channel;
    batch_handle.context = reinterpret_cast<__ubuf__ uint64_t*>(context);
    batch_handle.group_rank = group_rank;
    batch_handle.group_size = group_size;
    return batch_handle;
}

template <typename coop_group>
__simt_callee__ inline ubc_ctp_batch_handle asccomm_impl<COMM_PROTOCOL_UB_CTP>::make_batch_handle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, const coop_group& group)
{
    ubc_ctp_batch_handle batch_handle;
    constexpr uint32_t workspace_bytes = ASCCOMM_SIMT_BATCH_CONTEXT_BYTES + ASCCOMM_SIMT_BATCH_ITEM_BYTES;
    // All lanes must supply the same arguments; reject invalid storage before any lane reads it.
    if (buff == nullptr || buff_len < workspace_bytes) {
        return batch_handle;
    }
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    if (rank == 0U) {
        (void)make_batch_handle(channel, buff, buff_len, 0U, size);
    }
    group.sync();

    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(buff);
    if (context->head_addr == 0U) {
        return batch_handle;
    }
    batch_handle.channel = channel;
    batch_handle.context = reinterpret_cast<__ubuf__ uint64_t*>(context);
    batch_handle.group_rank = rank;
    batch_handle.group_size = size;
    return batch_handle;
}

// Each operator prepares its request-specific values at final WQE offsets. The same-BB contract
// lets coop_group commit derive every final SQ position directly from group rank.
template <bool commit_flag, asccomm_urma_op_code op_code, auto const& config>
struct asccomm_simt_data_desc {
    static constexpr bool commit = commit_flag;
    static constexpr uint32_t sge_num = commit_flag ? 2U : 1U;
    static constexpr uint32_t bb_cnt = commit_flag ? ASCCOMM_URMA_DWQE_BB_CNT : ASCCOMM_URMA_WQE_BB_CNT;
    static constexpr uint32_t cqe_cnt = config.cqe;

    __gm__ uint8_t* remote_addr;
    __gm__ uint8_t* local_addr;
    uint64_t target_len;

    template <uint32_t word_cnt, typename word_ptr>
    __simt_callee__ inline void store(
        word_ptr words, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head,
        uint32_t next_head) const
    {
        asccomm_simt_write_data_wqe<op_code, sge_num, word_cnt, config>(
            words, remote_addr, local_addr, target_len, sqe_template, cur_head, next_head);
    }
};

template <bool commit_flag, auto const& config>
struct asccomm_simt_notify_desc {
    static constexpr bool commit = commit_flag;
    static constexpr uint32_t sge_num = 1U;
    static constexpr uint32_t bb_cnt = ASCCOMM_URMA_DWQE_BB_CNT;
    static constexpr uint32_t cqe_cnt = config.cqe;

    __gm__ uint8_t* remote_addr;
    __gm__ uint8_t* local_addr;
    uint64_t target_len;
    __gm__ uint8_t* notify_addr;
    uint64_t notify_val;

    template <uint32_t word_cnt, typename word_ptr>
    __simt_callee__ inline void store(
        word_ptr words, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head,
        uint32_t next_head) const
    {
        asccomm_simt_write_notify_wqe<word_cnt, config>(
            words, remote_addr, local_addr, target_len, notify_addr, notify_val, sqe_template, cur_head, next_head);
    }
};

template <typename T, bool commit_flag, auto const& config>
struct asccomm_simt_inline_desc {
    static constexpr bool commit = commit_flag;
    static constexpr uint32_t sge_num = 0U;
    // A deferred inline Write occupies one BB; a committed one occupies two (the second is a NOP
    // that pads the DWQE window to 128B so the doorbell can publish it).
    static constexpr uint32_t bb_cnt = commit ? ASCCOMM_URMA_DWQE_BB_CNT : ASCCOMM_URMA_WQE_BB_CNT;
    static constexpr uint32_t cqe_cnt = config.cqe;

    __gm__ uint8_t* remote_addr;
    T value;
    uint64_t target_len = sizeof(T);

    template <uint32_t word_cnt, typename word_ptr>
    __simt_callee__ inline void store(
        word_ptr words, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head,
        uint32_t next_head) const
    {
        static_assert(asccomm_config_inline_en(config) == 1, "write_value_nbi requires inline data in WQE");
        static_assert(
            sizeof(T) <= ASCCOMM_URMA_WQE_BB_SIZE - sizeof(asccomm_urma_sqe_ctx),
            "WriteValue<T>: sizeof(T) must be less equal than primitive type");
        asccomm_simt_write_inline_wqe<T, word_cnt, config, commit>(
            words, remote_addr, value, sqe_template, cur_head, next_head);
    }
};

template <typename T, bool commit_flag, asccomm_urma_op_code op_code, auto const& config>
struct asccomm_simt_atomic_desc {
    static constexpr bool commit = commit_flag;
    static constexpr uint32_t sge_num = 1U;
    static constexpr uint32_t bb_cnt = ASCCOMM_URMA_DWQE_BB_CNT;
    static constexpr uint32_t cqe_cnt = config.cqe;

    __gm__ uint8_t* remote_addr;
    __gm__ uint8_t* fetch_addr;
    T value;
    T cond;
    uint64_t target_len = sizeof(T);

    template <uint32_t word_cnt, typename word_ptr>
    __simt_callee__ inline void store(
        word_ptr words, const asccomm_simt_packed_sqe_template& sqe_template, uint32_t cur_head,
        uint32_t next_head) const
    {
        asccomm_simt_write_atomic_wqe<op_code, T, word_cnt, config>(
            words, remote_addr, fetch_addr, value, cond, sqe_template, cur_head, next_head);
    }
};

template <bool publisher, bool commit_flag, asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline void asccomm_simt_emit_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, __gm__ uint8_t* first_bb, __gm__ uint8_t* second_bb,
    uint32_t cur_head, const asccomm_simt_data_desc<commit_flag, op_code, config>& desc)
{
    constexpr uint32_t sge_num = publisher ? 2U : asccomm_simt_data_desc<commit_flag, op_code, config>::sge_num;
    const uint32_t next_head = cur_head + (publisher ? ASCCOMM_URMA_DWQE_BB_CNT : context->item_bb);
    const uint64_t word0 = asccomm_simt_batch_sqe_word0<op_code, config>(context, cur_head, next_head);
    const uint64_t word1 = context->word1Base | (static_cast<uint64_t>(sge_num) << 24U);
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 0U, word0, word1, context->remote_eid_l, context->remote_eid_h);
    if constexpr (publisher) {
        const uint32_t first_len = static_cast<uint32_t>(desc.target_len >> 1U);
        const uint32_t last_len = static_cast<uint32_t>(desc.target_len - first_len);
        asccomm_simt_store_batch_vector<true>(
            context, first_bb, second_bb, 1U, context->token_word, reinterpret_cast<uint64_t>(desc.remote_addr),
            first_len, reinterpret_cast<uint64_t>(desc.local_addr));
        asccomm_simt_store_batch_vector<true>(
            context, first_bb, second_bb, 2U, last_len, reinterpret_cast<uint64_t>(desc.local_addr + first_len), 0U,
            0U);
    } else {
        asccomm_simt_store_batch_vector<false>(
            context, first_bb, second_bb, 1U, context->token_word, reinterpret_cast<uint64_t>(desc.remote_addr),
            static_cast<uint32_t>(desc.target_len), reinterpret_cast<uint64_t>(desc.local_addr));
    }
}

template <bool publisher, bool commit_flag, auto const& config>
__simt_callee__ inline void asccomm_simt_emit_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, __gm__ uint8_t* first_bb, __gm__ uint8_t* second_bb,
    uint32_t cur_head, const asccomm_simt_notify_desc<commit_flag, config>& desc)
{
    const uint32_t next_head = cur_head + ASCCOMM_URMA_DWQE_BB_CNT;
    const uint64_t word0 =
        asccomm_simt_batch_sqe_word0<asccomm_urma_op_code::WRITE_WITH_NOTIFY, config>(context, cur_head, next_head);
    const uint64_t word1 = context->word1Base | (1ULL << 24U);
    const uint64_t notify_token =
        static_cast<uint64_t>((word1 >> 32U) & 0xFFFFFU) | ((context->token_word & 0xFFFFFFFFULL) << 32U);
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 0U, word0, word1, context->remote_eid_l, context->remote_eid_h);
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 1U, context->token_word, reinterpret_cast<uint64_t>(desc.remote_addr),
        notify_token, reinterpret_cast<uint64_t>(desc.notify_addr));
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 2U, desc.notify_val, 0U, static_cast<uint32_t>(desc.target_len),
        reinterpret_cast<uint64_t>(desc.local_addr));
}

template <bool publisher, typename T, bool commit_flag, auto const& config>
__simt_callee__ inline void asccomm_simt_emit_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, __gm__ uint8_t* first_bb, __gm__ uint8_t* second_bb,
    uint32_t cur_head, const asccomm_simt_inline_desc<T, commit_flag, config>& desc)
{
    static_assert(asccomm_config_inline_en(config) == 1, "write_value_nbi requires inline data in WQE");
    static_assert(
        sizeof(T) <= ASCCOMM_URMA_WQE_BB_SIZE - sizeof(asccomm_urma_sqe_ctx),
        "WriteValue<T>: sizeof(T) must be less equal than primitive type");
    const uint32_t next_head = cur_head + (publisher ? ASCCOMM_URMA_DWQE_BB_CNT : context->item_bb);
    const uint64_t word0 =
        asccomm_simt_batch_sqe_word0<asccomm_urma_op_code::WRITE, config>(context, cur_head, next_head, sizeof(T));
    uint64_t payload[2] = {0U, 0U};
    const uint8_t* value_addr = reinterpret_cast<const uint8_t*>(&desc.value);
#pragma unroll
    for (uint32_t byte = 0U; byte < sizeof(T); ++byte) {
        payload[byte / sizeof(uint64_t)] |= static_cast<uint64_t>(value_addr[byte]) << ((byte % sizeof(uint64_t)) * 8U);
    }
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 0U, word0, context->word1Base, context->remote_eid_l, context->remote_eid_h);
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 1U, context->token_word, reinterpret_cast<uint64_t>(desc.remote_addr), payload[0],
        payload[1]);
    if constexpr (publisher) {
        const uint64_t owner = (next_head & context->sq_depth) == 0U ? 1U : 0U;
        const uint64_t nop_word = static_cast<uint64_t>((next_head + 1U) & 0xFFFFU) | (1ULL << 28U) | (1ULL << 29U) |
                                  (owner << 31U) |
                                  (static_cast<uint64_t>(static_cast<uint32_t>(asccomm_urma_op_code::NOP)) << 40U);
        asccomm_simt_store_batch_vector<true>(context, first_bb, second_bb, 2U, nop_word, 0U, 0U, 0U);
    }
}

template <bool publisher, typename T, bool commit_flag, asccomm_urma_op_code op_code, auto const& config>
__simt_callee__ inline void asccomm_simt_emit_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, __gm__ uint8_t* first_bb, __gm__ uint8_t* second_bb,
    uint32_t cur_head, const asccomm_simt_atomic_desc<T, commit_flag, op_code, config>& desc)
{
    const uint32_t next_head = cur_head + ASCCOMM_URMA_DWQE_BB_CNT;
    const uint64_t word0 = asccomm_simt_batch_sqe_word0<op_code, config>(context, cur_head, next_head);
    const uint64_t word1 = context->word1Base | (1ULL << 24U);
    uint64_t operand0 = static_cast<uint64_t>(desc.value);
    uint64_t operand1 = op_code == asccomm_urma_op_code::CAS ? static_cast<uint64_t>(desc.cond) : 0U;
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        operand0 = static_cast<uint64_t>(static_cast<uint32_t>(desc.value)) |
                   (static_cast<uint64_t>(
                        static_cast<uint32_t>(op_code == asccomm_urma_op_code::CAS ? desc.cond : static_cast<T>(0)))
                    << 32U);
        operand1 = 0U;
    }
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 0U, word0, word1, context->remote_eid_l, context->remote_eid_h);
    asccomm_simt_store_batch_vector<publisher>(
        context, first_bb, second_bb, 1U, context->token_word, reinterpret_cast<uint64_t>(desc.remote_addr), sizeof(T),
        reinterpret_cast<uint64_t>(desc.fetch_addr));
    asccomm_simt_store_batch_vector<publisher>(context, first_bb, second_bb, 2U, operand0, operand1, 0U, 0U);
}

// Sets the WQE width to the appended request's layout and replans the SQ range for that width.
// The collective path runs this pass on rank 0 only and publishes the results with a group sync
// before any lane reads them back; the single-lane path needs no barrier because no other lane
// shares the context.
template <typename post_desc>
__simt_callee__ inline void asccomm_simt_plan_batch_append(__ubuf__ asccomm_simt_batch_static_context* context)
{
    context->item_bb = post_desc::bb_cnt;
    asccomm_simt_plan_batch_from_head(context, context->reserved_head);
}

// Emits one lane's WQE from the shared plan. The caller guarantees that the plan has been
// published to every lane, so the check below sees the same item_bb and reservation_status on all
// lanes and the append decision is uniform across the group.
template <typename post_desc>
__simt_callee__ inline int32_t asccomm_simt_append_planned_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, uint32_t rank, uint32_t size, const post_desc& desc)
{
    static_assert(post_desc::cqe_cnt == 1U, "SIMT batch requests require CQE");
    if (context->reservation_status != ASCCOMM_SUCCESS || context->item_bb != post_desc::bb_cnt) {
        return ASCCOMM_FAILED;
    }
    asccomm_simt_resolved_post post;
    asccomm_simt_load_batch_static_post(context, post);
    const bool publisher = rank + 1U == size;
    const uint32_t cur_head = asccomm_simt_head_idx(context->reserved_head) + rank * context->item_bb;
    __gm__ uint8_t* first_bb =
        reinterpret_cast<__gm__ uint8_t*>(post.sq_base_addr + post.sq_wqe_size * (cur_head % post.meta.sq_depth));
    __gm__ uint8_t* second_bb = (cur_head % post.meta.sq_depth) + 1U == post.meta.sq_depth ?
                                    reinterpret_cast<__gm__ uint8_t*>(post.sq_base_addr) :
                                    first_bb + ASCCOMM_URMA_WQE_BB_SIZE;
    if (publisher) {
        asccomm_simt_emit_final_batch_wqe<true>(context, first_bb, second_bb, cur_head, desc);
    } else {
        asccomm_simt_emit_final_batch_wqe<false>(context, first_bb, second_bb, cur_head, desc);
    }
    asc_threadfence();
    return ASCCOMM_SUCCESS;
}

// Single-lane append: no other lane races the plan, so no barrier is needed between the passes.
template <typename post_desc>
__simt_callee__ inline int32_t asccomm_simt_append_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, uint32_t rank, uint32_t size, const post_desc& desc)
{
    asccomm_simt_plan_batch_append<post_desc>(context);
    return asccomm_simt_append_planned_batch_wqe<post_desc>(context, rank, size, desc);
}

// Collective append: rank 0 re-plans with the appended request's width and the sync publishes the
// plan, so every lane reads the same item_bb and reservation_status before deciding whether to
// write its WQE.
template <typename post_desc, typename coop_group>
__simt_callee__ inline int32_t asccomm_simt_append_final_batch_wqe(
    __ubuf__ asccomm_simt_batch_static_context* context, uint32_t rank, uint32_t size, const post_desc& desc,
    const coop_group& group)
{
    if (rank == 0U) {
        asccomm_simt_plan_batch_append<post_desc>(context);
    }
    group.sync();
    return asccomm_simt_append_planned_batch_wqe<post_desc>(context, rank, size, desc);
}

template <bool refresh_cache>
__simt_callee__ inline uint32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::poll_cq(ChannelHandle channel, uint32_t expect_idx)
{
    if (expect_idx == 0U) {
        return ASCCOMM_SUCCESS;
    }
    __gm__ asccomm_simt_channel_entity* channel_entity = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ CqContext* cq_contexts = reinterpret_cast<__gm__ CqContext*>(channel_entity->cqContextAddr);
    __gm__ CqContext* cq_ctx = &cq_contexts[ASCCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* tail_addr = (__gm__ uint32_t*)cq_ctx->contextInfo.ubJfc.tailAddr;
    if constexpr (refresh_cache) {
        asc_dcci_single(tail_addr);
    }
    uint32_t cur_tail = *tail_addr;

    __gm__ SqContext* sq_contexts = reinterpret_cast<__gm__ SqContext*>(channel_entity->sqContextAddr);
    __gm__ SqContext* sq_ctx = &sq_contexts[ASCCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* sq_tail_addr = (__gm__ uint32_t*)sq_ctx->contextInfo.ubJfs.tailAddr;
    if constexpr (refresh_cache) {
        asc_dcci_single(sq_tail_addr);
    }
    uint32_t sq_tail = *sq_tail_addr;
    uint32_t sq_depth = sq_ctx->contextInfo.ubJfs.sqDepth;

    uint64_t cq_base_addr = cq_ctx->contextInfo.ubJfc.scqVa;
    uint32_t cqe_size = cq_ctx->contextInfo.ubJfc.cqeSize;
    uint32_t cq_depth = cq_ctx->contextInfo.ubJfc.cqDepth;
    // Both depths are powers of two (see the owner-bit computation in asccomm_simt_write_sqe_header),
    // so the wrap arithmetic below uses masks instead of modulo. Depth is loop-invariant, so the
    // masks are computed once here.
    uint32_t cq_mask = cq_depth - 1U;
    uint32_t sq_mask = sq_depth - 1U;
    while (cur_tail != expect_idx) {
        // Read the CQE header as raw 32-bit words instead of through the bitfield struct. This
        // lets the compiler see the reads are contiguous and generate tighter code. owner sits at
        // bit 2 of word 0, status/substatus at bits 24/16, entry_idx in the low 16 bits of word 1.
        __gm__ uint32_t* cqe_words = (__gm__ uint32_t*)(cq_base_addr + cqe_size * (cur_tail & cq_mask));
        uint32_t valid_owner = (cur_tail / cq_depth) & 1U;
        uint32_t retry = 0U;
        if constexpr (refresh_cache) {
            asc_dcci_single(cqe_words);
        }
        while ((((cqe_words[0] >> 2U) & 1U) == valid_owner) && retry < ASCCOMM_SIMT_MAX_CQ_RETRY) {
            asc_threadfence();
            asc_dcci_single(cqe_words);
            ++retry;
        }
        if (retry >= ASCCOMM_SIMT_MAX_CQ_RETRY) {
            // No CQE arrived: the WQE was never consumed.
            return 0xFFU;
        }
        uint32_t cqe_head = cqe_words[0];
        uint32_t status = (cqe_head >> 24U) & 0xFFU;
        uint32_t sub_status = (cqe_head >> 16U) & 0xFFU;
        if (status != 0U || sub_status != 0U) {
            constexpr uint8_t status_shift = 8U;
            return (status << status_shift) | sub_status;
        }
        if constexpr (refresh_cache) {
            if (sq_depth == 0U) {
                return 0xFFU;
            }
        }
        uint32_t sq_tail_slot = sq_tail & sq_mask;
        uint32_t completed_slot = (cqe_words[1] & 0xFFFFU) & sq_mask;
        // The parentheses matter: & binds looser than +, so `x & sq_mask + 1U` would compute
        // `x & (sq_mask + 1U)` -- a single-bit test against sq_depth rather than a wrap-around
        // distance. The distance from the current tail slot to the completed slot, plus one for
        // the completed block itself, is how far the tail advances.
        sq_tail += ((completed_slot + sq_depth - sq_tail_slot) & sq_mask) + 1U;
        ++cur_tail;
    }

    *tail_addr = cur_tail;
    __gm__ uint32_t* cq_db_addr = (__gm__ uint32_t*)cq_ctx->contextInfo.ubJfc.dbVa;
    *cq_db_addr = cur_tail & 0xFFFFFFU;

    *sq_tail_addr = sq_tail;
    if constexpr (refresh_cache) {
        asc_threadfence();
        asc_dcci_single(tail_addr);
        asc_dcci_single(sq_tail_addr);
        asc_threadfence();
    }
    return ASCCOMM_SUCCESS;
}

// Claims SQ space for one submission. The fast path is a single reservation attempt; when the
// queue is full it consumes completed CQEs one at a time to let poll_cq advance sq_tail, retrying
// after each. Waiting for every submitted WQE would be wrong: wqe_cnt may include deferred WQEs
// that no doorbell has published yet, so those completions never arrive.
template <typename post_desc>
__simt_callee__ inline bool asccomm_impl<COMM_PROTOCOL_UB_CTP>::reserve_post(
    ChannelHandle channel, const asccomm_simt_resolved_post& post, uint64_t& head_val)
{
    constexpr bool commit = post_desc::commit;
    constexpr uint32_t bb_cnt = post_desc::bb_cnt;
    constexpr uint32_t cqe_cnt = post_desc::cqe_cnt;
    // SIMT has no standalone commit interface, so a deferred post must also leave room for the
    // immediate 2-BB DWQE that will later publish the batch it belongs to.
    constexpr uint32_t extra_free_bb_cnt = commit ? 0U : ASCCOMM_URMA_DWQE_BB_CNT;
    uint32_t required_free_bb_cnt = bb_cnt + extra_free_bb_cnt;

    if (asccomm_simt_try_reserve(
            post.head_addr, post.sq_tail_addr, post.meta.sq_depth, bb_cnt, required_free_bb_cnt, cqe_cnt, head_val)) {
        return true;
    }
    if (post.meta.sq_depth == 0U || required_free_bb_cnt > post.meta.sq_depth) {
        return false;
    }

    __gm__ asccomm_simt_channel_entity* channel_entity = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ CqContext* cq_contexts = reinterpret_cast<__gm__ CqContext*>(channel_entity->cqContextAddr);
    __gm__ CqContext* cq_ctx = &cq_contexts[ASCCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* cq_tail_addr = reinterpret_cast<__gm__ uint32_t*>(cq_ctx->contextInfo.ubJfc.tailAddr);
    while (true) {
        // head_val was refreshed by the failed attempt, so its wqe_cnt is the number of CQEs the
        // channel has submitted so far. Once the CQ tail has reached it there is nothing left to
        // reap and the queue cannot free up any further.
        uint32_t cq_tail = *cq_tail_addr;
        if (cq_tail == asccomm_simt_wqe_cnt(head_val) || poll_cq<false>(channel, cq_tail + 1U) != ASCCOMM_SUCCESS) {
            return false;
        }
        if (asccomm_simt_try_reserve(
                post.head_addr, post.sq_tail_addr, post.meta.sq_depth, bb_cnt, required_free_bb_cnt, cqe_cnt,
                head_val)) {
            return true;
        }
    }
}

template <uint32_t bb_cnt, bool commit>
__simt_callee__ inline void asccomm_simt_publish_reserved_stage(
    const asccomm_simt_resolved_post& post, uint32_t cur_head, const asccomm_simt_wqe_stage& stage)
{
    __gm__ uint8_t* sqe_addr =
        reinterpret_cast<__gm__ uint8_t*>(post.sq_base_addr + post.sq_wqe_size * (cur_head % post.meta.sq_depth));
    asccomm_simt_copy_stage_wqe_to_sq<bb_cnt>(post.sq_base_addr, sqe_addr, post.meta.sq_depth, cur_head, stage);
    if constexpr (commit) {
        asccomm_simt_ring_dwqe(post.dwqe_addr, stage);
    }
}

// The single posting path: resolve the target, claim SQ space, lay the WQE down and, when the
// task is committed, ring the doorbell. Every step is lane-local; a caller that needs several
// lanes to post must serialize them itself.
template <typename post_desc>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::post_wqe(
    ChannelHandle channel, const post_desc& desc)
{
    constexpr uint32_t bb_cnt = post_desc::bb_cnt;
    constexpr uint32_t word_cnt = bb_cnt * ASCCOMM_SIMT_BB_WORDS;
    static_assert(
        bb_cnt == ASCCOMM_URMA_WQE_BB_CNT || bb_cnt == ASCCOMM_URMA_DWQE_BB_CNT, "A WQE spans one or two BBs");
    // Only a deferred task may occupy a single basic block: an immediate one is published
    // through the 128B DWQE window, which is two blocks wide.
    static_assert(
        post_desc::commit ? bb_cnt == ASCCOMM_URMA_DWQE_BB_CNT : true, "An immediate WQE must fill the DWQE window");

    if (!initialized) {
        return ASCCOMM_FAILED;
    }
    // asccomm_urma_sge_ctx::len is 32-bit.
    if (desc.target_len > 0xFFFFFFFFULL) {
        return ASCCOMM_FAILED;
    }

    asccomm_simt_resolved_post post;
    if (resolve_post(channel, post) != ASCCOMM_SUCCESS) {
        return ASCCOMM_FAILED;
    }

    uint64_t head_val = 0U;
    if (!reserve_post<post_desc>(channel, post, head_val)) {
        return ASCCOMM_FAILED;
    }
    uint32_t cur_head = asccomm_simt_head_idx(head_val);
    uint32_t next_head = cur_head + bb_cnt;

    asccomm_simt_packed_sqe_template sqe_template;
    asccomm_simt_build_packed_sqe_template<post_desc::sge_num>(post.meta, sqe_template);

    // Stage the WQE in lane-private storage, then copy it into the SQ. The SQ copy is what the
    // NIC always reads, so it happens on every path; a committed post then publishes the
    // doorbell out of the same staged bytes.
    asccomm_simt_wqe_stage stage;
    desc.template store<word_cnt>(stage.words, sqe_template, cur_head, next_head);
    asccomm_simt_publish_reserved_stage<bb_cnt, post_desc::commit>(post, cur_head, stage);
    return ASCCOMM_SUCCESS;
}

template <typename post_desc>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_post_wqe(
    ubc_ctp_batch_handle& batch_handle, const post_desc& desc)
{
    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(batch_handle.context);
    return asccomm_simt_append_final_batch_wqe(context, 0U, 1U, desc);
}

template <typename post_desc, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_post_wqe(
    ubc_ctp_batch_handle& batch_handle, const post_desc& desc, const coop_group& group)
{
    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(batch_handle.context);
    // The first append plans on rank 0 and syncs before the emit; later appends read the state
    // published by the previous sync. batch_commit synchronizes the group again before the
    // publisher advances the shared head.
    return asccomm_simt_append_final_batch_wqe(context, batch_handle.group_rank, batch_handle.group_size, desc, group);
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return post_wqe(
        channel, asccomm_simt_data_desc<commit, asccomm_urma_op_code::WRITE, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_data_desc<false, asccomm_urma_op_code::WRITE, config>{
                          reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group)
{
    return batch_post_wqe(
        batch_handle,
        asccomm_simt_data_desc<false, asccomm_urma_op_code::WRITE, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len},
        group);
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_value_nbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return post_wqe(
        channel, asccomm_simt_inline_desc<T, commit, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_value_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_inline_desc<T, false, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_value_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value, const coop_group& group)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_inline_desc<T, false, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value}, group);
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::read_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return post_wqe(
        channel, asccomm_simt_data_desc<commit, asccomm_urma_op_code::READ, config>{
                     reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::read_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_data_desc<false, asccomm_urma_op_code::READ, config>{
                          reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::read_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group)
{
    return batch_post_wqe(
        batch_handle,
        asccomm_simt_data_desc<false, asccomm_urma_op_code::READ, config>{
            reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len},
        group);
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_with_notify_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return post_wqe(
        channel, asccomm_simt_notify_desc<commit, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                     reinterpret_cast<__gm__ uint8_t*>(notify_addr), notify_val});
}

template <auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_notify_desc<true, config>{
                          reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                          reinterpret_cast<__gm__ uint8_t*>(notify_addr), notify_val});
}

template <auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::write_with_notify_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_val, const coop_group& group)
{
    return batch_post_wqe(
        batch_handle,
        asccomm_simt_notify_desc<true, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
            reinterpret_cast<__gm__ uint8_t*>(notify_addr), notify_val},
        group);
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_faa_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
{
    return post_wqe(
        channel, asccomm_simt_atomic_desc<T, commit, asccomm_urma_op_code::FAA, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), add_val,
                     static_cast<T>(0)});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_faa_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_atomic_desc<T, true, asccomm_urma_op_code::FAA, config>{
                          reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr),
                          add_val, static_cast<T>(0)});
}

template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_faa_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val, const coop_group& group)
{
    return batch_post_wqe(
        batch_handle,
        asccomm_simt_atomic_desc<T, true, asccomm_urma_op_code::FAA, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), add_val,
            static_cast<T>(0)},
        group);
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_cas_nbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
{
    return post_wqe(
        channel, asccomm_simt_atomic_desc<T, commit, asccomm_urma_op_code::CAS, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), swap_val,
                     compare_val});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_cas_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val)
{
    return batch_post_wqe(
        batch_handle, asccomm_simt_atomic_desc<T, true, asccomm_urma_op_code::CAS, config>{
                          reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr),
                          swap_val, compare_val});
}

template <typename T, auto const& config, typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::atomic_cas_nbi(
    ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val,
    const coop_group& group)
{
    return batch_post_wqe(
        batch_handle,
        asccomm_simt_atomic_desc<T, true, asccomm_urma_op_code::CAS, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetch_addr), swap_val,
            compare_val},
        group);
}

__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_commit(ubc_ctp_batch_handle& batch_handle)
{
    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(batch_handle.context);
    if (context->reservation_status != ASCCOMM_SUCCESS) {
        return ASCCOMM_FAILED;
    }
    asccomm_simt_resolved_post post;
    asccomm_simt_load_batch_static_post(context, post);
    const uint64_t published_head =
        context->reserved_head + ((1ULL << 32U) | asccomm_simt_batch_total_bb(1U, context->item_bb));
    *post.head_addr = published_head;
    asc_dcci_single(post.head_addr);
    asc_threadfence();
    asccomm_simt_ring_batch_dwqe(post.dwqe_addr, asccomm_simt_batch_publisher_words(context));
    asccomm_simt_plan_batch_from_head(context, published_head);
    return ASCCOMM_SUCCESS;
}

template <typename coop_group>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::batch_commit(
    ubc_ctp_batch_handle& batch_handle, const coop_group& group)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    __ubuf__ asccomm_simt_batch_static_context* context =
        reinterpret_cast<__ubuf__ asccomm_simt_batch_static_context*>(batch_handle.context);
    // Check the shared reservation after the sync so every lane reads the same value and the
    // collective call returns uniformly.
    if (context->reservation_status != ASCCOMM_SUCCESS) {
        return ASCCOMM_FAILED;
    }
    // Append has ordered every lane's SQ stores before the publisher advances the shared head.
    group.sync();
    if (publisher) {
        asccomm_simt_resolved_post post;
        asccomm_simt_load_batch_static_post(context, post);
        const uint32_t total_bb = asccomm_simt_batch_total_bb(size, context->item_bb);
        const uint64_t published_head = context->reserved_head + ((static_cast<uint64_t>(size) << 32U) | total_bb);
        *post.head_addr = published_head;
        asc_dcci_single(post.head_addr);
        asc_threadfence();
        asccomm_simt_ring_batch_dwqe(post.dwqe_addr, asccomm_simt_batch_publisher_words(context));
        asccomm_simt_plan_batch_from_head(context, published_head);
    }
    group.sync();
    return ASCCOMM_SUCCESS;
}

template <bool refresh_cache, auto pipe>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::drain_channel(ChannelHandle channel)
{
    (void)pipe;
    __gm__ asccomm_simt_channel_entity* channel_entity = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    __gm__ SqContext* sq_contexts = reinterpret_cast<__gm__ SqContext*>(channel_entity->sqContextAddr);
    __gm__ SqContext* sq_ctx = &sq_contexts[ASCCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* head_addr = (__gm__ uint64_t*)sq_ctx->contextInfo.ubJfs.headAddr;

    // wqe_cnt counts only CQE-producing WQEs, so it is directly comparable with the CQ tail.
    if constexpr (refresh_cache) {
        asc_dcci_single(head_addr);
    }
    uint64_t head_val = *head_addr;
    uint32_t wqe_cnt = asccomm_simt_wqe_cnt(head_val);
    uint32_t poll_ret = poll_cq<refresh_cache>(channel, wqe_cnt);
    return poll_ret == ASCCOMM_SUCCESS ? ASCCOMM_SUCCESS : ASCCOMM_FAILED;
}

template <auto pipe>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::drain(ChannelHandle channel)
{
    return drain_channel<false, pipe>(channel);
}

template <auto pipe>
__simt_callee__ inline int32_t asccomm_impl<COMM_PROTOCOL_UB_CTP>::drain(ubc_ctp_batch_handle& batch_handle)
{
    return drain_channel<true, pipe>(batch_handle.channel);
}

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_H
#endif
