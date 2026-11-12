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

#ifndef EXAMPLES_HCOMM_URMA_BATCH_API_PERF_DEF_H
#define EXAMPLES_HCOMM_URMA_BATCH_API_PERF_DEF_H

#include <cstdint>

#include "adv_api/hcomm/hcomm.h"
#include "hccl/hccl.h"

namespace UrmaBatchApiPerfExample {

// ---------------- 注册对称内存（1GB，各档共用，每档重铺初值） ----------------
// 按 (len, batchCap) 划分（公式见 CaseLayoutOf），每轮写的都是同一扇批窗口，
// host 校验也只查一个批窗口。
constexpr uint64_t BUFFER_BYTES = 1ULL << 30;

// ---------------- 接口选择（apiMask 位）与计时槽 ----------------
constexpr uint32_t API_WRITE = 1U << 0;  // WriteNbi
constexpr uint32_t API_READ = 1U << 1;   // ReadNbi
constexpr uint32_t API_NOTIFY = 1U << 2; // WriteWithNotifyNbi
constexpr uint32_t API_ALL = API_WRITE | API_READ | API_NOTIFY;
// API 位 → timing 第一维下标：--api all 时各操作的计时写各自的槽，互不覆盖
constexpr uint32_t API_WRITE_IDX = 0U;
constexpr uint32_t API_READ_IDX = 1U;
constexpr uint32_t API_NOTIFY_IDX = 2U;
constexpr uint32_t API_IDX_COUNT = 3U;

// ---------------- 性能口径（metricMask 位） ----------------
constexpr uint32_t METRIC_LAT = 1U << 0; // 时延：计时段内不 Drain
constexpr uint32_t METRIC_BW = 1U << 1;  // 带宽：计时段内含 Drain（端到端）
constexpr uint32_t METRIC_ALL = METRIC_LAT | METRIC_BW;

// ---------------- 运行期维度默认值（命令行可覆盖） ----------------
constexpr uint32_t DEFAULT_ROUNDS = 1U; // 批轮次重复次数
constexpr uint64_t DEFAULT_MIN_EXP = 3; // 数据量 = 2^exp，默认 3 17 即 8B~128KB
constexpr uint64_t DEFAULT_MAX_EXP = 17;
constexpr uint32_t DEFAULT_MIN_BATCH = 1024U; // 每批条数（决定地址窗口大小）
constexpr uint32_t DEFAULT_MAX_BATCH = 1024U;
constexpr uint32_t WARMUP_ROUNDS = 1U; // warmup 固定 1 个完整批轮次

// ---------------- 数据 pattern ----------------
constexpr int32_t SRC_PATTERN_BASE = 100; // source 区填 100+rank（int32）
constexpr uint64_t NOTIFY_VAL_BASE = 300; // notify flag 固定值（单槽并发写，只校验 == 常量）
constexpr uint64_t FLAG_STRIDE = sizeof(uint64_t);

constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_UB_CTP;
static_assert(
    static_cast<int32_t>(HOST_COMM_PROTOCOL) == static_cast<int32_t>(AscendC::COMM_PROTOCOL_UB_CTP),
    "Host and Kernel communication protocols must match");

// 每条请求的 WQEBB 占用：WriteWithNotifyNbi 占 2、其余占 1（每 WQEBB 64B）。
// batch WQE 暂存缓冲住在 VECOUT UB，按 192KB 预算校验（Ascend950 VECOUT UB 256KB）。
constexpr uint32_t NOTIFY_WQEBB = 2U;
constexpr uint32_t WQEBB_BYTES = 64U;
constexpr uint32_t BATCH_UB_BUDGET_BYTES = 192U * 1024U;

// 一档 (len, batchCap) 的注册内存布局，host 与 kernel 按同一公式计算。
struct CaseLayout {
    uint64_t writeBase;
    uint64_t readBase;
    uint64_t notifyBase;
    uint64_t flagsBase;
    uint64_t totalBytes;
};

// ASC 统一语言下 host / kernel 共用函数须双限定（单 __aicore__ 则 host 侧调不到）。
__host__ __aicore__ inline CaseLayout CaseLayoutOf(uint64_t len, uint32_t batchCap)
{
    const uint64_t window = static_cast<uint64_t>(batchCap) * len;
    CaseLayout layout{};
    layout.writeBase = window;
    layout.readBase = layout.writeBase + window;
    layout.notifyBase = layout.readBase + window;
    layout.flagsBase = (layout.notifyBase + window + 7ULL) & ~7ULL; // flag 槽 8B 对齐
    layout.totalBytes = layout.flagsBase + FLAG_STRIDE;
    return layout;
}

constexpr uint32_t WARMUP_METRIC_INDEX = 0U; // timing[apiIdx][0]：失败码（无计时值）
constexpr uint32_t LAT_CYCLES_INDEX = 1U;    // timing[apiIdx][1]：时延档 cycles（各轮之和）
constexpr uint32_t BW_CYCLES_INDEX = 2U;     // timing[apiIdx][2]：带宽档 cycles
constexpr uint32_t TIMING_WORDS = 3U;

// kernel 只上报原始 cycles，均值与派生列由 host 侧计算。
// 拓扑（--perf_type p2p|ring）在 kernel 侧是 send / recv 两对入参，p2p 与 ring-2P 下相等。
struct KernelContext {
    uint64_t channelHandle;     // send 通道（write/notify）
    uint64_t recvChannelHandle; // recv 通道（read）
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;     // send 对端基址（write/notify 的 dst、notify flag）
    uint64_t recvRemoteBufferAddr; // recv 对端基址（read 的 src）
    uint64_t msgBytes;
    uint32_t rounds;
    uint32_t batchCap;
    uint32_t apiMask;
    uint32_t commitInWindow; // 1=BatchCommit 计入时延计时段 / 0=停表后再发
    uint32_t metricMask;
    uint64_t timing[API_IDX_COUNT][TIMING_WORDS]; // [apiIdx][0]=失败码 [1]=lat cycles [2]=bw cycles
    uint32_t drainRet;                            // 最后一次 Drain 返回码（0=成功）
    uint32_t result;                              // KernelResult
};

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_INVALID_ARGS,
    KERNEL_HCOMM_INIT_FAILED,
    KERNEL_UB_OVERFLOW,     // batch 暂存缓冲超 VECOUT UB 预算（kernel 运行期校验）
    KERNEL_BUFFER_OVERFLOW, // (len, batchCap) 窗口超出注册内存（kernel 运行期校验）
    KERNEL_MAKE_HANDLE_FAILED,
    KERNEL_WARMUP_FAILED,
    KERNEL_LAT_POST_FAILED,
    KERNEL_LAT_COMMIT_FAILED,
    KERNEL_LAT_DRAIN_FAILED,
    KERNEL_BW_POST_FAILED,
    KERNEL_BW_COMMIT_FAILED,
    KERNEL_BW_DRAIN_FAILED,
};

