/*
 * xxHash - Extremely Fast Hash algorithm
 * Copyright (C) 2012-2023 Yann Collet
 *
 * BSD 2-Clause License (https://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following disclaimer
 *      in the documentation and/or other materials provided with the
 *      distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, OR FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Trimmed internal implementation for XXH3_64bits(input, length) only.

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace asccomm_xxh3_detail {
constexpr std::size_t XXH3_SECRETSIZE_MIN = 136;
constexpr std::size_t XXH_SECRET_DEFAULT_SIZE = 192;
constexpr std::size_t XXH_STRIPE_LEN = 64;
constexpr std::size_t XXH_SECRET_CONSUME_RATE = 8;
constexpr std::size_t XXH_ACC_NB = XXH_STRIPE_LEN / sizeof(uint64_t);
constexpr std::size_t XXH3_MIDSIZE_MAX = 240;
constexpr std::size_t XXH3_MIDSIZE_STARTOFFSET = 3;
constexpr std::size_t XXH3_MIDSIZE_LASTOFFSET = 17;
constexpr std::size_t XXH_SECRET_LASTACC_START = 7;
constexpr std::size_t XXH_SECRET_MERGEACCS_START = 11;

constexpr uint32_t PRIME32_1 = 0x9E3779B1U;
constexpr uint32_t PRIME32_2 = 0x85EBCA77U;
constexpr uint32_t PRIME32_3 = 0xC2B2AE3DU;
constexpr uint64_t PRIME64_1 = 11400714785074694791ULL;
constexpr uint64_t PRIME64_2 = 14029467366897019727ULL;
constexpr uint64_t PRIME64_3 = 1609587929392839161ULL;
constexpr uint64_t PRIME64_4 = 9650029242287828579ULL;
constexpr uint64_t PRIME64_5 = 2870177450012600261ULL;
constexpr uint64_t PRIME_MX1 = 0x165667919E3779F9ULL;
constexpr uint64_t PRIME_MX2 = 0x9FB21C651E98DF25ULL;

constexpr uint8_t XXH3_K_SECRET[XXH_SECRET_DEFAULT_SIZE] = {
    0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe, 0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
    0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb, 0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
    0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78, 0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
    0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e, 0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
    0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb, 0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
    0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e, 0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
    0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f, 0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
    0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31, 0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
    0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3, 0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
    0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49, 0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
    0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc, 0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
    0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28, 0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e,
};

static inline uint64_t Bswap64(uint64_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(value);
#else
    return ((value & 0x00000000000000ffULL) << 56) | ((value & 0x000000000000ff00ULL) << 40) |
        ((value & 0x0000000000ff0000ULL) << 24) | ((value & 0x00000000ff000000ULL) << 8) |
        ((value & 0x000000ff00000000ULL) >> 8) | ((value & 0x0000ff0000000000ULL) >> 24) |
        ((value & 0x00ff000000000000ULL) >> 40) | ((value & 0xff00000000000000ULL) >> 56);
#endif
}

static inline uint32_t Bswap32(uint32_t value)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(value);
#else
    return ((value & 0x000000ffU) << 24) | ((value & 0x0000ff00U) << 8) |
        ((value & 0x00ff0000U) >> 8) | ((value & 0xff000000U) >> 24);
#endif
}

static inline uint64_t Read64LE(const void *ptr)
{
    uint64_t value;
    std::memcpy(&value, ptr, sizeof(value));
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    value = Bswap64(value);
#endif
    return value;
}

static inline uint32_t Read32LE(const void *ptr)
{
    uint32_t value;
    std::memcpy(&value, ptr, sizeof(value));
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    value = Bswap32(value);
#endif
    return value;
}

static inline uint64_t Rotl64(uint64_t value, uint32_t shift)
{
    return (value << shift) | (value >> (64U - shift));
}

static inline uint64_t XXH64Avalanche(uint64_t hash)
{
    hash ^= hash >> 33;
    hash *= PRIME64_2;
    hash ^= hash >> 29;
    hash *= PRIME64_3;
    hash ^= hash >> 32;
    return hash;
}

static inline uint64_t Mul128Fold64(uint64_t lhs, uint64_t rhs)
{
#if defined(__SIZEOF_INT128__) || (defined(_INTEGRAL_MAX_BITS) && _INTEGRAL_MAX_BITS >= 128)
    __uint128_t product = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
    return static_cast<uint64_t>(product) ^ static_cast<uint64_t>(product >> 64);
#else
    const uint64_t loLo = (lhs & 0xFFFFFFFFULL) * (rhs & 0xFFFFFFFFULL);
    const uint64_t hiLo = (lhs >> 32) * (rhs & 0xFFFFFFFFULL);
    const uint64_t loHi = (lhs & 0xFFFFFFFFULL) * (rhs >> 32);
    const uint64_t hiHi = (lhs >> 32) * (rhs >> 32);
    const uint64_t cross = (loLo >> 32) + (hiLo & 0xFFFFFFFFULL) + loHi;
    const uint64_t upper = (hiLo >> 32) + (cross >> 32) + hiHi;
    const uint64_t lower = (cross << 32) | (loLo & 0xFFFFFFFFULL);
    return upper ^ lower;
#endif
}

static inline uint64_t XXH3Avalanche(uint64_t hash)
{
    hash ^= hash >> 37;
    hash *= PRIME_MX1;
    hash ^= hash >> 32;
    return hash;
}

static inline uint64_t Len1To3(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    const uint8_t c1 = input[0];
    const uint8_t c2 = input[len >> 1];
    const uint8_t c3 = input[len - 1];
    const uint32_t combined = (static_cast<uint32_t>(c1) << 16) |
        (static_cast<uint32_t>(c2) << 24) | static_cast<uint32_t>(c3) |
        (static_cast<uint32_t>(len) << 8);
    const uint64_t bitflip = static_cast<uint64_t>(Read32LE(secret) ^ Read32LE(secret + 4));
    return XXH64Avalanche(static_cast<uint64_t>(combined) ^ bitflip);
}

static inline uint64_t Len4To8(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    const uint32_t input1 = Read32LE(input);
    const uint32_t input2 = Read32LE(input + len - 4);
    uint64_t acc = Read64LE(secret + 8) ^ Read64LE(secret + 16);
    const uint64_t input64 = static_cast<uint64_t>(input2) | (static_cast<uint64_t>(input1) << 32);
    acc ^= input64;
    acc ^= Rotl64(acc, 49) ^ Rotl64(acc, 24);
    acc *= PRIME_MX2;
    acc ^= (acc >> 35) + static_cast<uint64_t>(len);
    acc *= PRIME_MX2;
    return acc ^ (acc >> 28);
}

static inline uint64_t Len9To16(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    uint64_t inputLo = Read64LE(secret + 24) ^ Read64LE(secret + 32);
    uint64_t inputHi = Read64LE(secret + 40) ^ Read64LE(secret + 48);
    inputLo ^= Read64LE(input);
    inputHi ^= Read64LE(input + len - 8);
    const uint64_t acc = static_cast<uint64_t>(len) + Bswap64(inputLo) + inputHi +
        Mul128Fold64(inputLo, inputHi);
    return XXH3Avalanche(acc);
}

static inline uint64_t Len0To16(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    if (len > 8) {
        return Len9To16(input, len, secret);
    }
    if (len >= 4) {
        return Len4To8(input, len, secret);
    }
    if (len != 0) {
        return Len1To3(input, len, secret);
    }
    return XXH64Avalanche(Read64LE(secret + 56) ^ Read64LE(secret + 64));
}

static inline uint64_t Mix16B(const uint8_t *input, const uint8_t *secret)
{
    const uint64_t lhs = Read64LE(secret) ^ Read64LE(input);
    const uint64_t rhs = Read64LE(secret + 8) ^ Read64LE(input + 8);
    return Mul128Fold64(lhs, rhs);
}

static inline uint64_t Len17To128(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    uint64_t acc = len * PRIME64_1;
    uint64_t accResult = Mix16B(input + len - 16, secret + 16);
    acc += Mix16B(input, secret);
    if (len > 32) {
        acc += Mix16B(input + 16, secret + 32);
        accResult += Mix16B(input + len - 32, secret + 48);
        if (len > 64) {
            acc += Mix16B(input + 32, secret + 64);
            accResult += Mix16B(input + len - 48, secret + 80);
            if (len > 96) {
                acc += Mix16B(input + 48, secret + 96);
                accResult += Mix16B(input + len - 64, secret + 112);
            }
        }
    }
    return XXH3Avalanche(acc + accResult);
}

static inline uint64_t Len129To240(const uint8_t *input, std::size_t len, const uint8_t *secret)
{
    uint64_t acc = static_cast<uint64_t>(len) * PRIME64_1;
    const std::size_t nbRounds = len / 16;
    for (std::size_t i = 0; i < 8; ++i) {
        acc += Mix16B(input + (16 * i), secret + (16 * i));
    }
    acc = XXH3Avalanche(acc);
    for (std::size_t i = 8; i < nbRounds; ++i) {
        acc += Mix16B(input + (16 * i), secret + (16 * (i - 8)) + XXH3_MIDSIZE_STARTOFFSET);
    }
    acc += Mix16B(input + len - 16, secret + XXH3_SECRETSIZE_MIN - XXH3_MIDSIZE_LASTOFFSET);
    return XXH3Avalanche(acc);
}

static inline void Accumulate512(uint64_t *acc, const uint8_t *input, const uint8_t *secret)
{
    for (std::size_t i = 0; i < XXH_ACC_NB; ++i) {
        const uint64_t dataVal = Read64LE(input + (8 * i));
        const uint64_t dataKey = dataVal ^ Read64LE(secret + (8 * i));
        acc[i ^ 1] += dataVal;
        acc[i] += static_cast<uint32_t>(dataKey) * (dataKey >> 32);
    }
}

static inline void ScrambleAcc(uint64_t *acc, const uint8_t *secret)
{
    for (std::size_t i = 0; i < XXH_ACC_NB; ++i) {
        acc[i] ^= acc[i] >> 47;
        acc[i] ^= Read64LE(secret + (8 * i));
        acc[i] *= PRIME32_1;
    }
}

static inline void Accumulate(uint64_t *acc, const uint8_t *input, const uint8_t *secret,
    std::size_t nbStripes)
{
    for (std::size_t n = 0; n < nbStripes; ++n) {
        Accumulate512(acc, input + (n * XXH_STRIPE_LEN), secret + (n * XXH_SECRET_CONSUME_RATE));
    }
}

static inline uint64_t Mix2Accs(const uint64_t *acc, const uint8_t *secret)
{
    return Mul128Fold64(acc[0] ^ Read64LE(secret), acc[1] ^ Read64LE(secret + 8));
}

static inline uint64_t MergeAccs(const uint64_t *acc, const uint8_t *secret, uint64_t start)
{
    uint64_t result = start;
    for (std::size_t i = 0; i < 4; ++i) {
        result += Mix2Accs(acc + (2 * i), secret + (16 * i));
    }
    return XXH3Avalanche(result);
}

static inline uint64_t HashLong(const uint8_t *input, std::size_t len, const uint8_t *secret,
    std::size_t secretSize)
{
    const std::size_t nbStripesPerBlock = (secretSize - XXH_STRIPE_LEN) / XXH_SECRET_CONSUME_RATE;
    const std::size_t blockLen = XXH_STRIPE_LEN * nbStripesPerBlock;
    const std::size_t nbBlocks = (len - 1) / blockLen;
    alignas(16) uint64_t acc[XXH_ACC_NB] = {
        PRIME32_3, PRIME64_1, PRIME64_2, PRIME64_3,
        PRIME64_4, PRIME32_2, PRIME64_5, PRIME32_1,
    };

    for (std::size_t n = 0; n < nbBlocks; ++n) {
        Accumulate(acc, input + (n * blockLen), secret, nbStripesPerBlock);
        ScrambleAcc(acc, secret + secretSize - XXH_STRIPE_LEN);
    }

    const std::size_t nbStripes = (len - 1 - (blockLen * nbBlocks)) / XXH_STRIPE_LEN;
    Accumulate(acc, input + (nbBlocks * blockLen), secret, nbStripes);
    Accumulate512(acc, input + len - XXH_STRIPE_LEN,
        secret + secretSize - XXH_STRIPE_LEN - XXH_SECRET_LASTACC_START);
    return MergeAccs(acc, secret + XXH_SECRET_MERGEACCS_START, static_cast<uint64_t>(len) * PRIME64_1);
}

static inline uint64_t XXH3_64bitsImpl(const void *input, std::size_t length)
{
    const uint8_t *data = static_cast<const uint8_t *>(input);
    if (length <= 16) {
        return Len0To16(data, length, XXH3_K_SECRET);
    }
    if (length <= 128) {
        return Len17To128(data, length, XXH3_K_SECRET);
    }
    if (length <= XXH3_MIDSIZE_MAX) {
        return Len129To240(data, length, XXH3_K_SECRET);
    }
    return HashLong(data, length, XXH3_K_SECRET, sizeof(XXH3_K_SECRET));
}
} // namespace asccomm_xxh3_detail

static inline uint64_t XXH3_64bits(const void *input, std::size_t length)
{
    return asccomm_xxh3_detail::XXH3_64bitsImpl(input, length);
}
