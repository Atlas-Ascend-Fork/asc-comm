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
 * \file hcomm_simt_utils.h
 * \brief Hcomm SIMT utils
 *
 * SIMT counterpart of hcomm_utils.h. The two cannot share function bodies: a __simt_callee__
 * function cannot call an __aicore__ one, so every helper is duplicated per execution space.
 * Only types and constants are shared, through hcomm_inner_def.h.
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H

#include "hcomm_simt_inner_def.h"

namespace AscendC::simt {

__simt_callee__ inline int32_t HcommSimtFindBufferIdx(
    __gm__ RegedBufferEntity* bufferAddr, uint32_t bufferNum, __gm__ uint8_t* addr, uint64_t len)
{
    if (bufferAddr == nullptr) {
        return HCOMM_FAILED;
    }
    uint64_t targetAddr = reinterpret_cast<uint64_t>(addr);
    for (uint32_t i = 0; i < bufferNum; ++i) {
        uint64_t baseAddr = bufferAddr[i].bufferInfo.rma.addr;
        uint64_t bufferSize = bufferAddr[i].bufferInfo.rma.size;
        if (targetAddr < baseAddr) {
            continue;
        }
        uint64_t offset = targetAddr - baseAddr;
        if (offset <= bufferSize && len <= bufferSize - offset) {
            return static_cast<int32_t>(i);
        }
    }
    return HCOMM_FAILED;
}

__simt_callee__ inline uint64_t HcommSimtLoadLe64(__gm__ const uint8_t* addr)
{
    uint64_t value = 0U;
#pragma unroll
    for (uint32_t i = 0; i < sizeof(uint64_t); ++i) {
        value |= static_cast<uint64_t>(addr[i]) << (i * 8U);
    }
    return value;
}

__simt_callee__ inline uint32_t HcommSimtHeadIdx(uint64_t headVal)
{
    return static_cast<uint32_t>(headVal & 0xFFFFFFFFU);
}

__simt_callee__ inline uint32_t HcommSimtWqeCnt(uint64_t headVal)
{
    return static_cast<uint32_t>((headVal >> 32U) & HCOMM_SIMT_WQE_CNT_MASK);
}

// Atomically reserves SQ space for one submission without overwriting unconsumed BBs.
// requiredFreeBbCnt may exceed bbCnt when a deferred post must leave room for the
// immediate DWQE that will publish it later.
__simt_callee__ inline bool HcommSimtTryReserve(
    __gm__ uint64_t* headAddr, __gm__ uint32_t* tailAddr, uint32_t sqDepth, uint32_t bbCnt, uint32_t requiredFreeBbCnt,
    uint32_t cqeCnt, uint64_t& headVal)
{
    if (sqDepth == 0U || requiredFreeBbCnt > sqDepth) {
        return false;
    }

    uint64_t delta = (static_cast<uint64_t>(cqeCnt) << 32U) | static_cast<uint64_t>(bbCnt);
    headVal = *headAddr;
    while (true) {
        uint32_t usedBbCnt = HcommSimtHeadIdx(headVal) - *tailAddr;
        if (usedBbCnt > sqDepth || requiredFreeBbCnt > sqDepth - usedBbCnt) {
            return false;
        }

        uint64_t previous = asc_atomic_cas(headAddr, headVal, headVal + delta);
        if (previous == headVal) {
            return true;
        }
        headVal = previous;
    }
}

__simt_callee__ inline __gm__ RegedBufferEntity* HcommSimtLocalBuffers(ChannelHandle channel)
{
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    return reinterpret_cast<__gm__ RegedBufferEntity*>(channelEntity->localBufferAddr);
}

__simt_callee__ inline __gm__ RegedBufferEntity* HcommSimtRemoteBuffers(ChannelHandle channel)
{
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    return reinterpret_cast<__gm__ RegedBufferEntity*>(channelEntity->remoteBufferAddr);
}

__simt_callee__ inline __gm__ uint8_t* HcommSimtLocalBufferAddr(ChannelHandle channel, uint32_t bufferIdx)
{
    __gm__ RegedBufferEntity* localBuffers = HcommSimtLocalBuffers(channel);
    return reinterpret_cast<__gm__ uint8_t*>(localBuffers[bufferIdx].bufferInfo.rma.addr);
}

__simt_callee__ inline __gm__ uint8_t* HcommSimtRemoteBufferAddr(ChannelHandle channel, uint32_t bufferIdx)
{
    __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
    return reinterpret_cast<__gm__ uint8_t*>(remoteBuffers[bufferIdx].bufferInfo.rma.addr);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_UTILS_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_UTILS_H
#endif
