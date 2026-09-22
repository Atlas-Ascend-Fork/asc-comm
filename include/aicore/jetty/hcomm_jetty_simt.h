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
 * \file hcomm_jetty_simt.h
 * \brief Low-level SIMT Jetty primitives for URMA point-to-point writes.
 */
#ifndef INCLUDE_ADV_API_JETTY_HCOMM_JETTY_SIMT_H
#define INCLUDE_ADV_API_JETTY_HCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

// The Jetty metadata structs are defined once in the SIMD public header and shared by
// both execution models; including it first makes them visible to the SIMT impl chain.
#include "hcomm_jetty.h"
#include "../../../impl/comm_api/aicore/jetty/impl/hcomm_jetty_simt_impl_def.h"

namespace AscendC::simt::jetty {

using HcommPeerInfo = AscendC::HcommJettyPeerInfo;
using HcommSge = JettySge;

/**
 * Public SIMT Jetty API. A call to Write publishes by default; use
 * kDoCommit=false for a group of posts followed by AdvanceSq and PublishSq.
 */
class HcommJetty {
public:
    // Number of 64-bit words in one WQEBB.
    static constexpr uint32_t kNumWQEBBLanes = JettyImpl::kNumWQEBBLanes;
    // Number of WQEBBs in one SQE slot reservation.
    static constexpr uint32_t kNumWQEBBsPerSQESlot = JettyImpl::kNumWQEBBsPerSQESlot;
    // Maximum number of SGEs one Write can carry.
    static constexpr uint32_t kNumMaxSGEsPerWrite = JettyImpl::kNumMaxSGEsPerWrite;
    // Number of WQEBBs occupied by a Write WQE reservation.
    static constexpr uint32_t kNumWriteWqebbs = JettyImpl::kNumWriteWqebbs;
    // Number of WQEBBs occupied by a WriteWithNotify WQE.
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = JettyImpl::kNumWriteWithNotifyWqebbs;
    // Size in bytes of the DWQE staging buffer callers must allocate.
    static constexpr uint32_t kNumMaxSQEBytes = JettyImpl::kNumMaxSQEBytes;
    // Number of WQEBBs one Write spans in the SQ.
    static constexpr uint32_t kNumWQEBBsPerWrite = JettyImpl::kNumWQEBBsPerWrite;
    // Number of 64-bit words in the two-BB DWQE window.
    static constexpr uint32_t kNumDwqeWords = JettyImpl::kNumDwqeWords;

    __simt_callee__ __forceinline__ HcommJetty();
    __simt_callee__ __forceinline__
    HcommJetty(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index);
    __simt_callee__ __forceinline__
    HcommJetty(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr);

    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const;

    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channel);

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop = Coop{}) const;

    // Cooperative-group-first spellings, mirroring the cooperative_groups argument order.
    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const;

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges) const;

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Put(const HcommPeer& peer, __gm__ void* dst, const HcommSge* sges, const Coop& coop = Coop{}) const;

    template <uint32_t kSgeNum, bool kDoCommit = true, typename Coop = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Put(const Coop& coop, const HcommPeer& peer, __gm__ void* dst, const HcommSge& sge) const;

    template <int64_t kTimeout, typename T, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value);

    template <int64_t kTimeout, bool kDoCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyValue);

    __simt_callee__ __forceinline__ void AdvanceSq() const;
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop) const;
    template <typename Coop>
    __simt_callee__ __forceinline__ void AdvanceSq(const Coop& coop, uint32_t a, uint32_t b) const;

    __simt_callee__ __forceinline__ void PublishSq() const;
    template <typename Coop>
    __simt_callee__ __forceinline__ void PublishSq(const Coop& coop) const;

    template <int64_t kTimeout>
    __simt_callee__ __forceinline__ int32_t Drain() const;
    template <int64_t kTimeout, typename Coop>
    __simt_callee__ __forceinline__ int32_t Drain(const Coop& coop) const;

private:
    JettyImpl impl_;
};

} // namespace AscendC::simt::jetty

// Must stay after class HcommJetty: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/jetty/impl/hcomm_jetty_simt_impl.h"

#endif // INCLUDE_ADV_API_JETTY_HCOMM_JETTY_SIMT_H
