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
 * \file asccomm_work_queue.h
 * \brief Low-level AIV work-queue primitives for point-to-point writes.
 * \note asccomm_peer and asccomm_work_queue require CANN 9.2.0 or later.
 *       CANN 9.2.0 beta releases are not supported. SIMT Jetty requires CANN 9.3.0 or later.
 */
#ifndef INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_WORK_QUEUE_H
#define INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_WORK_QUEUE_H

#include <cstddef>

#include "kernel_basic_intf.h"
#include "../asccomm/asccomm_common.h"

namespace asc {

// Protocol-specific peer metadata. The primary templates are intentionally undefined so an
// unsupported protocol fails at compile time. UB_CTP currently describes the remote Jetty peer.
template <CommProtocol comm_protocol>
struct asccomm_peer_entity;

template <>
struct alignas(8) asccomm_peer_entity<COMM_PROTOCOL_UB_CTP> {
    // Packed to match the SQE wire layout so building a WQE header is a single 64-bit read:
    // tpId in bits [23:0], num_sges in bits [31:24] (filled per-post, not stored here) and
    // remoteTokenId in bits [51:32].
    uint64_t tp_id_num_sges_remote_token_id;
    uint64_t remote_eid[2];
    uint64_t remote_token_value_udf;
    uint64_t remote_base_addr;
    uint64_t remote_buffer_size;
};
static_assert(sizeof(asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>) == 48U, "UB_CTP peer entity must be 48 bytes");
static_assert(
    offsetof(asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>, remote_eid) == sizeof(uint64_t),
    "unexpected UB_CTP peer entity layout");
static_assert(
    offsetof(asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>, remote_token_value_udf) == 3U * sizeof(uint64_t),
    "unexpected UB_CTP peer entity layout");

template <CommProtocol comm_protocol>
struct asccomm_work_queue_info;

template <>
struct alignas(8) asccomm_work_queue_info<COMM_PROTOCOL_UB_CTP> {
    uint64_t sq_base_addr;         // SQ base address in device memory.
    uint64_t sq_head_addr;         // Address of the packed 64-bit SQ head in GM.
    uint64_t completion_tail_addr; // Address of the 32-bit SQ completion tail published by the hardware.
    uint64_t sq_doorbell_addr;     // SQ doorbell address.
    uint64_t cq_base_addr;         // CQ base address in device memory.
    uint64_t cq_tail_addr;         // Address of the 32-bit CQ tail.
    uint64_t cq_doorbell_addr;     // CQ doorbell address.
    uint64_t packed_head;          // Producer state mirroring the GM SQ head: low 32 bits are the head
                                   // in WQEBBs, high 32 bits the expected CQE count. Kept packed so
                                   // publishing is a single 64-bit store.
    uint32_t num_wqebb_bytes;      // WQEBB size in bytes.
    uint32_t num_cqe_bytes;        // CQE size in bytes.
    uint32_t sq_depth;             // SQ depth in WQEBBs.
    uint32_t cq_depth;             // CQ depth in CQEs.
};
static_assert(sizeof(asccomm_work_queue_info<COMM_PROTOCOL_UB_CTP>) == 80U, "UB_CTP work-queue info must be 80 bytes");
static_assert(
    offsetof(asccomm_work_queue_info<COMM_PROTOCOL_UB_CTP>, packed_head) == 56U,
    "SIMT/SIMD packed head offset mismatch");
static_assert(
    offsetof(asccomm_work_queue_info<COMM_PROTOCOL_UB_CTP>, num_wqebb_bytes) == 64U,
    "UB_CTP work-queue scalar fields offset mismatch");

template <CommProtocol comm_protocol>
struct asccomm_peer;

template <>
struct asccomm_peer<COMM_PROTOCOL_UB_CTP> {
    static constexpr CommProtocol protocol = COMM_PROTOCOL_UB_CTP;
    using peer_entity = asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>;

    __ubuf__ peer_entity* entity = nullptr;
    bool valid = false;

    /*!
     * @brief Resolve the remote jetty metadata of a peer into user-allocated UB memory.
     * @param [out] entity: The user-allocated 48-byte UB_CTP peer descriptor in UB.
     * @param [in] channels: The device-side channel table published by the host.
     * @param [in] channel_idx: The index of the channel in the table.
     * @note Must be called after the jetty is initialized; violations trap in debug builds.
     */
    __aicore__ inline asccomm_peer(__ubuf__ peer_entity* entity, GM_ADDR channels, uint32_t channel_idx);
};

// Compatibility aliases for the pre-rename spellings.

} // namespace asc

