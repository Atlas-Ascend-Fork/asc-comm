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
