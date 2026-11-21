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
 * \file simt_jetty_common.h
 * \brief Constants shared by the SIMT Jetty example host and device code
 */

#ifndef EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
#define EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H

#include <cstdint>

namespace simt_jetty {

constexpr uint64_t SLOT_BYTES = sizeof(uint64_t);
constexpr uint32_t SLOT_COUNT = 12U;
constexpr uint64_t BUFFER_BYTES = SLOT_COUNT * SLOT_BYTES;

#ifndef SIMT_JETTY_SGE_NUM
#define SIMT_JETTY_SGE_NUM 12
#endif

static_assert(
    SIMT_JETTY_SGE_NUM >= 1 && SIMT_JETTY_SGE_NUM <= static_cast<int>(SLOT_COUNT),
    "SIMT_JETTY_SGE_NUM must be in the range [1, 12]");
constexpr uint32_t CONFIGURED_SGE_NUM = static_cast<uint32_t>(SIMT_JETTY_SGE_NUM);

// slot_value is host-only: the kernels move bytes via SGEs and never evaluate it, so it must stay
// a plain host constexpr. The ASC driver also sema-checks the .asc host TU with the device arch
// macros in scope, where an __aicore__-qualified slot_value would reject the host calls below.
constexpr uint64_t slot_value(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// ---------------------------------------------------------------------------------------------
// Modes
//
// thread/dispatch固定写入全部SLOT_COUNT个slot；warp-pad只写slot 0；group/warp/mixed按
// 编译期SGE数量写入。Host侧据此构造期望值表，因此该映射必须与kernel侧一致。
constexpr uint32_t MODE_THREAD = 0U;   // 单线程构造12个有效SGE的WQE，按2-SGE分段独立发布
constexpr uint32_t MODE_WARP_PAD = 1U; // 1个有效SGE加零长度SGE，验证NOP填充路径
constexpr uint32_t MODE_GROUP = 2U;    // 多warp共享batch状态，group leader统一提交
constexpr uint32_t MODE_DISPATCH = 3U; // 每个warp各预留一个WQE，block统一提交实际数量
constexpr uint32_t MODE_WARP = 4U;     // warp协作构造WQE
constexpr uint32_t MODE_MIXED = 5U;    // SIMT VF构造SQ内容，AIV侧SIMD ring_doorbell()发布

// 接收侧期望被写入的slot数：写入范围之外的slot保持清零并一并校验。
constexpr uint32_t expected_slot_count(uint32_t mode)
{
    if (mode == MODE_THREAD || mode == MODE_DISPATCH) {
        return SLOT_COUNT;
    }
    if (mode == MODE_WARP_PAD) {
        return 1U;
    }
    if (mode == MODE_GROUP || mode == MODE_WARP || mode == MODE_MIXED) {
        return CONFIGURED_SGE_NUM;
    }
    return 2U;
}

// ---------------------------------------------------------------------------------------------
// Timing buffer layout (--timing模式下kernel写回的设备侧计时结果)
constexpr uint32_t WRITE_CYCLES_INDEX = 0U;
constexpr uint32_t COMMIT_CYCLES_INDEX = 1U;
constexpr uint32_t COMPLETION_CYCLES_INDEX = 2U;
constexpr uint32_t COMPLETED_INDEX = 3U;
constexpr uint32_t TIMING_WORDS = 4U;

} // namespace simt_jetty

#endif // EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
