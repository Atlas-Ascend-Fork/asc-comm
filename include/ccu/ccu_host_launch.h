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

#if defined(__cplusplus) && __has_include("hcomm/ccu/ccu_types.h")
#include "hcomm/ccu/ccu_types.h"
#elif defined(__cplusplus) && __has_include("ccu/ccu_types.h")
#include "ccu/ccu_types.h"
#elif defined(__cplusplus) && __has_include("ccu_types.h")
#include "ccu_types.h"
#else
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCU_SUCCESS = 0,
    CCU_E_PARA = 1,
    CCU_E_PTR = 2,
    CCU_E_INTERNAL = 4,
    CCU_E_NOT_SUPPORT = 5,
    CCU_E_RUNTIME = 15
} CcuResult;

typedef uint64_t CcuInsHandle;
typedef uint64_t CcuKernelHandle;
typedef void *CcuKernelArg;

#ifdef __cplusplus
}
#endif
#endif

#if __has_include("acl/acl_base_rt.h")
#include "acl/acl_base_rt.h"
#elif !defined(INC_EXTERNAL_ACL_ACL_BASE_RT_H_)
typedef void *aclrtStream;
#endif

#ifndef HCOMM_WEAK_SYMBOL
#define HCOMM_WEAK_SYMBOL __attribute__((weak))
#endif

#ifdef __cplusplus
extern "C" {
#endif

extern CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegister(CcuInsHandle insHandle, uint32_t dieId,
    const char *kernelFuncName, const void *kernelFunc,
    const void **kernelArgs, uint32_t argNum, CcuKernelHandle *kernelHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle) HCOMM_WEAK_SYMBOL;
extern CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t *taskArgsNum) HCOMM_WEAK_SYMBOL;

#ifdef __cplusplus
}
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


#ifdef __cplusplus
}
#endif

#endif // ASC_COMM_CCU_HOST_LAUNCH_H
