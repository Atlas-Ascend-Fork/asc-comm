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
 * @file hcomm_ccu_launch.h
 * @brief 定义 CCU launch 跨动态库传递的 POD 数据结构。
 */

#ifndef HCOMM_CCU_LAUNCH_H
#define HCOMM_CCU_LAUNCH_H

#include <stddef.h>
#include <stdint.h>
#include "hcomm/hcomm_ccu_res.h"
#include "hcomm/hcomm_ccu_dfx.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_LAUNCH_ABI_VERSION = 3,                  // launch POD ABI 版本
    HCOMM_CCU_LAUNCH_CONTEXT_MAGIC_WORD = 0x434C4358U, // "CLCX"，launch 上下文 POD 魔数
    HCOMM_CCU_TASK_PROFILE_MAGIC_WORD = 0x43545046U,   // "CTPF"，task 级 profiling POD 魔数
    HCOMM_CCU_PROFILE_DETAIL_MAGIC_WORD = 0x43504446U, // "CPDF"，profiling 明细 POD 魔数
    HCOMM_CCU_PROFILE_NAME_CAPACITY = 128,             // profiling 名称缓冲长度
    HCOMM_CCU_PROFILE_CHANNEL_CAPACITY = 16,           // 明细中最多关联的 channel 数
};

/**
 * @brief 单个 CCU task 的 profiling POD（数据面 → hcomm 回调）。
 *
 * header 标识该结构的 ABI 布局；cycle、任务位置和 kernelHandle 由 hcomm 转换后交给原 callback。
 */
typedef struct {
    HcommCcuAbiHeaderPod header; // ABI 头：version/magic/size 校验
    uint64_t beginCycle;         // task 起始 cycle（profiling 采集值）
    uint64_t endCycle;           // task 结束 cycle
    uint32_t dieId;              // 所在 die
    uint32_t missionId;          // mission id
    uint32_t instructionId;      // 指令（起始）id
    uint32_t isMaster;           // 是否 master 核，1 为是
    uint64_t kernelHandle;       // kernel 句柄
    HcommCcuDiagnoseFn diagnose; // asc-comm 提供的诊断入口，hcomm 在任务异常时调用（见 HcommCcuDiagnoseFn）
    uint64_t reserved;           // 预留字段，当前必须为 0
} HcommCcuTaskProfilePod;

/**
 * @brief CCU task 的 profiling 明细 POD。
 *
 * header 在读取名称和数组前完成 ABI 校验；定长字段仅用于跨动态库传值，不携带资源所有权。
 */
typedef struct {
    HcommCcuAbiHeaderPod header; // ABI 头：version/magic/size 校验
    uint32_t nameLength;         // name 实际长度（必须 < HCOMM_CCU_PROFILE_NAME_CAPACITY）
    uint32_t profilingType;      // profiling 类型（L0/L1 等）
    uint32_t dieId;              // 所在 die
    uint32_t missionId;          // mission id
    uint32_t instructionId;      // 指令 id
    uint32_t reduceOpType;       // 归约操作类型（非归约时为无效值）
    uint32_t inputDataType;      // 输入数据类型
    uint32_t outputDataType;     // 输出数据类型
    uint64_t dataSize;           // 搬运/归约的数据量（字节或元素数）
    uint32_t ckeId;              // 关联信号量 CKE id
    uint32_t mask;               // 信号掩码
    uint16_t channelId[HCOMM_CCU_PROFILE_CHANNEL_CAPACITY];     // 关联 channel id 数组
    uint32_t remoteRankId[HCOMM_CCU_PROFILE_CHANNEL_CAPACITY];  // 远端 rank id 数组
    uint64_t channelHandle[HCOMM_CCU_PROFILE_CHANNEL_CAPACITY]; // 关联 channel 句柄数组
    char name[HCOMM_CCU_PROFILE_NAME_CAPACITY];                 // 任务名称
    uint64_t reserved[2];                                       // 预留扩展空间，当前必须清零
} HcommCcuProfileDetailPod;

