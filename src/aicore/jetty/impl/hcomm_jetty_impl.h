/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_IMPL_H

#if defined(__NPU_ARCH__) && __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_aiv_urma_jetty.h"
#endif

namespace AscendC {

__aicore__ inline HcommJetty::HcommJetty(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe, GM_ADDR jettyTable, uint32_t jettyIdx)
    : impl_(jettyInfo, ubufSqe, jettyTable, jettyIdx)
{}

template <int64_t timeoutCycles, bool doCommit>
__aicore__ inline int32_t HcommJetty::Write(const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes)
{
    return impl_.PostWrite<timeoutCycles, false, doCommit>(
        peer, dstAddr, srcAddr, numBytes, nullptr, 0U, kNumWriteWqebbs);
}

template <int64_t timeoutCycles, typename T, bool doCommit>
__aicore__ inline int32_t HcommJetty::WriteValue(const HcommPeer& peer, GM_ADDR dstAddr, T value)
{
    static_assert(
        sizeof(T) <= kNumWriteWqebbs * 64U - sizeof(HcommUrmaSqeCtx), "WriteValue value is too large for one WQEBB");
    return impl_.PostWriteValue<timeoutCycles, T, doCommit>(peer, dstAddr, value, kNumWriteWqebbs);
}

template <int64_t timeoutCycles>
__aicore__ inline int32_t HcommJetty::WriteWithNotify(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes, GM_ADDR notifyAddr,
    uint64_t notifyValue)
{
    return impl_.PostWrite<timeoutCycles, true, true>(
        peer, dstAddr, srcAddr, numBytes, notifyAddr, notifyValue, kNumWriteWithNotifyWqebbs);
}

template <int64_t timeoutCycles>
__aicore__ inline int32_t HcommJetty::Drain()
{
    return impl_.Drain<timeoutCycles>();
}

__aicore__ inline void HcommJetty::AdvanceSq(uint32_t numWqebbs) { impl_.AdvanceSq(numWqebbs); }

__aicore__ inline void HcommJetty::RingDoorbell() { impl_.RingDoorbell(); }

} // namespace AscendC

#endif
