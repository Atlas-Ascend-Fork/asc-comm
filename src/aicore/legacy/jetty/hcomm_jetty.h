/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// Deprecated Jetty SIMD interfaces, kept for source compatibility of 9.2.0-era code. The
// implementation now lives in asccomm_jetty.h (namespace asc): the PascalCase structs below are
// field-for-field mirrors of their snake_case replacements, and the deprecated HcommJetty class
// wraps an asc::asccomm_jetty instance and forwards every call. All symbols are removed after
// 2027/12/31; new code must use asccomm_jetty.h directly.

#ifndef INCLUDE_LEGACY_JETTY_HCOMM_JETTY_H
#define INCLUDE_LEGACY_JETTY_HCOMM_JETTY_H

#include <cstddef>

#include "asccomm_jetty.h"

namespace AscendC {

// Deprecated PascalCase mirror of asc::asccomm_jetty_peer_info. Field names are kept for source
// compatibility; the layout is locked to the snake_case replacement field by field.
struct alignas(8) HcommJettyPeerInfo {
    // Packed to match the SQE wire layout so building a WQE header is a single 64-bit read:
    // tpId in bits [23:0], numSges in bits [31:24] (filled per-post, not stored here) and
    // remoteTokenId in bits [51:32].
    uint64_t tpIdNumSgesRemoteTokenId;
    uint64_t remoteEid[2];
    uint64_t remoteTokenValueUdf;
    uint64_t remoteBaseAddr;
    uint64_t remoteBufferSize;
};
static_assert(sizeof(HcommJettyPeerInfo) == sizeof(asc::asccomm_jetty_peer_info), "mirror layout mismatch");
static_assert(sizeof(HcommJettyPeerInfo) == 48U, "HcommJettyPeerInfo must be 48 bytes");
static_assert(
    offsetof(HcommJettyPeerInfo, remoteEid) == offsetof(asc::asccomm_jetty_peer_info, remote_eid),
    "unexpected peer metadata layout");
static_assert(
    offsetof(HcommJettyPeerInfo, remoteTokenValueUdf) == offsetof(asc::asccomm_jetty_peer_info, remote_token_value_udf),
    "unexpected peer metadata layout");
static_assert(
    offsetof(HcommJettyPeerInfo, remoteBaseAddr) == offsetof(asc::asccomm_jetty_peer_info, remote_base_addr),
    "unexpected peer metadata layout");
static_assert(
    offsetof(HcommJettyPeerInfo, remoteBufferSize) == offsetof(asc::asccomm_jetty_peer_info, remote_buffer_size),
    "unexpected peer metadata layout");

// Deprecated PascalCase mirror of asc::asccomm_jetty_info.
struct alignas(8) HcommJettyInfo {
    uint64_t sqBaseAddr;         // SQ base address in device memory.
    uint64_t sqHeadAddr;         // Address of the packed 64-bit SQ head in GM.
    uint64_t completionTailAddr; // Address of the 32-bit SQ completion tail published by the hardware.
    uint64_t sqDoorbellAddr;     // SQ doorbell address.
    uint64_t cqBaseAddr;         // CQ base address in device memory.
    uint64_t cqTailAddr;         // Address of the 32-bit CQ tail.
    uint64_t cqDoorbellAddr;     // CQ doorbell address.
    uint64_t packedHead;         // Producer state mirroring the GM SQ head: low 32 bits are the head
                                 // in WQEBBs, high 32 bits the expected CQE count. Kept packed so
                                 // publishing is a single 64-bit store.
    uint32_t numWqebbBytes;      // WQEBB size in bytes.
    uint32_t numCqeBytes;        // CQE size in bytes.
    uint32_t sqDepth;            // SQ depth in WQEBBs.
    uint32_t cqDepth;            // CQ depth in CQEs.
};
static_assert(sizeof(HcommJettyInfo) == sizeof(asc::asccomm_jetty_info), "mirror layout mismatch");
static_assert(sizeof(HcommJettyInfo) == 80U, "HcommJettyInfo must be 80 bytes");
static_assert(
    offsetof(HcommJettyInfo, packedHead) == offsetof(asc::asccomm_jetty_info, packed_head),
    "SIMT/SIMD packed head offset mismatch");
static_assert(
    offsetof(HcommJettyInfo, numWqebbBytes) == offsetof(asc::asccomm_jetty_info, num_wqebb_bytes),
    "HcommJettyInfo scalar fields offset mismatch");

// Deprecated PascalCase mirror of asc::asccomm_peer. The peerInfo field points at the mirror
// descriptor above; layout is locked so the legacy class can forward peer references to the
// snake_case implementation.
struct HcommPeer {
    __ubuf__ HcommJettyPeerInfo* peerInfo = nullptr;
    bool valid = false;

