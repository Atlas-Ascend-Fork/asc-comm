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
 * \file asccomm_simt_urma_def.h
 * \brief Hcomm SIMT URMA definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_DEF_H

#include "../../common/asccomm_simt_inner_def.h"

namespace asc::simt {

// SIMT rings the doorbell by copying a 128B DWQE to dbVa - 0x80; the scalar register write the
// SIMD path uses is not available here.
constexpr uint32_t ASCCOMM_URMA_DWQE_DB_OFFSET = 0x80U;
constexpr uint32_t ASCCOMM_URMA_DWQE_SIZE = 128U;
constexpr uint32_t ASCCOMM_URMA_WQE_BB_SIZE = 64U;
constexpr uint32_t ASCCOMM_URMA_WQE_BB_CNT = 1U;
constexpr uint32_t ASCCOMM_URMA_DWQE_BB_CNT = ASCCOMM_URMA_DWQE_SIZE / ASCCOMM_URMA_WQE_BB_SIZE;
constexpr uint32_t ASCCOMM_SIMT_BATCH_CONTEXT_BYTES = 128U;
constexpr uint32_t ASCCOMM_SIMT_BATCH_ITEM_BYTES = ASCCOMM_URMA_DWQE_SIZE;
static_assert(
    ASCCOMM_SIMT_BATCH_CONTEXT_BYTES + ASCCOMM_SIMT_BATCH_ITEM_BYTES == 256U,
    "The SIMT batch workspace must match the documented 256-byte minimum");
constexpr uint32_t ASCCOMM_SIMT_MAX_CQ_RETRY = 1000000U;

// The SQE fields one post needs, read from the channel and SQ contexts by resolve_post.
struct asccomm_simt_post_meta {
    uint32_t sq_depth = 0U;
    uint32_t tp_id = 0U;
    uint64_t remote_eid_l = 0U;
    uint64_t remote_eid_h = 0U;
    uint32_t remote_token_id = 0U;
    uint32_t remote_token_value = 0U;
};

struct asccomm_simt_resolved_post {
    asccomm_simt_post_meta meta;
    __gm__ uint64_t* head_addr;
    // The SQ consumer index poll_cq advances. The reservation reads it to tell how many basic
    // blocks the NIC has already released.
    __gm__ uint32_t* sq_tail_addr;
    __gm__ uint8_t* dwqe_addr;
    uint64_t sq_base_addr;
    uint32_t sq_wqe_size;
};

// Stable fields shared by fixed-2BB Notify/Atomic WQEs for one resolved target.
struct asccomm_simt_packed_sqe_template {
    uint64_t word1 = 0U;
    uint64_t remote_eid_l = 0U;
    uint64_t remote_eid_h = 0U;
    uint64_t token_word = 0U;
    uint32_t sq_depth = 0U;
};

template <>
class asccomm_impl<COMM_PROTOCOL_UB_CTP> {
public:
    __simt_callee__ inline asccomm_impl();
    __simt_callee__ inline ~asccomm_impl();
    __simt_callee__ inline int32_t asccomm_init(__ubuf__ uint8_t* buff, uint32_t len);
    __simt_callee__ inline ubc_ctp_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len);
    __simt_callee__ inline ubc_ctp_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, uint32_t group_rank, uint32_t group_size);
    template <typename coop_group>
    __simt_callee__ inline ubc_ctp_batch_handle asccomm_make_batch_handle(
        ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buff_len, const coop_group& group);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_write_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group);
    template <typename T, bool commit = true, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t asccomm_write_value_nbi(ChannelHandle channel, __gm__ void* dst, T value);
    template <typename T, auto const& config = URMA_INLINE_CFG>
    __simt_callee__ inline int32_t asccomm_write_value_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value);
    template <typename T, auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_write_value_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, T value, const coop_group& group);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_read_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_read_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_read_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, const coop_group& group);
    template <bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_with_notify_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val);
    template <auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_write_with_notify_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val);
    template <auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_write_with_notify_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val, const coop_group& group);
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_faa_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T add_val);
    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_faa_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val);
    template <typename T, auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_atomic_faa_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T add_val,
        const coop_group& group);
    template <typename T, bool commit = true, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_cas_nbi(
        ChannelHandle channel, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val);
    template <typename T, auto const& config = URMA_DEFAULT_CFG>
    __simt_callee__ inline int32_t asccomm_atomic_cas_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val);
    template <typename T, auto const& config, typename coop_group>
    __simt_callee__ inline int32_t asccomm_atomic_cas_nbi(
        ubc_ctp_batch_handle& batch_handle, __gm__ void* dst, __gm__ void* fetch_addr, T compare_val, T swap_val,
        const coop_group& group);
    __simt_callee__ inline int32_t asccomm_batch_commit(ubc_ctp_batch_handle& batch_handle);
    template <typename coop_group>
    __simt_callee__ inline int32_t asccomm_batch_commit(ubc_ctp_batch_handle& batch_handle, const coop_group& group);
    template <auto pipe = 0>
    __simt_callee__ inline int32_t asccomm_drain(ChannelHandle channel);
    template <auto pipe = 0>
    __simt_callee__ inline int32_t asccomm_drain(ubc_ctp_batch_handle& batch_handle);

private:
    bool initialized = false;
    // Reads the channel entity and SQ context from global memory on every post. Nothing is
    // cached across calls: a channel may change from one post to the next, so a cache would
    // rarely hit while still costing a validity check and an invalidation on each post.
    __simt_callee__ inline int32_t resolve_post(ChannelHandle channel, asccomm_simt_resolved_post& post);

    // Claims SQ space for one submission, draining completed CQEs to release capacity when the
    // queue is full. commit selects how much free space the claim requires: a deferred post also
    // reserves room for the immediate DWQE that will publish it.
    template <typename post_desc>
    __simt_callee__ inline bool reserve_post(
        ChannelHandle channel, const asccomm_simt_resolved_post& post, uint64_t& head_val);

    // The one posting path shared by every operator. post_desc describes what distinguishes them:
    // the SQ footprint (bb_cnt/sge_num) and how the WQE words are laid out. See the descriptors
    // in asccomm_simt_urma.h.
    template <typename post_desc>
    __simt_callee__ inline int32_t post_wqe(ChannelHandle channel, const post_desc& desc);
    template <typename post_desc>
    __simt_callee__ inline int32_t batch_post_wqe(ubc_ctp_batch_handle& batch_handle, const post_desc& desc);
    template <typename post_desc, typename coop_group>
    __simt_callee__ inline int32_t batch_post_wqe(
        ubc_ctp_batch_handle& batch_handle, const post_desc& desc, const coop_group& group);
    template <bool refresh_cache>
    __simt_callee__ inline uint32_t poll_cq(ChannelHandle channel, uint32_t expect_idx);
    template <bool refresh_cache, auto pipe>
    __simt_callee__ inline int32_t drain_channel(ChannelHandle channel);
};

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_URMA_DEF_H
#endif
