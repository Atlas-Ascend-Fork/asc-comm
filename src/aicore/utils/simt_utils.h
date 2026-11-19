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
 * \file simt_utils.h
 * \brief SIMT warp primitives and cooperative-group helpers shared by the SIMT paths.
 */
#ifndef SRC_AICORE_UTILS_SIMT_UTILS_H
#define SRC_AICORE_UTILS_SIMT_UTILS_H

#include <cstdint>
#include <simt_api/device_warp_functions.h>
#include <simt_api/cooperative_groups.h>

#ifdef __CCE__
namespace asc::simt {

template <uint32_t num_cycles = 300>
__simt_callee__ __forceinline__ void nop()
{
    static_assert(num_cycles % 15 == 0, "SIMT nop cycles must be a multiple of 15");
#pragma unroll
    for (int i = 0; i < num_cycles / 15; ++i)
        asm volatile("NOP wait:0b0000000 stall:15" ::);
}

__simt_callee__ __forceinline__ int exchange(const int& value, const int& lane_idx)
{
    return asc_shfl(value, lane_idx);
}

template <int max_value>
__simt_callee__ __forceinline__ bool deduplicate(const int& value, const int& lane_idx)
{
    auto peers = 0xffffffffu;
#pragma unroll
    for (int i = 0; (1 << i) <= max_value; ++i) {
        const auto current_bit = (static_cast<unsigned>(value) >> i) & 1u;
        const auto ones = asc_ballot(current_bit);
        peers &= ones ^ (current_bit - 1u);
    }
    return (peers >> lane_idx) == 1u;
}

__simt_callee__ __forceinline__ int warp_inclusive_sum(int value, const int& lane_idx)
{
#pragma unroll
    for (int offset = 1; offset < warpSize; offset <<= 1) {
        const auto synced = asc_shfl_up(value, offset);
        if (lane_idx >= offset)
            value += synced;
    }
    return value;
}

__simt_callee__ __forceinline__ int warp_exclusive_sum(const int& value, const int& lane_idx)
{
    return warp_inclusive_sum(value, lane_idx) - value;
}

} // namespace asc::simt

namespace asc::simt {

struct asccomm_coop_thread {
    __simt_callee__ __forceinline__ void sync() const {}
    __simt_callee__ __forceinline__ int size() const { return 1; }
    __simt_callee__ __forceinline__ int thread_rank() const { return 0; }
};
struct asccomm_coop_warp {
    cooperative_groups::thread_block_tile<32, cooperative_groups::thread_block> group;
    __simt_callee__ __forceinline__ asccomm_coop_warp()
        : group(cooperative_groups::tiled_partition<32>(cooperative_groups::this_thread_block()))
    {}
    __simt_callee__ __forceinline__ void sync() const { group.sync(); }
    __simt_callee__ __forceinline__ int size() const { return 32; }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group.thread_rank()); }
};
struct asccomm_coop_group {
    cooperative_groups::thread_block group;
    __simt_callee__ __forceinline__ asccomm_coop_group() : group(cooperative_groups::this_thread_block()) {}
    __simt_callee__ __forceinline__ void sync() const { group.sync(); }
    __simt_callee__ __forceinline__ int size() const { return static_cast<int>(group.size()); }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group.thread_rank()); }
};
template <typename coop_group>
struct asccomm_coop_traits {
    static constexpr bool warp_capable = false;
    static constexpr bool is_warp = false;
};
template <>
struct asccomm_coop_traits<asccomm_coop_warp> {
    static constexpr bool warp_capable = true;
    static constexpr bool is_warp = true;
};
template <>
struct asccomm_coop_traits<asccomm_coop_group> {
    static constexpr bool warp_capable = true;
    static constexpr bool is_warp = false;
};

} // namespace asc::simt

// Legacy cooperative-group type spellings for AscendC::simt users.
namespace AscendC::simt {
using HcommCoopThread = asc::simt::asccomm_coop_thread;
using HcommCoopWarp = asc::simt::asccomm_coop_warp;
using HcommCoopGroup = asc::simt::asccomm_coop_group;
template <typename coop_group>
using HcommCoopTraits = asc::simt::asccomm_coop_traits<coop_group>;
} // namespace AscendC::simt
#endif

#endif // SRC_AICORE_UTILS_SIMT_UTILS_H
