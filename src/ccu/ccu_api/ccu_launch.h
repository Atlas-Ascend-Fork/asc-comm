/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LAUNCH_H
#define CCU_LAUNCH_H

// CCU kernel 注册与 launch 的 C ABI 头（仓内实现使用，不随用户头发布）。
// Register Context 以 void* 不透明句柄传递，与 hccl 内部类型解耦，符合控制面/数据面 ABI 边界约束。
#include "ccu/ccu_api/ccu_types.h"
#include "impl/adv_api/detail/hccl/cc/src/common/hcomm_dlsym/ccu/ccu_res_defs_dl.h"
#include "hcomm/ccu/hcomm_res_ccu_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// stream 直接以 void* 传递（调用方传 aclrtStream），避免公共头依赖 acl 头文件
typedef void* ccu_launch_stream;

extern ccu_result asccomm_ccu_kernel_register_start(void* ins_handle);

extern ccu_result asccomm_ccu_kernel_register(
    void* ins_handle, uint32_t die_id, const char* kernel_func_name, const void* kernel_func, const void** kernel_args,
    uint32_t arg_num, ccu_kernel_handle* kernel_handle);

extern ccu_result asccomm_ccu_kernel_register_end(void* ins_handle);

extern ccu_result asccomm_ccu_get_task_args_num(ccu_kernel_handle kernel_handle, uint32_t* task_args_num);

extern ccu_result asccomm_ccu_kernel_launch(
    ccu_launch_stream stream, ccu_kernel_handle kernel_handle, const void* task_args, uint32_t arg_num);

// kernel 资源查询输出到 INSTRUCTION；CASC_CNT 不由该查询结果表达。
#define ASCCOMM_CCU_RES_DESC_TYPE_COUNT (HCOMM_CCU_RES_TYPE_INSTRUCTION + 1U)

typedef struct asccomm_ccu_res_desc {
    uint32_t dieId;                                   // kernel 所属 die
    uint32_t resNum[ASCCOMM_CCU_RES_DESC_TYPE_COUNT]; // 按 HCOMM_CCU_RES_TYPE_* 枚举值索引
} asccomm_ccu_res_desc;

// 查询kernel需要的资源数
extern ccu_result asccomm_ccu_kernel_query_res_req(
    void* ins_handle, uint32_t die_id, const void* kernel_func, const void** kernel_args, uint32_t arg_num,
    asccomm_ccu_res_desc* res_desc);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_LAUNCH_H
