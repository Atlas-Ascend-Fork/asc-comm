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
 * \file asccomm_simt_urma_jetty.h
 * \brief SIMT Jetty implementation.
 *
 * A WRITE occupies the WQEBBs required by its 48-byte header and active SGEs,
 * up to four WQEBBs for twelve SGEs. SIMT fills the SQ; publication can be
 * performed by SIMD through asccomm_jetty_publish_sq().
 */
#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_JETTY_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_SIMT_URMA_JETTY_H

#include <cstddef>
#include <cstdint>
#include <kernel_operator.h>
#include <simt_api/device_atomic_functions.h>
#include <simt_api/device_warp_functions.h>
#include <simt_api/cooperative_groups.h>

#include "asccomm_simt_urma_jetty_def.h"
#include "../../../asccomm/common/asccomm_inner_def.h"
#include "../../../asccomm/common/asccomm_simt_utils.h"
#include "../../../asccomm/impl/asccomm_simt_impl_def.h"
#include "../../../utils/simt_utils.h"

namespace asc::simt {
constexpr uint32_t ASCCOMM_SIMT_SQE_HEADER_WORDS = 6U;
constexpr uint64_t ASCCOMM_SIMT_WRITE_HEADER = 0x0000030030010000ULL;
constexpr uint64_t ASCCOMM_SIMT_NOP_HEADER = 0x0000110000000000ULL;
constexpr uint64_t ASCCOMM_SIMT_FINAL_FLAGS = (1ULL << 18U) | (1ULL << 19U) | (1ULL << 21U);
constexpr uint64_t ASCCOMM_SIMT_NOP_CONTROL_FLAGS = (1ULL << 28U) | (1ULL << 29U);
constexpr uint64_t ASCCOMM_SIMT_NOP_FINAL_FLAGS =
    (1ULL << 16U) | ASCCOMM_SIMT_FINAL_FLAGS | ASCCOMM_SIMT_NOP_CONTROL_FLAGS;
constexpr uint64_t ASCCOMM_SIMT_NOP_PUBLISH_FLAGS = ASCCOMM_SIMT_NOP_FINAL_FLAGS & ~(1ULL << 21U);

template <uint32_t sge_num, asccomm_post_opcode opcode, uint32_t inline_bytes>
__simt_callee__ constexpr uint32_t post_send_payload_words()
{
    if constexpr (opcode == asccomm_post_opcode::WRITE) {
        return ASCCOMM_SIMT_SQE_HEADER_WORDS + sge_num * 2U;
    } else if constexpr (opcode == asccomm_post_opcode::WRITE_WITH_NOTIFY) {
        return 2U * ASCCOMM_SIMT_WQEBB_LANES;
    } else {
        return ASCCOMM_SIMT_SQE_HEADER_WORDS + (inline_bytes + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    }
}

template <uint32_t sge_num, asccomm_post_opcode opcode, uint32_t inline_bytes>
__simt_callee__ constexpr uint32_t post_send_payload_wqebbs()
{
    return (post_send_payload_words<sge_num, opcode, inline_bytes>() + ASCCOMM_SIMT_WQEBB_LANES - 1U) /
           ASCCOMM_SIMT_WQEBB_LANES;
}

// SIMD and SIMT describe the same hardware queues and exchange these descriptors through UB,
// so both use the single definition in jetty/common. asccomm_peer_info keeps the SIMT-side spelling
// of the peer descriptor; the layout and the size/offset asserts live with the definition.

namespace detail {

template <typename T>
__simt_callee__ __forceinline__ T load(const uint64_t addr, const uint32_t offset)
{
    return *reinterpret_cast<__gm__ T*>(addr + offset);
}
__simt_callee__ __forceinline__ uint64_t
peer_word(__ubuf__ const asccomm_peer_info* p, const uint32_t word, const uint32_t sge_num, __gm__ void* dst)
{
    auto* words = reinterpret_cast<__ubuf__ const uint64_t*>(p);
    const uint64_t cached = words[word - 1U];
    const uint64_t word1 = (cached & 0x000FFFFF00FFFFFFULL) | (static_cast<uint64_t>(sge_num) << 24U);
    const uint64_t word1_mask = 0ULL - static_cast<uint64_t>(word == 1U);
    const uint64_t word5_mask = 0ULL - static_cast<uint64_t>(word == 5U);
    return (cached & ~(word1_mask | word5_mask)) | (word1 & word1_mask) |
           (reinterpret_cast<uint64_t>(dst) & word5_mask);
}
} // namespace detail

class asccomm_peer {
public:
    __simt_callee__ __forceinline__ asccomm_peer() = default;
    __simt_callee__ __forceinline__
    asccomm_peer(__ubuf__ asccomm_peer_info* info, __gm__ void* jetty_table, uint32_t jetty_idx)
    {
        init(info, jetty_table, jetty_idx);
    }
    __simt_callee__ __forceinline__ asccomm_peer(__ubuf__ asccomm_peer_info* info, uint64_t channel_addr)
    {
        attach(info);
    }
    __simt_callee__ __forceinline__ __ubuf__ asccomm_peer_info* info() const
    {
        return reinterpret_cast<__ubuf__ asccomm_peer_info*>(info_addr);
    }

    // Keep the two-argument attach form used by existing SIMT kernels. The
    // channel is already represented by the cached peer metadata.
    __simt_callee__ __forceinline__ void attach(__ubuf__ asccomm_peer_info* info, uint64_t) { attach(info); }

    /*!
     * @brief Deprecated PascalCase alias. Forwards to info.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", info)
    __simt_callee__ __forceinline__ __ubuf__ asccomm_peer_info* Info() const { return info(); }

    /*!
     * @brief Deprecated PascalCase alias. Forwards to attach.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", attach)
    __simt_callee__ __forceinline__ void Attach(__ubuf__ asccomm_peer_info* info) { attach(info); }

    /*!
     * @brief Deprecated PascalCase alias. Forwards to attach.
     */
    ASC_DEPRECATED(9.3.0, "2027/12/31", attach)
    __simt_callee__ __forceinline__ void Attach(__ubuf__ asccomm_peer_info* info, uint64_t channel)
    {
        attach(info, channel);
    }

private:
    __simt_callee__ __forceinline__ void attach(__ubuf__ asccomm_peer_info* info)
    {
        info_addr = reinterpret_cast<uint64_t>(info);
    }
    __simt_callee__ __forceinline__ void init(
        __ubuf__ asccomm_peer_info* info, __gm__ void* jetty_table, uint32_t jetty_idx)
    {
        const uint64_t table = reinterpret_cast<uint64_t>(jetty_table);
        ASCCOMM_SIMT_DEBUG_TRAP_IF(table == 0U || info == nullptr, "invalid SIMT asccomm_peer initialization\n");
        const uint64_t channel_addr = *reinterpret_cast<__gm__ uint64_t*>(table + jetty_idx * sizeof(uint64_t));
        attach(info);
        ASCCOMM_SIMT_DEBUG_TRAP_IF(channel_addr == 0U, "invalid SIMT asccomm_peer channel\n");
        const uint64_t sq_ctx_addr = detail::load<uint64_t>(channel_addr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t rb_addr = detail::load<uint64_t>(channel_addr, offsetof(ChannelEntity, remoteBufferAddr));
        const uint32_t rb_num = detail::load<uint32_t>(channel_addr, offsetof(ChannelEntity, remoteBufferNum));
        ASCCOMM_SIMT_DEBUG_TRAP_IF(
            sq_ctx_addr == 0U || rb_addr == 0U || rb_num == 0U, "invalid SIMT asccomm_peer channel context\n");
        auto* sq = reinterpret_cast<__gm__ SqContext*>(sq_ctx_addr) + ASCCOMM_URMA_DEFAULT_QP_IDX;
        auto* rb = reinterpret_cast<__gm__ RegedBufferEntity*>(rb_addr) + rb_num - 1U;
        info->tp_id_num_sges_remote_token_id =
            static_cast<uint64_t>(sq->contextInfo.ubJfs.tpID) |
            (static_cast<uint64_t>(rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
        uint64_t remote_eid_l = 0U;
        uint64_t remote_eid_h = 0U;
        for (uint32_t i = 0U; i < 8U; ++i) {
            remote_eid_l |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i]) << (i * 8U);
            remote_eid_h |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i + 8U]) << (i * 8U);
        }
        info->remote_eid[0] = remote_eid_l;
        info->remote_eid[1] = remote_eid_h;
        info->remote_token_value_udf = rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        info->remote_base_addr = rb->bufferInfo.rma.addr;
        info->remote_buffer_size = rb->bufferInfo.rma.size;
    }

private:
    uint64_t info_addr = 0U;
};

__simt_callee__ __forceinline__ jetty_impl::jetty_impl(
    __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, __gm__ void* table,
    uint32_t index)
{
    init(info, stage, table, index);
}

__simt_callee__ __forceinline__ jetty_impl::jetty_impl(
    __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, uint64_t channel_addr)
{
    asccomm_jetty_attach(info, stage, channel_addr);
}

__simt_callee__ __forceinline__ __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* jetty_impl::info() const
{
    return reinterpret_cast<__ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>*>(info_addr);
}

template <uint32_t sge_num, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t jetty_impl::asccomm_jetty_write_nbi(
    const asccomm_peer& peer, __gm__ void* dst, const jetty_sge* sges, const coop_group& group) const
{
    if constexpr (asccomm_coop_traits<coop_group>::warp_capable) {
        const jetty_sge lane_sge = sges[0];
        return post_send<sge_num, asccomm_post_opcode::WRITE, do_commit>(
            group, peer, detail::post_send_args<>{dst, &lane_sge});
    } else {
        return post_send<sge_num, asccomm_post_opcode::WRITE, do_commit>(
            group, peer, detail::post_send_args<>{dst, sges});
    }
}

template <int64_t, typename T, bool do_commit>
__simt_callee__ __forceinline__ int32_t
jetty_impl::asccomm_jetty_write_value_nbi(const asccomm_peer& peer, __gm__ void* dst, T value)
{
    static_assert(
        sizeof(T) <= ASCCOMM_URMA_WQE_BB_SIZE - ASCCOMM_SIMT_SQE_HEADER_WORDS * sizeof(uint64_t),
        "inline value does not fit in a URMA WQEBB");
    asccomm_coop_thread thread;
    return post_send<0U, asccomm_post_opcode::WRITE_VALUE, do_commit, sizeof(T)>(
        thread, peer, detail::inline_post_send_args{dst, reinterpret_cast<const uint8_t*>(&value)});
}

template <int64_t, bool do_commit>
__simt_callee__ __forceinline__ int32_t jetty_impl::asccomm_jetty_write_with_notify_nbi(
    const asccomm_peer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
    uint64_t notify_value)
{
    asccomm_coop_thread thread;
    const jetty_sge sges[] = {{reinterpret_cast<uint64_t>(src), static_cast<int>(len)}};
    return post_send<1U, asccomm_post_opcode::WRITE_WITH_NOTIFY, do_commit>(
        thread, peer, detail::post_send_args<true>{dst, sges, notify_addr, notify_value});
}

__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_advance_sq() const
{
    if (implicit_active && implicit_wqebbs != 0U) {
        advance_implicit_sq();
    } else if (stage_addr != 0U && batch()->wqebb_count != 0U) {
        advance_batch();
    }
}

template <typename coop_group>
__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_advance_sq(const coop_group& group) const
{
    group.sync();
    if (group.thread_rank() == 0U)
        asccomm_jetty_advance_sq();
    group.sync();
}

template <typename coop_group>
__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_advance_sq(
    const coop_group& group, uint32_t, uint32_t) const
{
    asccomm_jetty_advance_sq(group);
}

__simt_callee__ __forceinline__ void jetty_impl::advance_implicit_sq() const
{
    auto* info = this->info();
    const uint32_t end = static_cast<uint32_t>(*reinterpret_cast<__ubuf__ uint32_t*>(&info->packed_head));
    const uint32_t begin = implicit_begin_head;
    const uint32_t submitted_wqebbs = implicit_wqebbs;
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        end <= begin || submitted_wqebbs > ASCCOMM_SIMT_SLOT_WQEBBS || submitted_wqebbs > end - begin,
        "invalid SIMT asccomm_jetty advance range\n");
    const uint32_t last = end - submitted_wqebbs;
    const uint32_t payload_wqebbs = implicit_payload_wqebbs == 0U ? submitted_wqebbs : implicit_payload_wqebbs;
    ASCCOMM_SIMT_DEBUG_TRAP_IF(payload_wqebbs > submitted_wqebbs, "invalid SIMT asccomm_jetty payload range\n");
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sq_base_addr);
    const uint32_t idx = last & (info->sq_depth - 1U);
    uint64_t header = ASCCOMM_SIMT_WRITE_HEADER | idx;
    if (payload_wqebbs == submitted_wqebbs) {
        header = ASCCOMM_SIMT_WRITE_HEADER | (end & 0xFFFFU) | ASCCOMM_SIMT_FINAL_FLAGS;
    } else {
        const uint32_t nop_head = begin + payload_wqebbs;
        const uint32_t nop_idx = nop_head & (info->sq_depth - 1U);
        const uint64_t nop = ASCCOMM_SIMT_NOP_HEADER | (end & 0xFFFFU) | ASCCOMM_SIMT_NOP_FINAL_FLAGS;
        asc_stwt(
            reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(nop_idx) * ASCCOMM_SIMT_WQEBB_LANES), nop);
    }
    asc_threadfence();
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * ASCCOMM_SIMT_WQEBB_LANES), header);
    asc_atomic_add(reinterpret_cast<__ubuf__ uint32_t*>(&info->packed_head) + 1, 1U);
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr), info->packed_head);
    implicit_active = false;
    implicit_wqebbs = 0U;
    implicit_payload_wqebbs = 0U;
}

