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
 * \file asccomm_simt_urma_work_queue_def.h
 * \brief asccomm_work_queue_impl class skeleton shared by the public header and the v310 implementation.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_WORK_QUEUE_DEF_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_WORK_QUEUE_DEF_H

#include <cstdint>

#include "../../../asccomm/impl/platform_v310/asccomm_simt_urma_def.h"
#include "../../../utils/simt_utils.h"

namespace asc::simt {

struct asccomm_sge;

// Geometry of the SIMT SQ layout: one WQEBB is 64B, a warp-cooperative WRITE
// reserves a fixed four-WQEBB slot for up to twelve SGEs.
constexpr uint32_t ASCCOMM_SIMT_WQEBB_LANES = ASCCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t);
constexpr uint32_t ASCCOMM_SIMT_WARP_SIZE = 32U;
constexpr uint32_t ASCCOMM_SIMT_MAX_SGES = 12U;
constexpr uint32_t ASCCOMM_SIMT_SLOT_WQEBBS = 4U;

enum class asccomm_post_opcode : uint8_t {
    WRITE,
    WRITE_VALUE,
    WRITE_WITH_NOTIFY,
};

namespace detail {
template <bool with_notify = false>
struct post_send_args {
    GM_ADDR dst_addr;
    const asccomm_sge* sges;
};

template <>
struct post_send_args<true> {
    GM_ADDR dst_addr;
    const asccomm_sge* sges;
    __gm__ void* notify_addr;
    uint64_t notify_value;
};

struct inline_post_send_args {
    GM_ADDR dst_addr;
    const uint8_t* value;
};
} // namespace detail

template <CommProtocol comm_protocol>
class asccomm_peer;

template <CommProtocol comm_protocol>
class asccomm_work_queue_impl;

template <>
class asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP> {
public:
    static constexpr CommProtocol protocol = COMM_PROTOCOL_UB_CTP;
    using peer_info = asccomm_peer<COMM_PROTOCOL_UB_CTP>;
    using work_queue = asccomm_work_queue_info<COMM_PROTOCOL_UB_CTP>;

    static constexpr uint32_t num_wqebb_lanes = ASCCOMM_SIMT_WQEBB_LANES;

    static constexpr uint32_t num_wqebbs_per_sqe_slot = ASCCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t num_max_sges_per_write = ASCCOMM_SIMT_MAX_SGES;

    static constexpr uint32_t num_write_wqebbs = ASCCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t num_write_with_notify_wqebbs = 2U;

    static constexpr uint32_t num_max_sqe_bytes = ASCCOMM_SIMT_SLOT_WQEBBS * ASCCOMM_URMA_WQE_BB_SIZE;

    static constexpr uint32_t num_wqebbs_per_write = ASCCOMM_SIMT_SLOT_WQEBBS;

    static constexpr uint32_t num_dwqe_words = 2U * ASCCOMM_SIMT_WQEBB_LANES;

    __simt_callee__ __forceinline__ asccomm_work_queue_impl() = default;

    __simt_callee__ __forceinline__ asccomm_work_queue_impl(
        __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx);

    __simt_callee__ __forceinline__
    asccomm_work_queue_impl(__ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, uint64_t channel_addr);

    __simt_callee__ __forceinline__ __ubuf__ work_queue* wq_buff() const;

    template <int64_t timeout_cycles, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t write_nbi(
        const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes) const;

    template <
        int64_t timeout_cycles, uint32_t sge_num, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t
    write_nbi(const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, const asccomm_sge* sges) const;

    template <int64_t timeout_cycles, typename T, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t
    write_value_nbi(const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, T value) const;

    template <int64_t timeout_cycles, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t write_with_notify_nbi(
        const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes,
        GM_ADDR notify_addr, uint64_t notify_value) const;

    __simt_callee__ __forceinline__ void ring_doorbell() const;

    template <typename coop_group>
    __simt_callee__ __forceinline__ void ring_doorbell(const coop_group& group) const;

