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

/** CCU Instance 句柄：64 位不透明注册上下文句柄；hcomm 负责编码、校验、销毁，asc-comm 只能按值保存和回传 */
typedef uint64_t CcuInsHandle;

#ifdef __cplusplus
}
#endif

#endif // HCOMM_CCU_COMMON_H