// asccomm_work_queue holds its protocol impl by value, so the impl declaration must come after
// the public structs it depends on and before class asccomm_work_queue; its definitions follow below.
#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_WORK_QUEUE_H
#endif
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_work_queue_impl_def.h"

namespace asc {

template <CommProtocol comm_protocol = COMM_PROTOCOL_UB_CTP>
class asccomm_work_queue {
public:
    static constexpr CommProtocol protocol = comm_protocol;
    using peer_info = asccomm_peer<comm_protocol>;
    using work_queue = asccomm_work_queue_info<comm_protocol>;

    /*!
     * @brief Bind a work queue to user-allocated UB bookkeeping and WQE staging buffers.
     * @param [out] wq_buff: The user-allocated 80-byte work-queue descriptor in UB.
     * @param [out] wqe_buff: The user-allocated WQE staging buffer in UB, at least 128 bytes.
     * @param [in] channels: The device-side jetty table published by the host.
     * @param [in] channel_idx: The index of the channel in the table.
     * @note Must be called after the work queue is initialized; violations trap in debug builds.
     */
    __aicore__ inline asccomm_work_queue(
        __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx);

    /*!
     * @brief Enqueue a write WQE on the work-queue SQ without waiting for completion.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @tparam do_commit: Set false to enqueue the WQE without ringing the SQ doorbell; a later
     *                    write<..., true> or ring_doorbell publishes the accumulated SQ entries.
     * @param [in] peer: The remote peer resolved by asccomm_peer.
     * @param [out] dst_addr: The externally supplied remote device address.
     * @param [in] src_addr: The local source device address.
     * @param [in] num_bytes: The length of the data to write in bytes.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles, bool do_commit = true>
    __aicore__ inline int32_t write_nbi(const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes);

    /*!
     * @brief Enqueue a URMA write WQE that carries the value inline in a single WQEBB.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @tparam do_commit: Set false to enqueue the WQE without ringing the SQ doorbell; a later
     *                    write<..., true> or ring_doorbell publishes the accumulated SQ entries.
     * @param [in] peer: The remote peer resolved by asccomm_peer.
     * @param [out] dst_addr: The externally supplied remote device address.
     * @param [in] value: The value to write; T must fit after the SQE header of one WQEBB.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles, typename T, bool do_commit = true>
    __aicore__ inline int32_t write_value_nbi(const peer_info& peer, GM_ADDR dst_addr, T value);

    /*!
     * @brief Enqueue a URMA write WQE and notify the remote side once it completes.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @tparam do_commit: Set false to enqueue the WQE without ringing the SQ doorbell; a later
     *                    write<..., true>, write_with_notify_nbi<..., true> or ring_doorbell
     *                    publishes the accumulated SQ entries.
     * @param [in] peer: The remote peer resolved by asccomm_peer.
     * @param [out] dst_addr: The externally supplied remote device address.
     * @param [in] src_addr: The local source device address.
     * @param [in] num_bytes: The length of the data to write in bytes.
     * @param [out] notify_addr: The externally supplied remote notify address.
     * @param [in] notify_value: The 64-bit value written to notify_addr on completion.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles, bool do_commit = true>
    __aicore__ inline int32_t write_with_notify_nbi(
        const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
        uint64_t notify_value);

    /*!
     * @brief Poll the CQ until all submitted WQEs complete.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles>
    __aicore__ inline int32_t drain();

    /*!
     * @brief Advance the SQ head by num_wqebbs and publish it to the device-side head address.
     * @param [in] num_wqebbs: The number of WQEBBs to advance.
     * @note The built-in write path synchronizes the WQE data to device memory before advancing;
     *       external callers must ensure their WQE writes are visible before calling this.
     */
    __aicore__ inline void advance_sq(uint32_t num_wqebbs);

    /*!
     * @brief Ring the SQ doorbell to publish all SQ entries enqueued so far.
     * @note Supports the scenario where SIMT composes WQEs while SIMD rings the doorbell: the SIMT
     *       side must complete its WQE writes before the SIMD side rings the doorbell, since the
     *       doorbell write from this core cannot order writes issued by another core.
     */
    __aicore__ inline void ring_doorbell();

private:
    asccomm_work_queue_impl<comm_protocol> impl;
};

} // namespace asc

// Must stay after class asccomm_work_queue: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_work_queue_impl.h"

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_WORK_QUEUE_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_WORK_QUEUE_H
#endif

#endif // INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_WORK_QUEUE_H
