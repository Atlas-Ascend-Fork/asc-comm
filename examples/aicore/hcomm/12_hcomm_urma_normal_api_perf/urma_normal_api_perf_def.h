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
 * \file urma_normal_api_perf_def.h
 * \brief Definitions shared by the URMA normal API performance example host and device code.
 */

#ifndef EXAMPLES_HCOMM_URMA_NORMAL_API_PERF_DEF_H
#define EXAMPLES_HCOMM_URMA_NORMAL_API_PERF_DEF_H

#include <cstdint>

#include "adv_api/hcomm/hcomm.h"
#include "hccl/hccl.h"

namespace UrmaNormalApiPerfExample {

// ---------------- 注册内存布局（2GB，各档共用，每档重铺初值） ----------------
//   [0, 256MB)       source —— 本卡发送源 / 对端 ReadNbi 的读源
//   之后每 512MB 依次为 write / read / notify 三个落点区，末尾 8B 为 notify flag
constexpr uint64_t MIB = 1ULL << 20;
constexpr uint64_t SOURCE_BASE = 0;
constexpr uint64_t SOURCE_BYTES = 256ULL * MIB;
constexpr uint64_t REGION_BYTES = 512ULL * MIB;
constexpr uint64_t WRITE_BASE = SOURCE_BASE + SOURCE_BYTES;
constexpr uint64_t READ_BASE = WRITE_BASE + REGION_BYTES;
constexpr uint64_t NOTIFY_BASE = READ_BASE + REGION_BYTES;
constexpr uint64_t FLAG_BASE = NOTIFY_BASE + REGION_BYTES;
constexpr uint64_t BUFFER_BYTES = FLAG_BASE + sizeof(uint64_t);

// ---------------- 接口选择（apiMask 位）与计时槽 ----------------
constexpr uint32_t API_WRITE = 1U << 0;  // WriteNbi
constexpr uint32_t API_READ = 1U << 1;   // ReadNbi
constexpr uint32_t API_NOTIFY = 1U << 2; // WriteWithNotifyNbi
constexpr uint32_t API_ALL = API_WRITE | API_READ | API_NOTIFY;
// API 位 → timing 第一维下标：--api all 时各接口的计时写各自的槽，互不覆盖
constexpr uint32_t API_WRITE_IDX = 0U;
constexpr uint32_t API_READ_IDX = 1U;
constexpr uint32_t API_NOTIFY_IDX = 2U;
constexpr uint32_t API_IDX_COUNT = 3U;

// ---------------- 性能口径（metricMask 位） ----------------
constexpr uint32_t METRIC_LAT = 1U << 0; // 时延：计时段内不 Drain
constexpr uint32_t METRIC_BW = 1U << 1;  // 带宽：计时段内含 Drain（端到端）
constexpr uint32_t METRIC_ALL = METRIC_LAT | METRIC_BW;

// ---------------- 运行期维度默认值（命令行可覆盖） ----------------
constexpr uint32_t DEFAULT_ROUNDS = 1000U;
constexpr uint64_t DEFAULT_MIN_EXP = 3; // 数据量 = 2^exp，默认 3 17 即 8B~128KB
constexpr uint64_t DEFAULT_MAX_EXP = 17;
constexpr uint32_t WARMUPS = 100U; // warmup 固定 min(100, rounds) 次

// ---------------- 数据 pattern ----------------
constexpr int32_t SRC_PATTERN_BASE = 100; // source 区填 100+rank（int32）
constexpr uint64_t NOTIFY_VAL_BASE = 300; // notify flag 固定值（单槽并发写，只校验 == 常量）

constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_UB_CTP;
static_assert(
    static_cast<int32_t>(HOST_COMM_PROTOCOL) == static_cast<int32_t>(AscendC::COMM_PROTOCOL_UB_CTP),
    "Host and Kernel communication protocols must match");

constexpr uint32_t WARMUP_METRIC_INDEX = 0U; // timing[apiIdx][0]：失败码（无计时值）
constexpr uint32_t LAT_CYCLES_INDEX = 1U;    // timing[apiIdx][1]：时延档 cycles
constexpr uint32_t BW_CYCLES_INDEX = 2U;     // timing[apiIdx][2]：带宽档 cycles
constexpr uint32_t TIMING_WORDS = 3U;

// kernel 只上报原始 cycles，均值与派生列由 host 侧计算。
// 拓扑（--perf_type p2p|ring）在 kernel 侧是 send / recv 两对入参，p2p 与 ring-2P 下相等。
struct KernelContext {
    uint64_t channelHandle;     // send 通道（write/notify）
    uint64_t recvChannelHandle; // recv 通道（read）
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;     // send 对端基址（write/notify 的 dst）
    uint64_t recvRemoteBufferAddr; // recv 对端基址（read 的 src）
    uint64_t msgBytes;
    uint32_t rounds;
    uint32_t apiMask;
    uint32_t commitInline; // 1=inline（commit=true）/ 0=final（commit=false + 末尾统一 Commit）
    uint32_t metricMask;
    uint64_t timing[API_IDX_COUNT][TIMING_WORDS]; // [apiIdx][0]=失败码 [1]=lat cycles [2]=bw cycles
    uint32_t drainRet;                            // 最后一次 Drain 返回码（0=成功）
    uint32_t result;                              // KernelResult
};

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_INVALID_ARGS,
    KERNEL_HCOMM_INIT_FAILED,
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
        case KERNEL_WARMUP_FAILED:
            return "warmup failed";
        case KERNEL_LAT_POST_FAILED:
            // 典型原因：时延档计时段内不 Drain，rounds 个请求须全部驻留 SQ，超深度即 Post 失败
            return "latency-window post failed (SQ may be full: reduce --rounds)";
        case KERNEL_LAT_COMMIT_FAILED:
            return "latency-window Commit failed";
        case KERNEL_LAT_DRAIN_FAILED:
            return "latency-window Drain failed";
        case KERNEL_BW_POST_FAILED:
            return "bandwidth-window post failed";
        case KERNEL_BW_COMMIT_FAILED:
            return "bandwidth-window Commit failed";
        case KERNEL_BW_DRAIN_FAILED:
            return "bandwidth-window Drain failed";
        default:
            return "unknown kernel error";
    }
}

} // namespace UrmaNormalApiPerfExample

#endif // EXAMPLES_HCOMM_URMA_NORMAL_API_PERF_DEF_H
