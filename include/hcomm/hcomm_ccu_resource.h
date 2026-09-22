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
 * @brief CCU instance、resource、channel 跨 SO POD 定义。
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
typedef enum { CCU_DEFAULT = 0, CCU_SCHED = 1, CCU_MS = 2, CCU_UNUSED = 254, CCU_RESERVED = 255 } CcuInstanceType;

struct HcommCcuInstance;

enum {
    HCOMM_CCU_INSTANCE_HANDLE_ABI_VERSION = 1,          // CcuInsHandle ABI 版本
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
    CommAbiHeader header;                     // ABI 头：version/magic/size 校验.
    uint64_t ccuInsKey;                       // 不透明实例键（0 为无效句柄哨兵）
    const struct HcommCcuInstance* ccuInsPtr; // hcomm 持有的 CCU Instance 借用指针
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
    HCOMM_CCU_CHANNEL_ABI_VERSION = 4,          // 当前 Channel Entity ABI 版本
    HCOMM_CCU_CHANNEL_MAGIC_WORD = 0x43435543U, // "CCUC"，标识 Channel Entity 类型
    HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY = 16,   // 每个 channel 持有的 XN/CKE 资源槽位数
};

/**
 * @brief CCU Channel 单次查询快照实体（16 槽 + 计数 + 事件掩码 + 远端 buffer 数组）。
 *
 * localVarIds/remoteVarIds 为本/远端 XN 槽，localEventIds/remoteEventIds 为本/远端 CKE 槽，
 * 实际有效数量由 localVarNum/remoteVarNum/localEventNum/remoteEventNum 给出（<= 容量）。
 * localEventMasks/remoteEventMasks 为预留槽位：mask 是 kernel 构建期的按次参数，
 * 非 channel 属性，当前固定填 0。
 * rmtCcuResBuffer 为对端 CCU 资源空间（RegedBufferEntity 口径，取代旧散字段）。
 * remoteBuffers 为 hcomm channel 内部远端 buffer 列表的借用指针，channel 销毁后失效；
 * 当前数据面未消费（翻译器仍按单 CCU 资源空间假设），为形状兼容预留。
 * 调用方提供存储并填好 ABI 头，hcomm 持锁一次填满快照，之后数据面只读该实体。
 */
typedef struct {
    CommAbiHeader header; // ABI 头：version/magic/size 校验

    int32_t deviceLogicId; // 所在设备逻辑 id
    uint32_t ccuVersion;   // CCU 版本（V1/V2）
    uint32_t dieId;        // die id
    uint32_t channelId;    // channel id（DFX 记录用）

    RegedBufferEntity rmtCcuResBuffer; // 对端 CCU 资源空间（基址/大小/token 折叠为 buffer 实体）

    uint32_t localVarNum;                   // 本端 XN 有效数量（<= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY）
    uint32_t remoteVarNum;                  // 远端 XN 有效数量
    uint32_t localEventNum;                 // 本端 CKE 有效数量
    uint32_t remoteEventNum;                // 远端 CKE 有效数量
    uint32_t remoteBufferNum;               // 远端 buffer 数量（remoteBuffers 数组长度）
    const RegedBufferEntity* remoteBuffers; // 远端 buffer 数组借用指针（hcomm 持有，当前数据面不消费）

    uint32_t localVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];      // 本端 XN id 槽
    uint32_t remoteVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];     // 远端 XN id 槽
    uint32_t localEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];    // 本端 CKE id 槽
    uint32_t remoteEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 远端 CKE id 槽
    uint32_t localEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 本端 CKE 掩码槽（预留，填 0）
    uint32_t remoteEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE 掩码槽（预留，填 0）

} HcommCcuChannelEntity;

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
    HCOMM_CCU_RES_DESC_ABI_VERSION = 1,          // ResDesc POD ABI 版本
    HCOMM_CCU_RES_DESC_MAGIC_WORD = 0x43524453U, // "CRDS"，ResDesc POD 魔数
    HCOMM_CCU_RES_DESC_TYPE_COUNT = 32,          // 资源类型槽位数（预留到 32，现用 0-7）
};

