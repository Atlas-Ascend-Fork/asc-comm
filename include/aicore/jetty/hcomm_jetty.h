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
 * \file hcomm_jetty.h
 * \brief Low-level AIV Jetty primitives for URMA point-to-point writes.
 */
#ifndef INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
#define INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H

#include "kernel_basic_intf.h"
#include "../hcomm/hcomm_common.h"

namespace AscendC {

struct alignas(8) HcommJettyPeerInfo {
    uint32_t tpId;
    uint32_t remoteTokenId;
    uint64_t remoteEid[2];
    uint64_t remoteTokenValue;
    uint64_t remoteBaseAddr;
    uint64_t remoteBufferSize;
};
static_assert(sizeof(HcommJettyPeerInfo) == 48U, "HcommJettyPeerInfo must be 48 bytes");

struct alignas(8) HcommJettyInfo {
    uint64_t sqBaseAddr;     // SQ base address in device memory.
    uint64_t sqHeadAddr;     // Address of the packed 64-bit SQ head: low 32 bits are the head in
                             // WQEBBs, high 32 bits are the expected CQE count.
    uint64_t sqTailAddr;     // Address of the 32-bit SQ completion tail published by the hardware.
    uint64_t sqDoorbellAddr; // SQ doorbell address.
    uint64_t cqBaseAddr;     // CQ base address in device memory.
    uint64_t cqTailAddr;     // Address of the 32-bit CQ tail.
    uint64_t cqDoorbellAddr; // CQ doorbell address.
    uint32_t sqHead;         // SQ producer head in WQEBBs.
    uint32_t expectedCqeCnt; // Number of CQEs expected from the submitted WQEs.
    uint32_t numWqebbBytes;  // WQEBB size in bytes.
    uint32_t numCqeBytes;    // CQE size in bytes.
    uint32_t sqDepth;        // SQ depth in WQEBBs.
    uint32_t cqDepth;        // CQ depth in CQEs.
};
static_assert(sizeof(HcommJettyInfo) == 80U, "HcommJettyInfo must be 80 bytes");

struct HcommPeer {
    __ubuf__ HcommJettyPeerInfo* peerInfo = nullptr;
    bool valid = false;

    /*!
     * @brief Resolve the remote jetty metadata of a peer into user-allocated UB memory.
     * @param [out] peerInfo: The user-allocated 48-byte peer descriptor in UB.
     * @param [in] jettyTable: The device-side jetty table published by the host.
     * @param [in] jettyIdx: The index of the jetty in the table.
     * @note Must be called after the jetty is initialized; violations trap in debug builds.
     */
    __aicore__ inline HcommPeer(__ubuf__ HcommJettyPeerInfo* peerInfo, GM_ADDR jettyTable, uint32_t jettyIdx);
};

} // namespace AscendC

// HcommJetty holds HcommJettyImpl by value, so the impl declaration must come after the public
// structs it depends on and before class HcommJetty; its definitions follow at the bottom of
// this file, like hcomm.h does.
#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H
#endif
#include "../../../impl/comm_api/aicore/jetty/impl/hcomm_jetty_impl_def.h"

namespace AscendC {

class HcommJetty {
public:
    // Number of WQEBBs occupied by a Write / WriteValue WQE.
    static constexpr uint32_t kNumWriteWqebbs = 1U;
    // Number of WQEBBs occupied by a WriteWithNotify WQE.
    static constexpr uint32_t kNumWriteWithNotifyWqebbs = 2U;
    // Size in bytes of the ubufSqe buffer that callers must allocate to hold the largest WQE.
    static constexpr uint32_t kNumMaxSqeBytes = kNumWriteWithNotifyWqebbs * 64U;

