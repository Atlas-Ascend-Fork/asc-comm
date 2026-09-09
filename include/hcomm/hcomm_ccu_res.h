/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the License).
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hcomm_ccu_res.h
 * @brief CCU 资源层跨 SO POD 定义（CCU Instance / Register Context）。
 *
 * 按《pod方案定档.md》口径：资源按 die × 资源类型两级嵌套组织，每类持多段连续 range；
 * die 元数据（回路 channel、CCU 资源空间 buffer）由 HcommCcuDieMetadata 承载；
 * 控制面注入 hcomm 实现的函数表 ascCustom（定档槽 0/1/2 + DFX 查询链）。
 * asc-comm 在 register_start 时对整个 Instance 做一次性深拷贝快照。
 */
#ifndef HCOMM_CCU_RES_H
#define HCOMM_CCU_RES_H

#include <stddef.h>
#include <stdint.h>
#include "hcomm/hcomm_ccu_abi.h"
#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/hcomm_ccu_common.h"
#include "hcomm/hcomm_ccu_dfx.h"
#include "hcomm/hcomm_res_entity_defs.h"
#include "hcomm/hcomm_types.h"
#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

struct HcommCcuInstance;
typedef const struct HcommCcuInstance *HcommCcuInstanceHandle;
enum {
    HCOMM_CCU_RES_ABI_VERSION = 8,             // 资源/注册上下文 POD ABI 版本（定档重构）
    HCOMM_CCU_INSTANCE_MAGIC_WORD = 0x43435549U, // "CCUI"，CCU Instance POD 魔数
    HCOMM_CCU_DIE_METADATA_MAGIC_WORD = 0x4343444DU, // "CCDM"，DieMetadata POD 魔数
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
 * loopChannelId 为该 die 唯一的环回 channel（硬件上 die 内/间访问共用同一条，见 ccu_comp.cc "不区分die内die间"）；
 * ccuVersion 为翻译器初始化所需的 die 级版本信息（遗留项：见 pod定档对齐实施记录.md）。
 */
typedef struct {
    HcommCcuAbiHeaderPod header;         // ABI 头：version/magic/size 校验
    uint32_t enabled;                    // 该 die 是否启用（0/1）
    uint32_t missionKey;                 // mission 密钥
    uint32_t ccuVersion;                 // 该 die 的 CCU 版本（V1/V2；同芯片各 die 一致）
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

// 定档 ascCustom 槽 0：获取 Channel Entity（返回 hcomm 持有的实体借用指针，调用方深拷贝后使用）
typedef int32_t (*HcommCcuGetChannelEntityFn)(ChannelHandle channel, uint64_t *channelPodEntityPtr);
// 定档 ascCustom 槽 1：申请 CCU 指令空间（从当前 RegisterContext 的指令池切分）
typedef int32_t (*HcommCcuAllocInstSpaceFn)(int32_t deviceLogicId, uint32_t dieId, uint32_t instNum,
    uint32_t *startInstId);
// 定档 ascCustom 槽 2：下发 CCU 指令（data 为 host 指针，hcomm 负责拷入设备内存并经驱动下发）
typedef int32_t (*HcommCcuSubmitInstsFn)(int32_t deviceLogicId, uint32_t dieId, uint32_t startInstructionId,
    const void *data, uint32_t instNum);

/**
 * CCU Instance（Register Context）：一次 Create 后快照的全部数据面所需信息（定档草图口径）。
 * 资源按 die × 类型嵌套组织；指令空间不再走 range 视图，改由 ascCustom.allocInstSpace 按需申请。
 * 不携带设备号（调用线程经 aclrtGetDevice 取）、die 启用位图（由 dieMetadata.enabled 推导）与
 * 句柄代际（由 ccuInsKey 槽位代际承担）；ccuVersion 归 DieMetadata。
 * ascCustom 为函数指针槽位数组（值按槽位含义强转对应 Fn 类型），由 hcomm 在创建时填充。
 */
typedef struct HcommCcuInstance {
    HcommCcuAbiHeaderPod header;                  // ABI 头：version/magic/size 校验
    uint32_t ccuDieNum;                           // POD 中 die 槽位数（<= HCOMM_CCU_INSTANCE_DIE_CAPACITY）
    uint32_t reserved0;                           // 对齐字段，当前必须为 0
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

static_assert(std::is_standard_layout<HcommCcuResRange>::value,
    "HcommCcuResRange must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuResRange>::value,
    "HcommCcuResRange must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuResRanges>::value,
    "HcommCcuResRanges must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuResRanges>::value,
    "HcommCcuResRanges must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuDieMetadata>::value,
    "HcommCcuDieMetadata must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuDieMetadata>::value,
    "HcommCcuDieMetadata must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuDieRes>::value,
    "HcommCcuDieRes must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuDieRes>::value,
    "HcommCcuDieRes must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuInstance>::value,
    "HcommCcuInstance must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuInstance>::value,
    "HcommCcuInstance must be trivially copyable");
static_assert(sizeof(HcommCcuResRange) == 16, "HcommCcuResRange ABI size changed");
static_assert(offsetof(HcommCcuResRange, count) == 4, "HcommCcuResRange.count ABI offset changed");
static_assert(sizeof(HcommCcuResRanges) == 24, "HcommCcuResRanges ABI size changed");
static_assert(offsetof(HcommCcuResRanges, resRanges) == 8, "HcommCcuResRanges.resRanges ABI offset changed");
static_assert(offsetof(HcommCcuResRanges, resRangeNum) == 16, "HcommCcuResRanges.resRangeNum ABI offset changed");
static_assert(sizeof(HcommCcuDieMetadata) == 152, "HcommCcuDieMetadata ABI size changed");
static_assert(offsetof(HcommCcuDieMetadata, ccuResBuffer) == 40, "HcommCcuDieMetadata.ccuResBuffer ABI offset changed");
static_assert(sizeof(HcommCcuDieRes) == 912, "HcommCcuDieRes ABI size changed");
static_assert(offsetof(HcommCcuDieRes, resTypes) == 8, "HcommCcuDieRes.resTypes ABI offset changed");
static_assert(offsetof(HcommCcuDieRes, dieMetadata) == 776, "HcommCcuDieRes.dieMetadata ABI offset changed");
static_assert(sizeof(HcommCcuInstance) == 7448, "HcommCcuInstance ABI size changed");
static_assert(offsetof(HcommCcuInstance, ccuDieReses) == 24, "HcommCcuInstance.ccuDieReses ABI offset changed");
static_assert(offsetof(HcommCcuInstance, ascCustom) == 7320, "HcommCcuInstance.ascCustom ABI offset changed");
#endif // __cplusplus

#endif // HCOMM_CCU_RES_H