__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_publish_sq() const
{
    auto* info = this->info();
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        info == nullptr || info->sq_head_addr == 0U || info->sq_doorbell_addr == 0U || info->sq_depth == 0U,
        "invalid SIMT asccomm_jetty publish state\n");
    const uint64_t packed_head = *reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr);
    const uint32_t head = static_cast<uint32_t>(packed_head);
    // A publication DWQE consumes two BBs. Its NOP headers therefore encode
    // the post-DWQE PI and the following BB index, respectively.
    const uint32_t next_head = head + ASCCOMM_URMA_DWQE_BB_CNT;
    const uint64_t nop0 = ASCCOMM_SIMT_NOP_HEADER | (next_head & 0xFFFFU);
    const uint64_t nop1 = ASCCOMM_SIMT_NOP_HEADER | ((next_head + 1U) & 0xFFFFU) | ASCCOMM_SIMT_NOP_PUBLISH_FLAGS;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sq_base_addr);
    store_sqe_pair(sq, info->sq_depth, head, 0U, nop0, 0U);
    store_sqe_pair(sq, info->sq_depth, head, ASCCOMM_SIMT_WQEBB_LANES, nop1, 0U);
    info->packed_head = (packed_head & 0xFFFFFFFF00000000ULL) | next_head;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr), info->packed_head);

    // Cache-locked Jetty doorbells consume a complete 128B DWQE. Both NOPs
    // follow the SQ payload instead of aliasing it. The second NOP publishes
    // their final PI but intentionally omits bit 21 (CQE).
    auto* dwqe = reinterpret_cast<__gm__ ulonglong2*>(info->sq_doorbell_addr - ASCCOMM_URMA_DWQE_DB_OFFSET);
    asc_threadfence();
    asc_stwt(&dwqe[0], make_ulonglong2(nop0, 0U));
    asc_stwt(&dwqe[1], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[2], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[3], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[4], make_ulonglong2(nop1, 0U));
}