/** 资源描述符统计口径的类型枚举（block/非 block 在此合并计数；值域为跨 SO ABI，不可改动） */
typedef enum {
    CCU_RES_DESC_LOOP = 0,     // LoopEngine（loop + block loop 之和）
    CCU_RES_DESC_MS = 1,       // Memory Slice（ms + block ms 之和）
    CCU_RES_DESC_CKE = 2,      // 完成事件/信号量（cke + block cke 之和）
    CCU_RES_DESC_XN = 3,       // 变量（xn + block xn 之和）
    CCU_RES_DESC_COUNT_XN = 4, // 计数变量（预留，当前恒为 0）
    CCU_RES_DESC_GSA = 5,      // 通用存储地址（gsa + block gsa 之和）
    CCU_RES_DESC_INS = 6,      // 指令空间（指令条数）
    CCU_RES_DESC_MISSION = 7,  // mission 数
} CcuResDescType;

/** CCU 资源描述符 POD：按资源类型统计的申请数量（创建时设 dieId，查询后填数量） */
typedef struct {
    CommAbiHeader header; // ABI 头：version/magic/size 校验
    uint32_t dieId;       // 资源归属的 die id（创建描述符时设置）
    uint32_t resLength;   // resNum 数组的有效长度（<= HCOMM_CCU_RES_DESC_TYPE_COUNT）
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
extern CcuResult HcommCcuInsResDescQuery(HcommCcuResDescHandle resDesc, HcommCcuResDesc* out);

#ifdef __cplusplus
} // extern "C"

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
typedef const struct HcommCcuInstance* HcommCcuInstanceHandle;
enum {
    HCOMM_CCU_RES_ABI_VERSION = 8,               // 资源/CCU Instance POD ABI 版本
    HCOMM_CCU_INSTANCE_MAGIC_WORD = 0x43435549U, // HcommCcuInstance 类型魔数，值即 ASCII "CCUI"（CCU Instance），
                                                 // 接收方校验 header.magicWord 确认传入 POD 类型正确
    HCOMM_CCU_DIE_METADATA_MAGIC_WORD = 0x4343444DU, // HcommCcuDieMetadata 类型魔数，值即 ASCII "CCDM"（CCU Die
                                                     // Metadata）， 接收方校验 header.magicWord 确认传入 POD 类型正确
    HCOMM_CCU_RES_TYPE_COUNT = 32,       // 单 die 预留的资源类型槽位数（现用 0-10，11-31 预留）
    HCOMM_CCU_BATCH_RES_TYPE_COUNT = 11, // HcommCcuBatchResType 枚举值数量（数组维度用）
    HCOMM_CCU_INSTANCE_DIE_CAPACITY = 8, // Instance 预留的 die 槽位数（现网 2 个）
    HCOMM_CCU_INSTR_WIDTH_BYTES = 32, // 单条 CCU 微码指令宽度（sizeof(asc ccu_instr)），SubmitInsts 换算用
    HCOMM_CCU_VERSION_V1 = 0,         // CCU V1 版本号（ABI 内透传）
    HCOMM_CCU_VERSION_V2 = 1,         // CCU V2 版本号
    HCOMM_CCU_VERSION_INVALID = 0xFFFFFFFFU,   // 无效版本哨兵
    HCOMM_CCU_RESOURCE_XN_PER_SIZE = 8,        // 单个 XN 资源占用的空间步长（地址换算用）
    HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY = 0,   // ascCustom 槽 0：获取 ChannelEntity 接口
    HCOMM_CCU_ASC_CUSTOM_ALLOC_INST_SPACE = 1, // ascCustom 槽 1：申请 CCU 指令空间资源的接口
    HCOMM_CCU_ASC_CUSTOM_SUBMIT_INSTS = 2,     // ascCustom 槽 2：下发 CCU 指令接口
    HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT = 8,       // ascCustom 槽位总数（3-7 预留）
};

/** 批量资源的 range 类型枚举（resTypes 数组按该枚举值索引；block/非 block 在 range 视图分开，ResDesc 统计口径才合并）
 */
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
    uint32_t startId;  // 连续资源的起始 ID
    uint32_t count;    // 从 startId 开始的资源数量，必须大于 0
    uint64_t reserved; // 预留字段，当前必须为 0
} HcommCcuResRange;

/**
 * 一类资源的全部 range 段（按资源类型枚举值挂在 HcommCcuDieRes.resTypes[type] 上）。
 * resRanges 指向 hcomm RegisterContext 持有的只读数组，asc-comm 在 register_start 期间深拷贝。
 */
typedef struct {
    uint32_t resourceType;             // 资源类别（HcommCcuBatchResType）
    const HcommCcuResRange* resRanges; // 该类型的 range 数组（只读借用）
    uint32_t resRangeNum;              // range 数组元素数量
    uint64_t reserved;                 // 预留字段，当前必须为 0
} HcommCcuResRanges;

