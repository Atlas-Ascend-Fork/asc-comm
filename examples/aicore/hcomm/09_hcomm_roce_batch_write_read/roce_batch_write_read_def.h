/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file roce_batch_write_read_def.h
 * \brief Constants shared by the Hcomm RoCE batch write/read example host and device code.
 */

#ifndef EXAMPLES_HCOMM_ROCE_BATCH_WRITE_READ_DEF_H
#define EXAMPLES_HCOMM_ROCE_BATCH_WRITE_READ_DEF_H

#include <cstdint>

#include "hccl/hccl.h"

namespace HcommRoceBatchWriteReadExample {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

constexpr uint32_t OPERATION_COUNT = 1000U;
constexpr uint32_t TRANSFER_BYTES = 64U;
constexpr uint64_t SEGMENT_BYTES = static_cast<uint64_t>(OPERATION_COUNT) * TRANSFER_BYTES;
constexpr uint64_t COMM_BUF_SIZE = 3U * SEGMENT_BYTES;
constexpr uint64_t SEND_DATA_OFFSET = 0U;
constexpr uint64_t WRITE_RESULT_OFFSET = SEGMENT_BYTES;
constexpr uint64_t READ_RESULT_OFFSET = 2U * SEGMENT_BYTES;

constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_ROCE;

// RoCE batch WQEs occupy one 64-byte basic block. The batch contains 1000 writes and 1000 reads.
constexpr uint32_t ROCE_WQEBB_BYTES = 64U;
constexpr uint32_t BATCH_WQE_COUNT = 2U * OPERATION_COUNT;
constexpr uint32_t BATCH_BUFFER_BYTES = BATCH_WQE_COUNT * ROCE_WQEBB_BYTES;

constexpr char MEMORY_TAG[] = "asccomm_roce_batch_write_read";

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
