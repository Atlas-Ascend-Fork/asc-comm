/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_HCOMM_ROCE_BATCH_WRITE_READ_DEF_H
#define EXAMPLES_HCOMM_ROCE_BATCH_WRITE_READ_DEF_H

#include <cstdint>

namespace HcommRoceBatchWriteReadExample {

constexpr uint32_t kOperationCount = 1000U;
constexpr uint32_t kTransferBytes = 64U;
constexpr uint64_t kSegmentBytes = static_cast<uint64_t>(kOperationCount) * kTransferBytes;
constexpr uint64_t kSendOffset = 0U;
constexpr uint64_t kWriteResultOffset = kSegmentBytes;
constexpr uint64_t kReadResultOffset = 2U * kSegmentBytes;
constexpr uint64_t kCommBufferBytes = 3U * kSegmentBytes;

// RoCE batch WQEs occupy one 64-byte basic block. The batch contains 1000 writes and 1000 reads.
constexpr uint32_t kRoceWqebbBytes = 64U;
constexpr uint32_t kBatchWqeCount = 2U * kOperationCount;
constexpr uint32_t kBatchBufferBytes = kBatchWqeCount * kRoceWqebbBytes;

constexpr char kMemoryTag[] = "asccomm_roce_batch_write_read";

// The host and device exchange only fixed-width fields in this structure.
enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_MAKE_HANDLE_FAILED,
    KERNEL_WRITE_FAILED,
    KERNEL_READ_FAILED,
    KERNEL_COMMIT_FAILED,
    KERNEL_DRAIN_FAILED,
};

struct KernelContext {
    uint64_t channelHandle;
    uint64_t localBuffer;
    uint64_t remoteBuffer;
    KernelResult result;
    int32_t hcommResult;
};

inline uint8_t DataValue(uint32_t rank, uint32_t slot, uint32_t byte)
{
    return static_cast<uint8_t>((rank * 37U + slot * 11U + byte) & 0xffU);
}

} // namespace HcommRoceBatchWriteReadExample

#endif // EXAMPLES_HCOMM_ROCE_BATCH_WRITE_READ_DEF_H