    template <int64_t timeout_cycles>
    __simt_callee__ __forceinline__ int32_t drain() const;

    template <int64_t timeout_cycles, typename coop_group>
    __simt_callee__ __forceinline__ int32_t drain(const coop_group& group) const;

    __simt_callee__ __forceinline__ void attach(
        __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, uint64_t channel_addr);

private:
    struct batch_state {
        uint32_t wqebb_count;
        uint32_t last_offset;
        uint32_t failed;
    };

    __simt_callee__ __forceinline__ void init(
        __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx);

    __simt_callee__ __forceinline__ __ubuf__ batch_state* batch() const;

    template <int64_t timeout_cycles>
    __simt_callee__ __forceinline__ uint32_t reserve_implicit_sq(const uint32_t reserve_wqebbs) const;

    __simt_callee__ __forceinline__ uint32_t reserve_batch_sq(const uint32_t reserve_wqebbs) const;

    __simt_callee__ __forceinline__ void advance_sq() const;

    __simt_callee__ __forceinline__ void advance_implicit_sq() const;

    template <
        int64_t timeout_cycles, uint32_t sge_num, asccomm_post_opcode opcode, bool do_commit,
        uint32_t inline_bytes = 0U, typename coop_group, typename post_args>
    __simt_callee__ __forceinline__ int32_t
    post_send(const coop_group& group, const peer_info& peer, const post_args& args) const;

    __simt_callee__ __forceinline__ void advance_batch() const;

    uint64_t info_addr = 0U;

    uint64_t stage_addr = 0U;

    uint64_t channel = 0U;

    mutable uint32_t implicit_begin_head = 0U;

    mutable uint32_t implicit_wqebbs = 0U;

    mutable uint32_t implicit_payload_wqebbs = 0U;

    mutable bool implicit_active = false;

    static constexpr int32_t poll_cq_success = 0;

    static constexpr int32_t poll_cq_unavailable = 1;

    // Poll one CQE and advance the local SQ completion tail. The caller owns CQ tail and
    // doorbell updates so this helper can serve both drain and the incremental reservation path.
    template <int64_t timeout_cycles = ASCCOMM_SIMT_MAX_CQ_RETRY>
    __simt_callee__ __forceinline__ int32_t poll_cq_entry(const uint32_t current, uint32_t& released) const;

    // Reap one completed CQE, matching the SIMD PullCq behavior. This is used only by
    // the single-producer path, where no other producer can race the CQ tail update.
    template <int64_t timeout_cycles>
    __simt_callee__ __forceinline__ bool pull_cq_one() const;

    __simt_callee__ __forceinline__ static bool is_valid_sge(const asccomm_sge& sge);

    template <uint32_t sge_num>
    __simt_callee__ __forceinline__ static uint32_t count_scalar_sges(const asccomm_sge* sges);

    template <
        uint32_t sge_num, asccomm_post_opcode opcode, uint32_t inline_bytes, uint32_t wqebb_count, typename coop_group,
        typename post_args>
    __simt_callee__ __forceinline__ void fill_sq_warp_lane(
        const coop_group& group, const peer_info& peer, const post_args& args, const uint32_t head,
        const uint32_t runtime_wqebb_count, const uint32_t scalar_sge_num) const;

    template <uint32_t payload_words, uint32_t inline_bytes>
    __simt_callee__ __forceinline__ static uint64_t build_value_word(
        const peer_info& peer, const detail::inline_post_send_args& args, const uint32_t word, const uint32_t sq_idx);

    __simt_callee__ __forceinline__ static uint64_t build_notify_word(
        const peer_info& peer, const detail::post_send_args<true>& args, const uint32_t word, const uint32_t sq_idx);

    __simt_callee__ __forceinline__ static void store_sqe_pair(
        __gm__ uint64_t* sq, const uint32_t depth, const uint32_t head, const uint32_t first_word, const uint64_t first,
        const uint64_t second);
};

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_WORK_QUEUE_DEF_H
