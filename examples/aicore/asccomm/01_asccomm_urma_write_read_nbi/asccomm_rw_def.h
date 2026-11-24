/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RW_DEF_H
#define ASCCOMM_RW_DEF_H

#include <cstdint>

#include "asccomm/asccomm.h"
#include "hccl/hccl.h"

// 流程返回值：0=成功，-1=失败。
constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

// 通信buffer按DATA_SIZE分段：SEND区存放本端数据（被对端read_nbi读取），
// WRITE区存放对端write_nbi写入的结果，READ区存放本端read_nbi取回的结果。
constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t COMM_BUF_SIZE = 4096U;
constexpr uint64_t SEND_DATA_OFFSET = 0U;
constexpr uint64_t WRITE_RESULT_OFFSET = DATA_SIZE;
constexpr uint64_t READ_RESULT_OFFSET = 2U * DATA_SIZE;

// host与kernel两侧的通信协议必须一致。
constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_UB_CTP;
constexpr asc::CommProtocol KERNEL_COMM_PROTOCOL = asc::COMM_PROTOCOL_UB_CTP;
static_assert(
    static_cast<int32_t>(HOST_COMM_PROTOCOL) == static_cast<int32_t>(KERNEL_COMM_PROTOCOL),
    "Host and Kernel communication protocols must match");
// asccomm_init所需的UB工作区大小。
constexpr uint32_t ASCCOMM_WORKSPACE_SIZE = 512U;

#include "utils/kernel_result.h"

// kernel操作类型位掩码：只读、只写、读写组合。
constexpr uint32_t ASCCOMM_READ_ONLY = 0x01U;
constexpr uint32_t ASCCOMM_WRITE_ONLY = 0x02U;
constexpr uint32_t ASCCOMM_READ_WRITE = ASCCOMM_READ_ONLY | ASCCOMM_WRITE_ONLY;

namespace asccomm_example {

// host下发给kernel的per-peer上下文：kernel据此取通道与两端buffer地址，并把执行结果写回test_result。
struct comm_context {
    uint64_t channel_handle;     // 与该peer的通信通道
    uint64_t local_buffer_addr;  // 本端通信buffer基址
    uint64_t remote_buffer_addr; // 对端通信buffer基址
    kernel_result test_result;   // kernel回写的执行结果
};

} // namespace asccomm_example

// 只读kernel入口：对next做一次read_nbi。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_read_nbi(GM_ADDR context);

// 只写kernel入口：对next做一次write_nbi。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_write_nbi(GM_ADDR context);

// 读写组合kernel入口：对next做一次write_nbi与一次read_nbi。
extern "C" __vector__ __global__ __aicore__ void kernel_asccomm_write_read_nbi(GM_ADDR context);

#endif // ASCCOMM_RW_DEF_H
