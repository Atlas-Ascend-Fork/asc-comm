/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file asccomm_utils.h
 * \brief asccomm utils
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_UTILS_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_UTILS_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_UTILS_H

#include "asccomm_inner_def.h"
#include "asccomm_log.h"
#include <basic_api/kernel_operator_atomic_intf.h>
#include <basic_api/kernel_operator_utils_intf.h>
#include <c_api/sync/sync.h>

namespace asc {

constexpr uint32_t BITS_1BYTE = 8;
constexpr uint32_t BITS_3BYTE = 24;
constexpr uint32_t BITS_5BYTE = 40;
constexpr uint32_t BITS_7BYTE = 56;
constexpr uint32_t ASCCOMM_DEFAULT_QP_IDX = 0;

__aicore__ inline constexpr uint16_t hto_ns(uint16_t x) { return __builtin_bswap16(x); }

__aicore__ inline constexpr uint32_t hto_nl(uint32_t x) { return __builtin_bswap32(x); }

__aicore__ inline constexpr uint64_t hto_nll(uint64_t x) { return __builtin_bswap64(x); }

template <pipe_t pipe_src, pipe_t pipe_dst>
__aicore__ inline void sync_action(event_t id)
{
    asc_sync_notify(pipe_src, pipe_dst, id);
    asc_sync_wait(pipe_src, pipe_dst, id);
}

__aicore__ inline __ubuf__ uint8_t* align_addr_to32_bytes(__ubuf__ uint8_t* buff)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(buff);
    const uintptr_t alignment = 32;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<__ubuf__ uint8_t*>(aligned_addr);
}

__aicore__ inline int32_t asccomm_find_buffer_idx(
    RegedBufferEntity* buffer_addr, uint32_t buffer_num, GM_ADDR addr, uint64_t len)
{
    if (buffer_addr == nullptr) {
        ASCCOMM_KERNEL_LOG(
            KERNEL_ERROR, "asccomm_find_buffer_idx failed with null buffer_addr, bufferNum=%u\n", buffer_num);
        return ASCCOMM_FAILED;
    }
    uint64_t target_addr = reinterpret_cast<uint64_t>(addr);
    for (uint32_t i = 0; i < buffer_num; i++) {
        uint64_t base_addr = buffer_addr[i].bufferInfo.rma.addr;
        uint64_t buffer_size = buffer_addr[i].bufferInfo.rma.size;
        if (target_addr < base_addr) {
            continue;
        }
        uint64_t offset = target_addr - base_addr;
        if (offset <= buffer_size && len <= buffer_size - offset) {
            ASCCOMM_KERNEL_LOG(
                KERNEL_INFO, "asccomm_find_buffer_idx hit idx=%u addr=%llu len=%llu base=%llu size=%llu\n", i,
                static_cast<uint64_t>(target_addr), static_cast<uint64_t>(len), static_cast<uint64_t>(base_addr),
                static_cast<uint64_t>(buffer_size));
            return static_cast<int32_t>(i);
        }
    }
    ASCCOMM_KERNEL_LOG(
        KERNEL_ERROR, "asccomm_find_buffer_idx failed addr=%llu len=%llu bufferNum=%u\n",
        static_cast<uint64_t>(target_addr), static_cast<uint64_t>(len), buffer_num);
    return ASCCOMM_FAILED;
}

template <typename T>
__aicore__ inline void cache_write_through(__gm__ T* source_addr, uint64_t length)
{
    if (length == 0) {
        return;
    }
    __gm__ T* start = (__gm__ T*)((uint64_t)source_addr / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ T* end = (__gm__ T*)(((uint64_t)source_addr + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<T> global;
    global.SetGlobalBuffer(start);
    for (uint32_t i = 0; i <= end - start; i += CACHE_LINE_SIZE) {
        DataCacheCleanAndInvalid<T, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[i]);
    }
}

template <CommProtocol protocol>
__aicore__ inline __gm__ uint32_t* asccomm_get_lock_addr(__gm__ ChannelEntity* channel_entity)
{
    __gm__ uint64_t* cq_contexts_addr_field = reinterpret_cast<__gm__ uint64_t*>(
        reinterpret_cast<__gm__ uint8_t*>(channel_entity) + offsetof(ChannelEntity, cqContextAddr));
    uint64_t cq_contexts_addr = ld_dev(cq_contexts_addr_field, 0);
    uint64_t lock_addr_value;
    __gm__ CqContext* cq_context = reinterpret_cast<__gm__ CqContext*>(cq_contexts_addr) + ASCCOMM_DEFAULT_QP_IDX;
    if constexpr (protocol == COMM_PROTOCOL_ROCE) {
        lock_addr_value = cq_context->contextInfo.roceCq.headAddr;
    } else if constexpr (protocol_traits<protocol>::uses_urma_backend) {
        lock_addr_value = cq_context->contextInfo.ubJfc.headAddr;
    } else {
        static_assert(
            protocol == COMM_PROTOCOL_ROCE || protocol_traits<protocol>::uses_urma_backend,
            "Invalid CommProtocol, it must be RoCE, UB_CTP, UBOE or UB_RTP");
    }
    return reinterpret_cast<__gm__ uint32_t*>(lock_addr_value);
}

template <CommProtocol protocol>
__aicore__ inline int32_t asccomm_channel_lock(ChannelHandle channel)
{
    if (channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U) {
        return ASCCOMM_FAILED;
    }
    __gm__ ChannelEntity* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint32_t* lock_addr = asccomm_get_lock_addr<protocol>(channel_entity);
    while (AtomicCas(lock_addr, ASCCOMM_LOCK_FREE, ASCCOMM_LOCK_HELD) != ASCCOMM_LOCK_FREE) {
        // Back off for 800 cycles before retrying to reduce atomic contention.
        Nop<800>();
    }
    return ASCCOMM_SUCCESS;
}

template <CommProtocol protocol>
__aicore__ inline int32_t asccomm_channel_unlock(ChannelHandle channel)
{
    if (channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U) {
        return ASCCOMM_FAILED;
    }

    __gm__ ChannelEntity* channel_entity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint8_t* counter_addr = reinterpret_cast<__gm__ uint8_t*>(channel_entity) + offsetof(ChannelEntity, sqHead);
    // Flush the four contiguous channel counters: sqHead, sqTail, cqHead, and cqTail.
    cache_write_through<uint8_t>(counter_addr, 4U * sizeof(uint32_t));

    __gm__ uint32_t* lock_addr = asccomm_get_lock_addr<protocol>(channel_entity);
    uint32_t old_value = AtomicCas(lock_addr, ASCCOMM_LOCK_HELD, ASCCOMM_LOCK_FREE);
    return old_value == ASCCOMM_LOCK_HELD ? ASCCOMM_SUCCESS : ASCCOMM_FAILED;
}
} // namespace asc

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_UTILS_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_UTILS_H
#endif
