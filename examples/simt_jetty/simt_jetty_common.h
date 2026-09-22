/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
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

#if defined(__CCE_KT_TEST__) || defined(__DAV_C310__) || defined(__NPU_ARCH__)
#define SIMT_JETTY_DEVICE_FN __aicore__
#else
#define SIMT_JETTY_DEVICE_FN
#endif

SIMT_JETTY_DEVICE_FN constexpr uint64_t SlotValue(uint32_t slot) { return static_cast<uint64_t>(slot) + 1U; }

} // namespace simt_jetty

#endif // EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
