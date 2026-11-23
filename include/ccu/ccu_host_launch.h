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
 * @file ccu_host_launch.h
 * @brief CCU kernel 的 host 侧下发接口（launch、hash tag 等）。
 */

#ifndef ASC_COMM_CCU_HOST_LAUNCH_H
#define ASC_COMM_CCU_HOST_LAUNCH_H

// CCU host 侧下发接口；同时保留 hcomm 旧调用方的驼峰入口，仅用于过渡期兼容。
#include <stdint.h>

#include "ccu/ccu_api/ccu_types.h"

#ifndef ASCCOMM_CCU_HOST_LAUNCH_HAS_ACLRT_STREAM
typedef void* aclrtStream; // 无 acl 头环境的最小兜底，实际为 aclrtStream（void*）
#endif

#ifdef __cplusplus
extern "C" {
#endif

// 与 hcomm 侧同名句柄类型 ABI 等价，供过渡期驼峰入口及实现使用。
typedef uint64_t CcuInsHandle;
typedef uint64_t CcuKernelHandle;

typedef struct {
    uint32_t num_blocks; // mission 个数
    uint32_t reserved;
    uint64_t phy_die_mask; // 按位表示 0x01表示die0 0x02表示die1
    uint64_t binary_cache_tag;
} asccomm_ccu_schd;

typedef struct {
    asccomm_ccu_schd ccu_schd;
    void* ccu_ins; // CCU 实例句柄
    aclrtStream stream;
    void* attrs;
} asccomm_launch_kernel_cfg;

/**
 * @brief 下发 CCU kernel。
 * @param[in] kernel_func kernel 函数地址
 * @param[in] kernel_name kernel 名
 * @param[in] cfg 下发配置
 * @param[in] args kernel 入参
 * @return ccu_result
 */
extern ccu_result asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const char* kernel_name, const asccomm_launch_kernel_cfg* cfg, void* args);

/**
 * @brief 计算下发哈希 tag。
 * @param[in] tag 原始 tag 字符串
 * @return 哈希值
 */
extern uint64_t asccomm_ccu_get_launch_hash_tag(const char* tag);

// hcomm 旧调用方的驼峰风格配置，逐字段等价于 asccomm_launch_kernel_cfg；
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

// 兼容入口
extern ccu_result HcommCcuHostKernelLaunch(
    const void* kernel_func, const char* kernel_name, const HcommLaunchKernelCfg* cfg, void* args);

extern uint64_t HcommCcuGetLaunchHashTag(const char* tag);

#ifdef __cplusplus
}
#endif

#endif // ASC_COMM_CCU_HOST_LAUNCH_H
