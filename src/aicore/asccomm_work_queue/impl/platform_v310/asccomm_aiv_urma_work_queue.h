/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_WORK_QUEUE_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_WORK_QUEUE_H

// Requires CANN 9.2.0 or later (excluding 9.2.0 beta releases):
// c_api/utils/sys_var.h and ReadGmBypassDCache/WriteGmBypassDCache are SDK dependencies.
#include <c_api/sync/sync.h>
#if __has_include(<c_api/utils/sys_var.h>)
#include <c_api/utils/sys_var.h>
#else
#include <c_api/sys_var/sys_var.h>
#endif
#include <c_api/vector_datamove/vector_datamove.h>

#include "../../../asccomm/common/asccomm_inner_def.h"
#include "../../../asccomm/common/asccomm_log.h"

namespace asc {

// asccomm_urma_jfc_cqe_ctx first-word field positions, read as a raw uint32_t header by poll_cq.
static constexpr uint32_t ASCCOMM_JETTY_CQE_OWNER_BIT = 2U;
static constexpr uint32_t ASCCOMM_JETTY_CQE_SUBSTATUS_BIT = 16U;
static constexpr uint32_t ASCCOMM_JETTY_CQE_STATUS_BIT = 24U;
static constexpr uint32_t ASCCOMM_JETTY_CQE_FIELD_MASK = 0xFFU;
// The CQ doorbell only takes the low 24 bits of the CQ tail.
static constexpr uint32_t ASCCOMM_JETTY_CQ_DOORBELL_MASK = 0xFFFFFFU;

template <typename config_type>
__aicore__ inline uint32_t asccomm_jetty_pack_sqe_flag(const config_type& config)
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
           ((config.cqe & 0x1U) << 5U) | ((asccomm_config_inline_en(config) & 0x1U) << 6U);
}