template <typename coop_group>
__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_publish_sq(const coop_group& group) const
{
    group.sync();
    if (group.thread_rank() == 0U)
        asccomm_jetty_publish_sq();
    group.sync();
}

template <int64_t timeout_cycles>
__simt_callee__ __forceinline__ int32_t jetty_impl::asccomm_jetty_drain() const
{
    auto* info = this->info();
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        info == nullptr || info->cq_tail_addr == 0U || info->cq_base_addr == 0U || info->cq_depth == 0U ||
            info->num_cqe_bytes == 0U || info->completion_tail_addr == 0U || info->cq_doorbell_addr == 0U,
        "invalid SIMT asccomm_jetty CQ state\n");
    const uint32_t expected = static_cast<uint32_t>(info->packed_head >> 32U);
    auto* tail = reinterpret_cast<__gm__ uint32_t*>(info->cq_tail_addr);
    auto* sq_tail = reinterpret_cast<__gm__ uint32_t*>(info->completion_tail_addr);
    uint32_t current = *tail;
    uint32_t released = *sq_tail;
    while (current != expected) {
        if (poll_cq_entry<timeout_cycles>(current, released) != poll_cq_success)
            return -1;
        ++current;
    }
    *tail = current;
    *reinterpret_cast<__gm__ uint32_t*>(info->cq_doorbell_addr) = current & 0xFFFFFFU;
    *sq_tail = released;
    return 0;
}

