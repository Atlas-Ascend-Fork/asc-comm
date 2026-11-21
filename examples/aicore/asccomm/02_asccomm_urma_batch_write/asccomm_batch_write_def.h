/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef EXAMPLES_ASCCOMM_BATCH_WRITE_DEF_H
#define EXAMPLES_ASCCOMM_BATCH_WRITE_DEF_H

#include <cstdint>

#include "utils/kernel_result.h"

namespace asccomm_batch_write_example {

constexpr uint32_t DEFAULT_RANK_NUM = 2U;
constexpr uint32_t MAX_RANK_NUM = 16U;
constexpr uint32_t MAX_CHANNEL_NUM = MAX_RANK_NUM - 1U;
constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t SEND_OFFSET = 0U;
constexpr uint64_t RECV_BASE_OFFSET = DATA_SIZE;
constexpr uint32_t BATCH_BUFFER_SIZE = 2048U;
constexpr char MEMORY_TAG[] = "asccomm_batch_write";
constexpr char SHARED_QUEUE_TAG_PREFIX[] = "asccomm_batch_write_queue";

// host下发给kernel的批量写上下文。
struct batch_context {
    uint64_t multi_channel;                   // 多通道句柄
    uint64_t local_buffer;                    // 本端通信buffer地址
    uint64_t remote_buffers[MAX_CHANNEL_NUM]; // 各逻辑通道的对端buffer地址
    uint32_t channel_num;                     // 逻辑通道数
    kernel_result result;                     // kernel执行结果
    int32_t asccomm_result;                   // asccomm接口返回码
};

} // namespace asccomm_batch_write_example

#endif // EXAMPLES_ASCCOMM_BATCH_WRITE_DEF_H
