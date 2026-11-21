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
 * \file urma_batch_api_perf_kernel.cpp
 * \brief Device side of the URMA batch API performance example.
 */

#include "kernel_operator.h"
#include "asccomm/asccomm.h"

#include "urma_batch_api_perf_def.h"

namespace urma_batch_api_perf_example {

// 批内 WQE 模板入参：只有每批的末条请求用 cqe=1
constexpr asc::wqe_entry WQE_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};
constexpr asc::wqe_entry WQE_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 0,
};

// OP_* 同时是 timing 第一维槽位（与 def.h 的 API_*_IDX 同值），
// static_assert 编译期钉死这个 ABI。
enum test_operation : int32_t {
    OP_WRITE = 0, // asccomm_write_nbi
    OP_READ = 1,  // asccomm_read_nbi
    OP_NOTIFY = 2 // asccomm_write_with_notify_nbi
};

static_assert(
    OP_WRITE == static_cast<int32_t>(API_WRITE_IDX) && OP_READ == static_cast<int32_t>(API_READ_IDX) &&
        OP_NOTIFY == static_cast<int32_t>(API_NOTIFY_IDX),
    "OP_* values must match API_*_IDX timing slots");

class kernel_urma_batch_api_perf {
    using asccomm_type = asc::asccomm<asc::COMM_PROTOCOL_UB_CTP>;

public:
    // 批量工作区由kernel入口的UB数组提供（容量取BATCH_UB_BUDGET_BYTES预算上限，实际大小batch_buf_bytes运行期传入）。
    __aicore__ inline void init(GM_ADDR context, __ubuf__ uint8_t* workspace)
    {
        this->context = reinterpret_cast<__gm__ kernel_context*>(context);
        this->channel = this->context->channel_handle;
        this->recv_channel = this->context->recv_channel_handle;
        this->local_base = reinterpret_cast<GM_ADDR>(this->context->local_buffer_addr);
        this->remote_base = reinterpret_cast<GM_ADDR>(this->context->remote_buffer_addr);
        this->recv_remote_base = reinterpret_cast<GM_ADDR>(this->context->recv_remote_buffer_addr);
        this->batch_buf = workspace;
        this->init_ok = true;
    }

