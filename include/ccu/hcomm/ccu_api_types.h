/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_API_TYPES_H
#define CCU_API_TYPES_H

#include <stdint.h>

#include "hcomm/ccu/ccu_types.h"
#include "hcomm/hcomm_res_ccu_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ccu_condition_eq = 0,
    ccu_condition_ne = 1,
    ccu_condition_lt = 2,
    ccu_condition_le = 3,
    ccu_condition_gt = 4,
    ccu_condition_ge = 5,
} ccu_condition_type;

typedef uint64_t ccu_loop;
typedef uint64_t ccu_loop_group;
typedef uint64_t ccu_loop_executors;

typedef struct {
    uint64_t addr_offset;
    uint64_t iter_num;
} ccu_loop_config;

// 带版本头的 loop 配置 POD：header 校验通过后，version + size 决定尾部哪些字段有效；
// 新字段仅可在尾部追加（reserved 区）以保持 ABI 兼容。布局与 CANN 包 CcuLoopCfg 对齐。
#define CCU_CFG_MAGIC_WORD 0xCC0CF000u
#define CCU_LOOP_CFG_VERSION 1u
#define CCU_LOOPGROUP_CFG_VERSION 1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t reserved;
} ccu_cfg_header;

typedef struct {
    ccu_cfg_header header;
    uint64_t addr_offset;
    uint64_t iter_num;
    uint64_t reserved[4];
} ccu_loop_cfg;

typedef struct {
    uint32_t clone_num;
    uint32_t clone_loop_offset;
    uint32_t addr_offset;
    uint32_t ccu_buffer_offset;
    uint32_t event_offset;
} ccu_loop_group_config;

typedef struct {
    ccu_cfg_header header;
    uint32_t clone_num;
    uint32_t clone_loop_offset;
    uint32_t addr_offset;
    uint32_t ccu_buffer_offset;
    uint32_t event_offset;
    uint32_t var_offset; // A6 专属，旧 ccu_loop_group_config 不含此字段
    uint32_t reserved[8];
} ccu_loop_group_cfg;

typedef uint64_t ccu_kernel_handle;
typedef uint64_t ccu_variable_handle;
typedef uint64_t ccu_address_handle;
typedef uint64_t ccu_event_handle;
typedef uint64_t ccu_buffer_handle;
typedef uint64_t ccu_local_addr_handle;
typedef uint64_t ccu_remote_addr_handle;
typedef void* ccu_kernel_arg;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
// 由封装统一盖版本头，用户无需关心；所有望产出/传递 ccu_loop_cfg 的路径必须经此填头，
// 禁止手抄四元组内联构造。
static inline void ccu_loop_cfg_init(ccu_loop_cfg* cfg)
{
    cfg->header.magic = CCU_CFG_MAGIC_WORD;
    cfg->header.version = CCU_LOOP_CFG_VERSION;
    cfg->header.size = static_cast<uint32_t>(sizeof(ccu_loop_cfg));
    cfg->header.reserved = 0u;
}

static inline void ccu_loop_group_cfg_init(ccu_loop_group_cfg* cfg)
{
    cfg->header.magic = CCU_CFG_MAGIC_WORD;
    cfg->header.version = CCU_LOOPGROUP_CFG_VERSION;
    cfg->header.size = static_cast<uint32_t>(sizeof(ccu_loop_group_cfg));
    cfg->header.reserved = 0u;
}
#endif

#endif // CCU_API_TYPES_H