/**
 * 单个 die 的元数据：回路 channel 与 CCU 资源空间 buffer。
 * ccuResBuffer 按 {REGED_BUFFER_RMA, addr=XN 基址, ProtectionInfo.ub{tokenId, tokenValue}} 装配；
 * loopChannelId 为该 die 唯一的环回 channel（硬件上 die 内/间访问共用同一条，见 ccu_comp.cc "不区分die内die间"）。
 */
typedef struct {
    CommAbiHeader header;           // ABI 头：version/magic/size 校验
    uint32_t enabled;               // 该 die 是否启用（0/1）
    uint32_t missionKey;            // mission 密钥
    uint32_t loopChannelId;         // 环回 channel id（每 die 一条，die 内/间访问共用）
    RegedBufferEntity ccuResBuffer; // CCU 资源空间（XN 基址 + token）映射后的 buffer 信息
    uint32_t reserved;              // 预留字段，当前必须为 0
} HcommCcuDieMetadata;

/** 单个 die 的全部资源视图：按资源类型索引的 range 段数组 + die 元数据指针 */
typedef struct {
    uint32_t dieId;      // 该资源视图所属 die
    uint32_t resTypeNum; // 已使用的资源类型数（<= HCOMM_CCU_RES_TYPE_COUNT）
    HcommCcuResRanges resTypes[HCOMM_CCU_RES_TYPE_COUNT]; // 按资源类型枚举值索引
    const HcommCcuDieMetadata* dieMetadata;               // hcomm 持有的 die 元数据（只读借用）
    uint64_t reserved[16];                                // 预留字段，当前必须清零
} HcommCcuDieRes;

// ascCustom 槽 0：获取 Channel Entity（返回 hcomm 持有的实体借用指针，调用方深拷贝后使用）
typedef int32_t (*HcommCcuGetChannelEntityFn)(ChannelHandle channel, uint64_t* channelEntityPtr);
// ascCustom 槽 1：申请 CCU 指令空间（从当前 Instance 的指令池切分）
typedef int32_t (*HcommCcuAllocInstSpaceFn)(
    int32_t deviceLogicId, uint32_t dieId, uint32_t instNum, uint32_t* startInstId);
// ascCustom 槽 2：下发 CCU 指令（data 为 host 指针，hcomm 负责拷入设备内存并经驱动下发）
typedef int32_t (*HcommCcuSubmitInstsFn)(
    int32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId, const void* data, uint32_t instNum);

/**
 * CCU Instance（Register Context）：一次 Create 后快照的全部数据面所需信息。
 * 资源按 die × 类型嵌套组织；指令空间不再走 range 视图，改由 ascCustom.allocInstSpace 按需申请。
 * 不携带设备号（调用线程经 aclrtGetDevice 取）、die 启用位图（由 dieMetadata.enabled 推导）与
 * 句柄代际（由 ccuInsKey 槽位代际承担）；ccuVersion 为翻译器初始化所需的设备级版本（定稿 2026-09-10 修订：放 Instance
 * 顶层）。 ascCustom 为函数指针槽位数组（值按槽位含义强转对应 Fn 类型），由 hcomm 在创建时填充。
 */
typedef struct HcommCcuInstance {
    CommAbiHeader header; // ABI 头：version/magic/size 校验
    uint32_t ccuDieNum;   // POD 中 die 槽位数（<= HCOMM_CCU_INSTANCE_DIE_CAPACITY）
    uint32_t ccuVersion;  // 当前设备的 CCU 版本（V1/V2）
    HcommCcuDieRes ccuDieReses[HCOMM_CCU_INSTANCE_DIE_CAPACITY]; // 按 die 组织的资源视图（预留 8 槽）
    uint64_t
        ascCustom[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]; // 槽 0: 获取ChannelEntity接口；1：申请指令空间；2：下发指令；3-7
                                                    // 预留
} HcommCcuInstance;

/**
 * @brief 查询 HCCL 通信域持有的 CCU Instance。
 * @param[in] comm HCCL 通信域句柄，不可为 nullptr。
 * @param[out] instance hcomm 持有的只读 CCU Instance 借用指针。
 * @return HcclResult。
 * @note instance 从通信域初始化成功后有效，到 HcclCommDestroy 开始销毁前失效；
 *       RegisterStart 不得与通信域销毁并发，RegisterStart 返回前由 asc-comm 深拷贝。
 */
extern HcclResult HcclCommQueryCcuRegisterContext(HcclComm comm, HcommCcuInstanceHandle* instance);

#ifdef __cplusplus
} // extern "C"

#endif // __cplusplus

#endif // HCOMM_CCU_RESOURCE_H
