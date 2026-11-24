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
 * \file urma_batch_api_perf_def.h
 * \brief Definitions shared by the URMA batch API performance example host and device code.
 */

#ifndef EXAMPLES_ASCCOMM_URMA_BATCH_API_PERF_DEF_H
#define EXAMPLES_ASCCOMM_URMA_BATCH_API_PERF_DEF_H

#include <cstdint>

#include "asccomm/asccomm.h"
#include "hccl/hccl.h"

namespace urma_batch_api_perf_example {

// ---------------- 注册对称内存（2GB，各档共用，每档重铺初值） ----------------
// source/write/read/notify 各占一个 len 窗口；batch 表示每次 batch_commit 聚合的请求数。
constexpr uint64_t BUFFER_BYTES = 2ULL << 30;

// ---------------- 接口选择（api_mask 位）与计时槽 ----------------
constexpr uint32_t API_WRITE = 1U << 0;  // write_nbi
constexpr uint32_t API_READ = 1U << 1;   // read_nbi
constexpr uint32_t API_NOTIFY = 1U << 2; // write_with_notify_nbi
constexpr uint32_t API_ALL = API_WRITE | API_READ | API_NOTIFY;
// API 位 → timing 第一维下标：--api all 时各操作的计时写各自的槽，互不覆盖
constexpr uint32_t API_WRITE_IDX = 0U;
constexpr uint32_t API_READ_IDX = 1U;
constexpr uint32_t API_NOTIFY_IDX = 2U;
constexpr uint32_t API_IDX_COUNT = 3U;

// ---------------- 性能口径（metric_mask 位） ----------------
constexpr uint32_t METRIC_LAT = 1U << 0; // 时延：计时段内不 Drain
constexpr uint32_t METRIC_BW = 1U << 1;  // 带宽：计时段内含 Drain（端到端）
constexpr uint32_t METRIC_ALL = METRIC_LAT | METRIC_BW;

// ---------------- BatchCommit 计时方式 ----------------
// combine：Post 与 BatchCommit 放在同一个时延窗口；
// split：Post 与 BatchCommit 分别计时，并输出 commit-lat 结果行。
constexpr uint32_t COMMIT_MODE_COMBINE = 0U;
constexpr uint32_t COMMIT_MODE_SPLIT = 1U;

// ---------------- 运行期维度默认值（命令行可覆盖） ----------------
constexpr uint32_t DEFAULT_ROUNDS = 1U; // 批轮次重复次数
constexpr uint64_t DEFAULT_MIN_EXP = 3; // 数据量 = 2^exp，默认 3 17 即 8B~128KB
constexpr uint64_t DEFAULT_MAX_EXP = 17;
constexpr uint32_t DEFAULT_MIN_BATCH = 1024U; // 每批聚合提交条数
constexpr uint32_t DEFAULT_MAX_BATCH = 1024U;
constexpr uint32_t WARMUP_ROUNDS = 50U; // warmup 固定 50 个完整批轮次，稳定缓存与队列状态

// ---------------- 数据 pattern ----------------
constexpr int32_t SRC_PATTERN_BASE = 100; // source 区填 100+rank（int32）
constexpr uint64_t NOTIFY_VAL_BASE = 300; // notify flag 固定值（单槽并发写，只校验 == 常量）
constexpr uint64_t FLAG_STRIDE = sizeof(uint64_t);

constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_UB_CTP;
static_assert(
    static_cast<int32_t>(HOST_COMM_PROTOCOL) == static_cast<int32_t>(COMM_PROTOCOL_UB_CTP),
    "Host and Kernel communication protocols must match");

// 每条请求的 WQEBB 占用：write_with_notify_nbi 占 2、其余占 1（每 WQEBB 64B）。
// batch WQE 暂存缓冲住在 VECOUT UB，按 256KiB 总预算校验。
constexpr uint32_t NOTIFY_WQEBB = 2U;
constexpr uint32_t WQEBB_BYTES = 64U;
constexpr uint32_t BATCH_UB_BUDGET_BYTES = 256U * 1024U;

// 一档 len 的注册内存布局，host 与 kernel 按同一公式计算。
struct case_layout {
    uint64_t write_base;
    uint64_t read_base;
    uint64_t notify_base;
    uint64_t flags_base;
    uint64_t total_bytes;
};

// ASC 统一语言下 host / kernel 共用函数须双限定（单 __aicore__ 则 host 侧调不到）。
__host__ __aicore__ inline case_layout case_layout_of(uint64_t len)
{
    case_layout layout{};
    layout.write_base = len;
    layout.read_base = layout.write_base + len;
    layout.notify_base = layout.read_base + len;
    layout.flags_base = (layout.notify_base + len + 7ULL) & ~7ULL; // flag 槽 8B 对齐
    layout.total_bytes = layout.flags_base + FLAG_STRIDE;
    return layout;
}

constexpr uint32_t WARMUP_METRIC_INDEX = 0U; // timing[api_idx][0]：失败码（无计时值）
constexpr uint32_t LAT_CYCLES_INDEX = 1U;    // timing[api_idx][1]：时延档 cycles（各轮之和）
constexpr uint32_t BW_CYCLES_INDEX = 2U;     // timing[api_idx][2]：带宽档 cycles
constexpr uint32_t COMMIT_CYCLES_INDEX = 3U; // timing[api_idx][3]：split 时 batch_commit cycles（各轮之和）
constexpr uint32_t TIMING_WORDS = 4U;

// kernel 只上报原始 cycles，均值与派生列由 host 侧计算。
// 拓扑（--perf_type p2p|ring）在 kernel 侧是 send / recv 两对入参，p2p 与 ring-2P 下相等。
struct kernel_context {
    uint64_t channel_handle;      // send 通道（write/notify）
    uint64_t recv_channel_handle; // recv 通道（read）
    uint64_t local_buffer_addr;
    uint64_t remote_buffer_addr;      // send 对端基址（write/notify 的 dst、notify flag）
    uint64_t recv_remote_buffer_addr; // recv 对端基址（read 的 src）
    uint64_t msg_bytes;
    uint32_t rounds;
    uint32_t batch_cap;
    uint32_t api_mask;
    uint32_t commit_mode; // COMMIT_MODE_COMBINE / COMMIT_MODE_SPLIT
    uint32_t metric_mask;
    // [api_idx][0]=失败码 [1]=lat cycles [2]=bw cycles [3]=split 时 batch_commit cycles
    uint64_t timing[API_IDX_COUNT][TIMING_WORDS];
    uint32_t drain_ret; // 最后一次 drain 返回码（0=成功）
    uint32_t result;    // kernel_result
};

enum kernel_result : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_INVALID_ARGS,
    KERNEL_INIT_FAILED,
    KERNEL_UB_OVERFLOW,     // batch 暂存缓冲超 VECOUT UB 预算（kernel 运行期校验）
    KERNEL_BUFFER_OVERFLOW, // len 数据窗口超出注册内存（kernel 运行期校验）
    KERNEL_MAKE_HANDLE_FAILED,
    KERNEL_WARMUP_FAILED,
    KERNEL_LAT_POST_FAILED,
    KERNEL_LAT_COMMIT_FAILED,
    KERNEL_LAT_DRAIN_FAILED,
    KERNEL_BW_POST_FAILED,
    KERNEL_BW_COMMIT_FAILED,
    KERNEL_BW_DRAIN_FAILED,
};