template <int64_t timeout_cycles, typename coop_group>
__simt_callee__ __forceinline__ int32_t jetty_impl::asccomm_jetty_drain(const coop_group& group) const
{
    group.sync();
    int32_t result = 0;
    if (group.thread_rank() == 0U)
        result = asccomm_jetty_drain<timeout_cycles>();
    result = asc_shfl(result, 0, ASCCOMM_SIMT_WARP_SIZE);
    group.sync();
    return result;
}

__simt_callee__ __forceinline__ void jetty_impl::asccomm_jetty_attach(
    __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, uint64_t channel_addr)
{
    info_addr = reinterpret_cast<uint64_t>(info);
    stage_addr = reinterpret_cast<uint64_t>(stage);
    channel = channel_addr;
}

__simt_callee__ __forceinline__ void jetty_impl::init(
    __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, __gm__ void* table,
    uint32_t index)
{
    const uint64_t table_addr = reinterpret_cast<uint64_t>(table);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(table_addr == 0U || info == nullptr, "invalid SIMT asccomm_jetty initialization\n");
    const uint64_t channel_addr = *reinterpret_cast<__gm__ uint64_t*>(table_addr + index * sizeof(uint64_t));
    asccomm_jetty_attach(info, stage, channel_addr);
    if (stage != nullptr) {
        auto* batch = reinterpret_cast<__ubuf__ batch_state*>(stage);
        batch->wqebb_count = 0U;
        batch->last_offset = 0U;
        batch->failed = 0U;
    }
    ASCCOMM_SIMT_DEBUG_TRAP_IF(channel_addr == 0U, "invalid SIMT asccomm_jetty channel\n");
    const uint64_t sq_ctx_addr = detail::load<uint64_t>(channel_addr, offsetof(ChannelEntity, sqContextAddr));
    const uint64_t cq_ctx_addr = detail::load<uint64_t>(channel_addr, offsetof(ChannelEntity, cqContextAddr));
    ASCCOMM_SIMT_DEBUG_TRAP_IF(sq_ctx_addr == 0U || cq_ctx_addr == 0U, "invalid SIMT asccomm_jetty channel context\n");
    auto* sq = reinterpret_cast<__gm__ SqContext*>(sq_ctx_addr) + ASCCOMM_URMA_DEFAULT_QP_IDX;
    auto* cq = reinterpret_cast<__gm__ CqContext*>(cq_ctx_addr) + ASCCOMM_URMA_DEFAULT_QP_IDX;
    info->sq_base_addr = sq->contextInfo.ubJfs.sqVa;
    info->sq_head_addr = sq->contextInfo.ubJfs.headAddr;
    info->completion_tail_addr = sq->contextInfo.ubJfs.tailAddr;
    info->sq_doorbell_addr = sq->contextInfo.ubJfs.dbVa;
    info->cq_base_addr = cq->contextInfo.ubJfc.scqVa;
    info->cq_tail_addr = cq->contextInfo.ubJfc.tailAddr;
    info->cq_doorbell_addr = cq->contextInfo.ubJfc.dbVa;
    info->num_wqebb_bytes = sq->contextInfo.ubJfs.wqeSize;
    info->num_cqe_bytes = cq->contextInfo.ubJfc.cqeSize;
    info->sq_depth = sq->contextInfo.ubJfs.sqDepth;
    info->cq_depth = cq->contextInfo.ubJfc.cqDepth;
    ASCCOMM_SIMT_DEBUG_TRAP_IF(info->sq_head_addr == 0U, "invalid SIMT asccomm_jetty SQ head\n");
    info->packed_head = *reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr);
}

