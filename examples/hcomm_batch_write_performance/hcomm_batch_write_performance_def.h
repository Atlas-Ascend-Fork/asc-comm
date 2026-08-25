/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_HCOMM_BATCH_WRITE_PERFORMANCE_HCOMM_BATCH_WRITE_PERFORMANCE_DEF_H
#define EXAMPLES_HCOMM_BATCH_WRITE_PERFORMANCE_HCOMM_BATCH_WRITE_PERFORMANCE_DEF_H

#include <cstdint>

namespace HcommBatchWritePerformanceExample {

constexpr uint32_t DEFAULT_RANK_NUM = 2U;
constexpr uint32_t MAX_RANK_NUM = 16U;
constexpr uint32_t MAX_CHANNEL_NUM = MAX_RANK_NUM - 1U;
constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t SEND_OFFSET = 0U;
constexpr uint64_t RECV_BASE_OFFSET = DATA_SIZE;
constexpr uint32_t WQEBB_SIZE = 64U;
constexpr uint32_t BATCH_BUFFER_SIZE = 32U * 1024U;
constexpr uint32_t DEFAULT_READS_PER_BATCH = 500U;
constexpr uint32_t MAX_READS_PER_BATCH = BATCH_BUFFER_SIZE / WQEBB_SIZE;
constexpr uint32_t DEFAULT_WARMUP_BATCHES = 100U;
constexpr uint32_t DEFAULT_MEASURE_BATCHES = 1U;
constexpr uint32_t MAX_BATCH_COUNT = 1000000U;
constexpr uint32_t CYCLES_PER_US = 1000U;
constexpr char MEMORY_TAG[] = "asccomm_batch_write_performance";
constexpr char SHARED_QUEUE_TAG_PREFIX[] = "asccomm_batch_write_performance_queue";

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_READ_FAILED,
    KERNEL_COMMIT_FAILED,
    KERNEL_DRAIN_FAILED,
    KERNEL_INVALID_ARGUMENT,
};

struct BatchContext {
    uint64_t multiChannel;
    uint64_t localBuffer;
    uint64_t remoteBuffers[MAX_CHANNEL_NUM];
    uint64_t elapsedCycles;
    uint64_t completedReads;
    uint32_t channelNum;
    uint32_t readsPerBatch;
    uint32_t warmupBatches;
    uint32_t measureBatches;
    KernelResult result;
    int32_t hcommResult;
};

} // namespace HcommBatchWritePerformanceExample

#endif // EXAMPLES_HCOMM_BATCH_WRITE_PERFORMANCE_HCOMM_BATCH_WRITE_PERFORMANCE_DEF_H
