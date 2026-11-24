/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Deprecated Jetty SIMT interfaces, kept for source compatibility of 9.2.0-era code. The
// implementation now lives in asccomm_jetty_simt.h (namespace asc::simt): the PascalCase
// structs from hcomm_jetty.h keep field access compatible, and the deprecated HcommJetty class
// wraps an asc::simt::asccomm_jetty instance and forwards every call. All symbols are
// removed after 2027/12/31; new code must use asccomm_jetty_simt.h directly.

#ifndef INCLUDE_LEGACY_JETTY_HCOMM_JETTY_SIMT_H
#define INCLUDE_LEGACY_JETTY_HCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

#include "hcomm_jetty.h"
#include "asccomm_jetty_simt.h"

namespace AscendC::simt::jetty {

using HcommPeerInfo = AscendC::HcommJettyPeerInfo;
using HcommJettyInfo = AscendC::HcommJettyInfo;
using JettySge = asc::simt::asccomm_sge;
using HcommSge = asc::simt::asccomm_sge;

// Deprecated PascalCase mirror of asc::simt::asccomm_peer. The peerInfo field points at
// the mirrored HcommJettyPeerInfo descriptor; layout is locked so the legacy classes can forward
// peer references to the snake_case implementation.
struct HcommPeer {
    __ubuf__ HcommJettyPeerInfo* peerInfo = nullptr;

    __simt_callee__ __forceinline__ HcommPeer() = default;

    /*!
     * @brief Deprecated PascalCase constructor mirroring asc::simt::asccomm_peer.
     */
    __simt_callee__ __forceinline__
    HcommPeer(__ubuf__ HcommJettyPeerInfo* peerInfo, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        *reinterpret_cast<asc::simt::asccomm_peer*>(this) = asc::simt::asccomm_peer(
            reinterpret_cast<__ubuf__ asc::simt::asccomm_peer_info*>(peerInfo), jettyTable, jettyIdx);
    }

    /*!
     * @brief Deprecated PascalCase method mirroring asc::simt::asccomm_peer::attach.
     */
    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommJettyPeerInfo* peerInfo, uint64_t channel)
    {
        reinterpret_cast<asc::simt::asccomm_peer*>(this)->attach(
            reinterpret_cast<__ubuf__ asc::simt::asccomm_peer_info*>(peerInfo), channel);
    }
};
static_assert(sizeof(HcommPeer) == sizeof(asc::simt::asccomm_peer), "mirror layout mismatch");
static_assert(offsetof(HcommPeer, peerInfo) == 0U, "mirror layout mismatch");

/*!
 * @class HcommJetty
 * @brief Deprecated PascalCase Jetty SIMT interface. Wraps an asc::simt::asccomm_jetty
 *        instance and forwards every call; removed after 2027/12/31. New code must use
 *        asccomm_jetty_simt.h directly.
 */
class ASC_DEPRECATED(9.3.0, "2027/12/31", asccomm_jetty) HcommJetty {
public:
    /*!
     * @brief Deprecated PascalCase default constructor; bind a descriptor later with Attach.
     */
    __simt_callee__ __forceinline__ HcommJetty() = default;

