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
    __aicore__ inline HcommJettyImpl(
        __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
        GM_ADDR jettyTable, uint32_t jettyIdx);

    template <int64_t kNumTimeoutCycles, bool kWithNotify, bool kDoCommit>
    __aicore__ inline int32_t PostWrite(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
        GM_ADDR notifyAddr, uint64_t notifyValue, uint32_t numWqebbs);

    template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit>
    __aicore__ inline int32_t PostWriteValue(
        const HcommPeer& peer, GM_ADDR dstAddr, T value, uint32_t numWqebbs);

    template <int64_t kNumTimeoutCycles>
    __aicore__ inline int32_t Drain();

    __aicore__ inline void AdvanceSq(uint32_t numWqebbs);

    __aicore__ inline void RingDoorbell();

private:
    template <bool kWithNotify>
    __aicore__ inline bool BuildWriteSqe(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
        uint32_t currentHead, GM_ADDR notifyAddr, uint64_t notifyValue);

    template <typename T>
    __aicore__ inline bool BuildWriteValueSqe(
        const HcommPeer& peer, GM_ADDR dstAddr, T value, uint32_t currentHead);

    __aicore__ inline void CopySqeToSq(uint32_t currentHead, uint32_t numWqebbs);

    template <int64_t kNumTimeoutCycles>
    __aicore__ inline int32_t PollCq(uint32_t expectedTail);

    template <int64_t kNumTimeoutCycles>
    __aicore__ inline void PollCqWhenSqOverflow();

    __ubuf__ HcommJettyInfo* jettyInfo_ = nullptr;
    __ubuf__ uint8_t* ubufSqe_ = nullptr;
    bool valid_ = false;
};

} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_DEF_H