inline const char* KernelResultName(KernelResult result)
{
    switch (result) {
        case KERNEL_SUCCESS:
            return "success";
        case KERNEL_INVALID_ARGS:
            return "invalid kernel args";
        case KERNEL_HCOMM_INIT_FAILED:
            return "Hcomm Init failed";
        case KERNEL_UB_OVERFLOW:
            // 预算 = batchCap × 每条WQEBB × 64B ≤ 192KB（notify 占 2 WQEBB，其余 1），缩小 --batch
            return "batch exceeds VECOUT UB budget (reduce --batch)";
        case KERNEL_BUFFER_OVERFLOW:
            // 窗口 = 4 × batchCap × len + 8 ≤ 1GB 注册内存，缩小 --batch 或 --exponent-range
            return "batch window exceeds registered buffer (reduce --batch or --exponent-range)";
        case KERNEL_MAKE_HANDLE_FAILED:
            return "MakeBatchHandle failed";
        case KERNEL_WARMUP_FAILED:
            return "warmup failed";
        case KERNEL_LAT_POST_FAILED:
            // 典型原因：一批 batchCap 条请求须驻留 SQ（notify 占 2 WQEBB），超深度即 Post 失败
            return "latency-window post failed (SQ may be full: reduce --batch)";
        case KERNEL_LAT_COMMIT_FAILED:
            return "latency-window BatchCommit failed";
        case KERNEL_LAT_DRAIN_FAILED:
            return "latency-window Drain failed";
        case KERNEL_BW_POST_FAILED:
            return "bandwidth-window post failed";
        case KERNEL_BW_COMMIT_FAILED:
            return "bandwidth-window BatchCommit failed";
        case KERNEL_BW_DRAIN_FAILED:
            return "bandwidth-window Drain failed";
        default:
            return "unknown kernel error";
    }
}

} // namespace UrmaBatchApiPerfExample

#endif // EXAMPLES_HCOMM_URMA_BATCH_API_PERF_DEF_H
