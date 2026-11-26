/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_WORK_QUEUE_IMPL_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_WORK_QUEUE_IMPL_H

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
#include "platform_v310/asccomm_aiv_urma_work_queue.h"
#endif

namespace asc {

namespace detail {

// Number of WQEBBs occupied by a write / write_value WQE.
constexpr uint32_t num_write_wqebbs = 1U;
// Number of WQEBBs occupied by a write_with_notify WQE.
constexpr uint32_t num_write_with_notify_wqebbs = 2U;

} // namespace detail

template <CommProtocol comm_protocol>
__aicore__ inline asccomm_work_queue<comm_protocol>::asccomm_work_queue(
    __ubuf__ work_queue* wq_buff, __ubuf__ uint8_t* wqe_buff, GM_ADDR channels, uint32_t channel_idx)
    : impl(wq_buff, wqe_buff, channels, channel_idx)
{}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue<comm_protocol>::write_nbi(
    const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes)
{
    return impl.template post_write<timeout_cycles, false, do_commit>(
        peer, dst_addr, src_addr, num_bytes, nullptr, 0U, detail::num_write_wqebbs);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, typename T, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue<comm_protocol>::write_value_nbi(
    const peer_info& peer, GM_ADDR dst_addr, T value)
{
    static_assert(
        sizeof(T) <= detail::num_write_wqebbs * 64U - sizeof(asccomm_urma_sqe_ctx),
        "write_value value is too large for one WQEBB");
    return impl.template post_write_value<timeout_cycles, T, do_commit>(
        peer, dst_addr, value, detail::num_write_wqebbs);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles, bool do_commit>
__aicore__ inline int32_t asccomm_work_queue<comm_protocol>::write_with_notify_nbi(
    const peer_info& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
    uint64_t notify_value)
{
    return impl.template post_write<timeout_cycles, true, do_commit>(
        peer, dst_addr, src_addr, num_bytes, notify_addr, notify_value, detail::num_write_with_notify_wqebbs);
}

template <CommProtocol comm_protocol>
template <int64_t timeout_cycles>
__aicore__ inline int32_t asccomm_work_queue<comm_protocol>::drain()
{
    return impl.template drain<timeout_cycles>();
}

template <CommProtocol comm_protocol>
__aicore__ inline void asccomm_work_queue<comm_protocol>::advance_sq(uint32_t num_wqebbs)
{
    impl.advance_sq(num_wqebbs);
}

template <CommProtocol comm_protocol>
__aicore__ inline void asccomm_work_queue<comm_protocol>::ring_doorbell()
{
    impl.ring_doorbell();
}

} // namespace asc

#endif
