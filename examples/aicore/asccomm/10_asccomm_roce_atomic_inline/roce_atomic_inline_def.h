/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file roce_atomic_inline_def.h
 * \brief Constants shared by the Hcomm RoCE atomic/inline example host and device code.
 */

#ifndef HCOMM_ROCE_ATOMIC_INLINE_DEF_H
#define HCOMM_ROCE_ATOMIC_INLINE_DEF_H

#include <cstdint>

#include "hccl/hccl.h"

namespace HcommRoceAtomicExample {

constexpr int32_t SUCCESS = 0;
constexpr int32_t FAIL = -1;

constexpr uint64_t COMM_BUF_SIZE = 4096U;
constexpr uint64_t FAA_VALUE_OFFSET = 0U;
constexpr uint64_t FAA_FETCH_OFFSET = 8U;
constexpr uint64_t CAS_VALUE_OFFSET = 16U;
constexpr uint64_t CAS_FETCH_OFFSET = 24U;
constexpr uint64_t INLINE_VALUE_OFFSET = 32U;

constexpr uint64_t FAA_ADD_VALUE = 100U;
constexpr uint64_t CAS_COMPARE_VALUE = 200U;
constexpr uint64_t CAS_SWAP_VALUE = 300U;
constexpr uint64_t INLINE_WRITE_VALUE = 400U;

constexpr uint32_t HCOMM_WORKSPACE_SIZE = 512U;

inline uint32_t PeerRank(uint32_t rank, uint32_t rankNum) { return (rank + rankNum / 2U) % rankNum; }

enum TestResult : uint32_t {
    TEST_SUCCESS = 0U,
    TEST_HCOMM_INIT_FAILED = 1U,
    TEST_HCOMM_ATOMIC_FAA_FAILED = 2U,
    TEST_HCOMM_ATOMIC_CAS_FAILED = 3U,
    TEST_HCOMM_WRITE_VALUE_FAILED = 4U,
    TEST_HCOMM_COMMIT_FAILED = 5U,
    TEST_HCOMM_DRAIN_FAILED = 6U,
};

struct CommContext {
    uint64_t channelHandle;
    uint64_t localBufferAddr;
    uint64_t remoteBufferAddr;
    TestResult testResult;
};

} // namespace HcommRoceAtomicExample

#endif // HCOMM_ROCE_ATOMIC_INLINE_DEF_H
