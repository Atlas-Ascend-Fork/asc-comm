/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_RW_DEF_H
#define HCOMM_RW_DEF_H

#include <cstdint>

#include "adv_api/hcomm/hcomm.h"
#include "hccl/hccl.h"

// Host侧流程的返回值与错误检查宏（原TCP环工具utils.h中仅剩的被使用部分）。
constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t COMM_BUF_SIZE = 4096U;
constexpr uint64_t SEND_DATA_OFFSET = 0U;
constexpr uint64_t WRITE_RESULT_OFFSET = DATA_SIZE;
constexpr uint64_t READ_RESULT_OFFSET = 2U * DATA_SIZE;

constexpr uint32_t NRANKS = 2U;
constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_UB_CTP;
constexpr AscendC::CommProtocol KERNEL_COMM_PROTOCOL = AscendC::COMM_PROTOCOL_UB_CTP;
static_assert(
    static_cast<int32_t>(HOST_COMM_PROTOCOL) == static_cast<int32_t>(KERNEL_COMM_PROTOCOL),
    "Host and Kernel communication protocols must match");
constexpr uint32_t HCOMM_WORKSPACE_SIZE = 512U;

enum TestResult : uint32_t {
    TEST_SUCCESS = 0U,
    TEST_HCOMM_INIT_FAILED = 1U,
    TEST_WRITE_FAILED = 2U,
    TEST_READ_FAILED = 3U,
    TEST_DRAIN_FAILED = 4U,
};
static_assert(sizeof(TestResult) == sizeof(uint32_t), "TestResult must remain uint32_t-sized");

constexpr uint32_t HCOMM_READ_ONLY = 0x01U;
constexpr uint32_t HCOMM_WRITE_ONLY = 0x02U;
constexpr uint32_t HCOMM_READ_WRITE = HCOMM_READ_ONLY | HCOMM_WRITE_ONLY;

namespace HcommExample {

struct CommContext {
    uint64_t channelHandle;
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;
    TestResult testResult;
};

} // namespace HcommExample

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_read_nbi(GM_ADDR context);

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_write_nbi(GM_ADDR context);

extern "C" __vector__ __global__ __aicore__ void kernel_hcomm_write_read_nbi(GM_ADDR context);

#endif // HCOMM_RW_DEF_H
