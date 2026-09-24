/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository.
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

// Passed by value to the kernel. All offsets and products on device are uint64_t.
struct Tiling {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t ranks;
    uint32_t cores;
    uint32_t tileM;
    uint32_t rounds;
};

inline uint64_t CeilDiv(uint64_t value, uint64_t divisor)
{
    return value / divisor + (value % divisor != 0);
}

inline uint64_t CheckedProduct(uint64_t a, uint64_t b)
{
    const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (b != 0 && a > limit / b) {
        throw std::invalid_argument("tensor size exceeds addressable memory");
    }
    return a * b;
}

inline Tiling MakeTiling(uint32_t m, uint32_t n, uint32_t k, uint32_t ranks, uint32_t cores, uint32_t tileM)
{
    if (m == 0 || n == 0 || k == 0 || ranks < 2 || ranks > 64 || cores == 0 || n % 16 || k % 16) {
        throw std::invalid_argument("require M>0, N/K positive multiples of 16, 2<=ranks<=64 and cores>0");
    }
    CheckedProduct(CheckedProduct(CheckedProduct(m, k), ranks), 2);
    CheckedProduct(CheckedProduct(CheckedProduct(m, n), ranks), 2);
    CheckedProduct(CheckedProduct(k, n), 2);
    const uint64_t maxRows = MAX_GET_BYTES / (static_cast<uint64_t>(k) * 2);
    if (maxRows == 0) {
        throw std::invalid_argument("one A row exceeds the 256 MiB AIN Get limit");
    }
    if (tileM == 0) {
        const uint64_t nTiles = CeilDiv(n, CUBE_N);
        const uint64_t rowTiles = CeilDiv(cores, (ranks - 1) * nTiles);
        const uint64_t alignedMaxRows = maxRows / CUBE_M * CUBE_M;
        const uint64_t capacity = alignedMaxRows == 0 ? maxRows : alignedMaxRows;
        tileM = static_cast<uint32_t>(std::min<uint64_t>(m, std::min(rowTiles * CUBE_M, capacity)));
    }
    // A final/whole-input block may be shorter than one Cube tile.
    if (tileM > m || tileM > maxRows || (tileM < m && tileM % CUBE_M != 0)) {
        throw std::invalid_argument("tile-m must fit M and 256 MiB; non-final tiles must be multiples of 256");
    }
    return {m, n, k, ranks, cores, tileM, static_cast<uint32_t>(CeilDiv(m, tileM))};
}

} // namespace ain_agmm
