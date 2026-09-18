/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. Please make sure you use this file only in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software directory for the full text of the License.
 */

#ifndef EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
#define EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H

#include <cstdint>

namespace simt_jetty {

constexpr uint64_t kSlotBytes = sizeof(uint64_t);
constexpr uint32_t kSlotCount = 12U;
constexpr uint64_t kBufferBytes = kSlotCount * kSlotBytes;

#if defined(__CCE_KT_TEST__) || defined(__DAV_C310__) || defined(__NPU_ARCH__)
#define SIMT_JETTY_DEVICE_FN __aicore__
#else
#define SIMT_JETTY_DEVICE_FN
#endif

SIMT_JETTY_DEVICE_FN constexpr uint64_t SlotValue(uint32_t slot)
{
    return static_cast<uint64_t>(slot) + 1U;
}

} // namespace simt_jetty

#endif // EXAMPLES_SIMT_JETTY_SIMT_JETTY_COMMON_H
