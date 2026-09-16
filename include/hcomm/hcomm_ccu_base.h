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
 * @file hcomm_ccu_base.h
 * @brief CCU 跨 SO ABI 基础常量和返回码。
 */
#ifndef HCOMM_CCU_BASE_H
#define HCOMM_CCU_BASE_H

#include <stdint.h>

#include "hcomm_res_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 支持的最大 DIE 数量 */
enum {
    HCOMM_CCU_MAX_DIE_NUM = 2,
};

/** CCU C 接口返回码：异常不能穿过 C ABI */
typedef enum {
    CCU_SUCCESS = 0,
    CCU_E_PARA = 1,
    CCU_E_PTR = 2,
    CCU_E_INTERNAL = 4,
    CCU_E_NOT_SUPPORT = 5,
    CCU_E_NOT_FOUND = 6,
    CCU_E_UNAVAIL = 7,
    CCU_E_RUNTIME = 15,
    CCU_E_DRV_START = 4096,
    CCU_E_DRV_INIT_FAILED = 4097,
    CCU_E_DRV_BUSY = 4098,
    CCU_E_DRV_END = 4224,
    CCU_E_RESERVED = 9216
} CcuResult;

#ifdef __cplusplus
}
#endif

#endif // HCOMM_CCU_BASE_H
