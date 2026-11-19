/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_JETTY_DEF_H
#define IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_JETTY_DEF_H

namespace asc {

class asccomm_jetty_impl {
public:
    __aicore__ inline asccomm_jetty_impl(
        __ubuf__ asccomm_jetty_info* jetty_info, __ubuf__ uint8_t* ubuf_sqe, GM_ADDR jetty_table, uint32_t jetty_idx);

    template <int64_t timeout_cycles, bool with_notify, bool do_commit>
    __aicore__ inline int32_t post_write(
        const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
        uint64_t notify_value, uint32_t num_wqebbs);

    template <int64_t timeout_cycles, typename T, bool do_commit>
    __aicore__ inline int32_t post_write_value(
        const asccomm_peer& peer, GM_ADDR dst_addr, T value, uint32_t num_wqebbs);

    template <int64_t timeout_cycles>
    __aicore__ inline int32_t asccomm_jetty_drain();

    __aicore__ inline void asccomm_jetty_advance_sq(uint32_t num_wqebbs);

    __aicore__ inline void asccomm_jetty_ring_doorbell();

private:
    __aicore__ inline bool ready();

    // Submit the WQE already staged in ubuf_sqe: poll for SQ space, copy, advance and ring the doorbell.
    template <int64_t timeout_cycles, bool do_commit>
    __aicore__ inline int32_t post_sqe(uint32_t num_wqebbs);

    template <bool with_notify>
    __aicore__ inline void build_write_sqe(
        const asccomm_peer& peer, GM_ADDR dst_addr, GM_ADDR src_addr, uint64_t num_bytes, GM_ADDR notify_addr,
        uint64_t notify_value);

    template <typename T>
    __aicore__ inline void build_write_value_sqe(const asccomm_peer& peer, GM_ADDR dst_addr, T value);

    template <asccomm_urma_op_code op_code, auto const& config>
    __aicore__ inline void fill_write_sqe_header(
        const asccomm_peer& peer, GM_ADDR dst_addr, uint32_t inline_msg_len, uint32_t sge_num);

    __aicore__ inline void copy_wqe_to_sq(uint32_t current_head, uint32_t num_wqebbs);

    template <int64_t timeout_cycles>
    __aicore__ inline int32_t poll_cq(uint32_t expected_tail);

    template <int64_t timeout_cycles>
    __aicore__ inline void poll_cq_when_sq_overflow();

    __ubuf__ asccomm_jetty_info* jetty_info = nullptr;
    __ubuf__ uint8_t* ubuf_sqe = nullptr;
    bool valid = false;
};

} // namespace asc

#endif // IMPL_COMM_API_AICORE_JETTY_IMPL_PLATFORM_V310_ASCCOMM_AIV_URMA_JETTY_DEF_H