__simt_callee__ __forceinline__ __ubuf__ jetty_impl::batch_state* jetty_impl::batch() const
{
    return reinterpret_cast<__ubuf__ batch_state*>(stage_addr);
}

__simt_callee__ __forceinline__ uint32_t jetty_impl::reserve_implicit_sq(const uint32_t reserve_wqebbs) const
{
    auto* info = this->info();
    auto* sq_head = reinterpret_cast<__ubuf__ uint32_t*>(&info->packed_head);
    auto* sq_tail = reinterpret_cast<__gm__ uint32_t*>(info->completion_tail_addr);
    const uint32_t depth = info->sq_depth;
    const uint32_t old_head = *sq_head;
    // Leave room for the two-BB publication DWQE.
    const uint32_t required_free_wqebbs = reserve_wqebbs + ASCCOMM_URMA_DWQE_BB_CNT;
    uint32_t consumed = old_head - *sq_tail;
    while ((consumed > depth || required_free_wqebbs > depth - consumed) && pull_cq_one()) {
        consumed = old_head - *sq_tail;
    }
    uint32_t head = 0xFFFFFFFFU;
    if (consumed <= depth && required_free_wqebbs <= depth - consumed) {
        head = old_head;
        *sq_head = old_head + reserve_wqebbs;
        implicit_begin_head = old_head;
        implicit_wqebbs = reserve_wqebbs;
        implicit_payload_wqebbs = reserve_wqebbs;
        implicit_active = true;
    }
    return head;
}

__simt_callee__ __forceinline__ uint32_t jetty_impl::reserve_batch_sq(const uint32_t reserve_wqebbs) const
{
    if (stage_addr == 0U)
        return 0xFFFFFFFFU;
    auto* info = this->info();
    auto* batch = this->batch();
    // Keep packed_head at the published base while warps claim batch offsets.
    const uint32_t offset = asc_atomic_add(&batch->wqebb_count, reserve_wqebbs);
    const uint32_t base = static_cast<uint32_t>(info->packed_head);
    const uint32_t consumed = base - *reinterpret_cast<__gm__ uint32_t*>(info->completion_tail_addr);
    const uint32_t required_free_wqebbs = reserve_wqebbs + ASCCOMM_URMA_DWQE_BB_CNT;
    if (consumed <= info->sq_depth && offset <= info->sq_depth - consumed &&
        required_free_wqebbs <= info->sq_depth - consumed - offset) {
        asc_atomic_max(&batch->last_offset, offset);
        return base + offset;
    }
    asc_atomic_exch(&batch->failed, 1U);
    return 0xFFFFFFFFU;
}

template <
    uint32_t sge_num, asccomm_post_opcode opcode, bool do_commit, uint32_t inline_bytes, typename coop_group,
    typename post_args>
