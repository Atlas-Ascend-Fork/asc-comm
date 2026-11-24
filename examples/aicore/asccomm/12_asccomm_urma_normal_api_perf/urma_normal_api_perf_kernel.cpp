/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file urma_normal_api_perf_kernel.cpp
 * \brief Device side of the URMA normal API performance example.
 */

#include "kernel_operator.h"
#include "asccomm/asccomm.h"

#include "urma_normal_api_perf_def.h"

namespace urma_normal_api_perf_example {

// commit=false 路径的 WQE 配置：同默认配置，仅 cqe=0（不依赖完成通知）
constexpr asc::wqe_entry WQE_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

// MODE_* 同时是 timing 第一维槽位（与 def.h 的 API_*_IDX 同值），
// static_assert 编译期钉死这个 ABI。
enum test_api : int32_t {
    MODE_WRITE = 0, // write_nbi
    MODE_READ = 1,  // read_nbi
    MODE_NOTIFY = 2 // write_with_notify_nbi
};

static_assert(
    MODE_WRITE == static_cast<int32_t>(API_WRITE_IDX) && MODE_READ == static_cast<int32_t>(API_READ_IDX) &&
        MODE_NOTIFY == static_cast<int32_t>(API_NOTIFY_IDX),
    "MODE_* values must match API_*_IDX timing slots");

class kernel_urma_normal_api_perf {
    using asccomm_type = asc::asccomm<asc::COMM_PROTOCOL_UB_CTP>;

public:
    __aicore__ inline void init(GM_ADDR context, __ubuf__ uint8_t* workspace)
    {
        this->context = reinterpret_cast<__gm__ kernel_context*>(context);
        this->channel = this->context->channel_handle;
        this->recv_channel = this->context->recv_channel_handle;
        this->local_base = reinterpret_cast<GM_ADDR>(this->context->local_buffer_addr);
        this->remote_base = reinterpret_cast<GM_ADDR>(this->context->remote_buffer_addr);
        this->recv_remote_base = reinterpret_cast<GM_ADDR>(this->context->recv_remote_buffer_addr);

        this->comm_buf = workspace;
        if (asccomm.init(comm_buf, asc::HCOMM_UB_BUF_SIZE) == asc::HCOMM_SUCCESS) {
            init_ok = true;
        }
    }

    __aicore__ inline void process()
    {
        context->result = KERNEL_SUCCESS;
        if (!init_ok) {
            context->result = KERNEL_INIT_FAILED;
            return;
        }
        const uint32_t rounds = context->rounds;
        const uint32_t api_mask = context->api_mask;
        const uint32_t metric_mask = context->metric_mask;
        const uint64_t len = context->msg_bytes;
        if (rounds == 0U || api_mask == 0U || (api_mask & ~API_ALL) != 0U || (metric_mask & ~METRIC_ALL) != 0U ||
            len == 0U || (len & (sizeof(int32_t) - 1U)) != 0U) {
            context->result = KERNEL_INVALID_ARGS;
            return;
        }

        // --api all 时一次 launch 依次测三接口；MODE_*（0/1/2）先换算成 api_mask 位再查掩码
        for (int mode = MODE_WRITE; mode <= MODE_NOTIFY; ++mode) {
            const uint32_t api_bit = (mode == MODE_WRITE) ? API_WRITE : (mode == MODE_READ) ? API_READ : API_NOTIFY;
            if ((api_mask & api_bit) == 0U) {
                continue;
            }
            bool ok = false;
            switch (mode) {
                case MODE_WRITE:
                    ok = context->commit_inline != 0U ? run_api<true, MODE_WRITE>(rounds, metric_mask, len) :
                                                        run_api<false, MODE_WRITE>(rounds, metric_mask, len);
                    break;
                case MODE_READ:
                    ok = context->commit_inline != 0U ? run_api<true, MODE_READ>(rounds, metric_mask, len) :
                                                        run_api<false, MODE_READ>(rounds, metric_mask, len);
                    break;
                default:
                    ok = context->commit_inline != 0U ? run_api<true, MODE_NOTIFY>(rounds, metric_mask, len) :
                                                        run_api<false, MODE_NOTIFY>(rounds, metric_mask, len);
                    break;
            }
            if (!ok) {
                return; // run_api 已写 result / timing，终止后续接口
            }
        }
    }

private:
    // 一次发起：mode / commit 都是模板参数，编译期分派。
    // write/notify 走 send 通道；read 走 recv 通道。
    // flag_addr 只有 notify 实例使用。
    template <bool commit, int mode>
    __aicore__ inline int32_t post(uint64_t len, GM_ADDR local_addr, GM_ADDR remote_addr, GM_ADDR flag_addr)
    {
        if constexpr (mode == MODE_WRITE) {
            if constexpr (commit) {
                return asccomm.write_nbi(channel, remote_addr, local_addr, len);
            }
            return asccomm.write_nbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(channel, remote_addr, local_addr, len);
        } else if constexpr (mode == MODE_READ) {
            if constexpr (commit) {
                return asccomm.read_nbi(recv_channel, local_addr, remote_addr, len);
            }
            return asccomm.read_nbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(
                recv_channel, local_addr, remote_addr, len);
        } else {
            if constexpr (commit) {
                return asccomm.write_with_notify_nbi(channel, remote_addr, local_addr, len, flag_addr, NOTIFY_VAL_BASE);
            }
            return asccomm.write_with_notify_nbi<false, PIPE_S, PIPE_MTE3, WQE_NO_CQE_CFG>(
                channel, remote_addr, local_addr, len, flag_addr, NOTIFY_VAL_BASE);
        }
    }

