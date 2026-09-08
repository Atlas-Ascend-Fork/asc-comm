/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_DEF_H

namespace AscendC {

class HcommJettyImpl {
public:
    __aicore__ __forceinline__ HcommJettyImpl(
        __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
        __gm__ void* jettyTable, uint32_t jettyIdx);

    template <int64_t kNumTimeoutCycles, bool kWithNotify, bool kDoCommit>
    __aicore__ __forceinline__ int32_t PostWrite(
        const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes,
        __gm__ void* notifyAddr, uint64_t notifyValue, uint32_t numWqebbs);

    template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit>
    __aicore__ __forceinline__ int32_t PostWriteValue(
        const HcommPeer& peer, __gm__ void* dstAddr, T value, uint32_t numWqebbs);

    template <int64_t kNumTimeoutCycles>
    __aicore__ __forceinline__ int32_t Drain();

private:
    template <bool kWithNotify>
    __aicore__ __forceinline__ bool BuildWriteSqe(
        const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes,
        uint32_t currentHead, __gm__ void* notifyAddr, uint64_t notifyValue);

    template <typename T>
    __aicore__ __forceinline__ bool BuildWriteValueSqe(
        const HcommPeer& peer, __gm__ void* dstAddr, T value, uint32_t currentHead);

    __aicore__ __forceinline__ void CopySqeToSq(uint32_t currentHead, uint32_t numWqebbs);
    __aicore__ __forceinline__ void AdvanceSq(uint32_t numWqebbs);
    __aicore__ __forceinline__ void RingDoorbell();

    template <int64_t kNumTimeoutCycles>
    __aicore__ __forceinline__ int32_t PollCq(uint32_t expectedTail);

    template <int64_t kNumTimeoutCycles>
    __aicore__ __forceinline__ void PollCqWhenSqOverflow();

    __ubuf__ HcommJettyInfo* jettyInfo_ = nullptr;
    __ubuf__ uint8_t* ubufSqe_ = nullptr;
    bool valid_ = false;
};

} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_DEF_H
