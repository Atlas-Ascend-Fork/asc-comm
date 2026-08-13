/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_HCOMM_BATCH_WRITE_HCOMM_BATCH_WRITE_DEF_H
#define EXAMPLES_HCOMM_BATCH_WRITE_HCOMM_BATCH_WRITE_DEF_H

#include <cstdint>

namespace HcommBatchWriteExample {

constexpr uint32_t RANK_NUM = 2U;
constexpr uint32_t CHANNEL_NUM = 2U;
constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t COMM_BUFFER_SIZE = 4096U;
constexpr uint64_t SEND_OFFSET_0 = 0U;
constexpr uint64_t SEND_OFFSET_1 = DATA_SIZE;
constexpr uint64_t RECV_OFFSET_0 = 2U * DATA_SIZE;
constexpr uint64_t RECV_OFFSET_1 = 3U * DATA_SIZE;
constexpr uint32_t BATCH_BUFFER_SIZE = 512U;
constexpr char MEMORY_TAG[] = "asccomm_batch_write";
constexpr char SHARED_QUEUE_TAG[] = "asccomm_batch_write_queue";

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
    uint64_t remoteBuffer;
    KernelResult result;
};

} // namespace HcommBatchWriteExample

#endif // EXAMPLES_HCOMM_BATCH_WRITE_HCOMM_BATCH_WRITE_DEF_H
