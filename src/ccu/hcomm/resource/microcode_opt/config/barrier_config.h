/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_MICROCODE_OPT_BARRIER_CONFIG_H
#define CCU_MICROCODE_OPT_BARRIER_CONFIG_H

#include <cstdint>

namespace asc {
namespace ccu_opt {

// V2 microcode opcode 常量, 汇总自 ccu_microcode_v2_.cc.
namespace instr_code_v2 {
constexpr uint16_t load_type = 0x0;
constexpr uint16_t ctrl_type = 0x1;
constexpr uint16_t trans_type = 0x2;
constexpr uint16_t reduce_type = 0x3;

// load_type
constexpr uint16_t loadsqeargstox_code = 0x1;
constexpr uint16_t loadimdtox_code = 0x2;
constexpr uint16_t loadstorex_code = 0x6; // (预留, 当前 v2 未生成)
constexpr uint16_t storex_code = 0x7;     // (预留)
constexpr uint16_t clearx_code = 0x8;
constexpr uint16_t nop_code = 0x9;
constexpr uint16_t load_code = 0xA;
constexpr uint16_t store_code = 0xB;
constexpr uint16_t add_code = 0xD;
constexpr uint16_t sub_code = 0xE;
constexpr uint16_t mul_code = 0xF;
constexpr uint16_t and_code = 0x10;
constexpr uint16_t or_code = 0x11;
constexpr uint16_t not_code = 0x12;
constexpr uint16_t xor_code = 0x13;
constexpr uint16_t shl_code = 0x14;
constexpr uint16_t shr_code = 0x15;
constexpr uint16_t popcnt_code = 0x16;

// ctrl_type
constexpr uint16_t loop_code = 0x0;
constexpr uint16_t loopgroup_code = 0x1;
constexpr uint16_t setckbit_code = 0x2;
constexpr uint16_t clearckbit_code = 0x4;
constexpr uint16_t jmp_code = 0x5;
constexpr uint16_t wait_code = 0x7;
constexpr uint16_t fence_code = 0x8;

// trans_type
constexpr uint16_t translocmemtolocms_code = 0x0;
constexpr uint16_t translocmstolocmem_code = 0x2;
constexpr uint16_t translocmstolocms_code = 0x5;
constexpr uint16_t translocmemtolocmem_code = 0x6;
constexpr uint16_t transmem_code = 0x10;
constexpr uint16_t syncwtx_code = 0xD;
constexpr uint16_t syncatx_code = 0xE;

// reduce_type
constexpr uint16_t reduce_add_code = 0x0;
constexpr uint16_t reduce_max_code = 0x1;
constexpr uint16_t reduce_min_code = 0x2;
} // namespace instr_code_v2

} // namespace ccu_opt
} // namespace asc

#endif // CCU_MICROCODE_OPT_BARRIER_CONFIG_H
