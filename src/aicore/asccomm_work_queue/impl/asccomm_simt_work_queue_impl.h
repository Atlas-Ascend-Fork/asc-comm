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
 * \file asccomm_simt_work_queue_impl.h
 * \brief Definitions of the public SIMT work-queue member functions.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_SIMT_WORK_QUEUE_IMPL_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_SIMT_WORK_QUEUE_IMPL_H

#include "asccomm_simt_work_queue_impl_def.h"

// SIMT Jetty is only available on v310. The ASC driver compiles each translation unit twice:
// __NPU_ARCH__ is 3510, 9201, or 9202 in the device pass and undefined in the host pass. The public asccomm_work_queue
// declares its members out-of-line, so definitions are only emitted on platforms that have the
// implementation; other platforms fail at link time if the API is used.
#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202
#include "platform_v310/asccomm_simt_urma_work_queue.h"
#endif

namespace asc::simt {

#if !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202

template <CommProtocol comm_protocol>
__simt_callee__ __forceinline__ asccomm_work_queue<comm_protocol>::asccomm_work_queue(
    __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx)
    : impl(wq_buff, wqe_buff, channels, channel_idx)
{}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, uint32_t sge_num, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_work_queue<comm_protocol>::write_nbi(
    const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, const asccomm_sge* sges) const
{
    return impl.template write_nbi<timeout_cycles, sge_num, do_commit>(group, peer, dst_addr, sges);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_work_queue<comm_protocol>::write_nbi(
    const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes) const
{
    return impl.template write_nbi<timeout_cycles, do_commit>(group, peer, dst_addr, src_addr, num_bytes);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, typename T, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_work_queue<comm_protocol>::write_value_nbi(
    const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, T value) const
{
    return impl.template write_value_nbi<timeout_cycles, T, do_commit>(group, peer, dst_addr, value);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, bool do_commit, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_work_queue<comm_protocol>::write_with_notify_nbi(
    const coop_group& group, const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes,
    GM_ADDR notify_addr, uint64_t notify_value) const
{
    return impl.template write_with_notify_nbi<timeout_cycles, do_commit>(
        group, peer, dst_addr, src_addr, num_bytes, notify_addr, notify_value);
}

template <CommProtocol comm_protocol>
template <typename coop_group>
__simt_callee__ __forceinline__ void asccomm_work_queue<comm_protocol>::ring_doorbell(const coop_group& group) const
{
    impl.ring_doorbell(group);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, typename coop_group>
__simt_callee__ __forceinline__ int32_t asccomm_work_queue<comm_protocol>::drain(const coop_group& group) const
{
    return impl.template drain<timeout_cycles>(group);
}

#endif // !defined(__NPU_ARCH__) || __NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202

} // namespace asc::simt

#endif // IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_SIMT_WORK_QUEUE_IMPL_H
