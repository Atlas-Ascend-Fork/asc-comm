/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_CURSOR_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_CURSOR_H
#include <cstdint>
namespace AscendC::HcommDfx {
inline bool ResolveAivSampleTail(uint32_t head, uint32_t completed, uint32_t consumedCq, uint32_t depth, uint32_t& tail)
{
    if (depth == 0U || depth > 65536U) {
        return false;
    }
    if (consumedCq == 0U) {
        tail = 0U;
        // Initial state is indistinguishable from a wrapped CQ counter; reject implausible ranges.
        return head <= depth;
    }
    const uint32_t pending = static_cast<uint16_t>(head - (completed + 1U));
    tail = head - pending;
    return pending < depth;
}
} // namespace AscendC::HcommDfx
#endif
