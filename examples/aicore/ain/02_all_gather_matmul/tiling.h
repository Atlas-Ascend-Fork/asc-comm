/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace ain_agmm {

constexpr uint32_t CUBE_M = 256;
constexpr uint32_t CUBE_N = 256;
constexpr uint32_t CUBE_K = 64;
constexpr uint64_t MAX_GET_BYTES = 256ULL * 1024 * 1024;

// Host 生成后按值传入 kernel；M 是每 rank 的输入行数，输出总行数为 ranks*M。
// 设备侧地址乘积使用 uint64_t，避免大矩阵跨过 4 GiB 时发生 32 位截断。
struct tiling {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t ranks;
    uint32_t cores;  // 启动的 AIC 数；融合模式按 1:1 配对相同数量的 AIV。
    uint32_t tile_m; // 一轮通信处理的 M 行数，不等于单个 Cube tile 的行数。
    uint32_t rounds; // ceil(M/tile_m)，最后一轮可能不足 tile_m。
};

// 向上取整除法，用商和余数避免 value+divisor-1 的中间加法溢出。
inline uint64_t ceil_div(uint64_t value, uint64_t divisor) { return value / divisor + (value % divisor != 0); }

// 在申请内存前检查尺寸乘积，确保不超出指针差值可表示范围。
inline uint64_t checked_product(uint64_t a, uint64_t b)
{
    const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (b != 0 && a > limit / b) {
        throw std::invalid_argument("tensor size exceeds addressable memory");
    }
    return a * b;
}

// 校验 shape/内存范围，并确定通信 tile_m；这里做 Host 侧校验，kernel 依赖这些约束。
inline tiling make_tiling(uint32_t m, uint32_t n, uint32_t k, uint32_t ranks, uint32_t cores, uint32_t tile_m)
{
    if (m == 0 || n == 0 || k == 0 || ranks < 2 || ranks > 64 || cores == 0 || n % 16 || k % 16) {
        throw std::invalid_argument("require M>0, N/K positive multiples of 16, 2<=ranks<=64 and cores>0");
    }
    // 依次检查完整接收区、输出 C 和 B 的字节数，FP16/BF16 每元素均为 2 字节。
    checked_product(checked_product(checked_product(m, k), ranks), 2);
    checked_product(checked_product(checked_product(m, n), ranks), 2);
    checked_product(checked_product(k, n), 2);
    const uint64_t max_rows = MAX_GET_BYTES / (static_cast<uint64_t>(k) * 2);
    if (max_rows == 0) {
        throw std::invalid_argument("one A row exceeds the 256 MiB ain get limit");
    }
    if (tile_m == 0) {
        const uint64_t n_tiles = ceil_div(n, CUBE_N);
        // 每个 M tile 可提供 (P-1)*n_tiles 个远端计算任务；优先凑够一轮核数，
        // 再受M和单次get的256 MiB上限约束。这只是初始启发式，不保证最优。
        const uint64_t row_tiles = ceil_div(cores, (ranks - 1) * n_tiles);
        const uint64_t aligned_max_rows = max_rows / CUBE_M * CUBE_M;
        const uint64_t capacity = aligned_max_rows == 0 ? max_rows : aligned_max_rows;
        tile_m = static_cast<uint32_t>(std::min<uint64_t>(m, std::min(row_tiles * CUBE_M, capacity)));
    }
    // 中间通信块按 Cube M 对齐；整段不足一块或最后的尾块允许非对齐，kernel 按实际行数处理。
    if (tile_m > m || tile_m > max_rows || (tile_m < m && tile_m % CUBE_M != 0)) {
        throw std::invalid_argument("tile-m must fit M and 256 MiB; non-final tiles must be multiples of 256");
    }
    return {m, n, k, ranks, cores, tile_m, static_cast<uint32_t>(ceil_div(m, tile_m))};
}

} // namespace ain_agmm
