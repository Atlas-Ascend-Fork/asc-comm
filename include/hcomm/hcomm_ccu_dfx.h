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
 * @file hcomm_ccu_dfx.h
 * @brief CCU 诊断（Diagnose）跨 SO POD 定义（hcomm 控制面 × asc-comm 数据面双向边界）。
 *
 * 任务异常定位调用链：
 *   hcomm 检测到 CCU 任务异常
 *     -> 从 TaskParam 取得 asc-comm 注入的 diagnose 回调（见 hcomm_ccu_launch.h）
 *     -> hcomm 构造 HcommCcuDfxRequestPod（指定设备/die/逻辑与执行 mission/kernel）
 *     -> 调用 HcommCcuDiagnoseFn，asc-comm 查询 mission/loop/CKE/XN/GSA 和 Channel 现场
 *     -> asc-comm 逐条构造 HcommCcuDfxRecordPod，经 HcommCcuDfxEmitFn 同步回调返回 hcomm
 *     -> hcomm 在回调内按值复制，格式化、打印并补充 Channel/Rank/Jetty 信息
 *
 * 诊断过程中 asc-comm 还会调用 hcomm 注入的 mission/loop/资源查询函数
 * （见 hcomm_ccu_control.h 的 missionContextQuery/loopContextQuery/ckeQuery/xnQuery/gsaQuery）
 * 补充硬件现场。
 */
#ifndef HCOMM_CCU_DFX_H
#define HCOMM_CCU_DFX_H

#include <stddef.h>
#include <stdint.h>

#include "hcomm/hcomm_ccu_channel.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_DFX_ABI_VERSION = 1,               // DFX POD ABI 版本；布局变化时必须与 asc-comm 同步升级
    HCOMM_CCU_DFX_CHANNEL_CAPACITY = 16,         // 单条记录最多关联的 channel 数
    HCOMM_CCU_DFX_BUFFER_CAPACITY = 8,           // BUFFER_REDUCE 最多关联的 buffer 数
    HCOMM_CCU_DFX_MISSION_MESSAGE_CAPACITY = 64, // mission 错误信息缓冲长度
};

/** 诊断记录类型：决定 msg 联合体中的有效子结构；hcomm 按 type 读取对应成员，不能同时解释多个成员 */
typedef enum {
    HCOMM_CCU_DFX_RECORD_DEFAULT = 0,         // 默认（未分类）记录
    HCOMM_CCU_DFX_RECORD_MISSION = 1,         // mission 错误文本记录（msg.mission）
    HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL = 2,     // 等待/通知信号记录（msg.waitSignal）
    HCOMM_CCU_DFX_RECORD_TRANSFER = 3,        // 内存搬运记录（msg.transMem）
    HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER = 4, // Buffer 搬运记录（msg.bufTransMem）
    HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE = 5,   // Buffer 归约记录（msg.bufferReduce）
    HCOMM_CCU_DFX_RECORD_LOOP = 6,            // 循环执行现场记录（msg.loop）
    HCOMM_CCU_DFX_RECORD_LOOP_GROUP = 7,      // 循环组执行现场记录（msg.loopGroup）
} HcommCcuDfxRecordType;

/** REP 类型的稳定数值（随 HcommCcuDfxRecordPod.repType 透传）；属于跨 SO ABI 的一部分，不可随意改动 */
typedef enum {
    HCOMM_CCU_REP_LOOP = 28,
    HCOMM_CCU_REP_LOOP_GROUP = 29,
    HCOMM_CCU_REP_LOC_RECORD_EVENT = 32,
    HCOMM_CCU_REP_LOC_WAIT_EVENT = 33,
    HCOMM_CCU_REP_LOC_WAIT_NOTIFY = 34,
    HCOMM_CCU_REP_REM_POST_SEM = 35,
    HCOMM_CCU_REP_REM_WAIT_SEM = 36,
    HCOMM_CCU_REP_REM_POST_VAR = 37,
    HCOMM_CCU_REP_READ = 39,
    HCOMM_CCU_REP_WRITE = 40,
    HCOMM_CCU_REP_LOCAL_COPY = 41,
    HCOMM_CCU_REP_LOCAL_REDUCE = 42,
    HCOMM_CCU_REP_BUFFER_READ = 44,
    HCOMM_CCU_REP_BUFFER_WRITE = 45,
    HCOMM_CCU_REP_BUFFER_LOCAL_READ = 46,
    HCOMM_CCU_REP_BUFFER_LOCAL_WRITE = 47,
    HCOMM_CCU_REP_BUFFER_REDUCE = 48,
    HCOMM_CCU_REP_RECORD_SHARED_NOTIFY = 51,
} HcommCcuRepType;

