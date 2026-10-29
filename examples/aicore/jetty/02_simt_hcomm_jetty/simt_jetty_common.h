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

constexpr uint64_t kSlotBytes = sizeof(uint64_t);
constexpr uint32_t kSlotCount = 12U;
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

#ifndef SIMT_JETTY_SGE_NUM
#define SIMT_JETTY_SGE_NUM 12
#endif

static_assert(
    SIMT_JETTY_SGE_NUM >= 1 && SIMT_JETTY_SGE_NUM <= static_cast<int>(kSlotCount),
    "SIMT_JETTY_SGE_NUM must be in the range [1, 12]");
constexpr uint32_t kConfiguredSgeNum = static_cast<uint32_t>(SIMT_JETTY_SGE_NUM);

// SlotValue is host-only: the kernels move bytes via SGEs and never evaluate it, so it must stay
// a plain host constexpr. The ASC driver also sema-checks the .asc host TU with the device arch
// macros in scope, where an __aicore__-qualified SlotValue would reject the host calls below.
constexpr uint64_t SlotValue(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

// ---------------------------------------------------------------------------------------------
// Modes
//
// thread/dispatch固定写入全部kSlotCount个slot；warp-pad只写slot 0；group/warp/mixed按
// 编译期SGE数量写入。Host侧据此构造期望值表，因此该映射必须与kernel侧一致。
constexpr uint32_t kModeThread = 0U;   // 单线程构造12个有效SGE的WQE，按2-SGE分段独立发布
constexpr uint32_t kModeWarpPad = 1U;  // 1个有效SGE加零长度SGE，验证NOP填充路径
constexpr uint32_t kModeGroup = 2U;    // 多warp共享batch状态，group leader统一提交
constexpr uint32_t kModeDispatch = 3U; // 每个warp各预留一个WQE，block统一提交实际数量
constexpr uint32_t kModeWarp = 4U;     // warp协作构造WQE
constexpr uint32_t kModeMixed = 5U;    // SIMT VF构造SQ内容，AIV侧SIMD RingDoorbell()发布

// 接收侧期望被写入的slot数：写入范围之外的slot保持清零并一并校验。
constexpr uint32_t ExpectedSlotCount(uint32_t mode)
{
    if (mode == kModeThread || mode == kModeDispatch) {
        return kSlotCount;
    }
    if (mode == kModeWarpPad) {
        return 1U;
    }
    if (mode == kModeGroup || mode == kModeWarp || mode == kModeMixed) {
        return kConfiguredSgeNum;
    }
    return 2U;
}

// ---------------------------------------------------------------------------------------------
// Timing buffer layout (--timing模式下kernel写回的设备侧计时结果)
constexpr uint32_t kWriteCyclesIndex = 0U;
constexpr uint32_t kCommitCyclesIndex = 1U;
constexpr uint32_t kCompletionCyclesIndex = 2U;
constexpr uint32_t kCompletedIndex = 3U;
constexpr uint32_t kTimingWords = 4U;

} // namespace simt_jetty

#endif // EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
