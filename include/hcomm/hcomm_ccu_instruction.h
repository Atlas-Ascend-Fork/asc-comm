/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_INSTRUCTION_H
#define HCOMM_CCU_INSTRUCTION_H

#include <stdint.h>

#include "hcomm/hcomm_ccu_abi.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_INSTRUCTION_ABI_VERSION = 1,               // 指令下发 POD ABI 版本
    HCOMM_CCU_INSTRUCTION_LOAD_MAGIC_WORD = 0x43494E53U, // "CINS"
};

/**
 * 指令下发请求。
 *
 * 调用方先将编译好的微码指令拷入设备内存，再通过本 POD 告知 hcomm 指令所在的设备地址、
 * 目标 die、起始指令槽位与字节数；报文组装与驱动交互均在 libhcomm.so 内完成。
 * deviceAddress 指向的内存由调用方持有，hcomm 仅在本次调用期间使用，不保存该地址。
 *
 * @param header            ABI 头：version/magic/size 校验
 * @param deviceLogicId     目标设备逻辑 id
 * @param dieId             目标 die（< HCOMM_CCU_MAX_DIE_NUM）
 * @param startInstructionId 硬件指令槽位起始 idx
 * @param reserved          保留字段，使 deviceAddress 按 8 字节对齐
 * @param deviceAddress     指令所在设备内存地址（调用方持有，仅本次调用借用）
 * @param byteSize          指令总字节数，hcomm 内部校验后收窄为 uint32_t
 */
typedef struct {
    HcommCcuAbiHeaderPod header; // ABI 头：version/magic/size 校验
    int32_t deviceLogicId;       // 目标设备逻辑 id
    uint32_t dieId;              // 目标 die（< HCOMM_CCU_MAX_DIE_NUM）
    uint32_t startInstructionId; // 硬件指令槽位起始 idx
    uint32_t reserved;           // 保留字段，使 deviceAddress 按 8 字节对齐
    uint64_t deviceAddress;      // 指令所在设备内存地址
    uint64_t byteSize;           // 指令总字节数，hcomm 内部校验后收窄为 uint32_t
} HcommCcuInstructionLoadPod;

#ifdef __cplusplus
static_assert(
    std::is_standard_layout<HcommCcuInstructionLoadPod>::value, "HcommCcuInstructionLoadPod must be standard layout");
static_assert(
    std::is_trivially_copyable<HcommCcuInstructionLoadPod>::value,
    "HcommCcuInstructionLoadPod must be trivially copyable");
#endif

#ifdef __cplusplus
}
#endif

#endif // HCOMM_CCU_INSTRUCTION_H