inline const char* kernel_result_name(kernel_result result)
{
    switch (result) {
        case KERNEL_SUCCESS:
            return "success";
        case KERNEL_INVALID_ARGS:
            return "invalid kernel args";
        case KERNEL_INIT_FAILED:
            return "init failed";
        case KERNEL_UB_OVERFLOW:
            // 预算 = batch_cap × 每条WQEBB × 64B ≤ 256KiB（notify 占 2 WQEBB，其余 1），缩小 --batch
            return "batch exceeds VECOUT UB budget (reduce --batch)";
        case KERNEL_BUFFER_OVERFLOW:
            // 窗口 = 4 × len + 8 ≤ 2GB 注册内存，缩小 --exponent-range
            return "data window exceeds registered buffer (reduce --exponent-range)";
        case KERNEL_MAKE_HANDLE_FAILED:
            return "make_batch_handle failed";
        case KERNEL_WARMUP_FAILED:
            return "warmup failed";
        case KERNEL_LAT_POST_FAILED:
            // 典型原因：一批 batch_cap 条请求须驻留 SQ（notify 占 2 WQEBB），超深度即 Post 失败
            return "latency-window post failed (SQ may be full: reduce --batch)";
        case KERNEL_LAT_COMMIT_FAILED:
            return "latency-window batch_commit failed";
        case KERNEL_LAT_DRAIN_FAILED:
            return "latency-window drain failed";
        case KERNEL_BW_POST_FAILED:
            return "bandwidth-window post failed";
        case KERNEL_BW_COMMIT_FAILED:
            return "bandwidth-window batch_commit failed";
        case KERNEL_BW_DRAIN_FAILED:
            return "bandwidth-window drain failed";
        default:
            return "unknown kernel error";
    }
}

} // namespace urma_batch_api_perf_example

#endif // EXAMPLES_ASCCOMM_URMA_BATCH_API_PERF_DEF_H
