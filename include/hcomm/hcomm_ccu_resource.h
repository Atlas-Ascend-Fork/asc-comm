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
 * @file hcomm_ccu_resource.h
 * @brief CCU instance、resource、channel 和 DFX 跨 SO POD 定义。
 */

#ifndef HCOMM_CCU_RESOURCE_H
#define HCOMM_CCU_RESOURCE_H

#include <stdint.h>
#include "hcomm/hcomm_ccu_base.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "hcomm/hcomm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CCU Instance 类型：注册上下文申请的实例类型；hcomm 创建 RegisterContext 时校验，并据此分配资源句柄 */
typedef enum {
    CCU_DEFAULT = 0,
    CCU_SCHED = 1,
    CCU_MS = 2,
    CCU_UNUSED = 254,
    CCU_RESERVED = 255
} CcuInstanceType;

struct HcommCcuInstance;

enum {
    HCOMM_CCU_INSTANCE_HANDLE_ABI_VERSION = 1,        // CcuInsHandle ABI 版本
    HCOMM_CCU_INSTANCE_HANDLE_MAGIC_WORD = 0x43435548U, // CcuInsHandle 类型魔数，值即 ASCII "CCUH"（CCU Handle），
                                                          // 接收方校验 header.magicWord 确认传入 POD 类型正确
};

/**
 * CCU Instance 句柄（CommAbiHeader + ccuInsKey + ccuInsPtr 三字段）：
 * header 为 ABI 头：version/magic/size 校验，防两仓布局不一致时按值传递越界读；
 * ccuInsKey 为 hcomm 位编码的不透明键（generation<<32 | slot+1），用于跨 SO 传递与查找；
 * ccuInsPtr 指向 hcomm 持有的 HcommCcuInstance（只读借用，随 RegisterContext 生命周期有效）。
 * hcomm 负责编码、校验、销毁，asc-comm 只能按值保存和回传，不得解引用 ccuInsPtr 之外的成员。
 */
typedef struct {
    CommAbiHeader header;                      // ABI 头：version/magic/size 校验.
    uint64_t ccuInsKey;                        // 不透明实例键（0 为无效句柄哨兵）
    const struct HcommCcuInstance *ccuInsPtr;  // hcomm 持有的 CCU Instance 借用指针
} CcuInsHandle;

#ifdef __cplusplus
}
#endif

#ifndef CHANNEL_HANDLE_DEFINED
#define CHANNEL_HANDLE_DEFINED
// 通道句柄：不透明唯一标识，跨 SO 以整型值传递
typedef uint64_t ChannelHandle;
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    HCOMM_CCU_CHANNEL_ABI_VERSION = 4,               // 当前 POD ABI 版本
    HCOMM_CCU_CHANNEL_POD_MAGIC_WORD = 0x43435543U,  // "CCUC"，标识 Channel POD 类型
    HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY = 16,        // 每个 channel 持有的 XN/CKE 资源槽位数
};

/**
 * @brief CCU Channel 单次查询快照 POD（16 槽 + 计数 + 事件掩码 + 远端 buffer 数组）。
 *
 * localVarIds/remoteVarIds 为本/远端 XN 槽，localEventIds/remoteEventIds 为本/远端 CKE 槽，
 * 实际有效数量由 localVarNum/remoteVarNum/localEventNum/remoteEventNum 给出（<= 容量）。
 * localEventMasks/remoteEventMasks 为预留槽位：mask 是 kernel 构建期的按次参数，
 * 非 channel 属性，当前固定填 0。
 * rmtCcuResBuffer 为对端 CCU 资源空间（RegedBufferEntity 口径，取代旧散字段）。
 * remoteBuffers 为 hcomm channel 内部远端 buffer 列表的借用指针，channel 销毁后失效；
 * 当前数据面未消费（翻译器仍按单 CCU 资源空间假设），为形状兼容预留。
 * 调用方提供 POD 存储并填好 ABI 头，hcomm 持锁一次填满快照，之后数据面只读该 POD。
 */
