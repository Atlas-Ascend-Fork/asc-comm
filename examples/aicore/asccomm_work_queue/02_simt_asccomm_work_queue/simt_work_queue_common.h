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
 * \file simt_work_queue_common.h
 * \brief Constants shared by the SIMT Jetty example host and device code
 */

#ifndef EXAMPLES_SIMT_WORK_QUEUE_SIMT_WORK_QUEUE_COMMON_H
#define EXAMPLES_SIMT_WORK_QUEUE_SIMT_WORK_QUEUE_COMMON_H

#include <cstdint>

namespace simt_work_queue {

constexpr uint64_t SLOT_BYTES = sizeof(uint64_t);
constexpr uint32_t SLOT_COUNT = 12U;
constexpr uint64_t BUFFER_BYTES = SLOT_COUNT * SLOT_BYTES;

#ifndef SIMT_WORK_QUEUE_SGE_NUM
#define SIMT_WORK_QUEUE_SGE_NUM 12
#endif

static_assert(
    SIMT_WORK_QUEUE_SGE_NUM >= 1 && SIMT_WORK_QUEUE_SGE_NUM <= static_cast<int>(SLOT_COUNT),
    "SIMT_WORK_QUEUE_SGE_NUM must be in the range [1, 12]");
constexpr uint32_t CONFIGURED_SGE_NUM = static_cast<uint32_t>(SIMT_WORK_QUEUE_SGE_NUM);

// slot_value is host-only: the kernels move bytes via SGEs and never evaluate it, so it must stay
// a plain host constexpr. The ASC driver also sema-checks the .asc host TU with the device arch
// macros in scope, where an __aicore__-qualified slot_value would reject the host calls below.
constexpr uint64_t slot_value(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// ---------------------------------------------------------------------------------------------
// Modes
//
// 两种模式都由一个warp协作提交，并按编译期SGE数量写入相同长度的数据：
// write使用连续源内存的普通write接口，write-sge使用多SGE接口。
constexpr uint32_t MODE_WRITE = 0U;     // warp调用普通连续write
constexpr uint32_t MODE_WRITE_SGE = 1U; // warp调用多SGE write

// 接收侧期望被写入的slot数：写入范围之外的slot保持清零并一并校验。
constexpr uint32_t expected_slot_count(uint32_t mode)
{
    return mode == MODE_WRITE || mode == MODE_WRITE_SGE ? CONFIGURED_SGE_NUM : 0U;
}

// ---------------------------------------------------------------------------------------------
// Timing buffer layout (--timing模式下kernel写回的设备侧计时结果)
constexpr uint32_t WRITE_CYCLES_INDEX = 0U;
constexpr uint32_t COMMIT_CYCLES_INDEX = 1U;
constexpr uint32_t COMPLETION_CYCLES_INDEX = 2U;
constexpr uint32_t COMPLETED_INDEX = 3U;
constexpr uint32_t TIMING_WORDS = 4U;

} // namespace simt_work_queue

#endif // EXAMPLES_SIMT_WORK_QUEUE_SIMT_WORK_QUEUE_COMMON_H
