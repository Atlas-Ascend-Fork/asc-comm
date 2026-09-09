/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_MICROCODE_OPT_EXTRACT_OPERANDS_H
#define CCU_MICROCODE_OPT_EXTRACT_OPERANDS_H

#include <cstdint>
#include <vector>

#include "hcomm/resource/microcode/ccu_microcode_v1.h"

namespace asc {
namespace ccu_opt {

enum class reg_type : uint8_t {
    xn = 0,  // 64-bit 标量寄存器, 8 路 Bank, 主要优化对象
    ms = 1,  // memory Scratchpad
    cke = 2, // Completion / Kick Event
};

struct reg_operand {
    reg_type type{};
    uint16_t reg_id = 0;
    bool is_def = false; // true = write, false = read

    bool operator==(const reg_operand& other) const
    {
        return type == other.type && reg_id == other.reg_id && is_def == other.is_def;
    }
};

std::vector<reg_operand> extract_operands_v2(const ccu_rep::ccu_instr& instr);

} // namespace ccu_opt
} // namespace asc

#endif // CCU_MICROCODE_OPT_EXTRACT_OPERANDS_H