    /*!
     * @brief Deprecated PascalCase constructor mirroring asc::asccomm_peer.
     */
    __aicore__ inline HcommPeer(__ubuf__ HcommJettyPeerInfo* peerInfo, GM_ADDR jettyTable, uint32_t jettyIdx)
    {
        *reinterpret_cast<asc::asccomm_peer*>(this) =
            asc::asccomm_peer(reinterpret_cast<__ubuf__ asc::asccomm_jetty_peer_info*>(peerInfo), jettyTable, jettyIdx);
    }
};
static_assert(sizeof(HcommPeer) == sizeof(asc::asccomm_peer), "mirror layout mismatch");
static_assert(offsetof(HcommPeer, peerInfo) == offsetof(asc::asccomm_peer, peer_info), "mirror layout mismatch");

/*!
 * @class HcommJetty
 * @brief Deprecated PascalCase Jetty SIMD interface. Wraps an asc::asccomm_jetty instance and
 *        forwards every call; removed after 2027/12/31. New code must use asccomm_jetty.h
 *        (namespace asc) directly.
 * @note HcommJetty requires CANN 9.2.0 or later. CANN 9.2.0 beta releases are not supported.
 */
class ASC_DEPRECATED(9.3.0, "2027/12/31", asccomm_jetty) HcommJetty {
public:
    /*!
     * @brief Deprecated PascalCase overload accepting the mirrored HcommJettyInfo descriptor.
     */
    __aicore__ inline HcommJetty(
        __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe, GM_ADDR jettyTable, uint32_t jettyIdx)
        : impl(reinterpret_cast<__ubuf__ asc::asccomm_jetty_info*>(jettyInfo), ubufSqe, jettyTable, jettyIdx)
    {}

    // Deprecated PascalCase spellings of the snake_case class constants.
    static constexpr uint32_t kNumWriteWqebbs = asc::asccomm_jetty::num_write_wqebbs;
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = asc::asccomm_jetty::num_write_with_notify_wqebbs;
    static constexpr uint32_t kNumMaxSqeBytes = asc::asccomm_jetty::num_max_sqe_bytes;

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommPeer descriptor.
     */
    template <int64_t timeoutCycles, bool doCommit = true>
    __aicore__ inline int32_t Write(const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes)
    {
        return impl.template asccomm_jetty_write_nbi<timeoutCycles, doCommit>(
            reinterpret_cast<const asc::asccomm_peer&>(peer), dstAddr, srcAddr, numBytes);
    }

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommPeer descriptor.
     */
    template <int64_t timeoutCycles, typename T, bool doCommit = true>
    __aicore__ inline int32_t WriteValue(const HcommPeer& peer, GM_ADDR dstAddr, T value)
    {
        return impl.template asccomm_jetty_write_value_nbi<timeoutCycles, T, doCommit>(
            reinterpret_cast<const asc::asccomm_peer&>(peer), dstAddr, value);
    }

    /*!
     * @brief Deprecated PascalCase method accepting the mirrored HcommPeer descriptor.
     */
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t WriteWithNotify(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes, GM_ADDR notifyAddr,
        uint64_t notifyValue)
    {
        return impl.template asccomm_jetty_write_with_notify_nbi<timeoutCycles>(
            reinterpret_cast<const asc::asccomm_peer&>(peer), dstAddr, srcAddr, numBytes, notifyAddr, notifyValue);
    }

    /*!
     * @brief Deprecated PascalCase method.
     */
    __aicore__ inline void AdvanceSq(uint32_t numWqebbs) { impl.asccomm_jetty_advance_sq(numWqebbs); }

    /*!
     * @brief Deprecated PascalCase method.
     */
    __aicore__ inline void RingDoorbell() { impl.asccomm_jetty_ring_doorbell(); }

    /*!
     * @brief Deprecated PascalCase method.
     */
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t Drain()
    {
        return impl.asccomm_jetty_drain<timeoutCycles>();
    }

private:
    asc::asccomm_jetty impl;
};

} // namespace AscendC

#endif // INCLUDE_LEGACY_JETTY_HCOMM_JETTY_H
