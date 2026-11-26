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
 * \file asccomm_work_queue_simt.h
 * \brief Low-level SIMT work-queue primitives for URMA point-to-point writes.
 */
#ifndef INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H
#define INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H

#include <cstddef>
#include <cstdint>

// The Jetty metadata structs are defined once in the SIMD public header and shared by
// both execution models; including it first makes them visible to the SIMT impl chain.
#include "asccomm_work_queue.h"
#include "../../../impl/comm_api/aicore/asccomm_work_queue/impl/asccomm_simt_work_queue_impl_def.h"

namespace asc::simt {

// One contiguous local source segment. Multi-segment WRITE users pass an array;
// the single-segment API uses src/num_bytes directly, matching the SIMD API.
struct asccomm_sge {
    uint64_t addr = 0U;
    uint32_t len = 0U;
};

template <CommProtocol comm_protocol>
class asccomm_work_queue_impl;

template <>
struct asccomm_peer<COMM_PROTOCOL_UB_CTP> {
    static constexpr CommProtocol protocol = COMM_PROTOCOL_UB_CTP;
    using peer_entity = asccomm_peer_entity<COMM_PROTOCOL_UB_CTP>;

    __ubuf__ peer_entity* entity = nullptr;
    bool valid = false;

    __simt_callee__ __forceinline__ asccomm_peer(__ubuf__ peer_entity* entity, GM_ADDR channels, uint32_t channel_idx);
};

/**
 * Public SIMT work-queue API. A call to write publishes by default; use
 * do_commit=false for a group of posts followed by ring_doorbell.
 * @tparam comm_protocol: The communication protocol to use. UB_CTP is the default
 *                        and currently the only supported protocol.
 */
template <CommProtocol comm_protocol = COMM_PROTOCOL_UB_CTP>
class asccomm_work_queue {
public:
    static constexpr CommProtocol protocol = comm_protocol;
    using peer_info = asccomm_peer<comm_protocol>;
    using work_queue = asccomm_work_queue_info<comm_protocol>;

    __simt_callee__ __forceinline__ asccomm_work_queue(
        __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx);

    // Single contiguous source segment, matching the SIMD write API.
    template <int64_t timeout_cycles, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t write_nbi(
        const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes) const;

    // Scatter/gather write. sges points to an array of sge_num source
    // segments; the implementation maps SGE words to cooperating lanes.
    template <
        int64_t timeout_cycles, uint32_t sge_num, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t
    write_nbi(const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, const asccomm_sge* sges) const;

    template <int64_t timeout_cycles, typename T, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t
    write_value_nbi(const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, T value) const;

    template <int64_t timeout_cycles, bool do_commit = true, typename coop_group = asccomm_coop_thread>
    __simt_callee__ __forceinline__ int32_t write_with_notify_nbi(
        const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes,
        GM_ADDR notify_addr, uint64_t notify_value) const;

    template <typename coop_group>
    __simt_callee__ __forceinline__ void ring_doorbell(const coop_group& group) const;

    template <int64_t timeout_cycles, typename coop_group>
    __simt_callee__ __forceinline__ int32_t drain(const coop_group& group) const;

private:
    asccomm_work_queue_impl<comm_protocol> impl;
};

} // namespace asc::simt

// Must stay after class asccomm_work_queue: it defines the member functions declared above.
#include "../../../impl/comm_api/aicore/asccomm_work_queue/impl/asccomm_simt_work_queue_impl.h"

#endif // INCLUDE_COMM_API_AICORE_JETTY_ASCCOMM_JETTY_SIMT_H