__simt_callee__ __forceinline__ int32_t
jetty_impl::post_send(const coop_group& group, const asccomm_peer& peer, const post_args& args) const
{
    static_assert(opcode != asccomm_post_opcode::WRITE_VALUE || sge_num == 0U, "inline writes do not carry SGEs");
    static_assert(
        opcode != asccomm_post_opcode::WRITE_WITH_NOTIFY || sge_num == 1U,
        "WRITE_WITH_NOTIFY currently supports one SGE");
    static_assert(sge_num <= ASCCOMM_SIMT_MAX_SGES, "invalid SGE count");
    static_assert(
        post_send_payload_wqebbs<sge_num, opcode, inline_bytes>() <= ASCCOMM_SIMT_SLOT_WQEBBS,
        "post-send payload exceeds the SQ slot");
    static_assert(
        opcode != asccomm_post_opcode::WRITE_VALUE || inline_bytes != 0U,
        "inline WRITE requires a compile-time value size");
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t lane_rank = rank % ASCCOMM_SIMT_WARP_SIZE;
    if constexpr (do_commit) {
        if (group.size() > static_cast<int>(ASCCOMM_SIMT_WARP_SIZE)) {
            group.sync();
            return -1;
        }
    }
    if (group.size() != 1) {
        group.sync();
    }

    constexpr uint32_t static_payload_wqebbs = post_send_payload_wqebbs<sge_num, opcode, inline_bytes>();
    constexpr bool deep_ep_warp_write =
        asccomm_coop_traits<coop_group>::warp_capable && opcode == asccomm_post_opcode::WRITE && !do_commit;
    // Scalar WRITE reserves only its actual payload; warp producers retain their
    // fixed reservation so all lanes can fill one uniform SQ layout.
    constexpr bool scalar_write =
        !asccomm_coop_traits<coop_group>::warp_capable && opcode == asccomm_post_opcode::WRITE;
    constexpr uint32_t reserved_wqebbs = deep_ep_warp_write ? ASCCOMM_SIMT_SLOT_WQEBBS : static_payload_wqebbs;
    uint32_t reserve_wqebbs = reserved_wqebbs;
    uint32_t actual_sge_num = sge_num;
    if constexpr (scalar_write) {
        actual_sge_num = count_scalar_sges<sge_num>(args.sges);
        reserve_wqebbs = (ASCCOMM_SIMT_SQE_HEADER_WORDS + actual_sge_num * 2U + ASCCOMM_SIMT_WQEBB_LANES - 1U) /
                         ASCCOMM_SIMT_WQEBB_LANES;
    }

    // Reserve once per warp; only single-producer posts may reclaim CQ entries.
    uint32_t head = 0xFFFFFFFFU;
    if (lane_rank == 0U) {
        if constexpr (do_commit || scalar_write) {
            head = reserve_implicit_sq(reserve_wqebbs);
        } else {
            head = reserve_batch_sq(reserve_wqebbs);
        }
    }
    if (group.size() > 1)
        head = asc_shfl(head, 0, ASCCOMM_SIMT_WARP_SIZE);
    if (head == 0xFFFFFFFFU) {
        if (group.size() > 1)
            group.sync();
        return group.size() > ASCCOMM_SIMT_WARP_SIZE ? 0 : -1;
    }

    fill_sq_warp_lane<sge_num, opcode, inline_bytes, reserved_wqebbs>(
        group, peer, args, head, reserve_wqebbs, actual_sge_num);
    asc_threadfence();
    group.sync();
    if constexpr (do_commit) {
        asccomm_jetty_advance_sq(group);
        asccomm_jetty_publish_sq(group);
    }
    return 0;
}

__simt_callee__ __forceinline__ void jetty_impl::advance_batch() const
{
    auto* info = this->info();
    auto* batch = this->batch();
    const uint32_t count = batch->wqebb_count;
    if (count == 0U)
        return;
    if (batch->failed != 0U) {
        batch->wqebb_count = 0U;
        batch->last_offset = 0U;
        batch->failed = 0U;
        return;
    }
    const uint32_t begin = static_cast<uint32_t>(info->packed_head);
    const uint32_t end = begin + count;
    const uint32_t last = begin + batch->last_offset;
    const uint32_t depth = info->sq_depth;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sq_base_addr);
    const uint32_t idx = last & (depth - 1U);
    const uint64_t first_word =
        *reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * ASCCOMM_SIMT_WQEBB_LANES);
    const uint64_t header = (first_word & ~0xFFFFULL) | (end & 0xFFFFU) | ASCCOMM_SIMT_FINAL_FLAGS;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * ASCCOMM_SIMT_WQEBB_LANES), header);
    asc_threadfence();
    const uint64_t packed = static_cast<uint64_t>(end) |
                            ((static_cast<uint64_t>(static_cast<uint32_t>(info->packed_head >> 32U) + 1U)) << 32U);
    info->packed_head = packed;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sq_head_addr), packed);
    batch->wqebb_count = 0U;
    batch->last_offset = 0U;
    batch->failed = 0U;
}

// Poll one CQE and advance the local SQ completion tail. The caller owns CQ tail and
// doorbell updates so this helper can serve both drain and the incremental reservation path.
template <int64_t timeout_cycles>
__simt_callee__ __forceinline__ int32_t jetty_impl::poll_cq_entry(const uint32_t current, uint32_t& released) const
{
    auto* info = this->info();
    auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
        info->cq_base_addr + static_cast<uint64_t>(info->num_cqe_bytes) * (current & (info->cq_depth - 1U)));
    const uint32_t phase = (current / info->cq_depth) & 1U;
    uint32_t header = *cqe;
    int64_t retry = 0;
    while (((header >> 2U) & 1U) == phase && retry < timeout_cycles) {
        asc_threadfence();
        asc_dcci_single(cqe);
        header = *cqe;
        ++retry;
    }
    if (retry >= timeout_cycles)
        return poll_cq_unavailable;
    if (((header >> 16U) & 0xFFU) != 0U || ((header >> 24U) & 0xFFU) != 0U)
        return -1;
    const uint32_t completed = cqe[1] & (info->sq_depth - 1U);
    released += (completed + info->sq_depth - (released & (info->sq_depth - 1U))) & (info->sq_depth - 1U);
    ++released;
    return poll_cq_success;
}