// 获取 profiling cycle 的回调（hcomm 侧实现：未注册动态符号时返回 0）
typedef int32_t (*HcommCcuGetProfilingCycleCallback)(uint64_t* cycle);
// task 上报回调：hcomm 侧把 POD 转回原 TaskParam 类型后回调（streamId/taskId 由 hcomm 填补）
typedef int32_t (*HcommCcuReportTaskCallback)(
    uint64_t threadHandle, const HcommCcuTaskProfilePod* task, const HcommCcuProfileDetailPod* details,
    uint32_t detailNum);

/**
 * @brief CCU 同步 launch 上下文 POD（数据面调用同步 launch 时传入）。
 *
 * header 用于校验版本、结构类型、大小和预留字段；runtimeStream、threadHandle 和回调仅在同步 launch
 * 返回前有效。其余字段描述线程所属设备、超时、主从属性和 profiling 开关，调用方不得接管任何资源。
 */
typedef struct {
    HcommCcuAbiHeaderPod header;                         // ABI 头：version/magic/size 校验
    int32_t deviceLogicId;                               // 线程所在设备逻辑 id
    uint32_t timeoutSec;                                 // launch 超时秒数
    uint64_t runtimeStream;                              // runtime stream 句柄（借用，不发生所有权转移）
    uint32_t isMaster;                                   // 线程是否 master，1 为是
    uint32_t profilingL0Enabled;                         // L0 profiling 开关
    uint32_t profilingL1Enabled;                         // L1 profiling 开关
    uint32_t profilingCached;                            // profiling 缓存标记
    uint64_t threadHandle;                               // 调用方线程句柄（hcomm 在回调时重新解释）
    HcommCcuGetProfilingCycleCallback getProfilingCycle; // 获取 cycle 的回调函数指针
    HcommCcuReportTaskCallback reportTask;               // profiling 上报回调函数指针
    uint64_t reserved[2];                                // 预留扩展空间，当前必须清零
} HcommCcuLaunchContextPod;

#ifdef __cplusplus
}

static_assert(
    std::is_standard_layout<HcommCcuLaunchContextPod>::value, "HcommCcuLaunchContextPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuLaunchContextPod>::value, "HcommCcuLaunchContextPod must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuTaskProfilePod>::value, "HcommCcuTaskProfilePod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuTaskProfilePod>::value, "HcommCcuTaskProfilePod must be trivially copyable");
static_assert(
    std::is_standard_layout<HcommCcuProfileDetailPod>::value, "HcommCcuProfileDetailPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuProfileDetailPod>::value, "HcommCcuProfileDetailPod must be trivially copyable");
static_assert(sizeof(HcommCcuLaunchContextPod) == 88, "HcommCcuLaunchContextPod ABI size changed");
static_assert(
    offsetof(HcommCcuLaunchContextPod, runtimeStream) == 24,
    "HcommCcuLaunchContextPod.runtimeStream ABI offset changed");
static_assert(
    offsetof(HcommCcuLaunchContextPod, threadHandle) == 48, "HcommCcuLaunchContextPod.threadHandle ABI offset changed");
static_assert(sizeof(HcommCcuTaskProfilePod) == 72, "HcommCcuTaskProfilePod ABI size changed");
static_assert(
    offsetof(HcommCcuTaskProfilePod, kernelHandle) == 48, "HcommCcuTaskProfilePod.kernelHandle ABI offset changed");
static_assert(sizeof(HcommCcuProfileDetailPod) == 432, "HcommCcuProfileDetailPod ABI size changed");
static_assert(
    offsetof(HcommCcuProfileDetailPod, channelId) == 64, "HcommCcuProfileDetailPod.channelId ABI offset changed");
static_assert(offsetof(HcommCcuProfileDetailPod, name) == 288, "HcommCcuProfileDetailPod.name ABI offset changed");
#endif

#endif // HCOMM_CCU_LAUNCH_H
