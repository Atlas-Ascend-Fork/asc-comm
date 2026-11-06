/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_RUNTIME_ABI_H
#define CCU_RUNTIME_ABI_H

#include "ccu/experimental/ccu_runtime.h"

typedef enum ccu_task_arg_kind_t { CCU_TASK_ARG_U64 = 1, CCU_TASK_ARG_I64 = 2 } ccu_task_arg_kind_t;

typedef struct ccu_task_arg_schema_t {
    uint32_t kind;
    uint32_t flags;
    const char* name;
} ccu_task_arg_schema_t;

typedef enum ccu_specialization_kind_t {
    CCU_SPECIALIZATION_UNSIGNED = 1,
    CCU_SPECIALIZATION_SIGNED = 2,
    CCU_SPECIALIZATION_ENUM = 3
} ccu_specialization_kind_t;

typedef struct ccu_specialization_value_t {
    uint32_t kind;
    uint32_t bit_width;
    uint64_t bits;
    const char* type_identity;
} ccu_specialization_value_t;

typedef struct ccu_image_descriptor_t {
    uint32_t struct_size;
    const char* target;
    const uint8_t* ccuobj;
    uint64_t ccuobj_size;
} ccu_image_descriptor_t;

typedef struct ccu_rank_entry_t {
    uint32_t communicator_rank;
    uint32_t kernel_index;
} ccu_rank_entry_t;

typedef struct ccu_kernel_descriptor_t {
    uint32_t struct_size;
    const ccu_image_descriptor_t* image;
    const ccu_specialization_value_t* specialization_values;
    uint32_t specialization_value_count;
    uint32_t communicator_size;
    const ccu_rank_entry_t* rank_entries;
    const ccu_task_arg_schema_t* task_args;
    uint32_t task_arg_count;
} ccu_kernel_descriptor_t;

#endif /* CCU_RUNTIME_ABI_H */