    __aicore__ inline void process()
    {
        context->result = KERNEL_SUCCESS;
        if (!init_ok) {
            context->result = KERNEL_INIT_FAILED;
            return;
        }
        const uint32_t rounds = context->rounds;
        const uint32_t batch_cap = context->batch_cap;
        const uint32_t api_mask = context->api_mask;
        const uint32_t metric_mask = context->metric_mask;
        const uint64_t len = context->msg_bytes;
        if (rounds == 0U || batch_cap == 0U || api_mask == 0U || (api_mask & ~API_ALL) != 0U ||
            (metric_mask & ~METRIC_ALL) != 0U || len == 0U || (len & (sizeof(int32_t) - 1U)) != 0U) {
            context->result = KERNEL_INVALID_ARGS;
            return;
        }
        const case_layout layout = case_layout_of(len);
        if (layout.total_bytes > BUFFER_BYTES) {
            context->result = KERNEL_BUFFER_OVERFLOW;
            return;
        }

        // batch 暂存缓冲大小（每条 WQEBB 占用取三操作里最大的 notify），并校验 UB 预算
        const uint32_t max_wqebb_per_op = (api_mask & API_NOTIFY) ? NOTIFY_WQEBB : 1U;
        const uint64_t batch_buf_bytes = static_cast<uint64_t>(batch_cap) * max_wqebb_per_op * WQEBB_BYTES;
        if (batch_buf_bytes > BATCH_UB_BUDGET_BYTES) {
            context->result = KERNEL_UB_OVERFLOW;
            return;
        }

        // --api all 时一次 launch 依次测三操作（OP_* 先换算成 api_mask 位再查掩码）
        for (int op = OP_WRITE; op <= OP_NOTIFY; ++op) {
            const uint32_t api_bit = (op == OP_WRITE) ? API_WRITE : (op == OP_READ) ? API_READ : API_NOTIFY;
            if ((api_mask & api_bit) == 0U) {
                continue;
            }
            bool ok = false;
            switch (op) {
                case OP_WRITE:
                    ok = run_op<OP_WRITE>(layout, rounds, batch_cap, static_cast<uint32_t>(batch_buf_bytes), len);
                    break;
                case OP_READ:
                    ok = run_op<OP_READ>(layout, rounds, batch_cap, static_cast<uint32_t>(batch_buf_bytes), len);
                    break;
                default:
                    ok = run_op<OP_NOTIFY>(layout, rounds, batch_cap, static_cast<uint32_t>(batch_buf_bytes), len);
                    break;
            }
            if (!ok) {
                return; // run_op 已写 result / timing，终止后续操作
            }
        }
    }

private:
    // 往 batch_handle 攒一条 WQE；op / CQE 都是模板参数，编译期分派。
    // write/notify 走 send 通道；read 走 recv 通道。flag_addr 只有 notify 实例使用。
    template <int op, bool enable_cqe>
    __aicore__ inline int32_t post_batch(
        asc::ubc_batch_handle& batch_handle, uint32_t len, GM_ADDR local_addr, GM_ADDR remote_addr, GM_ADDR flag_addr)
    {
        if constexpr (op == OP_WRITE) {
            if constexpr (enable_cqe) {
                return asccomm.asccomm_write_nbi<WQE_CQE_CFG>(batch_handle, remote_addr, local_addr, len);
            }
            return asccomm.asccomm_write_nbi<WQE_NO_CQE_CFG>(batch_handle, remote_addr, local_addr, len);
        } else if constexpr (op == OP_READ) {
            if constexpr (enable_cqe) {
                return asccomm.asccomm_read_nbi<WQE_CQE_CFG>(batch_handle, local_addr, remote_addr, len);
            }
            return asccomm.asccomm_read_nbi<WQE_NO_CQE_CFG>(batch_handle, local_addr, remote_addr, len);
        } else {
            if constexpr (enable_cqe) {
                return asccomm.asccomm_write_with_notify_nbi<WQE_CQE_CFG>(
                    batch_handle, remote_addr, local_addr, len, flag_addr, NOTIFY_VAL_BASE);
            }
            return asccomm.asccomm_write_with_notify_nbi<WQE_NO_CQE_CFG>(
                batch_handle, remote_addr, local_addr, len, flag_addr, NOTIFY_VAL_BASE);
        }
    }

