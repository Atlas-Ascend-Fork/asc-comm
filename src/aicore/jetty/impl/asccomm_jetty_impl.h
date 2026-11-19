/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_IMPL_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_ASCCOMM_JETTY_IMPL_H

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 3510 || __NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
#include "platform_v310/asccomm_aiv_urma_jetty.h"
#endif

namespace asc {

__aicore__ inline asccomm_jetty::asccomm_jetty(
    __ubuf__ asccomm_jetty_info* jetty_info, __ubuf__ uint8_t* ubuf_sqe, GM_ADDR jetty_table, uint32_t jetty_idx)
    : impl(jetty_info, ubuf_sqe, jetty_table, jetty_idx)
{}

template <int64_t timeout_cycles, bool do_commit>
__aicore__ inline int32_t asccomm_jetty::asccomm_jetty_write_nbi(
    const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes)
{
    return impl.post_write<timeout_cycles, false, do_commit>(
        peer, dst_addr, src_addr, num_bytes, nullptr, 0U, num_write_wqebbs);
}

template <int64_t timeout_cycles, typename T, bool do_commit>
__aicore__ inline int32_t asccomm_jetty::asccomm_jetty_write_value_nbi(
    const asccomm_peer& peer, GM_ADDR dst_addr, T value)
{
    static_assert(
        sizeof(T) <= num_write_wqebbs * 64U - sizeof(asccomm_urma_sqe_ctx),
        "write_value value is too large for one WQEBB");
    return impl.post_write_value<timeout_cycles, T, do_commit>(peer, dst_addr, value, num_write_wqebbs);
}

template <int64_t timeout_cycles>
__aicore__ inline int32_t asccomm_jetty::asccomm_jetty_write_with_notify_nbi(
    const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
    uint64_t notify_value)
{
    return impl.post_write<timeout_cycles, true, true>(
        peer, dst_addr, src_addr, num_bytes, notify_addr, notify_value, num_write_with_notify_wqebbs);
}

template <int64_t timeout_cycles>
__aicore__ inline int32_t asccomm_jetty::asccomm_jetty_drain()
{
    return impl.asccomm_jetty_drain<timeout_cycles>();
}

__aicore__ inline void asccomm_jetty::asccomm_jetty_advance_sq(uint32_t num_wqebbs)
{
    impl.asccomm_jetty_advance_sq(num_wqebbs);
}

__aicore__ inline void asccomm_jetty::asccomm_jetty_ring_doorbell() { impl.asccomm_jetty_ring_doorbell(); }

} // namespace asc

#endif
