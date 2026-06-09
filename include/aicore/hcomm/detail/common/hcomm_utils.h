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

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/common/hcomm_utils.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_UTIL_H

#include "hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline uint32_t HtoNL(uint32_t x)
{
    constexpr uint32_t byte0Mask = 0x000000ffU;
    constexpr uint32_t byte1Mask = 0x0000ff00U;
    constexpr uint32_t byte2Mask = 0x00ff0000U;
    constexpr uint32_t byte3Mask = 0xff000000U;
    constexpr uint32_t byteShift = 8;
    constexpr uint32_t wordShift = 24;

    return (((x & byte3Mask) >> wordShift) | ((x & byte2Mask) >> byteShift) | ((x & byte1Mask) << byteShift) |
        ((x & byte0Mask) << wordShift));
}

__aicore__ inline uint64_t HtoNLL(uint64_t x)
{
    constexpr uint64_t byte0Mask = 0x00000000000000ffU;
    constexpr uint64_t byte1Mask = 0x000000000000ff00U;
    constexpr uint64_t byte2Mask = 0x0000000000ff0000U;
    constexpr uint64_t byte3Mask = 0x00000000ff000000U;
    constexpr uint64_t byte4Mask = 0x000000ff00000000U;
    constexpr uint64_t byte5Mask = 0x0000ff0000000000U;
    constexpr uint64_t byte6Mask = 0x00ff000000000000U;
    constexpr uint64_t byte7Mask = 0xff00000000000000U;

    constexpr uint64_t shift8 = 8;
    constexpr uint64_t shift24 = 24;
    constexpr uint64_t shift40 = 40;
    constexpr uint64_t shift56 = 56;

    return (((x & byte0Mask) << shift56) | ((x & byte1Mask) << shift40) |
            ((x & byte2Mask) << shift24) | ((x & byte3Mask) << shift8) |
            ((x & byte4Mask) >> shift8) | ((x & byte5Mask) >> shift24) |
            ((x & byte6Mask) >> shift40) | ((x & byte7Mask) >> shift56));
}

template<HardEvent event>
__aicore__ inline void SyncAction()
{
    TEventID eventID = GetTPipePtr()->FetchEventID(event);
    SetFlag<event>(eventID);
    WaitFlag<event>(eventID);
}
__aicore__ inline __ubuf__ uint8_t* AlignAddrTo32Bytes(__ubuf__ uint8_t* buff)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(buff);
    const uintptr_t alignment = 32;
    uintptr_t alignedAddr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<__ubuf__ uint8_t*>(alignedAddr);
}

template <typename T>
__aicore__ inline void Gm2Ub(
    const AscendC::LocalTensor<T>& dstLocal, const AscendC::GlobalTensor<T>& srcGlobal, int32_t length)
{
    AscendC::DataCopyExtParams copyParams{1, (uint32_t)(length * sizeof(T)), 0, 0, 0};
    AscendC::DataCopyPadExtParams<T> padParams{true, 0, (ONE_BLK_SIZE - sizeof(T)) / sizeof(T), 0};

    PipeBarrier<PIPE_ALL>();
    AscendC::DataCopyPad(dstLocal, srcGlobal, copyParams, padParams);
    PipeBarrier<PIPE_ALL>();
}

template <typename T>
__aicore__ inline void Ub2Gm(
    const AscendC::GlobalTensor<T>& dstGlobal, const AscendC::LocalTensor<T>& srcLocal, int32_t length)
{
    AscendC::DataCopyExtParams copyParams{1, (uint32_t)(length * sizeof(T)), 0, 0, 0};
    PipeBarrier<PIPE_ALL>();
    AscendC::DataCopyPad(dstGlobal, srcLocal, copyParams);
    PipeBarrier<PIPE_ALL>();
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
            KERNEL_LOG(KERNEL_INFO, "HcommFindBufferIdx hit idx=%u addr=%llu len=%llu base=%llu size=%llu\n", i,
                static_cast<uint64_t>(targetAddr), static_cast<uint64_t>(len),
                static_cast<uint64_t>(baseAddr), static_cast<uint64_t>(bufferSize));
            return static_cast<int32_t>(i);
        }
    }
    KERNEL_LOG(KERNEL_ERROR, "HcommFindBufferIdx failed addr=%llu len=%llu bufferNum=%u\n",
        static_cast<uint64_t>(targetAddr), static_cast<uint64_t>(len), bufferNum);
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
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_UTILS_H__
#endif
