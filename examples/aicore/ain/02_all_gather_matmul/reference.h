/*
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 * See LICENSE in the root of the software repository.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ain_agmm {

// Fixtures use exactly representable multiples of 1/16 in both FP16 and BF16.
inline float InputValue(uint32_t rank, uint32_t row, uint32_t col, uint32_t seed, bool weight)
{
    uint32_t x = rank * 0x9e3779b9U + row * 0x85ebca6bU + col * 0xc2b2ae35U + seed * 97U;
    x ^= weight ? 0xa511e9b3U : 0x63d83595U;
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    return static_cast<float>(static_cast<int32_t>(x % 9U) - 4) / 16.0F;
}

inline uint16_t Encode(float value, bool bf16)
{
    uint32_t u = 0;
    std::memcpy(&u, &value, sizeof(u));
    if (bf16) {
        if ((u & 0x7fffffffU) > 0x7f800000U) {
            return static_cast<uint16_t>((u >> 16) | 0x40U);
        }
        return static_cast<uint16_t>((u + 0x7fffU + ((u >> 16) & 1U)) >> 16);
    }
    const uint32_t sign = (u >> 16) & 0x8000U;
    const uint32_t exp = (u >> 23) & 0xffU;
    const uint32_t mantissa = u & 0x7fffffU;
    if (exp == 255) {
        return static_cast<uint16_t>(sign | 0x7c00U | (mantissa ? 0x200U : 0U));
    }
    int32_t halfExp = static_cast<int32_t>(exp) - 127 + 15;
    if (halfExp >= 31) {
        return static_cast<uint16_t>(sign | 0x7c00U);
    }
    if (halfExp < -10) {
        return static_cast<uint16_t>(sign);
    }
    if (halfExp <= 0) {
        const uint32_t shift = static_cast<uint32_t>(14 - halfExp);
        const uint32_t m = mantissa | 0x800000U;
        return static_cast<uint16_t>(sign | ((m + ((1U << (shift - 1)) - 1) + ((m >> shift) & 1)) >> shift));
    }
    const uint32_t rounded = mantissa + 0xfffU + ((mantissa >> 13) & 1U);
    return static_cast<uint16_t>(sign | ((static_cast<uint32_t>(halfExp) << 10) + (rounded >> 13)));
}

inline float Decode(uint16_t value, bool bf16)
{
    if (bf16) {
        uint32_t u = static_cast<uint32_t>(value) << 16;
        float result;
        std::memcpy(&result, &u, sizeof(result));
        return result;
    }
    const uint32_t exp = (value >> 10) & 31U;
    const uint32_t frac = value & 1023U;
    float result = exp == 0 ? std::ldexp(static_cast<float>(frac), -24) :
                   exp == 31 ? (frac ? NAN : INFINITY) :
                               std::ldexp(static_cast<float>(1024 + frac), static_cast<int>(exp) - 25);
    return value & 0x8000U ? -result : result;
}

// Periodic-K fixtures allow a full-output golden in O(P*M*N*16).
// --random uses every K index, for independent small-shape O(P*M*N*K) checks.
inline float Golden(uint32_t sourceRank, uint32_t weightRank, uint32_t row, uint32_t col,
                    uint32_t k, uint32_t seed, bool random)
{
    float sum = 0;
    const uint32_t count = random ? k : 16;
    for (uint32_t i = 0; i < count; ++i) {
        sum += InputValue(sourceRank, row, i, seed, false) * InputValue(weightRank, i, col, seed, true);
    }
    return random ? sum : sum * static_cast<float>(k / 16);
}

} // namespace ain_agmm
