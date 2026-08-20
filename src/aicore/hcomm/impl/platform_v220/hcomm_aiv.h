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
 * \file asccomm_aiv.h
 * \brief asccomm AIV implementation for V220
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_H

#include "hcomm_aiv_def.h"
#include "../../common/hcomm_utils.h"
#include "../../common/hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline asccomm_impl<comm_protocol_roce>::asccomm_impl()
{
    TBuf<TPosition::VECOUT> rdma_in_buf;
    GetTPipePtr()->InitBuffer(rdma_in_buf, ONE_BLK_SIZE);
    ub_local_ = rdma_in_buf.Get<uint64_t>();

    TBuf<TPosition::VECOUT> rdma_in_buf2;
    GetTPipePtr()->InitBuffer(rdma_in_buf2, ONE_BLK_SIZE);
    ub_local_head_ = rdma_in_buf2.Get<uint32_t>();
}

__aicore__ inline asccomm_impl<comm_protocol_roce>::~asccomm_impl() {}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    KERNEL_LOG(KERNEL_INFO, "hcomm Write channel:%llu, dst:%p, src:%p, len:%llu", channel, dst, src, len);
    post_send(channel, dst, src, len, false);
    KERNEL_LOG(KERNEL_INFO, "hcomm Write complete");
    return asccomm_success;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::read_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    KERNEL_LOG(KERNEL_INFO, "hcomm Read channel:%llu, dst:%p, src:%p, len:%llu", channel, dst, src, len);
    post_send(channel, dst, src, len, true);
    KERNEL_LOG(KERNEL_INFO, "hcomm Read complete");
    return asccomm_success;
}

template <typename t, bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_value_nbi(channel_handle channel, GM_ADDR dst, t value)
{
    (void)commit;
    (void)commit_pipe;
    (void)req_pipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)value;
    KERNEL_LOG(KERNEL_ERROR, "hcomm ROCE write_value_nbi is not supported.");
    return asccomm_failed;
}

template <bool commit, pipe_t commit_pipe, pipe_t req_pipe, auto const& config>
__aicore__ inline int32_t asccomm_impl<comm_protocol_roce>::write_with_notify_nbi(
    channel_handle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notify_addr, uint64_t notify_val)
{
    (void)commit;
    (void)commit_pipe;
    (void)req_pipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)src;
    (void)len;
    (void)notify_addr;
    (void)notify_val;
    KERNEL_LOG(KERNEL_ERROR, "hcomm ROCE write_with_notify_nbi is not supported");
    return asccomm_failed;
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::door_bell(__gm__ channel_entity* channel, uint64_t cur_head)
{
    uint64_t door_bell_info = 0;
    door_bell_info |= channel->sq_context_addr[0].context_info.roce_sq.qpn; // [0:23] DB_TAG (qp_num)
    door_bell_info |= 0UL << 24UL;                                      // [24:27] DB_CMD = HNS_ROCE_V2_SQ_DB (0)
    door_bell_info |= (cur_head % 65536UL) << 32UL;                      // [32:47] DB_PI = sq.head
    door_bell_info |= (uint64_t)(channel->sq_context_addr[0].context_info.roce_sq.sl) << 48UL; // [48:50] DB_SL = qp.sl

    __gm__ uint64_t* door_bell_addr = (__gm__ uint64_t*)(channel->sq_context_addr[0].context_info.roce_sq.db_hw_va);
    KERNEL_LOG(KERNEL_INFO, "hcomm door_bell door_bell_addr:%p, door_bell_info:%llu", door_bell_addr, door_bell_info);

    ub_local_.SetValue(0, door_bell_info);
    AscendC::GlobalTensor<uint64_t> DBGlobalTensor;
    DBGlobalTensor.SetGlobalBuffer(door_bell_addr);
    AscendC::DataCopyExtParams copy_params{1, 1 * sizeof(uint64_t), 0, 0, 0};
    AscendC::DataCopyPad(DBGlobalTensor, ub_local_, copy_params);
}