    /*!
     * @brief Deprecated PascalCase overloads accepting the mirrored HcommJettyInfo descriptor.
     */
    __simt_callee__ __forceinline__
    HcommJetty(__ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
        : impl(
              reinterpret_cast<__ubuf__ asc::asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>*>(jettyInfo), stage,
              table, index)
    {}
    __simt_callee__ __forceinline__
    HcommJetty(__ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* stage, uint64_t channelAddr)
        : impl(
              reinterpret_cast<__ubuf__ asc::asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>*>(jettyInfo), stage,
              channelAddr)
    {}

    // Deprecated PascalCase spellings of the snake_case class constants.
    static constexpr uint32_t kNumWQEBBLanes = asc::simt::asccomm_jetty::num_wqebb_lanes;
    static constexpr uint32_t kNumWQEBBsPerSQESlot = asc::simt::asccomm_jetty::num_wqebbs_per_sqe_slot;
    static constexpr uint32_t kNumMaxSGEsPerWrite = asc::simt::asccomm_jetty::num_max_sges_per_write;
    static constexpr uint32_t kNumWriteWqebbs = asc::simt::asccomm_jetty::num_write_wqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = asc::simt::asccomm_jetty::num_write_with_notify_wqebbs;
    static constexpr uint32_t kNumMaxSQEBytes = asc::simt::asccomm_jetty::num_max_sqe_bytes;
    static constexpr uint32_t kNumWQEBBsPerWrite = asc::simt::asccomm_jetty::num_wqebbs_per_write;
    static constexpr uint32_t kNumDwqeWords = asc::simt::asccomm_jetty::num_dwqe_words;

    /*!
     * @brief Deprecated PascalCase methods accepting the mirrored HcommPeer descriptor.
     */
    template <uint32_t sgeNum, bool doCommit = true, typename coopGroup = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const coopGroup& group = coopGroup{}) const
    {
        return impl.template asccomm_jetty_write_nbi<sgeNum, doCommit, coopGroup>(
            reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, sges, group);
    }
    template <uint32_t sgeNum, bool doCommit = true, typename coopGroup = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const coopGroup& group, const HcommPeer& peer, __gm__ void* dst, const JettySge& sge) const
    {
        return impl.template asccomm_jetty_write_nbi<sgeNum, doCommit, coopGroup>(
            group, reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, sge);
    }
    template <uint32_t sgeNum, bool doCommit = true, typename coopGroup = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Write(const coopGroup& group, const HcommPeer& peer, __gm__ void* dst, const JettySge* sges) const
    {
        return impl.template asccomm_jetty_write_nbi<sgeNum, doCommit, coopGroup>(
            group, reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, sges);
    }

    /*!
     * @brief Deprecated PascalCase methods accepting the mirrored HcommPeer descriptor.
     */
    template <uint32_t sgeNum, bool doCommit = true, typename coopGroup = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Put(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const coopGroup& group = coopGroup{}) const
    {
        return impl.template asccomm_jetty_write_nbi<sgeNum, doCommit, coopGroup>(
            reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, sges, group);
    }
    template <uint32_t sgeNum, bool doCommit = true, typename coopGroup = HcommCoopThread>
    __simt_callee__ __forceinline__ int32_t
    Put(const coopGroup& group, const HcommPeer& peer, __gm__ void* dst, const JettySge& sge) const
    {
        return impl.template asccomm_jetty_write_nbi<sgeNum, doCommit, coopGroup>(
            group, reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, sge);
    }

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommPeer descriptor.
     */
    template <int64_t timeoutCycles, typename T, bool doCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
    {
        return impl.template asccomm_jetty_write_value_nbi<timeoutCycles, T, doCommit>(
            reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, value);
    }

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommPeer descriptor.
     */
    template <int64_t timeoutCycles, bool doCommit = true>
    __simt_callee__ __forceinline__ int32_t WriteWithNotify(
        const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
        uint64_t notifyValue)
    {
        return impl.template asccomm_jetty_write_with_notify_nbi<timeoutCycles, doCommit>(
            reinterpret_cast<const asc::simt::asccomm_peer&>(peer), dst, src, len, notifyAddr, notifyValue);
    }

    /*!
     * @brief Deprecated PascalCase methods.
     */
    __simt_callee__ __forceinline__ void AdvanceSq() const { impl.asccomm_jetty_advance_sq(); }
    template <typename coopGroup>
    __simt_callee__ __forceinline__ void AdvanceSq(const coopGroup& group) const
    {
        impl.asccomm_jetty_advance_sq(group);
    }
    template <typename coopGroup>
    __simt_callee__ __forceinline__ void AdvanceSq(const coopGroup& group, uint32_t a, uint32_t b) const
    {
        impl.asccomm_jetty_advance_sq(group, a, b);
    }
    __simt_callee__ __forceinline__ void PublishSq() const { impl.asccomm_jetty_publish_sq(); }
    template <typename coopGroup>
    __simt_callee__ __forceinline__ void PublishSq(const coopGroup& group) const
    {
        impl.asccomm_jetty_publish_sq(group);
    }
    template <int64_t timeoutCycles>
    __simt_callee__ __forceinline__ int32_t Drain() const
    {
        return impl.template asccomm_jetty_drain<timeoutCycles>();
    }
    template <int64_t timeoutCycles, typename coopGroup>
    __simt_callee__ __forceinline__ int32_t Drain(const coopGroup& group) const
    {
        return impl.template asccomm_jetty_drain<timeoutCycles, coopGroup>(group);
    }

    /*!
     * @brief Deprecated PascalCase method returning the mirrored HcommJettyInfo descriptor.
     */
    __simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommJettyInfo*>(impl.info());
    }

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommJettyInfo descriptor.
     */
    __simt_callee__ __forceinline__ void Attach(
        __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channel)
    {
        impl.asccomm_jetty_attach(
            reinterpret_cast<__ubuf__ asc::asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>*>(info), stage, channel);
    }

private:
    asc::simt::asccomm_jetty impl;
};

} // namespace AscendC::simt::jetty

#endif // INCLUDE_LEGACY_JETTY_HCOMM_JETTY_SIMT_H
