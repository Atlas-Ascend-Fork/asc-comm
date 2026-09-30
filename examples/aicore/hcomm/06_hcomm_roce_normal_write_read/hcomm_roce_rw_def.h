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
 * \file hcomm_roce_rw_def.h
 * \brief Constants shared by the Hcomm RoCE normal write/read example host and device code.
 */

#ifndef HCOMM_ROCE_RW_DEF_H
#define HCOMM_ROCE_RW_DEF_H

#include <cstdint>

#include "hccl/hccl.h"

namespace HcommRoceExample {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

constexpr uint32_t DATA_SIZE = 256U;
constexpr uint64_t COMM_BUF_SIZE = 4096U;
constexpr uint64_t SEND_DATA_OFFSET = 0U;
constexpr uint64_t WRITE_RESULT_OFFSET = DATA_SIZE;
constexpr uint64_t READ_RESULT_OFFSET = 2U * DATA_SIZE;

constexpr CommProtocol HOST_COMM_PROTOCOL = COMM_PROTOCOL_ROCE;

constexpr uint32_t HCOMM_WORKSPACE_SIZE = 512U;

enum TestResult : uint32_t {
    TEST_SUCCESS = 0U,
    TEST_HCOMM_INIT_FAILED = 1U,
    TEST_HCOMM_WRITE_FAILED = 2U,
    TEST_HCOMM_READ_FAILED = 3U,
    TEST_HCOMM_COMMIT_FAILED = 4U,
    TEST_HCOMM_DRAIN_FAILED = 5U,
    TEST_HCOMM_LOCK_FAILED = 6U,
    TEST_HCOMM_UNLOCK_FAILED = 7U,
};

struct CommContext {
    uint64_t channelHandle;
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;
    TestResult testResult;
};

} // namespace HcommRoceExample

#endif // HCOMM_ROCE_RW_DEF_H