__aicore__ inline void asccomm_impl<comm_protocol_roce>::post_send(
    channel_handle channel_handle, GM_ADDR dst, GM_ADDR src, uint64_t len, bool is_read)
{
    __gm__ channel_entity* channel = (__gm__ channel_entity*)channel_handle;
    auto qp_num = channel->sq_context_addr[0].context_info.roce_sq.qpn;
    auto sq_base_addr = channel->sq_context_addr[0].context_info.roce_sq.sq_va;
    auto wqe_size = channel->sq_context_addr[0].context_info.roce_sq.wqe_size;
    auto cur_hardware_head = channel->sq_context_addr[0].context_info.roce_sq.head_addr;
    cache_write_through(reinterpret_cast<__gm__ uint8_t*>(cur_hardware_head), 8);
    uint64_t cur_head = *(__gm__ uint32_t*)(cur_hardware_head);

    auto cur_hardware_tail_addr = channel->sq_context_addr[0].context_info.roce_sq.tail_addr;
    uint64_t shift = 15U;
    auto qp_depth = channel->sq_context_addr[0].context_info.roce_sq.depth;

    KERNEL_LOG(
        KERNEL_INFO, "hcomm door_bell qp_num:%d, sq_base_addr:%p, wqe_size:%d, cur_head:%d, qp_depth:%d", qp_num, sq_base_addr,
        wqe_size, cur_head, qp_depth);

    // Make sure we don't overflow the SQ in an infinite loop - no need to mitigate endless loop as the host
    // will timeout and kill the kernel, same as all2all kernel if it fails to complete (e.g. in case of link loss)
    while (1) {
        cache_write_through((__gm__ uint8_t*)cur_hardware_tail_addr, 8);
        if ((cur_head - *(__gm__ uint32_t*)(cur_hardware_tail_addr)) < qp_depth - 1) {
            break;
        }
    }

    __gm__ uint8_t* wqe_addr = (__gm__ uint8_t*)(sq_base_addr + wqe_size * (cur_head % qp_depth));
    KERNEL_LOG(KERNEL_INFO, "hcomm post_send wqe_addr:%p", wqe_addr);

    // Write the WQE to GM
    uint64_t own_bit = (cur_head >> shift) & 1U;
    uint32_t byte_4 = is_read ?
                          static_cast<uint32_t>(asccomm_op_type::read) :
                          static_cast<uint32_t>(asccomm_op_type::write); // [0:4] opcode=0x3(RDMA_WRITE), 0x5(RDMA_READ)
    byte_4 |= ((~own_bit) << 7U) & (1U << 7U);                          // [7] owner_bit
    byte_4 |= 1U << 8U;                                                // [8:8] IBV_SEND_SIGNALED

    *(__gm__ uint32_t*)(wqe_addr) = byte_4;         // Control set by local parameter see above lines
    *(__gm__ uint32_t*)(wqe_addr + 4) = len;        // message size
    *(__gm__ uint32_t*)(wqe_addr + 8) = 0;          // immtdata is always 0 till we provide poll CQ flow in AIV
    *(__gm__ uint32_t*)(wqe_addr + 12) = 1U << 24U; // [120:127] num_sge = 1
    *(__gm__ uint32_t*)(wqe_addr + 16) = 0;         // [128:151] start_sge_idx = 0;
    *(__gm__ uint32_t*)(wqe_addr + 20) = channel->remote_buffer_addr[0].buffer_info.rma.protection_info_value.mem_info.roce.rkey;
    *(__gm__ uint64_t*)(wqe_addr + 24) = (uint64_t)dst; // destination VA

    constexpr uint32_t sge_addr_offset = 32;
    __gm__ uint8_t* sge_addr = wqe_addr + sge_addr_offset;
    KERNEL_LOG(KERNEL_INFO, "hcomm post_send sge_addr:%p", sge_addr);
    *(__gm__ uint32_t*)(sge_addr) = len;
    *(__gm__ uint32_t*)(sge_addr + sizeof(uint32_t)) =
        channel->local_buffer_addr[0].buffer_info.rma.protection_info_value.mem_info.roce.lkey;
    *(__gm__ uint64_t*)(sge_addr + 2 * sizeof(uint32_t)) = (uint64_t)src; // src VA addr memory registered by RNIC

    constexpr uint32_t wqe_addr_write_length = 48;
    cache_write_through(wqe_addr, wqe_addr_write_length);

    cur_head++;

    // Post doorbell
    door_bell(channel, cur_head);

    ub_local_head_.SetValue(0, (uint32_t)cur_head);
    AscendC::GlobalTensor<uint32_t> HeadGlobalTensor;
    HeadGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)cur_hardware_head);
    AscendC::DataCopyExtParams copy_params_head{1, 1 * sizeof(uint32_t), 0, 0, 0};
    AscendC::DataCopyPad(HeadGlobalTensor, ub_local_head_, copy_params_head);
    KERNEL_LOG(KERNEL_INFO, "hcomm post_send finish");
}

} // namespace AscendC

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V220_HCOMM_AIV_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H
#endif