/** 诊断请求：定位到具体 die/mission/kernel 的执行现场；由 hcomm 在任务异常时构造，asc-comm 消费 */
typedef struct {
    int32_t deviceId;       // 异常任务所在设备
    uint32_t dieId;         // 异常 mission 所在 die
    uint32_t missionId;     // 原始逻辑 mission ID
    uint32_t execMissionId; // 展开后实际提交硬件执行的 mission ID
    uint64_t kernelHandle;  // 用于 asc-comm 查找 Kernel 和 REP 映射的句柄
    uint16_t missionStatus; // mission 状态和子状态组成的 16 位异常状态
    uint16_t reserved0;     // 预留字段，当前必须为 0
    uint32_t reserved1;     // 预留字段，当前必须为 0
} HcommCcuDfxRequestPod;

/** 等待信号类记录：signal 的 id/掩码/当前值与关联 channel */
typedef struct {
    uint16_t signalId;                                  // 等待、记录或远端通知使用的 CKE 信号 ID
    uint16_t signalMask;                                // 需要检查的信号位掩码
    uint16_t signalValue;                               // 诊断时查询到的 CKE 当前值；无需读取时填 0
    uint16_t paramId;                                   // REM_POST_VAR：远端 XN 参数 id（其他类型为 0）
    uint64_t paramValue;                                // REM_POST_VAR：参数当前值（其他类型为 0）
    uint16_t channelId[HCOMM_CCU_DFX_CHANNEL_CAPACITY]; // 关联 Channel ID；未使用槽填 UINT16_MAX
    ChannelHandle channelHandle[HCOMM_CCU_DFX_CHANNEL_CAPACITY]; // 与 channelId[] 同下标对应的 Channel Handle
} HcommCcuDfxWaitSignalPod;

/** 内存搬运类记录：本/远端地址、token、长度与信号 */
typedef struct {
    uint64_t locAddr;            // 本端地址；本地复制场景中表示源地址
    uint64_t locToken;           // 本端地址对应的访问 Token
    uint64_t rmtAddr;            // 远端地址；本地复制场景中表示目的地址
    uint64_t rmtToken;           // 远端或目的地址对应的访问 Token
    uint64_t len;                // 搬运长度
    uint16_t signalId;           // 搬运关联的 CKE 信号 ID
    uint16_t signalMask;         // 搬运完成或等待使用的信号掩码
    uint16_t channelId;          // 远端搬运关联的 Channel ID；本地操作可为无效值
    uint16_t dataType;           // 搬运或归约的数据类型编码
    uint16_t opType;             // 搬运中附带的操作类型编码
    uint16_t reserved;           // 对齐和扩展字段，当前必须为 0
    ChannelHandle channelHandle; // hcomm 用于补充远端 Rank 和 Channel 现场的不透明 Handle
} HcommCcuDfxTransferPod;

/** Buffer 搬运类记录 */
typedef struct {
    uint16_t bufId;              // CCU 内部 Buffer ID
    uint16_t signalId;           // Buffer 操作关联的 CKE 信号 ID
    uint16_t signalMask;         // Buffer 操作关联的信号掩码
    uint16_t channelId;          // 远端 Buffer 操作关联的 Channel ID；本地操作填 UINT16_MAX
    uint64_t addr;               // 与 Buffer 互相搬运的设备内存地址
    uint64_t token;              // 该设备内存地址对应的访问 Token
    uint64_t len;                // Buffer 搬运长度
    ChannelHandle channelHandle; // 远端 Buffer 操作对应的 Channel Handle
} HcommCcuDfxBufferTransferPod;

/** Buffer 归约类记录 */
typedef struct {
    uint16_t bufIds[HCOMM_CCU_DFX_BUFFER_CAPACITY]; // 参加归约的 Buffer ID；未使用槽填 UINT16_MAX
    uint16_t count;                                 // 参加归约的有效 Buffer 数量
    uint16_t dataType;                              // 输入数据类型编码
    uint16_t outputDataType;                        // 输出数据类型编码
    uint16_t opType;                                // 归约操作类型编码
    uint16_t signalId;                              // 归约关联的 CKE 信号 ID
    uint16_t signalMask;                            // 归约关联的信号掩码
    uint16_t xnIdLength;                            // 保存归约长度的 XN 资源 ID，不是长度值本身
} HcommCcuDfxBufferReducePod;

/** 循环执行现场记录 */
typedef struct {
    uint16_t startInstrId;   // loop 体的起始指令 ID
    uint16_t endInstrId;     // loop 体的结束指令 ID
    uint16_t loopEngineId;   // 执行该 loop 的 Loop Engine ID
    uint16_t loopCnt;        // loop 计划执行总次数
    uint16_t loopCurrentCnt; // 从硬件上下文查询到的当前执行次数
    uint16_t reserved;       // 对齐和扩展字段，当前必须为 0
    uint32_t addrStride;     // 每轮 loop 使用的地址步长
} HcommCcuDfxLoopPod;

/** 循环组执行现场记录 */
typedef struct {
    uint16_t startLoopInsId; // Loop Group 中第一条 loop 指令 ID
    uint16_t loopInsCnt;     // Group 内包含的 loop 指令数量
    uint16_t expandOffset;   // 硬件展开参数中的起始偏移
    uint16_t expandCount;    // 硬件展开次数
} HcommCcuDfxLoopGroupPod;

