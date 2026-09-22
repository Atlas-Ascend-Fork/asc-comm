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

#include "hcomm/hcomm_ccu_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ccu_condition_eq = 0,
    ccu_condition_ne = 1,
} ccu_condition_type;

typedef uint64_t ccu_loop;
typedef uint64_t ccu_loop_group;
typedef uint64_t ccu_loop_executors;

typedef struct {
    uint64_t addr_offset;
    uint64_t iter_num;
} ccu_loop_config;

typedef struct {
    uint32_t clone_num;
    uint32_t clone_loop_offset;
    uint32_t addr_offset;
    uint32_t ccu_buffer_offset;
    uint32_t event_offset;
} ccu_loop_group_config;

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

#endif // CCU_API_TYPES_H
