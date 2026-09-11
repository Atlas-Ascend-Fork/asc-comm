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
 * @file hcomm_ccu_res.h
 * @brief CCU 资源层跨 SO POD 定义（Register Context 及相关资源 range/metadata）。
 *
 * 用户通过只读不透明句柄借用 Register Context，具体 POD 仅由 hcomm 和 asc-comm 内部解释。
 */
#ifndef HCOMM_CCU_RES_H
#define HCOMM_CCU_RES_H

#include <stddef.h>
#include <stdint.h>
#include "hcomm/hcomm_ccu_abi.h"
#include "hcomm/hcomm_ccu_common.h"
#include "hcomm/hcomm_ccu_control.h"
#include "hcomm/hcomm_types.h"
#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

typedef const struct HcommCcuRegisterContextOpaque* HcommCcuRegisterContextHandle;

enum {
    HCOMM_CCU_RES_ABI_VERSION = 7,                              // 资源/注册上下文 POD ABI 版本
    HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD = 0x43435532U,        // "CCU2"，RegisterContext POD 魔数
    HCOMM_CCU_BATCH_RES_TYPE_COUNT = 11,                        // 批量资源类型总数（不含 INS）
    HCOMM_CCU_RES_INSTRUCTION = HCOMM_CCU_BATCH_RES_TYPE_COUNT, // INS 资源的 range 类型号
    HCOMM_CCU_VERSION_V1 = 0,                                   // CCU V1 版本号（ABI 内透传）
    HCOMM_CCU_VERSION_V2 = 1,                                   // CCU V2 版本号
    HCOMM_CCU_VERSION_INVALID = 0xFFFFFFFFU,                    // 无效版本哨兵
    HCOMM_CCU_RESOURCE_XN_PER_SIZE = 8, // 单个 XN 资源占用的空间步长（地址换算用）
};

/** 批量资源的 range 类型枚举（与 CcuResRepository 各资源组一一对应） */
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

/** 一段连续资源的 range：类型 + die + 起始 id + 数量 */
typedef struct {
    uint32_t resourceType; // 资源类别：LOOP、MS、CKE、XN、GSA、MISSION 或 INS
    uint32_t dieId;        // 该资源范围所属 die
    uint32_t startId;      // 连续资源的起始 ID
    uint32_t count;        // 从 startId 开始的资源数量，必须大于 0
} HcommCcuResRangePod;

/**
 * 资源 range 数组的只读视图，由 Register Context 持有。
 */
typedef struct {
    const HcommCcuResRangePod* ranges; // Register Context 持有的只读 Range 数组
    uint32_t count;                    // Range 元素数量
    uint32_t reserved;                 // 对齐和扩展字段，当前必须为 0
} HcommCcuResRangeViewPod;

/** 单个 die 的元数据：回路 channel、XN 基址与资源空间 token 等 */
typedef struct {
    uint32_t enabled;                 // 该 die 是否启用
    uint32_t missionKey;              // mission 密钥
    uint32_t innerDieLoopChannelId;   // die 内回路 channel id
    uint32_t interDieLoopChannelId;   // die 间回路 channel id
    uint64_t xnBaseAddr;              // XN 基址
    uint64_t resourceSpaceTokenId;    // CCU 资源空间 token id
    uint64_t resourceSpaceTokenValue; // CCU 资源空间 token value
    uint32_t instructionCapacity;     // 已分配的指令容量
    uint32_t reserved;                // 预留字段，当前必须为 0
} HcommCcuDieMetadataPod;