    // warmup：固定 WARMUP_ROUNDS(=50) 个完整批轮次（B 条 → asccomm_batch_commit → asccomm_drain）
    template <int op>
    __aicore__ inline bool warmup(
        asc::ubc_batch_handle& batch_handle, uint32_t last_batch_index, uint32_t msg_len, GM_ADDR local_fixed,
        GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        for (uint32_t round = 0; round < WARMUP_ROUNDS; ++round) {
            for (uint32_t i = 0; i < last_batch_index; ++i) {
                const int32_t ret = post_batch<op, false>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
                if (ret != asc::HCOMM_SUCCESS) {
                    context->result = KERNEL_WARMUP_FAILED;
                    context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                    return false;
                }
            }
            int32_t ret = post_batch<op, true>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_WARMUP_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
            ret = asccomm.asccomm_batch_commit(batch_handle);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_WARMUP_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
            ret = asccomm.asccomm_drain(batch_handle);
            context->drain_ret = static_cast<uint32_t>(ret);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_WARMUP_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        return true;
    }

    // 时延档：逐轮分段计时，asccomm_drain 在段外。COMMIT_IN_WINDOW=true 时 Post 与
    // asccomm_batch_commit 同窗；false 时 Post 窗先停表，asccomm_batch_commit 再单独开窗计时。
    template <int op, bool COMMIT_IN_WINDOW>
    __aicore__ inline bool run_latency(
        asc::ubc_batch_handle& batch_handle, uint32_t rounds, uint32_t last_batch_index, uint32_t msg_len,
        GM_ADDR local_fixed, GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        uint64_t elapsed = 0;
        uint64_t commit_elapsed = 0;
        int32_t ret_post = asc::HCOMM_SUCCESS;
        int32_t ret_commit = asc::HCOMM_SUCCESS;
        for (uint32_t r = 0; r < rounds; ++r) {
            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t t0 = AscendC::GetSystemCycle();
            for (uint32_t i = 0; i < last_batch_index; ++i) {
                ret_post = post_batch<op, false>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
            }
            ret_post = post_batch<op, true>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
            if constexpr (COMMIT_IN_WINDOW) {
                ret_commit = asccomm.asccomm_batch_commit(batch_handle);
            }
            AscendC::PipeBarrier<PIPE_ALL>();
            uint64_t t1 = AscendC::GetSystemCycle();
            elapsed += t1 - t0;
            // 计时段结束（以下出窗）
            if constexpr (!COMMIT_IN_WINDOW) {
                AscendC::PipeBarrier<PIPE_ALL>();
                uint64_t c0 = AscendC::GetSystemCycle();
                ret_commit = asccomm.asccomm_batch_commit(batch_handle);
                AscendC::PipeBarrier<PIPE_ALL>();
                uint64_t c1 = AscendC::GetSystemCycle();
                commit_elapsed += c1 - c0;
            }
            if (ret_post != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_LAT_POST_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_post);
                return false;
            }
            if (ret_commit != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_LAT_COMMIT_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_commit);
                return false;
            }
            // 段外清空 SQ，保证下一轮从空批次开始。
            int32_t ret = asccomm.asccomm_drain(batch_handle);
            context->drain_ret = static_cast<uint32_t>(ret);
            if (ret != asc::HCOMM_SUCCESS) {
                context->result = KERNEL_LAT_DRAIN_FAILED;
                context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret);
                return false;
            }
        }
        context->timing[op][LAT_CYCLES_INDEX] = elapsed;
        if constexpr (!COMMIT_IN_WINDOW) {
            context->timing[op][COMMIT_CYCLES_INDEX] = commit_elapsed;
        }
        return true;
    }

    // 带宽档：单窗口包住全部轮次的 Post、BatchCommit 和 Drain；返回码出窗后判断。
    template <int op>
    __aicore__ inline bool run_bandwidth(
        asc::ubc_batch_handle& batch_handle, uint32_t rounds, uint32_t last_batch_index, uint32_t msg_len,
        GM_ADDR local_fixed, GM_ADDR remote_addr, GM_ADDR flag_slot)
    {
        int32_t ret_post = asc::HCOMM_SUCCESS;
        int32_t ret_commit = asc::HCOMM_SUCCESS;
        int32_t ret = asc::HCOMM_SUCCESS;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t0 = AscendC::GetSystemCycle();
        for (uint32_t r = 0; r < rounds; ++r) {
            for (uint32_t i = 0; i < last_batch_index; ++i) {
                ret_post = post_batch<op, false>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
            }
            ret_post = post_batch<op, true>(batch_handle, msg_len, local_fixed, remote_addr, flag_slot);
            ret_commit = asccomm.asccomm_batch_commit(batch_handle);
            ret = asccomm.asccomm_drain(batch_handle);
        }
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t t1 = AscendC::GetSystemCycle();
        // 计时段结束（以下出窗）
        if (ret_post != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_POST_FAILED;
            context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_post);
            return false;
        }
        if (ret_commit != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_COMMIT_FAILED;
            context->timing[op][WARMUP_METRIC_INDEX] = static_cast<uint64_t>(ret_commit);
            return false;
        }
        if (ret != asc::HCOMM_SUCCESS) {
            context->result = KERNEL_BW_DRAIN_FAILED;
            context->drain_ret = static_cast<uint32_t>(ret);
            return false;
        }
        context->timing[op][BW_CYCLES_INDEX] = t1 - t0;
        return true;
    }

    // 单个操作的全流程：算地址 → asccomm_make_batch_handle → warmup → 时延档 → 带宽档。
    template <int op>
    __aicore__ inline bool run_op(
        const case_layout& layout, uint32_t rounds, uint32_t batch_cap, uint32_t batch_buf_bytes, uint64_t msg_bytes)
    {
        constexpr bool is_read = (op == OP_READ);
        const uint32_t msg_len = static_cast<uint32_t>(msg_bytes);

        GM_ADDR local_fixed = is_read ? (local_base + layout.read_base) : local_base;
        GM_ADDR remote_fixed =
            is_read ? recv_remote_base : (remote_base + ((op == OP_WRITE) ? layout.write_base : layout.notify_base));
        // notify flag 单槽，地址窗外算好；write/read 实例中折叠为常量 0
        GM_ADDR flag_slot = 0;
        if constexpr (op == OP_NOTIFY) {
            flag_slot = remote_base + layout.flags_base;
        }
        const uint32_t last_batch_index = batch_cap - 1U;

        // 批句柄绑定单条通道：read 用 recv 通道，其余用 send 通道
        asc::ChannelHandle selected_channel = is_read ? recv_channel : channel;
        auto batch_handle =
            asccomm.asccomm_make_batch_handle(selected_channel, batch_buf, batch_buf_bytes, remote_fixed, local_fixed);
        if (batch_handle.channel_handle == 0U) {
            context->result = KERNEL_MAKE_HANDLE_FAILED;
            return false;
        }

        if (!warmup<op>(batch_handle, last_batch_index, msg_len, local_fixed, remote_fixed, flag_slot)) {
            return false;
        }
        if ((context->metric_mask & METRIC_LAT) != 0U) {
            // commit_mode 读一次选进模板参数，热循环内零分支
            const bool lat_ok =
                context->commit_mode == COMMIT_MODE_COMBINE ?
                    run_latency<op, true>(
                        batch_handle, rounds, last_batch_index, msg_len, local_fixed, remote_fixed, flag_slot) :
                    run_latency<op, false>(
                        batch_handle, rounds, last_batch_index, msg_len, local_fixed, remote_fixed, flag_slot);
            if (!lat_ok) {
                return false;
            }
        }
        if ((context->metric_mask & METRIC_BW) != 0U) {
            if (!run_bandwidth<op>(
                    batch_handle, rounds, last_batch_index, msg_len, local_fixed, remote_fixed, flag_slot)) {
                return false;
            }
        }
        return true;
    }

    __ubuf__ uint8_t* batch_buf{nullptr};
    asccomm_type asccomm;
    __gm__ kernel_context* context{nullptr};
    asc::ChannelHandle channel{0};
    asc::ChannelHandle recv_channel{0};
    GM_ADDR local_base{nullptr};
    GM_ADDR remote_base{nullptr};
    GM_ADDR recv_remote_base{nullptr};
    bool init_ok{false};
};

} // namespace urma_batch_api_perf_example

extern "C" __vector__ __global__ __aicore__ void urma_batch_api_perf_kernel(GM_ADDR context)
{
    // 批量工作区：UB数组按BATCH_UB_BUDGET_BYTES预算落UB，实际大小运行期传入。
    __ubuf__ uint8_t batch_buf[urma_batch_api_perf_example::BATCH_UB_BUDGET_BYTES];
    urma_batch_api_perf_example::kernel_urma_batch_api_perf op;
    op.init(context, batch_buf);
    op.process();
}

void launch_urma_batch_api_perf(void* stream, uint64_t context)
{
    urma_batch_api_perf_kernel<<<1, 0, stream>>>(reinterpret_cast<GM_ADDR>(context));
}
