/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#ifndef EXAMPLES_HCOMM_ROCE_BATCH_API_PERF_DEF_H
#define EXAMPLES_HCOMM_ROCE_BATCH_API_PERF_DEF_H

#include <cstdint>

#include "hccl/hccl.h"

namespace HcommRoceBatchApiPerfExample {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

constexpr uint32_t DEFAULT_BATCH_SIZE = 10U;
constexpr uint32_t MAX_BATCH_SIZE = 3U * 1024U;
constexpr uint64_t MIN_DATA_BYTES = 256U;
constexpr uint64_t MAX_DATA_BYTES = 1ULL << 28U;
constexpr uint64_t ROCE_ADDRESS_ALIGNMENT_BYTES = 64U;
constexpr uint64_t MIN_COMM_BUFFER_BYTES = 4096U;
constexpr uint64_t VALIDATION_BYTES_PER_SLOT = 256U;

#define GET_ROCE_ALIGNED_BYTES(bytes) \
    (((bytes) + ROCE_ADDRESS_ALIGNMENT_BYTES - 1U) / ROCE_ADDRESS_ALIGNMENT_BYTES * ROCE_ADDRESS_ALIGNMENT_BYTES)
#define GET_ROCE_SLOT_STRIDE(dataBytes) GET_ROCE_ALIGNED_BYTES(dataBytes)
#define GET_ROCE_PAYLOAD_SPAN_BYTES(dataBytes, batchSize) \
    (static_cast<uint64_t>(batchSize) * GET_ROCE_SLOT_STRIDE(dataBytes))

constexpr uint32_t ROCE_WQEBB_BYTES = 64U;
constexpr uint32_t BATCH_BUFFER_BYTES = MAX_BATCH_SIZE * ROCE_WQEBB_BYTES;
constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_ROCE;

constexpr char MEMORY_TAG[] = "asccomm_roce_batch_api_perf";

enum PerfApi : uint32_t {
    PERF_API_WRITE = 0U,
    PERF_API_READ = 1U,
};

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_MAKE_HANDLE_FAILED,
    KERNEL_ADDRESS_UNALIGNED,
};

struct KernelContext {
    uint64_t channelHandle;
    uint64_t localSendBuffer;
    uint64_t localRecvBuffer;
    uint64_t remoteSendBuffer;
    uint64_t remoteRecvBuffer;
    uint32_t api;
    uint32_t dataSize;
    uint32_t batchSize;
    uint64_t issueCycles;
    uint64_t commitCycles;
    uint64_t drainCycles;
    uint64_t totalCycles;
    KernelResult result;
};

constexpr uint32_t WARMUP_RUN_COUNT = 10U;
constexpr uint32_t MEASURED_RUN_COUNT = 100U;
constexpr uint32_t TOTAL_RUN_COUNT = WARMUP_RUN_COUNT + MEASURED_RUN_COUNT;

inline uint8_t DataValue(uint32_t rank, uint64_t offset)
{
    return static_cast<uint8_t>(0xA5U + rank + (offset & 0xFFU));
}

} // namespace HcommRoceBatchApiPerfExample

#endif // EXAMPLES_HCOMM_ROCE_BATCH_API_PERF_DEF_H