// Reap one completed CQE, matching the SIMD PullCq behavior. This is used only by
// the single-producer path, where no other producer can race the CQ tail update.
__simt_callee__ __forceinline__ bool jetty_impl::pull_cq_one() const
{
    auto* info = this->info();
    const uint32_t expected = static_cast<uint32_t>(info->packed_head >> 32U);
    auto* cq_tail = reinterpret_cast<__gm__ uint32_t*>(info->cq_tail_addr);
    auto* sq_tail = reinterpret_cast<__gm__ uint32_t*>(info->completion_tail_addr);
    uint32_t current = *cq_tail;
    if (current == expected)
        return false;
    uint32_t released = *sq_tail;
    if (poll_cq_entry<>(current, released) != poll_cq_success) {
        return false;
    }
    ++current;
    *cq_tail = current;
    *reinterpret_cast<__gm__ uint32_t*>(info->cq_doorbell_addr) = current & 0xFFFFFFU;
    *sq_tail = released;
    return true;
}

__simt_callee__ __forceinline__ bool jetty_impl::is_valid_sge(const jetty_sge& sge)
{
    return sge.addr != 0U && sge.len > 0;
}

template <uint32_t sge_num>
__simt_callee__ __forceinline__ uint32_t jetty_impl::count_scalar_sges(const jetty_sge* sges)
{
    uint32_t count = 0U;
#pragma unroll
    for (uint32_t i = 0U; i < sge_num; ++i) {
        count += static_cast<uint32_t>(is_valid_sge(sges[i]));
    }
    return count;
}

template <
    uint32_t sge_num, asccomm_post_opcode opcode, uint32_t inline_bytes, uint32_t wqebb_count, typename coop_group,
    typename post_args>
__simt_callee__ __forceinline__ void jetty_impl::fill_sq_warp_lane(
    const coop_group& group, const asccomm_peer& peer, const post_args& args, const uint32_t head,
    const uint32_t runtime_wqebb_count, const uint32_t scalar_sge_num) const
{
    constexpr uint32_t payload_words = post_send_payload_words<sge_num, opcode, inline_bytes>();
    constexpr uint32_t wqebb_total_words = wqebb_count * ASCCOMM_SIMT_WQEBB_LANES;
    constexpr bool warp_lane = asccomm_coop_traits<coop_group>::warp_capable;
    const uint32_t lane = warp_lane ? static_cast<uint32_t>(group.thread_rank()) % ASCCOMM_SIMT_WARP_SIZE : 0U;
    auto* info = this->info();
    const uint32_t depth = info->sq_depth;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sq_base_addr);
    uint32_t active_sges = scalar_sge_num;
    if constexpr (warp_lane) {
        active_sges = sge_num;
    }
    jetty_sge lane_sge{};
    if constexpr (opcode == asccomm_post_opcode::WRITE) {
        if constexpr (warp_lane) {
            lane_sge = args.sges[0];
        }
    }
    constexpr uint32_t warp_payload_words = ASCCOMM_SIMT_SQE_HEADER_WORDS + sge_num * 2U;
    const uint32_t active_payload_words =
        warp_lane ? warp_payload_words : ASCCOMM_SIMT_SQE_HEADER_WORDS + active_sges * 2U;

    // A warp maps one lane to one SQE word. Scalar producers walk the same layout
    // serially because they have no cooperating lanes.
    const uint32_t total_words = warp_lane ? wqebb_total_words : runtime_wqebb_count * ASCCOMM_SIMT_WQEBB_LANES;
    const uint32_t round_count = warp_lane ? 1U : total_words;
    for (uint32_t round = 0U; round < round_count; ++round) {
        const uint32_t word = warp_lane ? lane : round;
        const bool active = word < total_words;
        const uint32_t logical_word = active ? word : 0U;
        const uint32_t bb_head = head + logical_word / ASCCOMM_SIMT_WQEBB_LANES;
        const uint32_t sq_idx = bb_head & (depth - 1U);
        uint64_t value = 0U;
        if constexpr (opcode == asccomm_post_opcode::WRITE) {
            static_assert(sge_num > 0U, "WRITE requires at least one SGE");
            const uint32_t sge = logical_word >= ASCCOMM_SIMT_SQE_HEADER_WORDS && logical_word < active_payload_words ?
                                     (logical_word - ASCCOMM_SIMT_SQE_HEADER_WORDS) / 2U :
                                     0U;
            uint64_t sge_addr = 0U;
            int sge_len = 0;
            if constexpr (warp_lane) {
                // Every lane executes the shuffle, including header and padding
                // lanes, so the warp follows one uniform data path.
                sge_addr = asc_shfl(lane_sge.addr, static_cast<int>(sge), ASCCOMM_SIMT_WARP_SIZE);
                sge_len = asc_shfl(lane_sge.len, static_cast<int>(sge), ASCCOMM_SIMT_WARP_SIZE);
            } else if (logical_word >= ASCCOMM_SIMT_SQE_HEADER_WORDS && logical_word < active_payload_words) {
                sge_addr = args.sges[sge].addr;
                sge_len = args.sges[sge].len;
            }
            if (logical_word == 0U) {
                value = ASCCOMM_SIMT_WRITE_HEADER | sq_idx;
            } else if (logical_word < ASCCOMM_SIMT_SQE_HEADER_WORDS) {
                value = detail::peer_word(peer.info(), logical_word, active_sges, args.dst);
            } else if (logical_word < active_payload_words) {
                value = (logical_word & 1U) == 0U ? static_cast<uint64_t>(sge_len) : sge_addr;
            } else if constexpr (warp_lane) {
                if (logical_word < total_words && logical_word % ASCCOMM_SIMT_WQEBB_LANES == 0U &&
                    wqebb_count > (warp_payload_words + ASCCOMM_SIMT_WQEBB_LANES - 1U) / ASCCOMM_SIMT_WQEBB_LANES) {
                    value = ASCCOMM_SIMT_NOP_HEADER | sq_idx;
                }
            }
        } else if constexpr (opcode == asccomm_post_opcode::WRITE_VALUE) {
            value = build_value_word<payload_words, inline_bytes>(peer, args, logical_word, sq_idx);
        } else {
            value = build_notify_word(peer, args, logical_word, sq_idx);
        }
        if (active) {
            asc_stcg(
                sq + static_cast<uint64_t>(sq_idx) * ASCCOMM_SIMT_WQEBB_LANES + logical_word % ASCCOMM_SIMT_WQEBB_LANES,
                value);
        }
    }
}