typedef struct {
    CommAbiHeader header;         // ABI 头：version/magic/size 校验

    int32_t deviceLogicId;        // 所在设备逻辑 id
    uint32_t ccuVersion;          // CCU 版本（V1/V2）
    uint32_t dieId;               // die id
    uint32_t channelId;           // channel id（DFX 记录用）

    RegedBufferEntity rmtCcuResBuffer; // 对端 CCU 资源空间（基址/大小/token 折叠为 buffer 实体）

    uint32_t localVarNum;             // 本端 XN 有效数量（<= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY）
    uint32_t remoteVarNum;            // 远端 XN 有效数量
    uint32_t localEventNum;           // 本端 CKE 有效数量
    uint32_t remoteEventNum;          // 远端 CKE 有效数量
    uint32_t remoteBufferNum;         // 远端 buffer 数量（remoteBuffers 数组长度）
    uint32_t reserved0;               // 对齐字段，当前必须为 0
    const RegedBufferEntity *remoteBuffers; // 远端 buffer 数组借用指针（hcomm 持有，当前数据面不消费）

    uint32_t localVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 本端 XN id 槽
    uint32_t remoteVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 远端 XN id 槽
    uint32_t localEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 本端 CKE id 槽
    uint32_t remoteEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE id 槽
    uint32_t localEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 本端 CKE 掩码槽（预留，填 0）
    uint32_t remoteEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE 掩码槽（预留，填 0）

    uint64_t reserved[4];  // 预留扩展空间，当前必须清零
} HcommCcuChannelPod;

#ifdef __cplusplus
} // extern "C"

#endif

/**
 * @brief CCU 资源描述符（ResDesc）跨 SO POD 定义。
 *
 * hcomm 和 asc-comm 各自保留一份同布局的 HcommCcuResDesc POD 定义。
 * HcommCcuResDesc 不对用户暴露，用户只感知 HcommCcuResDescHandle；
 * asc-comm 侧由 handle table 解析出实际 POD。
 */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    HCOMM_CCU_RES_DESC_ABI_VERSION = 1,        // ResDesc POD ABI 版本
    HCOMM_CCU_RES_DESC_MAGIC_WORD = 0x43524453U, // "CRDS"，ResDesc POD 魔数
    HCOMM_CCU_RES_DESC_TYPE_COUNT = 32,        // 资源类型槽位数（预留到 32，现用 0-7）
};

/** 资源描述符统计口径的类型枚举（block/非 block 在此合并计数；值域为跨 SO ABI，不可改动） */
typedef enum {
    CCU_RES_DESC_LOOP = 0,       // LoopEngine（loop + block loop 之和）
    CCU_RES_DESC_MS = 1,         // Memory Slice（ms + block ms 之和）
    CCU_RES_DESC_CKE = 2,        // 完成事件/信号量（cke + block cke 之和）
    CCU_RES_DESC_XN = 3,         // 变量（xn + block xn 之和）
    CCU_RES_DESC_COUNT_XN = 4,   // 计数变量（预留，当前恒为 0）
    CCU_RES_DESC_GSA = 5,        // 通用存储地址（gsa + block gsa 之和）
    CCU_RES_DESC_INS = 6,        // 指令空间（指令条数）
    CCU_RES_DESC_MISSION = 7,    // mission 数
} CcuResDescType;

/** CCU 资源描述符 POD：按资源类型统计的申请数量（创建时设 dieId，查询后填数量） */
typedef struct {
    CommAbiHeader header;          // ABI 头：version/magic/size 校验
    uint32_t dieId;                // 资源归属的 die id（创建描述符时设置）
    uint32_t resLength;            // resNum 数组的有效长度（<= HCOMM_CCU_RES_DESC_TYPE_COUNT）
    uint32_t resNum[HCOMM_CCU_RES_DESC_TYPE_COUNT]; // 按资源类型索引的数量
} HcommCcuResDesc;

/** 资源描述符句柄：不对用户暴露 POD，由 asc-comm 侧 handle table 解析 */
typedef uint64_t HcommCcuResDescHandle;

