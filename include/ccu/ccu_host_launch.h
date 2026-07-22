/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_HOST_LAUNCH_H
#define CCU_HOST_LAUNCH_H

#include "ccu_launch.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    uint32_t numBlocks;  // mission个数
    uint32_t reserved;   // 对齐预留
    uint64_t phyDieMask; // 指定在哪个物理Die上执行
} HcommCcuSchd;

typedef struct {
    HcommCcuSchd ccuSchd;
    CcuInsHandle ccuIns;
    ThreadHandle thread;
    void *attrs; // 预留参数
    const char *kernelName;
} HcommCcuHostKernelLaunchCfg;

typedef struct {
    const void **kernelArgs;
    uint32_t kernelArgNum;
    const void *taskArgs;
    uint32_t taskArgNum;
} HcommCcuHostKernelArgs;

extern CcuResult HcommCcuHostKernelLaunch(const void *kernelFunc,
    const HcommCcuHostKernelLaunchCfg *cfg,
    const HcommCcuHostKernelArgs *args) HCOMM_WEAK_SYMBOL;

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_HOST_LAUNCH_H
