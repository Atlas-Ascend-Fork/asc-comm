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

// Host 生成后按值传入 kernel；M 是每 rank 的输入行数，输出总行数为 ranks*M。
// 设备侧地址乘积使用 uint64_t，避免大矩阵跨过 4 GiB 时发生 32 位截断。
struct Tiling {
    uint32_t m;
    uint32_t n;
    uint32_t k;
    uint32_t ranks;
    uint32_t cores; // 启动的 AIC 数；融合模式按 1:1 配对相同数量的 AIV。
    uint32_t tileM; // 一轮通信处理的 M 行数，不等于单个 Cube tile 的行数。
    uint32_t rounds; // ceil(M/tileM)，最后一轮可能不足 tileM。
};

// 向上取整除法，用商和余数避免 value+divisor-1 的中间加法溢出。
inline uint64_t CeilDiv(uint64_t value, uint64_t divisor)
{
    return value / divisor + (value % divisor != 0);
}

// 在申请内存前检查尺寸乘积，确保不超出指针差值可表示范围。
inline uint64_t CheckedProduct(uint64_t a, uint64_t b)
{
    const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<std::ptrdiff_t>::max());
    if (b != 0 && a > limit / b) {
        throw std::invalid_argument("tensor size exceeds addressable memory");
    }
    return a * b;
}

// 校验 shape/内存范围，并确定通信 tileM；这里做 Host 侧校验，kernel 依赖这些约束。
inline Tiling MakeTiling(uint32_t m, uint32_t n, uint32_t k, uint32_t ranks, uint32_t cores, uint32_t tileM)
{
    if (m == 0 || n == 0 || k == 0 || ranks < 2 || ranks > 64 || cores == 0 || n % 16 || k % 16) {
        throw std::invalid_argument("require M>0, N/K positive multiples of 16, 2<=ranks<=64 and cores>0");
    }
    // 依次检查完整接收区、输出 C 和 B 的字节数，FP16/BF16 每元素均为 2 字节。
    CheckedProduct(CheckedProduct(CheckedProduct(m, k), ranks), 2);
    CheckedProduct(CheckedProduct(CheckedProduct(m, n), ranks), 2);
    CheckedProduct(CheckedProduct(k, n), 2);
    const uint64_t maxRows = MAX_GET_BYTES / (static_cast<uint64_t>(k) * 2);
    if (maxRows == 0) {
        throw std::invalid_argument("one A row exceeds the 256 MiB AIN Get limit");
    }
    if (tileM == 0) {
        const uint64_t nTiles = CeilDiv(n, CUBE_N);
        // 每个 M tile 可提供 (P-1)*nTiles 个远端计算任务；优先凑够一轮核数，
        // 再受 M 和单次 Get 的 256 MiB 上限约束。这只是初始启发式，不保证最优。
        const uint64_t rowTiles = CeilDiv(cores, (ranks - 1) * nTiles);
        const uint64_t alignedMaxRows = maxRows / CUBE_M * CUBE_M;
        const uint64_t capacity = alignedMaxRows == 0 ? maxRows : alignedMaxRows;
        tileM = static_cast<uint32_t>(std::min<uint64_t>(m, std::min(rowTiles * CUBE_M, capacity)));
    }
    // 中间通信块按 Cube M 对齐；整段不足一块或最后的尾块允许非对齐，kernel 按实际行数处理。
    if (tileM > m || tileM > maxRows || (tileM < m && tileM % CUBE_M != 0)) {
        throw std::invalid_argument("tile-m must fit M and 256 MiB; non-final tiles must be multiples of 256");
    }
    return {m, n, k, ranks, cores, tileM, static_cast<uint32_t>(CeilDiv(m, tileM))};
}

} // namespace ain_agmm