__aicore__ inline asccomm_peer<COMM_PROTOCOL_UB_CTP>::asccomm_peer(
    __ubuf__ asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>* entity, GM_ADDR channels, uint32_t channel_idx)
    : entity(entity)
{
    ASCCOMM_DEBUG_TRAP_IF(entity == nullptr || channels == nullptr, "entity or channels is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(channels);
    uint64_t jetty_value = ReadGmBypassDCache(table + channel_idx);
    ASCCOMM_DEBUG_TRAP_IF(jetty_value == 0U, "channels[%u] is 0\n", channel_idx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
    ASCCOMM_DEBUG_TRAP_IF(
        jetty->remoteBufferNum == 0U || jetty->remoteBufferAddr == nullptr || jetty->sqContextAddr == nullptr,
        "jetty info is nullptr\n");
    // The last registered buffer carries the shared remote token metadata.
    auto* remote_buffer = jetty->remoteBufferAddr + (jetty->remoteBufferNum - 1U);
    entity->remote_token_value_udf = remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    auto* sq_context = jetty->sqContextAddr + ASCCOMM_URMA_DEFAULT_QP_IDX;
    // tpId occupies bits [23:0] and remoteTokenId bits [51:32]; numSges is OR-ed in per post.
    entity->tp_id_num_sges_remote_token_id =
        static_cast<uint64_t>(sq_context->contextInfo.ubJfs.tpID) |
        (static_cast<uint64_t>(remote_buffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
    auto* remote_eid = reinterpret_cast<const uint32_t*>(sq_context->contextInfo.ubJfs.remoteEID);
    entity->remote_eid[0] = static_cast<uint64_t>(remote_eid[0]) | (static_cast<uint64_t>(remote_eid[1]) << 32U);
    entity->remote_eid[1] = static_cast<uint64_t>(remote_eid[2]) | (static_cast<uint64_t>(remote_eid[3]) << 32U);
    valid = true;
}

__aicore__ inline asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::asccomm_work_queue_impl(
    __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx)
    : jetty_info(wq_buff), ubuf_wqe(wqe_buff)
{
    ASCCOMM_DEBUG_TRAP_IF(wq_buff == nullptr || channels == nullptr, "wq_buff or channels is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(channels);
    uint64_t jetty_value = ReadGmBypassDCache(table + channel_idx);
    ASCCOMM_DEBUG_TRAP_IF(jetty_value == 0U, "channels[%u] is 0\n", channel_idx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jetty_value);
    ASCCOMM_DEBUG_TRAP_IF(
        jetty->sqContextAddr == nullptr || jetty->cqContextAddr == nullptr, "jetty info is nullptr\n");
    auto* sq_context = jetty->sqContextAddr + ASCCOMM_URMA_DEFAULT_QP_IDX;
    auto* cq_context = jetty->cqContextAddr + ASCCOMM_URMA_DEFAULT_QP_IDX;
    jetty_info->sq_base_addr = sq_context->contextInfo.ubJfs.sqVa;
    jetty_info->sq_head_addr = sq_context->contextInfo.ubJfs.headAddr;
    jetty_info->completion_tail_addr = sq_context->contextInfo.ubJfs.tailAddr;
    jetty_info->sq_doorbell_addr = sq_context->contextInfo.ubJfs.dbVa;
    jetty_info->cq_base_addr = cq_context->contextInfo.ubJfc.scqVa;
    jetty_info->cq_tail_addr = cq_context->contextInfo.ubJfc.tailAddr;
    jetty_info->cq_doorbell_addr = cq_context->contextInfo.ubJfc.dbVa;
    jetty_info->packed_head = ReadGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr));
    jetty_info->num_wqebb_bytes = sq_context->contextInfo.ubJfs.wqeSize;
    jetty_info->num_cqe_bytes = cq_context->contextInfo.ubJfc.cqeSize;
    jetty_info->sq_depth = sq_context->contextInfo.ubJfs.sqDepth;
    jetty_info->cq_depth = cq_context->contextInfo.ubJfc.cqDepth;
    valid = true;
}

template <asccomm_urma_op_code op_code, auto const& config>
__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::fill_write_sqe_header(
    const asccomm_peer<COMM_PROTOCOL_UB_CTP>& peer, GM_ADDR dst_addr, uint32_t inline_msg_len, uint32_t sge_num)
{
    auto* info = peer.entity;
    const uint32_t owner = (static_cast<uint32_t>(jetty_info->packed_head) & jetty_info->sq_depth) == 0U ? 1U : 0U;
    // Fill asccomm_urma_sqe_ctx with one 64-bit store per word: word0/1 pack the bitfields, word5 is rmtAddr.
    __ubuf__ uint64_t* sqe_words = reinterpret_cast<__ubuf__ uint64_t*>(ubuf_wqe);
    sqe_words[0] = (static_cast<uint64_t>(asccomm_jetty_pack_sqe_flag(config) & 0xFFU) << 16U) | (1U << 28U) |
                   (1U << 29U) | (static_cast<uint64_t>(owner) << 31U) | (static_cast<uint64_t>(op_code) << 40U) |
                   (static_cast<uint64_t>(inline_msg_len) << 54U);
    // tpId and remoteTokenId already share one word in the peer descriptor, so a single load
    // covers both; numSges is not stored and is OR-ed in here.
    sqe_words[1] = info->tp_id_num_sges_remote_token_id | (static_cast<uint64_t>(sge_num) << 24U);
    sqe_words[2] = info->remote_eid[0];
    sqe_words[3] = info->remote_eid[1];
    sqe_words[4] = info->remote_token_value_udf;
    sqe_words[5] = reinterpret_cast<uint64_t>(dst_addr);
}

template <bool with_notify>
__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::build_write_sqe(
    const asccomm_peer<COMM_PROTOCOL_UB_CTP>& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes,
    GM_ADDR notify_addr, uint64_t notify_value)
{
    constexpr asccomm_urma_op_code op_code =
        with_notify ? asccomm_urma_op_code::WRITE_WITH_NOTIFY : asccomm_urma_op_code::WRITE;
    fill_write_sqe_header<op_code, URMA_DEFAULT_CFG>(peer, dst_addr, 0U, 1U);
    __ubuf__ uint64_t* sge_words = reinterpret_cast<__ubuf__ uint64_t*>(ubuf_wqe + sizeof(asccomm_urma_sqe_ctx));
    if constexpr (with_notify) {
        // Fill asccomm_urma_notify_ctx with one 64-bit store per word: word1 is notify_addr, word2 is notifyData.
        sge_words[0] = ((peer.entity->tp_id_num_sges_remote_token_id >> 32U) & 0xFFFFFU) |
                       ((peer.entity->remote_token_value_udf & 0xFFFFFFFFULL) << 32U);
        sge_words[1] = reinterpret_cast<uint64_t>(notify_addr);
        sge_words[2] = notify_value;
        sge_words[3] = 0U;
        sge_words += 4U;
    }
    // Fill asccomm_urma_sge_ctx with one 64-bit store per word: word1 is the source va.
    sge_words[0] = static_cast<uint64_t>(static_cast<uint32_t>(num_bytes));
    sge_words[1] = reinterpret_cast<uint64_t>(src_addr);
}

template <typename T>
__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::build_write_value_sqe(
    const asccomm_peer<COMM_PROTOCOL_UB_CTP>& peer, GM_ADDR dst_addr, T value)
{
    fill_write_sqe_header<asccomm_urma_op_code::WRITE, URMA_INLINE_CFG>(
        peer, dst_addr, static_cast<uint32_t>(sizeof(T)), 0U);
    *reinterpret_cast<__ubuf__ T*>(ubuf_wqe + sizeof(asccomm_urma_sqe_ctx)) = value;
}

__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::copy_wqe_to_sq(
    uint32_t current_head, uint32_t num_wqebbs)
{
    uint32_t sq_idx = current_head & (jetty_info->sq_depth - 1U);
    uint32_t first = num_wqebbs < jetty_info->sq_depth - sq_idx ? num_wqebbs : jetty_info->sq_depth - sq_idx;
    asc_sync_notify(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_copy_ub2gm_align(
        reinterpret_cast<__gm__ uint8_t*>(
            jetty_info->sq_base_addr + static_cast<uint64_t>(jetty_info->num_wqebb_bytes) * sq_idx),
        ubuf_wqe, 1, jetty_info->num_wqebb_bytes * first, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (first < num_wqebbs) {
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(jetty_info->sq_base_addr), ubuf_wqe + jetty_info->num_wqebb_bytes * first,
            1, jetty_info->num_wqebb_bytes * (num_wqebbs - first), asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    asc_sync_notify(PIPE_MTE3, PIPE_S, EVENT_ID0);
    asc_sync_wait(PIPE_MTE3, PIPE_S, EVENT_ID0);
}

__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::advance_sq(uint32_t num_wqebbs)
{
    // packed_head keeps the head in its low 32 bits and the expected CQE count in the high 32.
    // The two counters wrap independently, so they cannot be advanced with one 64-bit add: a
    // head wrapping past 0xFFFFFFFF would carry into the CQE count.
    const uint32_t head = static_cast<uint32_t>(jetty_info->packed_head) + num_wqebbs;
    const uint32_t expected_cqe_cnt = static_cast<uint32_t>(jetty_info->packed_head >> 32U) + 1U;
    jetty_info->packed_head = static_cast<uint64_t>(head) | (static_cast<uint64_t>(expected_cqe_cnt) << 32U);
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr), jetty_info->packed_head);
    asc_sync_data_barrier(mem_dsb_t::DSB_DDR);
}

__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::ring_doorbell()
{
    // The doorbell only takes the head, which lives in the low 32 bits of packed_head.
    WriteGmBypassDCache(
        reinterpret_cast<__gm__ uint32_t*>(jetty_info->sq_doorbell_addr),
        static_cast<uint32_t>(jetty_info->packed_head));
}

template <int64_t timeout_cycles>
__aicore__ inline void asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::poll_cq_when_sq_overflow()
{
    // Poll once the outstanding CQEs (expected minus completed) come within the threshold of
    // cq_depth. Use >= instead of ==: multiple WQEs may be posted between two checks, so the
    // distance can jump past an exact value and an equality test would never fire.
    constexpr uint32_t near_full_threshold = 10U;
    constexpr uint32_t reclaim_batch = 100U;
    uint32_t completions = static_cast<uint32_t>(jetty_info->packed_head >> 32U);
    uint32_t tail = ReadGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr));
    if (completions - tail + near_full_threshold >= jetty_info->cq_depth) {
        poll_cq<timeout_cycles>(tail + reclaim_batch < completions ? tail + reclaim_batch : completions);
    }
}

template <int64_t timeout_cycles>
__aicore__ inline int32_t asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::poll_cq(uint32_t expected_tail)
{
    uint32_t current_tail = ReadGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr));
    while (current_tail != expected_tail) {
        auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
            jetty_info->cq_base_addr +
            static_cast<uint64_t>(jetty_info->num_cqe_bytes) * (current_tail & (jetty_info->cq_depth - 1U)));
        uint32_t phase = (current_tail / jetty_info->cq_depth) & 1U;
        uint64_t start = static_cast<uint64_t>(asc_get_system_cycle());
        uint32_t header;
        do {
            header = ReadGmBypassDCache(cqe);
            if (((header >> ASCCOMM_JETTY_CQE_OWNER_BIT) & 1U) != phase) {
                break;
            }
            if (static_cast<uint64_t>(asc_get_system_cycle()) - start >= static_cast<uint64_t>(timeout_cycles)) {
                ASCCOMM_KERNEL_LOG(
                    KERNEL_ERROR, "asccomm_work_queue poll_cq timeout, cqTail=%u, expectedTail=%u, cqe phase=%u\n",
                    current_tail, expected_tail, phase);
                trap();
            }
            Nop<300>();
        } while (true);
        if (((header >> ASCCOMM_JETTY_CQE_SUBSTATUS_BIT) & ASCCOMM_JETTY_CQE_FIELD_MASK) != 0U ||
            ((header >> ASCCOMM_JETTY_CQE_STATUS_BIT) & ASCCOMM_JETTY_CQE_FIELD_MASK) != 0U) {
            return ASCCOMM_FAILED;
        }
        ++current_tail;
    }
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_tail_addr), current_tail);
    WriteGmBypassDCache(
        reinterpret_cast<__gm__ uint32_t*>(jetty_info->cq_doorbell_addr),
        current_tail & ASCCOMM_JETTY_CQ_DOORBELL_MASK);
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jetty_info->completion_tail_addr), current_tail);
    asc_sync_data_barrier(mem_dsb_t::DSB_DDR);
    return ASCCOMM_SUCCESS;
}