/** 诊断记录：按 type 区分 msg 联合体中的具体子结构（DFX 回调实际传输的外层记录） */
typedef struct {
    uint32_t type;     // 记录类别，决定 msg 的有效成员（见 HcommCcuDfxRecordType）
    int32_t repType;   // 产生本记录的稳定 REP 类型值（见 HcommCcuRepType）
    uint8_t dieId;     // 记录所属 die
    uint8_t missionId; // 记录所属逻辑 mission
    uint16_t instrId;  // 记录对应的指令 ID
    uint32_t reserved; // 联合体前的对齐和扩展字段，当前必须为 0
    union {
        struct {
            char missionError[HCOMM_CCU_DFX_MISSION_MESSAGE_CAPACITY]; // mission 错误文本，仅 MISSION 类型有效
        } mission;
        HcommCcuDfxWaitSignalPod waitSignal;      // 仅 WAIT_SIGNAL 类型有效
        HcommCcuDfxTransferPod transMem;          // 仅 TRANSFER 类型有效
        HcommCcuDfxBufferTransferPod bufTransMem; // 仅 BUFFER_TRANSFER 类型有效
        HcommCcuDfxBufferReducePod bufferReduce;  // 仅 BUFFER_REDUCE 类型有效
        HcommCcuDfxLoopPod loop;                  // 仅 LOOP 类型有效
        HcommCcuDfxLoopGroupPod loopGroup;        // 仅 LOOP_GROUP 类型有效
    } msg;
} HcommCcuDfxRecordPod;

// 记录回调：asc-comm 诊断时逐条同步输出；hcomm 传入 context 原样回传。
// record 指针仅在本次调用期间有效，hcomm 必须立即按值复制，不能保存指针。
typedef int32_t (*HcommCcuDfxEmitFn)(void* context, const HcommCcuDfxRecordPod* record);
// 诊断入口：hcomm 任务异常时调用（由 asc-comm 提供并经 HcommCcuTaskProfilePod.diagnose 注入）。
// 给定请求定位执行现场，并逐条调用 emit 回调输出全部相关记录。
typedef int32_t (*HcommCcuDiagnoseFn)(const HcommCcuDfxRequestPod* request, HcommCcuDfxEmitFn emit, void* context);

/** mission 执行上下文（指令展开现场）；由 hcomm 查询驱动后按 CCU 版本解析填充 */
typedef struct {
    uint16_t currentInstructionId; // mission 当前正在执行或失败的指令 ID
    uint16_t endInstructionId;     // mission 结束指令 ID
    uint16_t startInstructionId;   // mission 起始指令 ID
    uint16_t reserved;             // 对齐和扩展字段，当前必须为 0
} HcommCcuMissionContextPod;

/** loop 执行上下文；由 hcomm 查询驱动后按 CCU 版本解析填充 */
typedef struct {
    uint16_t currentCount;  // loop 当前已执行次数
    uint16_t reserved;      // 对齐和扩展字段，当前必须为 0
    uint32_t addressStride; // loop 硬件上下文中的地址步长
} HcommCcuLoopContextPod;

// 控制面注入的上下文查询函数指针（hcomm 实现，asc-comm 诊断时调用，结果按 CCU 版本解析）
typedef int32_t (*HcommCcuMissionContextQueryFn)(
    int32_t deviceId, uint32_t dieId, uint32_t missionId, HcommCcuMissionContextPod* context);
typedef int32_t (*HcommCcuLoopContextQueryFn)(
    int32_t deviceId, uint32_t dieId, uint32_t loopId, HcommCcuLoopContextPod* context);
// 资源值查询：按资源 id 返回 XN/CKE/GSA 的当前值（控制面实现，数据面诊断时调用）
typedef int32_t (*HcommCcuResourceQueryFn)(int32_t deviceId, uint32_t dieId, uint32_t resourceId, uint64_t* value);

#ifdef __cplusplus
}

static_assert(std::is_standard_layout<HcommCcuDfxRequestPod>::value, "HcommCcuDfxRequestPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuDfxRequestPod>::value, "HcommCcuDfxRequestPod must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuDfxRecordPod>::value, "HcommCcuDfxRecordPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuDfxRecordPod>::value, "HcommCcuDfxRecordPod must be trivially copyable");
static_assert(sizeof(HcommCcuDfxRequestPod) == 32, "HcommCcuDfxRequestPod ABI size changed");
static_assert(
    offsetof(HcommCcuDfxRequestPod, kernelHandle) == 16, "HcommCcuDfxRequestPod.kernelHandle ABI offset changed");
static_assert(offsetof(HcommCcuDfxRecordPod, msg) == 16, "HcommCcuDfxRecordPod.msg ABI offset changed");
static_assert(sizeof(HcommCcuDfxRecordPod) == 192, "HcommCcuDfxRecordPod ABI size changed");
#endif

#endif // HCOMM_CCU_DFX_H
