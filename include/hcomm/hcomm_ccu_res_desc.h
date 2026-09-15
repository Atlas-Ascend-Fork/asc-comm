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
 * @file hcomm_ccu_res_desc.h
 * @brief CCU 资源描述符（ResDesc）跨 SO POD 定义（恢复定档接口链）。
 *
 * hcomm 和 asc-comm 各自保留一份同布局的 HcommCcuResDesc POD 定义。
 * HcommCcuResDesc 不对用户暴露，用户只感知 HcommCcuResDescHandle；
 * asc-comm 侧由 handle table 解析出实际 POD。
 */
#ifndef HCOMM_CCU_RES_DESC_POD_H
#define HCOMM_CCU_RES_DESC_POD_H

#include <stddef.h>
#include <stdint.h>
#include "hcomm/hcomm_ccu_abi.h"
#include "hcomm/hcomm_ccu_common.h"

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_RES_DESC_ABI_VERSION = 1,          // ResDesc POD ABI 版本
    HCOMM_CCU_RES_DESC_MAGIC_WORD = 0x43524453U, // "CRDS"，ResDesc POD 魔数
    HCOMM_CCU_RES_DESC_TYPE_COUNT = 32,          // 资源类型槽位数（定档预留到 32，现用 0-7）
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
    CommAbiHeader header;                           // ABI 头：version/magic/size 校验
    uint32_t dieId;                                 // 资源归属的 die id（创建描述符时设置）
    uint32_t resLength;                             // resNum 数组的有效长度（<= HCOMM_CCU_RES_DESC_TYPE_COUNT）
    uint32_t resNum[HCOMM_CCU_RES_DESC_TYPE_COUNT]; // 按资源类型索引的数量
} HcommCcuResDesc;

/** 资源描述符句柄：不对用户暴露 POD，由 asc-comm 侧 handle table 解析 */
typedef uint64_t HcommCcuResDescHandle;

/**
 * @brief 反查资源描述符内容（定档："hcomm 侧可以拿到实际需要的 ccu 资源"）。
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

static_assert(std::is_standard_layout<HcommCcuResDesc>::value, "HcommCcuResDesc must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuResDesc>::value, "HcommCcuResDesc must be trivially copyable");
static_assert(sizeof(HcommCcuResDesc) == 152, "HcommCcuResDesc ABI size changed");
static_assert(offsetof(HcommCcuResDesc, dieId) == 16, "HcommCcuResDesc.dieId ABI offset changed");
static_assert(offsetof(HcommCcuResDesc, resNum) == 24, "HcommCcuResDesc.resNum ABI offset changed");
#endif

#endif // HCOMM_CCU_RES_DESC_POD_H
