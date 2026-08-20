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

#include "ccu/hcomm/ccu_launch.h"
#include "hcomm/hcomm_ccu_launch.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

typedef struct {
    uint32_t num_blocks;  // mission个数
    uint32_t reserved;   // 对齐预留
    uint64_t phy_die_mask; // 指定在哪个物理Die上执行
} asccomm_ccu_schd;

typedef struct {
    asccomm_ccu_schd ccu_schd;
    CcuInsHandle ccu_ins;
    const HcommCcuLaunchContextPod* launch_context;
    void* attrs; // 预留参数
    const char* kernel_name;
    HcommCcuRegisterContextHandle register_context;
} asccomm_ccu_host_kernel_launch_cfg;

typedef struct {
    const void** kernel_args;
    uint32_t kernel_arg_num;
    const void* task_args;
    uint32_t task_arg_num;
} asccomm_ccu_host_kernel_args;

extern CcuResult asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const asccomm_ccu_host_kernel_launch_cfg* cfg, const asccomm_ccu_host_kernel_args* args_);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_HOST_LAUNCH_H
