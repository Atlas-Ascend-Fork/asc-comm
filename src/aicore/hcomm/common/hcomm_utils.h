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
 * \file hcomm_util.h
 * \brief Hcomm utils
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H

#include "hcomm_inner_def.h"

namespace AscendC {

constexpr uint32_t BITS_1BYTE = 8;
constexpr uint32_t BITS_3BYTE = 24;
constexpr uint32_t BITS_5BYTE = 40;
constexpr uint32_t BITS_7BYTE = 56;
constexpr uint32_t HCOMM_DEFAULT_QP_IDX = 0;

__aicore__ inline uint16_t HtoNS(uint16_t x)
{
    return (uint16_t)(((x & 0x00ffU) << BITS_1BYTE) | ((x & 0xff00U) >> BITS_1BYTE));
}

__aicore__ inline uint32_t HtoNL(uint32_t x)
{
    return ((x & 0x000000ffU) << BITS_3BYTE) | ((x & 0x0000ff00U) << BITS_1BYTE) | ((x & 0x00ff0000U) >> BITS_1BYTE) |
           ((x & 0xff000000U) >> BITS_3BYTE);
}

__aicore__ inline uint64_t HtoNLL(uint64_t x)
{
    return ((x & 0x00000000000000ffULL) << BITS_7BYTE) | ((x & 0x000000000000ff00ULL) << BITS_5BYTE) |
           ((x & 0x0000000000ff0000ULL) << BITS_3BYTE) | ((x & 0x00000000ff000000ULL) << BITS_1BYTE) |
           ((x & 0x000000ff00000000ULL) >> BITS_1BYTE) | ((x & 0x0000ff0000000000ULL) >> BITS_3BYTE) |
           ((x & 0x00ff000000000000ULL) >> BITS_5BYTE) | ((x & 0xff00000000000000ULL) >> BITS_7BYTE);
}

__aicore__ inline __ubuf__ uint8_t* AlignAddrTo32Bytes(__ubuf__ uint8_t* buff)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(buff);
    const uintptr_t alignment = 32;
    uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<__ubuf__ uint8_t*>(alignedAddr);
}

__aicore__ inline int32_t HcommFindBufferIdx(
    RegedBufferEntity* bufferAddr, uint32_t bufferNum, GM_ADDR addr, uint64_t len)
{
    if (bufferAddr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "HcommFindBufferIdx failed with null bufferAddr, bufferNum=%u\n", bufferNum);
        return HCOMM_FAILED;
    }
    uint64_t targetAddr = reinterpret_cast<uint64_t>(addr);
    for (uint32_t i = 0; i < bufferNum; i++) {
        uint64_t baseAddr = bufferAddr[i].bufferInfo.rma.addr;
        uint64_t bufferSize = bufferAddr[i].bufferInfo.rma.size;
        if (targetAddr < baseAddr) {
            continue;
        }
        uint64_t offset = targetAddr - baseAddr;
        if (offset <= bufferSize && len <= bufferSize - offset) {
            KERNEL_LOG(
                KERNEL_INFO, "HcommFindBufferIdx hit idx=%u addr=%llu len=%llu base=%llu size=%llu\n", i,
                static_cast<uint64_t>(targetAddr), static_cast<uint64_t>(len), static_cast<uint64_t>(baseAddr),
                static_cast<uint64_t>(bufferSize));
            return static_cast<int32_t>(i);
        }
    }
    KERNEL_LOG(
        KERNEL_ERROR, "HcommFindBufferIdx failed addr=%llu len=%llu bufferNum=%u\n", static_cast<uint64_t>(targetAddr),
        static_cast<uint64_t>(len), bufferNum);
    return HCOMM_FAILED;
}

template <typename T>
__aicore__ inline void CacheWriteThrough(__gm__ T* sourceAddr, uint64_t length)
{
    if (length == 0) {
        return;
    }
    __gm__ T* start = (__gm__ T*)((uint64_t)sourceAddr / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    __gm__ T* end = (__gm__ T*)(((uint64_t)sourceAddr + length) / CACHE_LINE_SIZE * CACHE_LINE_SIZE);
    GlobalTensor<T> global;
    global.SetGlobalBuffer(start);
    for (uint32_t i = 0; i <= end - start; i += CACHE_LINE_SIZE) {
        DataCacheCleanAndInvalid<T, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[i]);
    }
}

template <CommProtocol protocol>
__aicore__ inline __gm__ uint32_t* HcommGetLockAddr(__gm__ ChannelEntity* channelEntity)
{
    __gm__ uint64_t* cqContextsAddrField = reinterpret_cast<__gm__ uint64_t*>(
        reinterpret_cast<__gm__ uint8_t*>(channelEntity) + offsetof(ChannelEntity, cqContextAddr));
    uint64_t cqContextsAddr = ld_dev(cqContextsAddrField, 0);
    uint64_t lockAddrValue;
    __gm__ CqContext* cqContext = reinterpret_cast<__gm__ CqContext*>(cqContextsAddr) + HCOMM_DEFAULT_QP_IDX;
    if constexpr (protocol == COMM_PROTOCOL_ROCE) {
        lockAddrValue = cqContext->contextInfo.roceCq.headAddr;
    } else if constexpr (protocol == COMM_PROTOCOL_UB_CTP) {
        lockAddrValue = cqContext->contextInfo.ubJfc.headAddr;
    } else {
        static_assert(
            protocol == COMM_PROTOCOL_ROCE || protocol == COMM_PROTOCOL_UB_CTP,
            "Invalid CommProtocol, it must be COMM_PROTOCOL_ROCE or COMM_PROTOCOL_UB_CTP");
    }
    return reinterpret_cast<__gm__ uint32_t*>(lockAddrValue);
}

template <CommProtocol protocol>
__aicore__ inline int32_t HcommChannelLock(ChannelHandle channel)
{
    if (channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U) {
        return HCOMM_FAILED;
    }
    __gm__ ChannelEntity* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint32_t* lockAddr = HcommGetLockAddr<protocol>(channelEntity);
    while (AtomicCas(lockAddr, HCOMM_LOCK_FREE, HCOMM_LOCK_HELD) != HCOMM_LOCK_FREE) {
        // Back off for 800 cycles before retrying to reduce atomic contention.
        Nop<800>();
    }
    return HCOMM_SUCCESS;
}

template <CommProtocol protocol>
__aicore__ inline int32_t HcommChannelUnlock(ChannelHandle channel)
{
    if (channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U) {
        return HCOMM_FAILED;
    }

    __gm__ ChannelEntity* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint8_t* counterAddr = reinterpret_cast<__gm__ uint8_t*>(channelEntity) + offsetof(ChannelEntity, sqHead);
    // Flush the four contiguous channel counters: sqHead, sqTail, cqHead, and cqTail.
    CacheWriteThrough<uint8_t>(counterAddr, 4U * sizeof(uint32_t));

    __gm__ uint32_t* lockAddr = HcommGetLockAddr<protocol>(channelEntity);
    uint32_t oldValue = AtomicCas(lockAddr, HCOMM_LOCK_HELD, HCOMM_LOCK_FREE);
    return oldValue == HCOMM_LOCK_HELD ? HCOMM_SUCCESS : HCOMM_FAILED;
}
} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H
#endif
