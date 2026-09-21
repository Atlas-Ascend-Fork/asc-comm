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
namespace AscendC::simt {

template <uint32_t kNumCycles = 300>
__simt_callee__ __forceinline__ void nop()
{
    static_assert(kNumCycles % 15 == 0, "SIMT nop cycles must be a multiple of 15");
#pragma unroll
    for (int i = 0; i < kNumCycles / 15; ++i)
        asm volatile("NOP wait:0b0000000 stall:15" ::);
}

__simt_callee__ __forceinline__ int exchange(const int& value, const int& lane_idx)
{
    return asc_shfl(value, lane_idx);
}

template <int kMaxValue>
__simt_callee__ __forceinline__ bool deduplicate(const int& value, const int& lane_idx)
{
    auto peers = 0xffffffffu;
#pragma unroll
    for (int i = 0; (1 << i) <= kMaxValue; ++i) {
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

struct HcommCoopThread {
    __simt_callee__ __forceinline__ void sync() const {}
    __simt_callee__ __forceinline__ int size() const { return 1; }
    __simt_callee__ __forceinline__ int thread_rank() const { return 0; }
};
struct HcommCoopWarp {
    cooperative_groups::thread_block_tile<32, cooperative_groups::thread_block> group_;
    __simt_callee__ __forceinline__ HcommCoopWarp()
        : group_(cooperative_groups::tiled_partition<32>(cooperative_groups::this_thread_block()))
    {}
    __simt_callee__ __forceinline__ void sync() const { group_.sync(); }
    __simt_callee__ __forceinline__ int size() const { return 32; }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group_.thread_rank()); }
};
struct HcommCoopGroup {
    cooperative_groups::thread_block group_;
    __simt_callee__ __forceinline__ HcommCoopGroup() : group_(cooperative_groups::this_thread_block()) {}
    __simt_callee__ __forceinline__ void sync() const { group_.sync(); }
    __simt_callee__ __forceinline__ int size() const { return static_cast<int>(group_.size()); }
    __simt_callee__ __forceinline__ int thread_rank() const { return static_cast<int>(group_.thread_rank()); }
};
template <typename Coop>
struct HcommCoopTraits {
    static constexpr bool kWarpCapable = false;
    static constexpr bool kIsWarp = false;
};
template <>
struct HcommCoopTraits<HcommCoopWarp> {
    static constexpr bool kWarpCapable = true;
    static constexpr bool kIsWarp = true;
};
template <>
struct HcommCoopTraits<HcommCoopGroup> {
    static constexpr bool kWarpCapable = true;
    static constexpr bool kIsWarp = false;
};

} // namespace AscendC::simt
#endif

#endif // SRC_AICORE_UTILS_SIMT_UTILS_H
