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
 * \file asccomm_jetty_simt.h
 * \brief Low-level SIMT Jetty primitives for URMA point-to-point writes.
 */
#ifndef INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H
#define INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

// The Jetty metadata structs are defined once in the SIMD public header and shared by
// both execution models; including it first makes them visible to the SIMT impl chain.
#include "asccomm_work_queue.h"
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_jetty_simt_impl_def.h"

namespace asc::simt {

using asccomm_peer_info = asc::asccomm_peer_entity<asc::COMM_PROTOCOL_UB_CTP>;
using asccomm_sge = jetty_sge;

/**
 * Public SIMT Jetty API. A call to write publishes by default; use
 * do_commit=false for a group of posts followed by advance_sq and publish_sq.
 */
class asccomm_jetty {
public:
    // Number of 64-bit words in one WQEBB.
    static constexpr uint32_t num_wqebb_lanes = jetty_impl::num_wqebb_lanes;
    // Number of WQEBBs in one SQE slot reservation.
    static constexpr uint32_t num_wqebbs_per_sqe_slot = jetty_impl::num_wqebbs_per_sqe_slot;
    // Maximum number of SGEs one write can carry.
    static constexpr uint32_t num_max_sges_per_write = jetty_impl::num_max_sges_per_write;
    // Number of WQEBBs occupied by a write WQE reservation.
    static constexpr uint32_t num_write_wqebbs = jetty_impl::num_write_wqebbs;
    // Number of WQEBBs occupied by a write_with_notify WQE.
    static constexpr uint32_t num_write_with_notify_wqebbs = jetty_impl::num_write_with_notify_wqebbs;
    // Size in bytes of the DWQE staging buffer callers must allocate.
    static constexpr uint32_t num_max_sqe_bytes = jetty_impl::num_max_sqe_bytes;
    // Number of WQEBBs one write spans in the SQ.
    static constexpr uint32_t num_wqebbs_per_write = jetty_impl::num_wqebbs_per_write;
    // Number of 64-bit words in the two-BB DWQE window.
    static constexpr uint32_t num_dwqe_words = jetty_impl::num_dwqe_words;

    __simt_callee__ __forceinline__ asccomm_jetty();
    __simt_callee__ __forceinline__ asccomm_jetty(
        __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, __gm__ void* table,
        uint32_t index);
    __simt_callee__ __forceinline__ asccomm_jetty(
        __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage,
        uint64_t channel_addr);

    __simt_callee__ __forceinline__ __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info() const;

    __simt_callee__ __forceinline__ void asccomm_jetty_attach(
        __ubuf__ asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP>* info, __ubuf__ uint8_t* stage, uint64_t channel);

    template <uint32_t sge_num, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_write_nbi(
        const asccomm_peer& peer, __gm__ void* dst, const jetty_sge* sges,
        const coop_group& group = coop_group{}) const;

    // Cooperative-group-first spellings, mirroring the cooperative_groups argument order.
    template <uint32_t sge_num, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_write_nbi(
        const coop_group& group, const asccomm_peer& peer, __gm__ void* dst, const asccomm_sge& sge) const;

    template <uint32_t sge_num, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_write_nbi(
        const coop_group& group, const asccomm_peer& peer, __gm__ void* dst, const asccomm_sge* sges) const;

    template <int64_t timeout_cycles, typename T, bool do_commit = true>
    __simt_callee__ __forceinline__ int32_t
    asccomm_jetty_write_value_nbi(const asccomm_peer& peer, __gm__ void* dst, T value);

    template <int64_t timeout_cycles, bool do_commit = true>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_write_with_notify_nbi(
        const asccomm_peer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notify_addr,
        uint64_t notify_value);

    __simt_callee__ __forceinline__ void asccomm_jetty_advance_sq() const;
    template <typename coop_group>
    __simt_callee__ __forceinline__ void asccomm_jetty_advance_sq(const coop_group& group) const;
    template <typename coop_group>
    __simt_callee__ __forceinline__ void asccomm_jetty_advance_sq(
        const coop_group& group, uint32_t a, uint32_t b) const;

    __simt_callee__ __forceinline__ void asccomm_jetty_publish_sq() const;
    template <typename coop_group>
    __simt_callee__ __forceinline__ void asccomm_jetty_publish_sq(const coop_group& group) const;

    template <int64_t timeout_cycles>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_drain() const;
    template <int64_t timeout_cycles, typename coop_group>
    __simt_callee__ __forceinline__ int32_t asccomm_jetty_drain(const coop_group& group) const;

private:
    jetty_impl impl;
};

} // namespace asc::simt

// Must stay after class asccomm_jetty: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/jetty/impl/asccomm_jetty_simt_impl.h"

#endif // INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H
