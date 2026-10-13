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

#include "hcomm/hcomm_ccu_resource.h"

#ifndef ASCCOMM_CCU_HOST_LAUNCH_HAS_ACLRT_STREAM
typedef void* aclrtStream; // 无 acl 头环境的最小兜底，实际为 aclrtStream（void*）
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t num_blocks; // mission 个数
    uint32_t reserved;
    uint64_t phy_die_mask; // 按位表示 0x01表示die0 0x02表示die1
    uint64_t binary_cache_tag;
} asccomm_ccu_schd;

typedef struct {
    asccomm_ccu_schd ccu_schd;
    CcuInsHandle ccu_ins;
    aclrtStream stream;
    void* attrs;
} asccomm_launch_kernel_cfg;

typedef struct {
    uint32_t numBlocks; // mission 个数
    uint32_t reserved;
    uint64_t phyDieMask; // 按位表示 0x01表示die0 0x02表示die1
    uint64_t binaryCacheTag;
} HcommCcuSchd;

typedef struct {
    HcommCcuSchd ccuSchd;
    CcuInsHandle ccuIns;
    aclrtStream stream;
    void* attrs;
} HcommLaunchKernelCfg;

typedef CcuResult ccu_result;

extern ccu_result asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args);

extern CcuResult HcommCcuHostKernelLaunch(
    const void* kernel_func, const char* kernel_name, const HcommLaunchKernelCfg* cfg, void* args);

extern uint64_t HcommCcuGetLaunchHashTag(const char* tag);
extern uint64_t asccomm_ccu_get_launch_hash_tag(const char* tag);

#ifdef __cplusplus
}
#endif

#endif // ASC_COMM_CCU_HOST_LAUNCH_H