/** 通信域注册上下文：一次 Create 后快照的全部数据面所需信息 */
typedef struct {
    HcommCcuAbiHeaderPod header;                       // ABI 头：version/magic/size 校验
    uint64_t generation;                               // 资源世代号（用于句柄校验）
    int32_t deviceLogicId;                             // 注册上下文所属设备逻辑 ID
    uint32_t ccuVersion;                               // 当前设备的 CCU 版本
    uint32_t dieNum;                                   // POD 中 die 槽位数，当前固定为 2
    uint32_t validDieMask;                             // 有效 die 位图
    CcuInsHandle instanceHandle;                       // 对应 hcomm 侧 instance/资源句柄
    uint32_t instanceType;                             // 本上下文申请的 CCU Instance 类型
    HcommCcuControlOpsPod controlOps;                  // 控制面注入的函数表（channelQuery 等）
    HcommCcuResRangeViewPod instanceResources;         // Context 持有的只读资源 range 视图
    HcommCcuDieMetadataPod die[HCOMM_CCU_MAX_DIE_NUM]; // 每个 die 的能力、基址、Token 和指令容量
    uint64_t reserved[2];                              // 预留扩展空间，当前必须清零
} HcommCcuRegisterContextPod;

/**
 * @brief 查询 HCCL 通信域持有的 CCU Register context。
 * @param[in] comm HCCL 通信域句柄，不可为 nullptr。
 * @param[out] context hcomm 持有的只读 Register Context 借用指针。
 * @return HcclResult。
 * @note context 从通信域初始化成功后有效，到 HcclCommDestroy 开始销毁前失效；
 *       RegisterStart 不得与通信域销毁并发，RegisterStart 返回前由 asc-comm 深拷贝。
 */
extern HcclResult HcclCommQueryCcuRegisterContext(HcclComm comm, HcommCcuRegisterContextHandle* context);

#ifdef __cplusplus
}

static_assert(std::is_standard_layout<HcommCcuResRangePod>::value, "HcommCcuResRangePod must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuResRangePod>::value, "HcommCcuResRangePod must be trivially copyable");
static_assert(
    std::is_standard_layout<HcommCcuResRangeViewPod>::value, "HcommCcuResRangeViewPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuResRangeViewPod>::value, "HcommCcuResRangeViewPod must be trivially copyable");
static_assert(std::is_standard_layout<HcommCcuDieMetadataPod>::value, "HcommCcuDieMetadataPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuDieMetadataPod>::value, "HcommCcuDieMetadataPod must be trivially copyable");
static_assert(
    std::is_standard_layout<HcommCcuRegisterContextPod>::value, "HcommCcuRegisterContextPod must be standard-layout");
static_assert(
    std::is_trivially_copyable<HcommCcuRegisterContextPod>::value,
    "HcommCcuRegisterContextPod must be trivially copyable");
static_assert(sizeof(HcommCcuResRangePod) == 16, "HcommCcuResRangePod ABI size changed");
static_assert(offsetof(HcommCcuResRangePod, count) == 12, "HcommCcuResRangePod.count ABI offset changed");
static_assert(sizeof(HcommCcuResRangeViewPod) == 16, "HcommCcuResRangeViewPod ABI size changed");
static_assert(offsetof(HcommCcuResRangeViewPod, count) == 8, "HcommCcuResRangeViewPod.count ABI offset changed");
static_assert(sizeof(HcommCcuDieMetadataPod) == 48, "HcommCcuDieMetadataPod ABI size changed");
static_assert(
    offsetof(HcommCcuDieMetadataPod, xnBaseAddr) == 16, "HcommCcuDieMetadataPod.xnBaseAddr ABI offset changed");
static_assert(sizeof(HcommCcuRegisterContextPod) == 264, "HcommCcuRegisterContextPod ABI size changed");
static_assert(
    offsetof(HcommCcuRegisterContextPod, generation) == 16, "HcommCcuRegisterContextPod.generation ABI offset changed");
static_assert(
    offsetof(HcommCcuRegisterContextPod, instanceHandle) == 40,
    "HcommCcuRegisterContextPod.instanceHandle ABI offset changed");
static_assert(
    offsetof(HcommCcuRegisterContextPod, controlOps) == 56, "HcommCcuRegisterContextPod.controlOps ABI offset changed");
static_assert(
    offsetof(HcommCcuRegisterContextPod, instanceResources) == 136,
    "HcommCcuRegisterContextPod.instanceResources ABI offset changed");
static_assert(offsetof(HcommCcuRegisterContextPod, die) == 152, "HcommCcuRegisterContextPod.die ABI offset changed");
#endif

#endif // HCOMM_CCU_RES_H
