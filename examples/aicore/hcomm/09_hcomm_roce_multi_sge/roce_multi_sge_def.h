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
 * \file roce_multi_sge_def.h
 * \brief Constants shared by the Hcomm RoCE multi-SGE batch read/write example host and device code.
 */

#ifndef HCOMM_ROCE_MULTI_SGE_DEF_H
#define HCOMM_ROCE_MULTI_SGE_DEF_H

#include <cstdint>

namespace HcommRoceMultiSge {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;
constexpr uint32_t PARTICIPANT_RANK_NUM = 2U;

constexpr uint32_t CALL_COUNT = 128U;
constexpr uint32_t SGE_COUNT = 8U;
constexpr uint32_t SEGMENT_SIZE = 64U;
constexpr uint32_t SEGMENT_STRIDE = 128U;
constexpr uint32_t MESSAGE_SIZE = SGE_COUNT * SEGMENT_SIZE;
constexpr uint32_t WQEBB_SIZE = 64U;
// One multi-SGE WQE contains a 48-byte SQE and eight 16-byte SGEs. Its 176-byte
// size is padded to three WQEBBs.
constexpr uint32_t WQEBB_PER_CALL = 3U;
constexpr uint32_t BATCH_BUFFER_SIZE = CALL_COUNT * WQEBB_PER_CALL * WQEBB_SIZE;

constexpr uint64_t SOURCE_SCATTERED_OFFSET = 0U;
constexpr uint64_t SCATTERED_SPAN = static_cast<uint64_t>(CALL_COUNT) * SGE_COUNT * SEGMENT_STRIDE;
constexpr uint64_t CONTIGUOUS_SPAN = static_cast<uint64_t>(CALL_COUNT) * SGE_COUNT * SEGMENT_SIZE;
constexpr uint64_t SOURCE_CONTIGUOUS_OFFSET = SOURCE_SCATTERED_OFFSET + SCATTERED_SPAN;
constexpr uint64_t WRITE_DST_OFFSET = SOURCE_CONTIGUOUS_OFFSET + CONTIGUOUS_SPAN;
constexpr uint64_t READ_DST_OFFSET = WRITE_DST_OFFSET + CONTIGUOUS_SPAN;
constexpr uint64_t COMM_BUFFER_SIZE = READ_DST_OFFSET + SCATTERED_SPAN;

constexpr char MEMORY_TAG[] = "hcomm_roce_multi_sge_buf";

enum KernelOperation : uint32_t {
    KERNEL_WRITE = 0U,
    KERNEL_READ = 1U,
};

enum KernelResult : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_MAKE_BATCH_HANDLE_FAILED,
    KERNEL_WRITE_FAILED,
    KERNEL_READ_FAILED,
    KERNEL_COMMIT_FAILED,
    KERNEL_DRAIN_FAILED,
};

struct KernelContext {
    uint64_t channel;
    uint64_t localBuffer;
    uint64_t remoteBuffer;
    uint32_t operation;
    KernelResult result;
    int32_t hcommResult;
};

} // namespace HcommRoceMultiSge

#endif // HCOMM_ROCE_MULTI_SGE_DEF_H
