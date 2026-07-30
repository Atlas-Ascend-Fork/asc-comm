/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_CCU_LAUNCH_KERNEL_H
#define ASC_COMM_CCU_LAUNCH_KERNEL_H

#if __has_include("hcomm/ccu/ccu_launch.h")
#include "hcomm/ccu/ccu_launch.h"
#elif __has_include("ccu/ccu_launch.h")
#include "ccu/ccu_launch.h"
#else
#include "ccu_launch.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t numBlocks;
    uint32_t reserved;
    uint64_t phyDieMask;
} HCommCcuSchd;

typedef struct {
    HCommCcuSchd ccuSchd;
    CcuInsHandle ccuIns;
    aclrtStream stream;
    void *attrs;
} HCommLaunchKernelCfg;


extern CcuResult HcommCcuHostKernelLaunch(const void *kernelFunc,
    const HCommLaunchKernelCfg *cfg, void *args);


#ifdef __cplusplus
}
#endif

#endif // ASC_COMM_CCU_LAUNCH_KERNEL_H
