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
 * \brief asccomm SIMT URMA definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H

#include "../../common/hcomm_simt_inner_def.h"

namespace AscendC::simt {

// SIMT rings the doorbell by copying a 128B DWQE to db_va - 0x80; the scalar register write the
// SIMD path uses is not available here.
constexpr uint32_t asccomm_urma_dwqe_db_offset = 0x80U;
constexpr uint32_t asccomm_urma_dwqe_size = 128U;
constexpr uint32_t asccomm_urma_wqe_bb_size = 64U;
constexpr uint32_t asccomm_urma_wqe_bb_cnt = 1U;
constexpr uint32_t asccomm_urma_dwqe_bb_cnt = asccomm_urma_dwqe_size / asccomm_urma_wqe_bb_size;
constexpr uint32_t asccomm_simt_max_cq_retry = 1000000U;

// Immutable for one channel while a kernel is running. The remote token fields
// are refreshed whenever the target address leaves the cached registered range.
struct asccomm_simt_ub_post_meta {
    uint32_t sq_depth = 0U;
    uint32_t tp_id = 0U;
    uint64_t remote_eid_l = 0U;
    uint64_t remote_eid_h = 0U;
    uint32_t remote_token_id = 0U;
    uint32_t remote_token_value = 0U;
};

// One read-only copy per SIMT block. Per-WQE token fields stay lane-local in
// asccomm_simt_ub_post_meta because lanes may target different registered buffers.
struct asccomm_simt_ub_post_context {
    uint32_t state;
    uint32_t reserved;
    channel_handle channel;
    uint64_t remote_buffers_addr;
    uint64_t head_addr;
    uint64_t sq_tail_addr;
    uint64_t dwqe_addr;
    uint64_t sq_base_addr;
    uint32_t sq_wqe_size;
    uint32_t remote_buffer_num;
    uint32_t sq_depth;
    uint32_t tp_id;
    uint64_t remote_eid_l;
    uint64_t remote_eid_h;
};

// Mutable remote-registration state is lane-private. Keeping it outside the
// block-shared post context avoids races when lanes target different buffers.
struct asccomm_simt_ub_remote_cache {
    uint64_t base_addr;
    uint64_t size;
    uint32_t token_id;
    uint32_t token_value;
    uint32_t valid;
    uint32_t reserved;
};

static_assert(sizeof(asccomm_simt_ub_remote_cache) == 32U, "SIMT per-lane remote cache entry must remain 32 bytes");
constexpr uint32_t asccomm_simt_ub_post_context_bytes = 128U;
static_assert(
    sizeof(asccomm_simt_ub_post_context) <= asccomm_simt_ub_post_context_bytes,
    "SIMT shared post context must fit in its 128-byte workspace region");
constexpr uint32_t asccomm_simt_ub_context_empty = 0U;
constexpr uint32_t asccomm_simt_ub_context_initializing = 1U;
constexpr uint32_t asccomm_simt_ub_context_ready = 2U;

struct asccomm_simt_ub_resolved_post {
    asccomm_simt_ub_post_meta meta;
    __gm__ uint64_t* head_addr;
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
class asccomm_impl<comm_protocol_ubc_ctp> {
public:
    __simt_callee__ inline asccomm_impl();
    __simt_callee__ inline ~asccomm_impl();
    __simt_callee__ inline int32_t init(__ubuf__ uint8_t* buff, uint32_t len);
    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t write_nbi(channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t read_nbi(channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len);
    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t write_with_notify_nbi(
        channel_handle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_val);
    template <typename t, bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t atomic_faa(channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t add_val);
    template <typename t, bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t atomic_cas(
        channel_handle channel, __gm__ void* dst, __gm__ void* fetch_addr, t compare_val, t swap_val);
    template <auto pipe = 0>
    __simt_callee__ inline int32_t drain(channel_handle channel);

private:
    __ubuf__ uint8_t* wqe_item_ = nullptr;
    __ubuf__ asccomm_simt_ub_post_context* ub_post_context_ = nullptr;
    __ubuf__ asccomm_simt_ub_remote_cache* ub_remote_cache_ = nullptr;
    bool ub_post_context_ready_ = false;

    __simt_callee__ inline void init_ub_post_context(channel_handle channel);
    __simt_callee__ inline bool ensure_ub_post_context(channel_handle channel);
    __simt_callee__ inline int32_t resolve_ub_post_context(
        channel_handle channel, __gm__ uint8_t* remote_addr, uint64_t len, asccomm_simt_ub_resolved_post& post);
    template <bool commit>
    __simt_callee__ inline bool reserve_post(
        channel_handle channel, const asccomm_simt_ub_resolved_post& post, uint32_t bb_cnt, uint64_t& head_val);

    template <
        bool commit = true, asccomm_urma_op_code op_code = asccomm_urma_op_code::write, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t post_send(
        channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len);
    template <bool commit = true, auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t post_write_with_notify(
        channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* local_addr, uint64_t len,
        __gm__ uint8_t* notify_addr, uint64_t notify_val);
    template <
        typename t, bool commit = true, asccomm_urma_op_code op_code = asccomm_urma_op_code::faa,
        auto const& config = urma_default_cfg>
    __simt_callee__ inline int32_t post_atomic(
        channel_handle channel, __gm__ uint8_t* remote_addr, __gm__ uint8_t* fetch_addr, t value, t cond);
    __simt_callee__ inline uint32_t poll_cq(channel_handle channel, uint32_t expect_idx);
};

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_DEF_H
#endif
