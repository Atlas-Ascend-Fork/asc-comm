/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hcomm_ccu_control.h
 * @brief CCU 控制面函数表 POD（供 asc-comm 数据面通过注入方式调用 hcomm 控制面查询）。
 *
 * 数据面不直接链接 hcomm 内部对象，而是通过 RegisterContext 中的函数指针表
 * 完成 channel 快照查询、指令下发、mission/loop 上下文查询与资源值查询，
 * 实现"数据面可访问控制面资源"且"两仓 ABI 稳定"的解耦目标。
 */
#ifndef HCOMM_CCU_CONTROL_H
#define HCOMM_CCU_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#include "hcomm/hcomm_ccu_abi.h"
#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/hcomm_ccu_dfx.h"
#include "hcomm/hcomm_ccu_instruction.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_CONTROL_ABI_VERSION = 2,              // 控制面函数表 POD ABI 版本
    HCOMM_CCU_CONTROL_OPS_MAGIC_WORD = 0x434F5053U, // "COPS"，函数表 POD 魔数
};

// 一次查询 Channel 的 POD 快照（hcomm 实现，asc-comm 调用方提供输出指针）
typedef int32_t (*HcommCcuChannelQueryFn)(ChannelHandle channel, HcommCcuChannelPod* channelPod);
// 将已位于设备内存的微码指令下发至 CCU（hcomm 内部完成报文组装与驱动交互）
typedef int32_t (*HcommCcuInstructionLoadFn)(const HcommCcuInstructionLoadPod* request);

/** 控制面函数表：所有函数指针在 RegisterContextCreate 时由 hcomm 填充，数据面只读调用 */
typedef struct {
    HcommCcuAbiHeaderPod header;                       // ABI 头：version/magic/size 校验
    HcommCcuChannelQueryFn channelQuery;               // 查询 channel 的 XN/CKE/远端 buffer 快照
    HcommCcuInstructionLoadFn loadInstruction;         // 下发微码指令
    HcommCcuMissionContextQueryFn missionContextQuery; // 查询 mission 上下文（当前/起止指令）
    HcommCcuLoopContextQueryFn loopContextQuery;       // 查询 loop 上下文（当前计数/地址步长）
    HcommCcuResourceQueryFn ckeQuery;                  // 按 id 查询 CKE（信号量）资源值
    HcommCcuResourceQueryFn xnQuery;                   // 按 id 查询 XN（变量）资源值
    HcommCcuResourceQueryFn gsaQuery;                  // 按 id 查询 GSA（通用存储）资源值
    uint64_t reserved;                                 // 预留函数表扩展位，当前必须为 0
} HcommCcuControlOpsPod;

#ifdef __cplusplus
}

static_assert(std::is_standard_layout<HcommCcuControlOpsPod>::value, "HcommCcuControlOpsPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuControlOpsPod>::value, "HcommCcuControlOpsPod must be trivially copyable");
static_assert(sizeof(HcommCcuControlOpsPod) == 80, "HcommCcuControlOpsPod ABI size changed");
static_assert(
    offsetof(HcommCcuControlOpsPod, channelQuery) == 16, "HcommCcuControlOpsPod.channelQuery ABI offset changed");
#endif

#endif // HCOMM_CCU_CONTROL_H