template <uint32_t payload_words, uint32_t inline_bytes>
__simt_callee__ __forceinline__ uint64_t jetty_impl::build_value_word(
    const asccomm_peer& peer, const detail::inline_post_send_args& args, const uint32_t word, const uint32_t sq_idx)
{
    if (word == 0U) {
        return ASCCOMM_SIMT_WRITE_HEADER | sq_idx | (1ULL << 22U) |
               (static_cast<uint64_t>(inline_bytes & 0x3FFU) << 54U);
    }
    if (word < ASCCOMM_SIMT_SQE_HEADER_WORDS)
        return detail::peer_word(peer.info(), word, 0U, args.dst);
    if (word < payload_words) {
        const uint32_t offset = (word - ASCCOMM_SIMT_SQE_HEADER_WORDS) * sizeof(uint64_t);
        uint64_t result = 0U;
        for (uint32_t byte = 0U; byte < sizeof(uint64_t) && offset + byte < inline_bytes; ++byte) {
            result |= static_cast<uint64_t>(args.value[offset + byte]) << (byte * 8U);
        }
        return result;
    }
    return 0U;
}

__simt_callee__ __forceinline__ uint64_t jetty_impl::build_notify_word(
    const asccomm_peer& peer, const detail::post_send_args<true>& args, const uint32_t word, const uint32_t sq_idx)
{
    if (word == 0U) {
        return (ASCCOMM_SIMT_WRITE_HEADER & ~(0xFFULL << 40U)) | (static_cast<uint64_t>(0x0CU) << 40U) | sq_idx;
    }
    if (word < ASCCOMM_SIMT_SQE_HEADER_WORDS)
        return detail::peer_word(peer.info(), word, 1U, args.dst);
    if (word == ASCCOMM_SIMT_SQE_HEADER_WORDS) {
        return static_cast<uint64_t>((peer.info()->tp_id_num_sges_remote_token_id >> 32U) & 0xFFFFFU) |
               ((peer.info()->remote_token_value_udf & 0xFFFFFFFFULL) << 32U);
    }
    if (word == ASCCOMM_SIMT_SQE_HEADER_WORDS + 1U)
        return reinterpret_cast<uint64_t>(args.notify_addr);
    if (word == ASCCOMM_SIMT_SQE_HEADER_WORDS + 2U)
        return args.notify_value;
    if (word == ASCCOMM_SIMT_SQE_HEADER_WORDS + 4U)
        return static_cast<uint32_t>(args.sges[0].len);
    if (word == ASCCOMM_SIMT_SQE_HEADER_WORDS + 5U)
        return args.sges[0].addr;
    return 0U;
}

__simt_callee__ __forceinline__ void jetty_impl::store_sqe_pair(
    __gm__ uint64_t* sq, const uint32_t depth, const uint32_t head, const uint32_t first_word, const uint64_t first,
    const uint64_t second)
{
    const uint32_t bb_offset = first_word / ASCCOMM_SIMT_WQEBB_LANES;
    const uint32_t sq_idx = (head + bb_offset) & (depth - 1U);
    auto* pairs = reinterpret_cast<__gm__ ulonglong2*>(sq + static_cast<uint64_t>(sq_idx) * ASCCOMM_SIMT_WQEBB_LANES);
    asc_stwt(pairs + (first_word % ASCCOMM_SIMT_WQEBB_LANES) / 2U, make_ulonglong2(first, second));
};

} // namespace asc::simt

#endif