    /*!
     * @brief Bind a jetty to user-allocated UB bookkeeping and WQE staging buffers.
     * @param [out] jettyInfo: The user-allocated 80-byte jetty descriptor in UB.
     * @param [out] ubufSqe: The user-allocated WQE staging buffer in UB, at least kNumMaxSqeBytes.
     * @param [in] jettyTable: The device-side jetty table published by the host.
     * @param [in] jettyIdx: The index of the jetty in the table.
     * @note Must be called after the jetty is initialized; violations trap in debug builds.
     */
    __aicore__ inline HcommJetty(
        __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe, GM_ADDR jettyTable, uint32_t jettyIdx);

    /*!
     * @brief Enqueue a URMA write WQE on the jetty SQ without waiting for completion.
     * @tparam timeoutCycles: Cycles to wait before trapping while polling CQEs.
     * @tparam doCommit: Set false to enqueue the WQE without ringing the SQ doorbell; a later
     *                    Write<..., true> or RingDoorbell publishes the accumulated SQ entries.
     * @param [in] peer: The remote peer resolved by HcommPeer.
     * @param [out] dstAddr: The externally supplied remote device address.
     * @param [in] srcAddr: The local source device address.
     * @param [in] numBytes: The length of the data to write in bytes.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeoutCycles, bool doCommit = true>
    __aicore__ inline int32_t Write(const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes);

    /*!
     * @brief Enqueue a URMA write WQE that carries the value inline in a single WQEBB.
     * @tparam timeoutCycles: Cycles to wait before trapping while polling CQEs.
     * @tparam doCommit: Set false to enqueue the WQE without ringing the SQ doorbell; a later
     *                    Write<..., true> or RingDoorbell publishes the accumulated SQ entries.
     * @param [in] peer: The remote peer resolved by HcommPeer.
     * @param [out] dstAddr: The externally supplied remote device address.
     * @param [in] value: The value to write; T must fit after the SQE header of one WQEBB.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeoutCycles, typename T, bool doCommit = true>
    __aicore__ inline int32_t WriteValue(const HcommPeer& peer, GM_ADDR dstAddr, T value);

    /*!
     * @brief Enqueue a URMA write WQE and notify the remote side once it completes.
     * @tparam timeoutCycles: Cycles to wait before trapping while polling CQEs.
     * @param [in] peer: The remote peer resolved by HcommPeer.
     * @param [out] dstAddr: The externally supplied remote device address.
     * @param [in] srcAddr: The local source device address.
     * @param [in] numBytes: The length of the data to write in bytes.
     * @param [out] notifyAddr: The externally supplied remote notify address.
     * @param [in] notifyValue: The 64-bit value written to notifyAddr on completion.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t WriteWithNotify(
        const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes, GM_ADDR notifyAddr,
        uint64_t notifyValue);

    /*!
     * @brief Poll the CQ until all submitted WQEs complete.
     * @tparam timeoutCycles: Cycles to wait before trapping while polling CQEs.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeoutCycles>
    __aicore__ inline int32_t Drain();

    /*!
     * @brief Advance the SQ head by numWqebbs and publish it to the device-side head address.
     * @param [in] numWqebbs: The number of WQEBBs to advance.
     * @note The built-in Write path synchronizes the WQE data to device memory before advancing;
     *       external callers must ensure their WQE writes are visible before calling this.
     */
    __aicore__ inline void AdvanceSq(uint32_t numWqebbs);

    /*!
     * @brief Ring the SQ doorbell to publish all SQ entries enqueued so far.
     * @note Supports the scenario where SIMT composes WQEs while SIMD rings the doorbell: the SIMT
     *       side must complete its WQE writes before the SIMD side rings the doorbell, since the
     *       doorbell write from this core cannot order writes issued by another core.
     */
    __aicore__ inline void RingDoorbell();

private:
    HcommJettyImpl impl_;
};

} // namespace AscendC

// Must stay after class HcommJetty: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/jetty/impl/hcomm_jetty_impl.h"

#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_JETTY_H
#endif

#endif // INCLUDE_ADV_API_HCOMM_HCOMM_JETTY_H
