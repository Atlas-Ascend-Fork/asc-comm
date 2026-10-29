/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RUNTIME_CCURESOURCEENCODING_H
#define CCU_RUNTIME_CCURESOURCEENCODING_H

#include <cstdint>

namespace ccu::runtime {

// HComm CcuBuf IDs use bit 15 to select the IO die. RegisterContext POD
// ranges expose the die-local MS startId, so AOT must restore this encoding
// before applying an MS relocation to device microcode.
constexpr bool encodeCcuBufId(uint32_t startId, uint32_t dieId, uint32_t& encodedId)
{
    constexpr uint32_t kDieIdBit = 0x8000u;
    if (startId >= kDieIdBit || dieId > 1u)
        return false;
    encodedId = startId | (dieId * kDieIdBit);
    return true;
}

} // namespace ccu::runtime

#endif // CCU_RUNTIME_CCURESOURCEENCODING_H
