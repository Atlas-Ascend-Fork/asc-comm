/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_CCU_HOST_LAUNCH_H
#define ASC_COMM_CCU_HOST_LAUNCH_H

#include <stdint.h>

#if defined(__has_include)
#if __has_include("hcomm/ccu/ccu_launch.h")
#include "hcomm/ccu/ccu_launch.h"
#define ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES 1
#elif __has_include("ccu/ccu_launch.h")
#include "ccu/ccu_launch.h"
#define ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES 1
#elif __has_include("ccu_launch.h")
#include "ccu_launch.h"
#define ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES 1
#endif
#endif

#ifndef ASCCOMM_CCU_HOST_LAUNCH_HAS_HCOMM_TYPES
#ifndef CCU_TYPES_H
#define CCU_TYPES_H
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

typedef uint64_t CcuInsHandle;
typedef uint64_t CcuKernelHandle;
typedef void *CcuKernelArg;
typedef CcuResult (*CcuKernelFunc)(CcuKernelArg arg);
#endif
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t num_blocks;
    uint32_t reserved;
    uint64_t phy_die_mask;
    uint64_t binary_cache_tag;
} asccomm_ccu_chd;

typedef struct {
    asccomm_ccu_chd ccu_schd;
    CcuInsHandle ccu_ins;
    aclrtStream stream;
    void *attrs;
} asccomm_launch_kernel_cfg;


extern CcuResult asccomm_ccu_host_kernel_launch(const void *kernel_func,
    const asccomm_launch_kernel_cfg *cfg, void *args);

extern uint64_t asccomm_ccu_get_launch_hash_tag(const char *tag);

#ifdef __cplusplus
}
#endif

#endif // ASC_COMM_CCU_HOST_LAUNCH_H
