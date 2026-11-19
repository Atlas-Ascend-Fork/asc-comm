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
 * \file asccomm_simt_utils.h
 * \brief Hcomm SIMT utils
 *
 * SIMT counterpart of asccomm_utils.h. The two cannot share function bodies: a __simt_callee__
 * function cannot call an __aicore__ one, so every helper is duplicated per execution space.
 * Only types and constants are shared, through asccomm_inner_def.h.
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_UTILS_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_UTILS_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_UTILS_H

#include "asccomm_simt_inner_def.h"

#if defined(ASCENDC_DEBUG)
#include "utils/debug/asc_assert.h"
#include "utils/debug/asc_printf.h"
#endif

namespace asc::simt {

#if defined(ASCENDC_DEBUG)
#define ASCCOMM_SIMT_DEBUG_TRAP_IF(condition, fmt, ...) \
    do {                                                \
        if (condition) {                                \
            __asc_simt_vf::printf(fmt, ##__VA_ARGS__);  \
            __asc_simt_vf::__trap();                    \
        }                                               \
    } while (0)
#else
#define ASCCOMM_SIMT_DEBUG_TRAP_IF(condition, fmt, ...) ((void)0)
#endif

__simt_callee__ inline uint32_t asccomm_simt_head_idx(uint64_t head_val)
{
    return static_cast<uint32_t>(head_val & 0xFFFFFFFFU);
}

__simt_callee__ inline uint32_t asccomm_simt_wqe_cnt(uint64_t head_val)
{
    return static_cast<uint32_t>((head_val >> 32U) & ASCCOMM_SIMT_WQE_CNT_MASK);
}

// Reserves SQ space for one submission without overwriting basic blocks the NIC has not consumed
// yet, and reports the pre-reservation head_addr value through head_val.
//
// bb_cnt counts the 64B basic blocks occupied; cqe_cnt counts the CQEs the submission will produce
// (NOP padding produces none), keeping the high half of head_addr comparable with the CQ tail
// poll_cq consumes. required_free_bb_cnt may exceed bb_cnt: a deferred post must also leave room for
// the immediate 2-BB DWQE that publishes it, since SIMT has no standalone Commit.
//
// The update is a plain read-modify-write rather than an atomic one because a channel is driven
// by a single lane (see hcomm_simt.h). Supporting several producers would need more than an
// atomic claim here: a doorbell publishes a head covering every slot below it, including a
// neighbour's half-written WQE.
//
// head_val is written before any capacity check so a failing caller can still read the submitted
// wqe_cnt out of it to decide how many CQEs are outstanding.
__simt_callee__ inline bool asccomm_simt_try_reserve(
    __gm__ uint64_t* head_addr, __gm__ uint32_t* tail_addr, uint32_t sq_depth, uint32_t bb_cnt,
    uint32_t required_free_bb_cnt, uint32_t cqe_cnt, uint64_t& head_val)
{
    head_val = *head_addr;
    if (sq_depth == 0U || required_free_bb_cnt > sq_depth) {
        return false;
    }

    uint32_t cur_head = asccomm_simt_head_idx(head_val);

    // Both counters are free-running, so the subtraction is done in modular arithmetic; the
    // used_bb_cnt > sq_depth guard rejects a head/tail pair that cannot be consistent.
    uint32_t used_bb_cnt = cur_head - *tail_addr;
    if (used_bb_cnt > sq_depth || required_free_bb_cnt > sq_depth - used_bb_cnt) {
        return false;
    }

    // The two halves of head_addr wrap independently. In particular, a 64-bit add would carry
    // from a wrapping cur_head into wqe_cnt and make poll_cq expect a completion that never occurs.
    uint32_t next_head = cur_head + bb_cnt;
    uint32_t next_wqe_cnt = asccomm_simt_wqe_cnt(head_val) + cqe_cnt;
    *head_addr = static_cast<uint64_t>(next_head) | (static_cast<uint64_t>(next_wqe_cnt) << 32U);
    return true;
}

__simt_callee__ inline __gm__ asccomm_simt_channel_entity* asccomm_simt_get_channel_entity(ChannelHandle channel)
{
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        channel == 0U || (channel & (alignof(asccomm_simt_channel_entity) - 1U)) != 0U, "invalid SIMT Hcomm channel\n");
    return reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
}

__simt_callee__ inline __gm__ RegedBufferEntity* asccomm_simt_get_buffer_table(uint64_t buffer_table_addr)
{
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        buffer_table_addr == 0U || (buffer_table_addr & (alignof(RegedBufferEntity) - 1U)) != 0U,
        "invalid SIMT Hcomm buffer table\n");
    return reinterpret_cast<__gm__ RegedBufferEntity*>(buffer_table_addr);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_simt_get_buffer_addr(
    uint64_t buffer_table_addr, uint32_t buffer_num, uint32_t buffer_idx)
{
    __gm__ RegedBufferEntity* buffers = asccomm_simt_get_buffer_table(buffer_table_addr);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        buffers == nullptr || buffer_num == 0U || buffer_idx >= buffer_num, "invalid SIMT Hcomm buffer index\n");

    uint64_t buffer_addr = buffers[buffer_idx].bufferInfo.rma.addr;
    ASCCOMM_SIMT_DEBUG_TRAP_IF(buffer_addr == 0U, "invalid SIMT Hcomm buffer address\n");
    return reinterpret_cast<__gm__ uint8_t*>(buffer_addr);
}

__simt_callee__ inline __gm__ RegedBufferEntity* asccomm_simt_local_buffers(ChannelHandle channel)
{
    __gm__ asccomm_simt_channel_entity* channel_entity = asccomm_simt_get_channel_entity(channel);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        channel_entity == nullptr || channel_entity->localBufferNum == 0U, "invalid local buffer table\n");
    return asccomm_simt_get_buffer_table(channel_entity->localBufferAddr);
}

__simt_callee__ inline __gm__ RegedBufferEntity* asccomm_simt_remote_buffers(ChannelHandle channel)
{
    __gm__ asccomm_simt_channel_entity* channel_entity = asccomm_simt_get_channel_entity(channel);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(
        channel_entity == nullptr || channel_entity->remoteBufferNum == 0U, "invalid remote buffer table\n");
    return asccomm_simt_get_buffer_table(channel_entity->remoteBufferAddr);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_simt_local_buffer_addr(ChannelHandle channel, uint32_t buffer_idx)
{
    __gm__ asccomm_simt_channel_entity* channel_entity = asccomm_simt_get_channel_entity(channel);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(channel_entity == nullptr, "invalid local buffer channel\n");
    return asccomm_simt_get_buffer_addr(channel_entity->localBufferAddr, channel_entity->localBufferNum, buffer_idx);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_simt_remote_buffer_addr(ChannelHandle channel, uint32_t buffer_idx)
{
    __gm__ asccomm_simt_channel_entity* channel_entity = asccomm_simt_get_channel_entity(channel);
    ASCCOMM_SIMT_DEBUG_TRAP_IF(channel_entity == nullptr, "invalid remote buffer channel\n");
    return asccomm_simt_get_buffer_addr(channel_entity->remoteBufferAddr, channel_entity->remoteBufferNum, buffer_idx);
}

} // namespace asc::simt

// Compatibility bridge for the legacy AscendC::simt free-function address resolvers. See the
// bridge block in asccomm_inner_def.h.
namespace AscendC::simt {
using asc::simt::asccomm_simt_local_buffer_addr;
using asc::simt::asccomm_simt_remote_buffer_addr;
} // namespace AscendC::simt

#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_SIMT_UTILS_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_UTILS_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_SIMT_UTILS_H
#endif