    // warmup：固定 commit=true 逐条提交（不受 commit=false 的 sq_depth 约束），
    // 跑完 drain 清干净。
    template <int mode>
    __aicore__ inline bool warmup(
        uint32_t warmups, uint64_t len, GM_ADDR local_fixed, GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        for (uint32_t i = 0; i < warmups; ++i) {
            const int32_t ret = post<true, mode>(len, local_fixed, remote_addr, flag_slot);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_WARMUP_FAILED;
                context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        const int32_t ret = asccomm.drain(mode == MODE_READ ? recv_channel : channel);
        context->drain_ret = static_cast<uint32_t>(ret);
        if (ret != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_WARMUP_FAILED;
            context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        return true;
    }

    // 时延档：计时段内只发起，commit（final 方式）与 drain 都在段外
    template <bool commit, int mode>
    __aicore__ inline bool run_latency(
        uint32_t rounds, uint64_t len, GM_ADDR local_fixed, GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        int32_t ret_post = asc::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t i = 0; i < rounds; ++i) {
            ret_post = post<commit, mode>(len, local_fixed, remote_addr, flag_slot);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (ret_post != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_LAT_POST_FAILED;
            context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_post);
            return false;
        }
        int32_t ret = asc::HCOMM_SUCCESS;
        if constexpr (!commit) {
            ret = asccomm.commit(mode == MODE_READ ? recv_channel : channel);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_LAT_COMMIT_FAILED;
                context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        ret = asccomm.drain(mode == MODE_READ ? recv_channel : channel);
        context->drain_ret = static_cast<uint32_t>(ret);
        if (ret != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_LAT_DRAIN_FAILED;
            context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
            return false;
        }
        context->timing[mode][LAT_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 带宽档：计时段内含收尾（commit + drain），端到端
    template <bool commit, int mode>
    __aicore__ inline bool run_bandwidth(
        uint32_t rounds, uint64_t len, GM_ADDR local_fixed, GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        int32_t ret_post = asc::HCOMM_SUCCESS;
        int32_t ret_commit = asc::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t i = 0; i < rounds; ++i) {
            ret_post = post<commit, mode>(len, local_fixed, remote_addr, flag_slot);
        }
        if constexpr (!commit) {
            ret_commit = asccomm.commit(mode == MODE_READ ? recv_channel : channel);
        }
        const int32_t ret = asccomm.drain(mode == MODE_READ ? recv_channel : channel);
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (ret_post != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_POST_FAILED;
            context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_post);
            return false;
        }
        if (ret_commit != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_COMMIT_FAILED;
            context->timing[mode][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_commit);
            return false;
        }
        if (ret != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_DRAIN_FAILED;
            context->drain_ret = static_cast<uint32_t>(ret);
            return false;
        }
        context->timing[mode][BW_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 一个接口的完整测试：算地址 → warmup → 时延档 → 带宽档。
    // write/notify 落点在 send 对端；read 落回本卡 read 区，源固定在 recv 对端。
    template <bool commit, int mode>
    __aicore__ inline bool run_api(uint32_t rounds, uint32_t metric_mask, uint64_t len)
    {
        constexpr bool is_read = (mode == MODE_READ);
        GM_ADDR local_fixed = is_read ? (local_base + READ_BASE) : local_base;
        GM_ADDR remote_fixed =
            is_read ? recv_remote_base : (remote_base + ((mode == MODE_WRITE) ? WRITE_BASE : NOTIFY_BASE));
        // notify flag 单槽；write/read 实例中折叠为常量 0
        GM_ADDR flag_slot = 0;
        if constexpr (mode == MODE_NOTIFY) {
            flag_slot = remote_base + FLAG_BASE;
        }

        const uint32_t warmups = rounds < WARMUPS ? rounds : WARMUPS;
        if (!warmup<mode>(warmups, len, local_fixed, remote_fixed, flag_slot)) {
            return false;
        }
        if ((metric_mask & METRIC_LAT) != 0U) {
            if (!run_latency<commit, mode>(rounds, len, local_fixed, remote_fixed, flag_slot)) {
                return false;
            }
        }
        if ((metric_mask & METRIC_BW) != 0U) {
            if (!run_bandwidth<commit, mode>(rounds, len, local_fixed, remote_fixed, flag_slot)) {
                return false;
            }
        }
        return true;
    }

    __ubuf__ uint8_t* comm_buf{nullptr};
    asccomm_type asccomm;
    __gm__ kernel_context* context{nullptr};
    asc::ChannelHandle channel{0};
    asc::ChannelHandle recv_channel{0};
    GM_ADDR local_base{nullptr};
    GM_ADDR remote_base{nullptr};
    GM_ADDR recv_remote_base{nullptr};
    bool init_ok{false};
};

} // namespace urma_normal_api_perf_example

extern "C" __vector__ __global__ __aicore__ void urma_normal_api_perf_kernel(GM_ADDR context)
{
    // asccomm_init工作区：UB数组直接落UB（c_api形式），无需TPipe。
    __ubuf__ uint8_t comm_buf[asc::HCOMM_UB_BUF_SIZE];
    urma_normal_api_perf_example::kernel_urma_normal_api_perf op;
    op.init(context, comm_buf);
    op.process();
}

void launch_urma_normal_api_perf(void* stream, uint64_t context)
{
    urma_normal_api_perf_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
