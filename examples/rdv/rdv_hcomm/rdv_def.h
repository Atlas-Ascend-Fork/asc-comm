/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_RDV_RDV_HCOMM_RDV_DEF_H
#define EXAMPLES_RDV_RDV_HCOMM_RDV_DEF_H

#include <cstdint>

namespace RdvHcommExample {

#ifndef RDV_AIV_CORE_NUM
#define RDV_AIV_CORE_NUM 1U
#endif

constexpr uint32_t DEFAULT_RANK_NUM = 8U;
constexpr uint32_t MAX_RANK_NUM = 128U;
constexpr uint32_t MAX_CHANNEL_NUM = MAX_RANK_NUM - 1U;
constexpr uint32_t AIV_CORE_NUM = RDV_AIV_CORE_NUM;
// Plain single-segment write: each WQE carries one SGE of DATA_SIZE bytes.
constexpr uint32_t DATA_SIZE = 1U << 20;
constexpr uint32_t WQES_PER_PEER_PER_ITERATION = 32U;
constexpr uint32_t PERF_ITERATION_COUNT = 1U;
constexpr uint32_t BATCH_BUFFER_SIZE = 224U * 1024U;
// One SQE (48B) plus one SGE (16B) fits exactly into one 64B WQEBB.
constexpr uint32_t URMA_SQE_CONTEXT_SIZE = 48U;
constexpr uint32_t URMA_SGE_CONTEXT_SIZE = 16U;
constexpr uint32_t URMA_WQE_BB_SIZE = 64U;
constexpr uint32_t WQE_BB_NUM =
    (URMA_SQE_CONTEXT_SIZE + URMA_SGE_CONTEXT_SIZE + URMA_WQE_BB_SIZE - 1U) / URMA_WQE_BB_SIZE;
constexpr uint32_t BATCH_CAPACITY_BB = BATCH_BUFFER_SIZE / URMA_WQE_BB_SIZE;
constexpr uint64_t SEND_BASE_OFFSET = 0U;
constexpr uint64_t RECV_BASE_OFFSET = static_cast<uint64_t>(AIV_CORE_NUM) * DATA_SIZE;
// Endpoint-mode channel setup fills ubAttr.sqDepth with SQ_DEPTH so the depth is explicit.
constexpr uint32_t SQ_DEPTH = 4096U;
constexpr uint16_t HCOMM_LISTEN_PORT = 60001U;
constexpr char MEMORY_TAG[] = "asccomm_rdv_hcomm";
constexpr char CHANNEL_NAME_PREFIX[] = "asccomm_rdv_hcomm_channel";

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_MAKE_BATCH_HANDLE_FAILED,
    KERNEL_GET_HANDLE_REF_FAILED,
    KERNEL_WRITE_FAILED,
    KERNEL_COMMIT_FAILED,
    KERNEL_DRAIN_FAILED,
};

struct BatchContext {
    uint64_t multiChannel;
    uint64_t localBuffer;
    uint64_t remoteBuffers[MAX_CHANNEL_NUM];
    uint32_t peerRanks[MAX_CHANNEL_NUM];
    uint32_t myRank;
    uint32_t channelNum;
    uint32_t wqesPerPeerPerCommit;
    uint32_t sqDepth;
    uint32_t cqDepth;
    KernelResult result;
    int32_t hcommResult;
};

} // namespace RdvHcommExample

#endif // EXAMPLES_RDV_RDV_HCOMM_RDV_DEF_H
