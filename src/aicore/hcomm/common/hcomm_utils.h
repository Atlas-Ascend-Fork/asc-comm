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
 * \file asccomm_util.h
 * \brief asccomm utils
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H

#include "hcomm_inner_def.h"

namespace AscendC {

constexpr uint32_t bits_1_byte = 8;
constexpr uint32_t bits_3_byte = 24;
constexpr uint32_t bits_5_byte = 40;
constexpr uint32_t bits_7_byte = 56;

__aicore__ inline uint16_t hto_ns(uint16_t x)
{
    return (uint16_t)(((x & 0x00ffU) << bits_1_byte) | ((x & 0xff00U) >> bits_1_byte));
}

__aicore__ inline uint32_t hto_nl(uint32_t x)
{
    return ((x & 0x000000ffU) << bits_3_byte) | ((x & 0x0000ff00U) << bits_1_byte) | ((x & 0x00ff0000U) >> bits_1_byte) |
           ((x & 0xff000000U) >> bits_3_byte);
}

__aicore__ inline uint64_t hto_nll(uint64_t x)
{
    return ((x & 0x00000000000000ffULL) << bits_7_byte) | ((x & 0x000000000000ff00ULL) << bits_5_byte) |
           ((x & 0x0000000000ff0000ULL) << bits_3_byte) | ((x & 0x00000000ff000000ULL) << bits_1_byte) |
           ((x & 0x000000ff00000000ULL) >> bits_1_byte) | ((x & 0x0000ff0000000000ULL) >> bits_3_byte) |
           ((x & 0x00ff000000000000ULL) >> bits_5_byte) | ((x & 0xff00000000000000ULL) >> bits_7_byte);
}

template <HardEvent event>
__aicore__ inline void sync_action()
{
    auto t_pipe_ptr = GetTPipePtr();
    TEventID event_id = 0;
    if (t_pipe_ptr != nullptr) {
        event_id = t_pipe_ptr->FetchEventID(event);
    }
    SetFlag<event>(event_id);
    WaitFlag<event>(event_id);
}
__aicore__ inline __ubuf__ uint8_t* align_addr_to_32_bytes(__ubuf__ uint8_t* buff)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(buff);
    const uintptr_t alignment = 32;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<__ubuf__ uint8_t*>(aligned_addr);
}

__aicore__ inline int32_t asccomm_find_buffer_idx(
    reged_buffer_entity* bufferAddr, uint32_t buffer_num, GM_ADDR addr, uint64_t len)
{
    if (bufferAddr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "asccomm_find_buffer_idx failed with null bufferAddr, buffer_num=%u\n", buffer_num);
        return asccomm_failed;
    }
    uint64_t target_addr = reinterpret_cast<uint64_t>(addr);
    for (uint32_t i = 0; i < buffer_num; i++) {
        uint64_t base_addr = bufferAddr[i].buffer_info.rma.addr;
        uint64_t buffer_size = bufferAddr[i].buffer_info.rma.size;
        if (target_addr < base_addr) {
            continue;
        }
        uint64_t offset = target_addr - base_addr;
        if (offset <= buffer_size && len <= buffer_size - offset) {
            KERNEL_LOG(
                KERNEL_INFO, "asccomm_find_buffer_idx hit idx=%u addr=%llu len=%llu base=%llu size=%llu\n", i,
                static_cast<uint64_t>(target_addr), static_cast<uint64_t>(len), static_cast<uint64_t>(base_addr),
                static_cast<uint64_t>(buffer_size));
            return static_cast<int32_t>(i);
        }
    }
    KERNEL_LOG(
        KERNEL_ERROR, "asccomm_find_buffer_idx failed addr=%llu len=%llu buffer_num=%u\n", static_cast<uint64_t>(target_addr),
        static_cast<uint64_t>(len), buffer_num);
    return asccomm_failed;
}

template <typename t>
__aicore__ inline void cache_write_through(__gm__ t* source_addr, uint64_t length)
{
    if (length == 0) {
        return;
    }
    __gm__ t* start = (__gm__ t*)((uint64_t)source_addr / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ t* end = (__gm__ t*)(((uint64_t)source_addr + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t i = 0; i <= end - start; i += CACHE_LINE_SIZE) {
        DataCacheCleanAndInvalid<t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[i]);
    }
}
} // namespace AscendC

#endif
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H
#endif
