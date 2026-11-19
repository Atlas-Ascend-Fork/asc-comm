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
 * \file asccomm_jetty.h
 * \brief Low-level AIV Jetty primitives for URMA point-to-point writes.
 * \note asccomm_peer and asccomm_jetty require CANN 9.2.0 or later.
 *       CANN 9.2.0 beta releases are not supported. SIMT Jetty requires CANN 9.3.0 or later.
 */
#ifndef INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_H
#define INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_H

#include <cstddef>

#include "kernel_basic_intf.h"
#include "../asccomm/asccomm_common.h"

namespace asc {

// Shared Jetty metadata. Both execution models describe the same hardware queues and
// exchange these descriptors through UB, so the layout is defined once here and the
// SIMT implementation reuses it through asccomm_jetty_simt.h.
struct alignas(8) asccomm_jetty_peer_info {
    // Packed to match the SQE wire layout so building a WQE header is a single 64-bit read:
    // tpId in bits [23:0], num_sges in bits [31:24] (filled per-post, not stored here) and
    // remoteTokenId in bits [51:32].
    uint64_t tp_id_num_sges_remote_token_id;
    uint64_t remote_eid[2];
    uint64_t remote_token_value_udf;
    uint64_t remote_base_addr;
    uint64_t remote_buffer_size;
};
static_assert(sizeof(asccomm_jetty_peer_info) == 48U, "asccomm_jetty_peer_info must be 48 bytes");
static_assert(offsetof(asccomm_jetty_peer_info, remote_eid) == sizeof(uint64_t), "unexpected peer metadata layout");
static_assert(
    offsetof(asccomm_jetty_peer_info, remote_token_value_udf) == 3U * sizeof(uint64_t),
    "unexpected peer metadata layout");

struct alignas(8) asccomm_jetty_info {
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
static_assert(sizeof(asccomm_jetty_info) == 80U, "asccomm_jetty_info must be 80 bytes");
static_assert(offsetof(asccomm_jetty_info, packed_head) == 56U, "SIMT/SIMD packed head offset mismatch");
static_assert(offsetof(asccomm_jetty_info, num_wqebb_bytes) == 64U, "asccomm_jetty_info scalar fields offset mismatch");

struct asccomm_peer {
    __ubuf__ asccomm_jetty_peer_info* peer_info = nullptr;
    bool valid = false;

    /*!
     * @brief Resolve the remote jetty metadata of a peer into user-allocated UB memory.
     * @param [out] peer_info: The user-allocated 48-byte peer descriptor in UB.
     * @param [in] jetty_table: The device-side jetty table published by the host.
     * @param [in] jetty_idx: The index of the jetty in the table.
     * @note Must be called after the jetty is initialized; violations trap in debug builds.
     */
    __aicore__ inline asccomm_peer(
        __ubuf__ asccomm_jetty_peer_info* peer_info, GM_ADDR jetty_table, uint32_t jetty_idx);
};

// Compatibility aliases for the pre-rename spellings.

} // namespace asc

// asccomm_jetty holds asccomm_jetty_impl by value, so the impl declaration must come after the public
// structs it depends on and before class asccomm_jetty; its definitions follow at the bottom of
// this file, like hcomm.h does.
#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_JETTY_H
#endif
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_jetty_impl_def.h"

namespace asc {

class asccomm_jetty {
public:
    // Number of WQEBBs occupied by a write / write_value WQE.
    static constexpr uint32_t num_write_wqebbs = 1U;
    // Number of WQEBBs occupied by a write_with_notify WQE.
    static constexpr uint32_t num_write_with_notify_wqebbs = 2U;
    // Size in bytes of the ubuf_sqe buffer that callers must allocate to hold the largest WQE.
    static constexpr uint32_t num_max_sqe_bytes = num_write_with_notify_wqebbs * 64U;

    /*!
     * @brief Bind a jetty to user-allocated UB bookkeeping and WQE staging buffers.
     * @param [out] jetty_info: The user-allocated 80-byte jetty descriptor in UB.
     * @param [out] ubuf_sqe: The user-allocated WQE staging buffer in UB, at least num_max_sqe_bytes.
     * @param [in] jetty_table: The device-side jetty table published by the host.
     * @param [in] jetty_idx: The index of the jetty in the table.
     * @note Must be called after the jetty is initialized; violations trap in debug builds.
     */
    __aicore__ inline asccomm_jetty(
        __ubuf__ asccomm_jetty_info* jetty_info, __ubuf__ uint8_t* ubuf_sqe, GM_ADDR jetty_table, uint32_t jetty_idx);

    /*!
     * @brief Enqueue a URMA write WQE on the jetty SQ without waiting for completion.
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
    __aicore__ inline int32_t asccomm_jetty_write_nbi(
        const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes);

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
    __aicore__ inline int32_t asccomm_jetty_write_value_nbi(const asccomm_peer& peer, GM_ADDR dst_addr, T value);

    /*!
     * @brief Enqueue a URMA write WQE and notify the remote side once it completes.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @param [in] peer: The remote peer resolved by asccomm_peer.
     * @param [out] dst_addr: The externally supplied remote device address.
     * @param [in] src_addr: The local source device address.
     * @param [in] num_bytes: The length of the data to write in bytes.
     * @param [out] notify_addr: The externally supplied remote notify address.
     * @param [in] notify_value: The 64-bit value written to notify_addr on completion.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles>
    __aicore__ inline int32_t asccomm_jetty_write_with_notify_nbi(
        const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
        uint64_t notify_value);

    /*!
     * @brief Poll the CQ until all submitted WQEs complete.
     * @tparam timeout_cycles: Cycles to wait before trapping while polling CQEs.
     * @return 0 indicates success and -1 indicates failure.
     */
    template <int64_t timeout_cycles>
    __aicore__ inline int32_t asccomm_jetty_drain();

    /*!
     * @brief Advance the SQ head by num_wqebbs and publish it to the device-side head address.
     * @param [in] num_wqebbs: The number of WQEBBs to advance.
     * @note The built-in write path synchronizes the WQE data to device memory before advancing;
     *       external callers must ensure their WQE writes are visible before calling this.
     */
    __aicore__ inline void asccomm_jetty_advance_sq(uint32_t num_wqebbs);

    /*!
     * @brief Ring the SQ doorbell to publish all SQ entries enqueued so far.
     * @note Supports the scenario where SIMT composes WQEs while SIMD rings the doorbell: the SIMT
     *       side must complete its WQE writes before the SIMD side rings the doorbell, since the
     *       doorbell write from this core cannot order writes issued by another core.
     */
    __aicore__ inline void asccomm_jetty_ring_doorbell();

private:
    asccomm_jetty_impl impl;
};

} // namespace asc

// Must stay after class asccomm_jetty: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_jetty_impl.h"

#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_JETTY_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_JETTY_H
#endif

#endif // INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_H