/**
 * @brief 反查资源描述符内容，hcomm 侧据此获取实际需要的 CCU 资源。
 *
 * 按 handle 值拷贝返回描述符当前内容（dieId + 各资源类型数量）。
 * 供 hcomm 按需建实例（HcommCcuInsCreate by ResDesc）等控制面消费；用户不直接调用。
 *
 * @param[in] resDesc 资源描述符句柄
 * @param[out] out 输出描述符内容（值拷贝，调用方持有存储）
 * @return CcuResult
 */
extern CcuResult HcommCcuInsResDescQuery(HcommCcuResDescHandle resDesc, HcommCcuResDesc *out);

#ifdef __cplusplus
} // extern "C"

#endif

/**
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
 * （见 hcomm_ccu_resource.h 中 ascCustom 槽位的 missionContextQuery/loopContextQuery/ckeQuery/xnQuery/gsaQuery）
 * 补充硬件现场。
 */
#ifdef __cplusplus
extern "C" {
#endif

enum {
    HCOMM_CCU_DFX_ABI_VERSION = 1,  // DFX POD ABI 版本；布局变化时必须与 asc-comm 同步升级
    HCOMM_CCU_DFX_CHANNEL_CAPACITY = 16, // 单条记录最多关联的 channel 数
    HCOMM_CCU_DFX_BUFFER_CAPACITY = 8,   // BUFFER_REDUCE 最多关联的 buffer 数
    HCOMM_CCU_DFX_MISSION_MESSAGE_CAPACITY = 64, // mission 错误信息缓冲长度
};

/** 诊断记录类型：决定 msg 联合体中的有效子结构；hcomm 按 type 读取对应成员，不能同时解释多个成员 */
typedef enum {
    HCOMM_CCU_DFX_RECORD_DEFAULT = 0,          // 默认（未分类）记录
    HCOMM_CCU_DFX_RECORD_MISSION = 1,          // mission 错误文本记录（msg.mission）
    HCOMM_CCU_DFX_RECORD_WAIT_SIGNAL = 2,      // 等待/通知信号记录（msg.waitSignal）
    HCOMM_CCU_DFX_RECORD_TRANSFER = 3,         // 内存搬运记录（msg.transMem）
    HCOMM_CCU_DFX_RECORD_BUFFER_TRANSFER = 4,  // Buffer 搬运记录（msg.bufTransMem）
    HCOMM_CCU_DFX_RECORD_BUFFER_REDUCE = 5,    // Buffer 归约记录（msg.bufferReduce）
    HCOMM_CCU_DFX_RECORD_LOOP = 6,             // 循环执行现场记录（msg.loop）
    HCOMM_CCU_DFX_RECORD_LOOP_GROUP = 7,       // 循环组执行现场记录（msg.loopGroup）
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
    int32_t deviceId;        // 异常任务所在设备
    uint32_t dieId;          // 异常 mission 所在 die
    uint32_t missionId;      // 原始逻辑 mission ID
    uint32_t execMissionId;  // 展开后实际提交硬件执行的 mission ID
    uint64_t kernelHandle;   // 用于 asc-comm 查找 Kernel 和 REP 映射的句柄
    uint16_t missionStatus;  // mission 状态和子状态组成的 16 位异常状态
    uint16_t reserved0;      // 预留字段，当前必须为 0
    uint32_t reserved1;      // 预留字段，当前必须为 0
} HcommCcuDfxRequestPod;

/** 等待信号类记录：signal 的 id/掩码/当前值与关联 channel */
typedef struct {
    uint16_t signalId;                                    // 等待、记录或远端通知使用的 CKE 信号 ID
    uint16_t signalMask;                                  // 需要检查的信号位掩码
    uint16_t signalValue;                                 // 诊断时查询到的 CKE 当前值；无需读取时填 0
    uint16_t paramId;                                     // REM_POST_VAR：远端 XN 参数 id（其他类型为 0）
    uint64_t paramValue;                                  // REM_POST_VAR：参数当前值（其他类型为 0）
    uint16_t channelId[HCOMM_CCU_DFX_CHANNEL_CAPACITY];   // 关联 Channel ID；未使用槽填 UINT16_MAX
    ChannelHandle channelHandle[HCOMM_CCU_DFX_CHANNEL_CAPACITY]; // 与 channelId[] 同下标对应的 Channel Handle
} HcommCcuDfxWaitSignalPod;

/** 内存搬运类记录：本/远端地址、token、长度与信号 */
typedef struct {
    uint64_t locAddr;         // 本端地址；本地复制场景中表示源地址
    uint64_t locToken;        // 本端地址对应的访问 Token
    uint64_t rmtAddr;         // 远端地址；本地复制场景中表示目的地址
    uint64_t rmtToken;        // 远端或目的地址对应的访问 Token
    uint64_t len;             // 搬运长度
    uint16_t signalId;        // 搬运关联的 CKE 信号 ID
    uint16_t signalMask;      // 搬运完成或等待使用的信号掩码
    uint16_t channelId;       // 远端搬运关联的 Channel ID；本地操作可为无效值
    uint16_t dataType;        // 搬运或归约的数据类型编码
    uint16_t opType;          // 搬运中附带的操作类型编码
    uint16_t reserved;        // 对齐和扩展字段，当前必须为 0
    ChannelHandle channelHandle; // hcomm 用于补充远端 Rank 和 Channel 现场的不透明 Handle
} HcommCcuDfxTransferPod;

/** Buffer 搬运类记录 */
typedef struct {
    uint16_t bufId;        // CCU 内部 Buffer ID
    uint16_t signalId;     // Buffer 操作关联的 CKE 信号 ID
    uint16_t signalMask;   // Buffer 操作关联的信号掩码
    uint16_t channelId;    // 远端 Buffer 操作关联的 Channel ID；本地操作填 UINT16_MAX
    uint64_t addr;         // 与 Buffer 互相搬运的设备内存地址
    uint64_t token;        // 该设备内存地址对应的访问 Token
    uint64_t len;          // Buffer 搬运长度
    ChannelHandle channelHandle; // 远端 Buffer 操作对应的 Channel Handle
} HcommCcuDfxBufferTransferPod;

/** Buffer 归约类记录 */
typedef struct {
    uint16_t bufIds[HCOMM_CCU_DFX_BUFFER_CAPACITY];  // 参加归约的 Buffer ID；未使用槽填 UINT16_MAX
    uint16_t count;          // 参加归约的有效 Buffer 数量
    uint16_t dataType;       // 输入数据类型编码
    uint16_t outputDataType; // 输出数据类型编码
    uint16_t opType;         // 归约操作类型编码
    uint16_t signalId;       // 归约关联的 CKE 信号 ID
    uint16_t signalMask;     // 归约关联的信号掩码
    uint16_t xnIdLength;     // 保存归约长度的 XN 资源 ID，不是长度值本身
} HcommCcuDfxBufferReducePod;

/** 循环执行现场记录 */
typedef struct {
    uint16_t startInstrId;    // loop 体的起始指令 ID
    uint16_t endInstrId;      // loop 体的结束指令 ID
    uint16_t loopEngineId;    // 执行该 loop 的 Loop Engine ID
    uint16_t loopCnt;         // loop 计划执行总次数
    uint16_t loopCurrentCnt;  // 从硬件上下文查询到的当前执行次数
    uint16_t reserved;        // 对齐和扩展字段，当前必须为 0
    uint32_t addrStride;      // 每轮 loop 使用的地址步长
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
    uint32_t type;            // 记录类别，决定 msg 的有效成员（见 HcommCcuDfxRecordType）
    int32_t repType;          // 产生本记录的稳定 REP 类型值（见 HcommCcuRepType）
    uint8_t dieId;            // 记录所属 die
    uint8_t missionId;        // 记录所属逻辑 mission
    uint16_t instrId;         // 记录对应的指令 ID
    uint32_t reserved;        // 联合体前的对齐和扩展字段，当前必须为 0
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
typedef int32_t (*HcommCcuDfxEmitFn)(void *context, const HcommCcuDfxRecordPod *record);
// 诊断入口：hcomm 任务异常时调用（由 asc-comm 提供并经 HcommCcuTaskProfilePod.diagnose 注入）。
// 给定请求定位执行现场，并逐条调用 emit 回调输出全部相关记录。
typedef int32_t (*HcommCcuDiagnoseFn)(
    const HcommCcuDfxRequestPod *request, HcommCcuDfxEmitFn emit, void *context);

/** mission 执行上下文（指令展开现场）；由 hcomm 查询驱动后按 CCU 版本解析填充 */
typedef struct {
    uint16_t currentInstructionId;  // mission 当前正在执行或失败的指令 ID
    uint16_t endInstructionId;      // mission 结束指令 ID
    uint16_t startInstructionId;    // mission 起始指令 ID
    uint16_t reserved;              // 对齐和扩展字段，当前必须为 0
} HcommCcuMissionContextPod;

/** loop 执行上下文；由 hcomm 查询驱动后按 CCU 版本解析填充 */
typedef struct {
    uint16_t currentCount;  // loop 当前已执行次数
    uint16_t reserved;      // 对齐和扩展字段，当前必须为 0
    uint32_t addressStride; // loop 硬件上下文中的地址步长
} HcommCcuLoopContextPod;

// 控制面注入的上下文查询函数指针（hcomm 实现，asc-comm 诊断时调用，结果按 CCU 版本解析）
typedef int32_t (*HcommCcuMissionContextQueryFn)(
    int32_t deviceId, uint32_t dieId, uint32_t missionId, HcommCcuMissionContextPod *context);
typedef int32_t (*HcommCcuLoopContextQueryFn)(
    int32_t deviceId, uint32_t dieId, uint32_t loopId, HcommCcuLoopContextPod *context);
// 资源值查询：按资源 id 返回 XN/CKE/GSA 的当前值（控制面实现，数据面诊断时调用）
typedef int32_t (*HcommCcuResourceQueryFn)(
    int32_t deviceId, uint32_t dieId, uint32_t resourceId, uint64_t *value);

#ifdef __cplusplus
}

#endif

/**
 * @brief CCU 资源层跨 SO POD 定义（CCU Instance / Register Context）。
 *
 * 资源按 die × 资源类型两级嵌套组织，每类持多段连续 range；
 * die 元数据（回路 channel、CCU 资源空间 buffer）由 HcommCcuDieMetadata 承载；
 * 控制面注入 hcomm 实现的函数表 ascCustom（槽 0/1/2 + DFX 查询链）。
 * asc-comm 在 register_start 时对整个 Instance 做一次性深拷贝快照。
 */
#ifdef __cplusplus
extern "C" {
#endif

struct HcommCcuInstance;
typedef const struct HcommCcuInstance *HcommCcuInstanceHandle;
enum {
    HCOMM_CCU_RES_ABI_VERSION = 8,             // 资源/CCU Instance POD ABI 版本
    HCOMM_CCU_INSTANCE_MAGIC_WORD = 0x43435549U, // HcommCcuInstance 类型魔数，值即 ASCII "CCUI"（CCU Instance），
                                                  // 接收方校验 header.magicWord 确认传入 POD 类型正确
    HCOMM_CCU_DIE_METADATA_MAGIC_WORD = 0x4343444DU, // HcommCcuDieMetadata 类型魔数，值即 ASCII "CCDM"（CCU Die Metadata），
                                                      // 接收方校验 header.magicWord 确认传入 POD 类型正确
    HCOMM_CCU_RES_TYPE_COUNT = 32,             // 单 die 预留的资源类型槽位数（现用 0-10，11-31 预留）
    HCOMM_CCU_BATCH_RES_TYPE_COUNT = 11,       // HcommCcuBatchResType 枚举值数量（数组维度用）
    HCOMM_CCU_INSTANCE_DIE_CAPACITY = 8,       // Instance 预留的 die 槽位数（现网 2 个）
    HCOMM_CCU_INSTR_WIDTH_BYTES = 32,          // 单条 CCU 微码指令宽度（sizeof(asc ccu_instr)），SubmitInsts 换算用
    HCOMM_CCU_VERSION_V1 = 0,                  // CCU V1 版本号（ABI 内透传）
    HCOMM_CCU_VERSION_V2 = 1,                  // CCU V2 版本号
    HCOMM_CCU_VERSION_INVALID = 0xFFFFFFFFU,   // 无效版本哨兵
    HCOMM_CCU_RESOURCE_XN_PER_SIZE = 8,        // 单个 XN 资源占用的空间步长（地址换算用）
    HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY = 0,   // ascCustom 槽 0：获取 ChannelEntity 接口
    HCOMM_CCU_ASC_CUSTOM_ALLOC_INST_SPACE = 1, // ascCustom 槽 1：申请 CCU 指令空间资源的接口
    HCOMM_CCU_ASC_CUSTOM_SUBMIT_INSTS = 2,     // ascCustom 槽 2：下发 CCU 指令接口
    HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT = 8,       // ascCustom 槽位总数（3-7 预留）
};

/** 批量资源的 range 类型枚举（resTypes 数组按该枚举值索引；block/非 block 在 range 视图分开，ResDesc 统计口径才合并） */
typedef enum {
    HCOMM_CCU_BATCH_RES_LOOP = 0,
    HCOMM_CCU_BATCH_RES_BLOCK_LOOP = 1,
    HCOMM_CCU_BATCH_RES_MS = 2,
    HCOMM_CCU_BATCH_RES_BLOCK_MS = 3,
    HCOMM_CCU_BATCH_RES_CKE = 4,
    HCOMM_CCU_BATCH_RES_BLOCK_CKE = 5,
    HCOMM_CCU_BATCH_RES_BLOCK_XN = 6,
    HCOMM_CCU_BATCH_RES_XN = 7,
    HCOMM_CCU_BATCH_RES_GSA = 8,
    HCOMM_CCU_BATCH_RES_BLOCK_GSA = 9,
    HCOMM_CCU_BATCH_RES_MISSION = 10,
} HcommCcuBatchResType;

/**
 * 一段连续资源：起始 id + 数量。所属资源类型与 die 由外层 HcommCcuResRanges / HcommCcuDieRes 表达。
 */
typedef struct {
    uint32_t startId;      // 连续资源的起始 ID
    uint32_t count;        // 从 startId 开始的资源数量，必须大于 0
    uint64_t reserved;     // 预留字段，当前必须为 0
} HcommCcuResRange;

/**
 * 一类资源的全部 range 段（按资源类型枚举值挂在 HcommCcuDieRes.resTypes[type] 上）。
 * resRanges 指向 hcomm RegisterContext 持有的只读数组，asc-comm 在 register_start 期间深拷贝。
 */
typedef struct {
    uint32_t resourceType;                 // 资源类别（HcommCcuBatchResType）
    uint32_t reserved0;                    // 对齐字段，当前必须为 0
    const HcommCcuResRange *resRanges;     // 该类型的 range 数组（只读借用）
    uint32_t resRangeNum;                  // range 数组元素数量
    uint32_t reserved1;                    // 对齐字段，当前必须为 0
} HcommCcuResRanges;

/**
 * 单个 die 的元数据：回路 channel 与 CCU 资源空间 buffer。
 * ccuResBuffer 按 {REGED_BUFFER_RMA, addr=XN 基址, ProtectionInfo.ub{tokenId, tokenValue}} 装配；
 * loopChannelId 为该 die 唯一的环回 channel（硬件上 die 内/间访问共用同一条，见 ccu_comp.cc "不区分die内die间"）。
 */
typedef struct {
    CommAbiHeader header;                // ABI 头：version/magic/size 校验
    uint32_t enabled;                    // 该 die 是否启用（0/1）
    uint32_t missionKey;                 // mission 密钥
    uint32_t loopChannelId;              // 环回 channel id（每 die 一条，die 内/间访问共用）
    uint32_t reserved0;                  // 对齐字段，当前必须为 0
    RegedBufferEntity ccuResBuffer;      // CCU 资源空间（XN 基址 + token）映射后的 buffer 信息
    uint64_t reserved[6];                // 预留字段，当前必须清零
} HcommCcuDieMetadata;

/** 单个 die 的全部资源视图：按资源类型索引的 range 段数组 + die 元数据指针 */
typedef struct {
    uint32_t dieId;                          // 该资源视图所属 die
    uint32_t resTypeNum;                     // 已使用的资源类型数（<= HCOMM_CCU_RES_TYPE_COUNT）
    HcommCcuResRanges resTypes[HCOMM_CCU_RES_TYPE_COUNT]; // 按资源类型枚举值索引
    const HcommCcuDieMetadata *dieMetadata;  // hcomm 持有的 die 元数据（只读借用）
    uint64_t reserved[16];                   // 预留字段，当前必须清零
} HcommCcuDieRes;

// ascCustom 槽 0：获取 Channel Entity（返回 hcomm 持有的实体借用指针，调用方深拷贝后使用）
typedef int32_t (*HcommCcuGetChannelEntityFn)(ChannelHandle channel, uint64_t *channelPodEntityPtr);
// ascCustom 槽 1：申请 CCU 指令空间（从当前 Instance 的指令池切分）
typedef int32_t (*HcommCcuAllocInstSpaceFn)(int32_t deviceLogicId, uint32_t dieId, uint32_t instNum,
    uint32_t *startInstId);
// ascCustom 槽 2：下发 CCU 指令（data 为 host 指针，hcomm 负责拷入设备内存并经驱动下发）
typedef int32_t (*HcommCcuSubmitInstsFn)(int32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId,
    const void *data, uint32_t instNum);

/**
 * CCU Instance（Register Context）：一次 Create 后快照的全部数据面所需信息。
 * 资源按 die × 类型嵌套组织；指令空间不再走 range 视图，改由 ascCustom.allocInstSpace 按需申请。
 * 不携带设备号（调用线程经 aclrtGetDevice 取）、die 启用位图（由 dieMetadata.enabled 推导）与
 * 句柄代际（由 ccuInsKey 槽位代际承担）；ccuVersion 为翻译器初始化所需的设备级版本（定稿 2026-09-10 修订：放 Instance 顶层）。
 * ascCustom 为函数指针槽位数组（值按槽位含义强转对应 Fn 类型），由 hcomm 在创建时填充。
 */
typedef struct HcommCcuInstance {
    CommAbiHeader header;                         // ABI 头：version/magic/size 校验
    uint32_t ccuDieNum;                           // POD 中 die 槽位数（<= HCOMM_CCU_INSTANCE_DIE_CAPACITY）
    uint32_t ccuVersion;                          // 当前设备的 CCU 版本（V1/V2）
    HcommCcuDieRes ccuDieReses[HCOMM_CCU_INSTANCE_DIE_CAPACITY]; // 按 die 组织的资源视图（预留 8 槽）
    uint64_t ascCustom[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]; // 槽 0: 获取ChannelEntity接口；1：申请指令空间；2：下发指令；3-7 预留
    uint64_t reserved[8];                         // 预留扩展空间，当前必须清零
} HcommCcuInstance;

/**
 * @brief 查询 HCCL 通信域持有的 CCU Instance。
 * @param[in] comm HCCL 通信域句柄，不可为 nullptr。
 * @param[out] instance hcomm 持有的只读 CCU Instance 借用指针。
 * @return HcclResult。
 * @note instance 从通信域初始化成功后有效，到 HcclCommDestroy 开始销毁前失效；
 *       RegisterStart 不得与通信域销毁并发，RegisterStart 返回前由 asc-comm 深拷贝。
 */
extern HcclResult HcclCommQueryCcuRegisterContext(
    HcclComm comm, HcommCcuInstanceHandle *instance);

#ifdef __cplusplus
} // extern "C"

#endif // __cplusplus

#endif // HCOMM_CCU_RESOURCE_H
