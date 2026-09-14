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
 * @file hcomm_ccu_common.h
 * @brief CCU 数据面（asc-comm）与控制面（hcomm）共用的基础错误码与句柄类型。
 *
 * 这些类型原定义于 hcomm 数据面侧（ccu_res.h），数据面迁移后统一由 hcomm 包内头提供，
 * asc-comm 通过 include 路径引用，保证两仓对同一组 CcuResult 枚举值编译一致。
 */
#ifndef HCOMM_CCU_COMMON_H
#define HCOMM_CCU_COMMON_H

#include <stdint.h>

#include "hcomm_res_defs.h"
#include "hcomm/hcomm_ccu_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** CCU C 接口返回码：hcomm C 适配层返回的错误码必须与 asc-comm 解释一致，异常不能穿过 C ABI */
typedef enum {
    CCU_SUCCESS = 0,
    CCU_E_PARA = 1,
    CCU_E_PTR = 2,
    CCU_E_INTERNAL = 4,
    CCU_E_NOT_SUPPORT = 5,
    CCU_E_NOT_FOUND = 6,
    CCU_E_UNAVAIL = 7,
    CCU_E_RUNTIME = 15,
    CCU_E_DRV_START = 4096,       // 驱动错误起始
    CCU_E_DRV_INIT_FAILED = 4097, // 驱动初始化失败
    CCU_E_DRV_BUSY = 4098,        // 驱动忙（可回退场景）
    CCU_E_DRV_END = 4224,         // 驱动错误结束
    CCU_E_RESERVED = 9216
} CcuResult;

/** CCU Instance 类型：注册上下文申请的实例类型；hcomm 创建 RegisterContext 时校验，并据此分配资源句柄 */
typedef enum { CCU_DEFAULT = 0, CCU_SCHED = 1, CCU_MS = 2, CCU_UNUSED = 254, CCU_RESERVED = 255 } CcuInstanceType;

struct HcommCcuInstance;

enum {
    HCOMM_CCU_INSTANCE_HANDLE_ABI_VERSION = 1,          // CcuInsHandle ABI 版本
    HCOMM_CCU_INSTANCE_HANDLE_MAGIC_WORD = 0x43435548U, // "CCUH"，句柄魔数
};

/**
 * CCU Instance 句柄（定档结构体口径，对应定档 CommAbiHeader + ccuInsKey + ccuInsPtr 三字段）：
 * header 为 ABI 头：version/magic/size 校验，防两仓布局不一致时按值传递越界读；
 * ccuInsKey 为 hcomm 位编码的不透明键（generation<<32 | slot+1），用于跨 SO 传递与查找；
 * ccuInsPtr 指向 hcomm 持有的 HcommCcuInstance（只读借用，随 RegisterContext 生命周期有效）。
 * hcomm 负责编码、校验、销毁，asc-comm 只能按值保存和回传，不得解引用 ccuInsPtr 之外的成员。
 */
typedef struct {
    CommAbiHeader header;                     // ABI 头：version/magic/size 校验
    uint64_t ccuInsKey;                       // 不透明实例键（0 为无效句柄哨兵）
    const struct HcommCcuInstance* ccuInsPtr; // hcomm 持有的 CCU Instance 借用指针
} CcuInsHandle;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <stddef.h>
#include <type_traits>
static_assert(std::is_standard_layout<CcuInsHandle>::value, "CcuInsHandle must be standard-layout");
static_assert(std::is_trivially_copyable<CcuInsHandle>::value, "CcuInsHandle must be trivially copyable");
static_assert(sizeof(CcuInsHandle) == 32, "CcuInsHandle ABI size changed");
static_assert(offsetof(CcuInsHandle, ccuInsKey) == 16, "CcuInsHandle.ccuInsKey ABI offset changed");
static_assert(offsetof(CcuInsHandle, ccuInsPtr) == 24, "CcuInsHandle.ccuInsPtr ABI offset changed");
#endif

#endif // HCOMM_CCU_COMMON_H
