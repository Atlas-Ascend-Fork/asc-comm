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
 * \file hcomm_jetty_simt_impl.h
 * \brief Definitions of the public SIMT Jetty member functions.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_SIMT_IMPL_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_SIMT_IMPL_H

#include "hcomm_jetty_simt_impl_def.h"

// SIMT Jetty is only available on v310. The ASC driver compiles each translation unit twice:
// __NPU_ARCH__ is 3510 in the device pass and undefined in the host pass. The public HcommJetty
// declares its members out-of-line, so definitions are only emitted on platforms that have the
// implementation; other platforms fail at link time if the API is used.
#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510
#include "platform_v310/hcomm_simt_urma_jetty.h"
#endif

namespace AscendC::simt::jetty {

#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510

__simt_callee__ __forceinline__ HcommJetty::HcommJetty() = default;

__simt_callee__ __forceinline__
HcommJetty::HcommJetty(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
    : impl_(info, stage, table, index)
{}

__simt_callee__ __forceinline__
HcommJetty::HcommJetty(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
    : impl_(info, stage, channelAddr)
{}

__simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* HcommJetty::Info() const { return impl_.Info(); }

__simt_callee__ __forceinline__ void HcommJetty::Attach(
    __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channel)
{
    impl_.Attach(info, stage, channel);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
HcommJetty::Write(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop) const
{
    return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, sges, coop);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
HcommJetty::Write(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
{
    return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, &sge, coop);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
HcommJetty::Write(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges) const
{
    return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, sges, coop);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
HcommJetty::Put(const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges, const Coop& coop) const
{
    return Write<kSgeNum, kDoCommit>(peer, dst, sges, coop);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
HcommJetty::Put(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const
{
    return impl_.template Write<kSgeNum, kDoCommit>(peer, dst, &sge, coop);
}

template <int64_t kTimeout, typename T, bool kDoCommit>
__simt_callee__ __forceinline__ int32_t HcommJetty::WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
{
    return impl_.template WriteValue<kTimeout, T, kDoCommit>(peer, dst, value);
}

template <int64_t kTimeout, bool kDoCommit>
__simt_callee__ __forceinline__ int32_t HcommJetty::WriteWithNotify(
    const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyValue)
{
    return impl_.template WriteWithNotify<kTimeout, kDoCommit>(peer, dst, src, len, notifyAddr, notifyValue);
}

__simt_callee__ __forceinline__ void HcommJetty::AdvanceSq() const { impl_.AdvanceSq(); }

template <typename Coop>
__simt_callee__ __forceinline__ void HcommJetty::AdvanceSq(const Coop& coop) const
{
    impl_.AdvanceSq(coop);
}

template <typename Coop>
__simt_callee__ __forceinline__ void HcommJetty::AdvanceSq(const Coop& coop, uint32_t a, uint32_t b) const
{
    impl_.AdvanceSq(coop, a, b);
}

__simt_callee__ __forceinline__ void HcommJetty::PublishSq() const { impl_.PublishSq(); }

template <typename Coop>
__simt_callee__ __forceinline__ void HcommJetty::PublishSq(const Coop& coop) const
{
    impl_.PublishSq(coop);
}

template <int64_t kTimeout>
__simt_callee__ __forceinline__ int32_t HcommJetty::Drain() const
{
    return impl_.template Drain<kTimeout>();
}

template <int64_t kTimeout, typename Coop>
__simt_callee__ __forceinline__ int32_t HcommJetty::Drain(const Coop& coop) const
{
    return impl_.template Drain<kTimeout>(coop);
}

#endif // !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510

} // namespace AscendC::simt::jetty

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_HCOMM_JETTY_SIMT_IMPL_H