__aicore__ inline bool asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::ready()
{
    return valid && jetty_info != nullptr && ubuf_wqe != nullptr && jetty_info->sq_depth != 0U &&
           jetty_info->cq_depth != 0U;
}

template <int64_t timeout_cycles, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::post_sqe(uint32_t num_wqebbs)
{
    uint32_t head = static_cast<uint32_t>(jetty_info->packed_head);
    poll_cq_when_sq_overflow<timeout_cycles>();
    advance_sq(num_wqebbs);
    copy_wqe_to_sq(head, num_wqebbs);
    if constexpr (do_commit) {
        ring_doorbell();
    }
    return ASCCOMM_SUCCESS;
}

template <int64_t timeout_cycles, bool with_notify, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::post_write(
    const asccomm_peer<COMM_PROTOCOL_UB_CTP>& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes,
    GM_ADDR notify_addr, uint64_t notify_value, uint32_t num_wqebbs)
{
    ASCCOMM_DEBUG_TRAP_IF(!ready(), "asccomm_work_queue write on an unready work queue\n");
    build_write_sqe<with_notify>(peer, dst_addr, src_addr, num_bytes, notify_addr, notify_value);
    return post_sqe<timeout_cycles, do_commit>(num_wqebbs);
}

template <int64_t timeout_cycles, typename T, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::post_write_value(
    const asccomm_peer<COMM_PROTOCOL_UB_CTP>& peer, GM_ADDR dst_addr, T value, uint32_t num_wqebbs)
{
    ASCCOMM_DEBUG_TRAP_IF(!ready(), "asccomm_work_queue write_value on an unready work queue\n");
    build_write_value_sqe<T>(peer, dst_addr, value);
    return post_sqe<timeout_cycles, do_commit>(num_wqebbs);
}

template <int64_t timeout_cycles>
__aicore__ inline int32_t asccomm_work_queue_impl<COMM_PROTOCOL_UB_CTP>::drain()
{
    ASCCOMM_DEBUG_TRAP_IF(
        !ready() || jetty_info->num_cqe_bytes == 0U, "asccomm_work_queue drain on an unready work queue\n");
    uint64_t packed_head = ReadGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jetty_info->sq_head_addr));
    return poll_cq<timeout_cycles>(static_cast<uint32_t>(packed_head >> 32U));
}

} // namespace asc

#endif
