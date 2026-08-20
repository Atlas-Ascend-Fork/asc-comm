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
 * \brief asccomm SIMT utils
 *
 * SIMT counterpart of asccomm_utils.h. The two cannot share function bodies: a __simt_callee__
 * function cannot call an __aicore__ one, so every helper is duplicated per execution space.
 * Only types and constants are shared, through asccomm_inner_def.h.
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H

#include "hcomm_simt_inner_def.h"

namespace AscendC::simt {

__simt_callee__ inline int32_t asccomm_simt_find_buffer_idx(
    __gm__ reged_buffer_entity* bufferAddr, uint32_t buffer_num, __gm__ uint8_t* addr, uint64_t len)
{
    if (bufferAddr == nullptr) {
        return asccomm_failed;
    }
    uint64_t target_addr = reinterpret_cast<uint64_t>(addr);
    for (uint32_t i = 0; i < buffer_num; ++i) {
        uint64_t base_addr = bufferAddr[i].buffer_info.rma.addr;
        uint64_t buffer_size = bufferAddr[i].buffer_info.rma.size;
        if (target_addr < base_addr) {
            continue;
        }
        uint64_t offset = target_addr - base_addr;
        if (offset <= buffer_size && len <= buffer_size - offset) {
            return static_cast<int32_t>(i);
        }
    }
    return asccomm_failed;
}

__simt_callee__ inline uint64_t asccomm_simt_load_le64(__gm__ const uint8_t* addr)
{
    uint64_t value = 0U;
#pragma unroll
    for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
        value |= static_cast<uint64_t>(addr[i]) << (i * 8U);
    }
    return value;
}

__simt_callee__ inline uint32_t asccomm_simt_head_idx(uint64_t head_val)
{
    return static_cast<uint32_t>(head_val & 0xFFFFFFFFU);
}

__simt_callee__ inline uint32_t asccomm_simt_wqe_cnt(uint64_t head_val)
{
    return static_cast<uint32_t>((head_val >> 32U) & asccomm_simt_wqe_cnt_mask);
}

// Atomically reserves SQ space for one submission without overwriting unconsumed BBs.
// required_free_bb_cnt may exceed bb_cnt when a deferred post must leave room for the
// immediate DWQE that will publish it later.
__simt_callee__ inline bool asccomm_simt_try_reserve(
    __gm__ uint64_t* head_addr, __gm__ uint32_t* tail_addr, uint32_t sq_depth, uint32_t bb_cnt, uint32_t required_free_bb_cnt,
    uint32_t cqe_cnt, uint64_t& head_val)
{
    if (sq_depth == 0U || required_free_bb_cnt > sq_depth) {
        return false;
    }

    uint64_t delta = (static_cast<uint64_t>(cqe_cnt) << 32U) | static_cast<uint64_t>(bb_cnt);
    head_val = *head_addr;
    while (true) {
        uint32_t used_bb_cnt = asccomm_simt_head_idx(head_val) - *tail_addr;
        if (used_bb_cnt > sq_depth || required_free_bb_cnt > sq_depth - used_bb_cnt) {
            return false;
        }

        uint64_t previous = asc_atomic_cas(head_addr, head_val, head_val + delta);
        if (previous == head_val) {
            return true;
        }
        head_val = previous;
    }
}

__simt_callee__ inline __gm__ reged_buffer_entity* asccomm_simt_local_buffers(channel_handle channel)
{
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    return reinterpret_cast<__gm__ reged_buffer_entity*>(channel_entity_ptr->local_buffer_addr);
}

__simt_callee__ inline __gm__ reged_buffer_entity* asccomm_simt_remote_buffers(channel_handle channel)
{
    __gm__ asccomm_simt_channel_entity* channel_entity_ptr = reinterpret_cast<__gm__ asccomm_simt_channel_entity*>(channel);
    return reinterpret_cast<__gm__ reged_buffer_entity*>(channel_entity_ptr->remote_buffer_addr);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_simt_local_buffer_addr(channel_handle channel, uint32_t buffer_idx)
{
    __gm__ reged_buffer_entity* local_buffers = asccomm_simt_local_buffers(channel);
    return reinterpret_cast<__gm__ uint8_t*>(local_buffers[buffer_idx].buffer_info.rma.addr);
}

__simt_callee__ inline __gm__ uint8_t* asccomm_simt_remote_buffer_addr(channel_handle channel, uint32_t buffer_idx)
{
    __gm__ reged_buffer_entity* remote_buffers = asccomm_simt_remote_buffers(channel);
    return reinterpret_cast<__gm__ uint8_t*>(remote_buffers[buffer_idx].buffer_info.rma.addr);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H
#endif
