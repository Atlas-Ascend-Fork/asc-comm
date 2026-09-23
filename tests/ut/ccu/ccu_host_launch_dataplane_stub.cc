/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software; you can redistribute it and/or modify it under the terms and conditions of
 * the CANN Open Software License Agreement Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * You should have received a copy of the License along with this program.
 * If not, see <http://www.gnu.org/licenses/>.
 */

// ccu_host_launch 依赖的数据面 C ABI stub：register 链恒失败（POD 句柄校验不过），
// 现有用例均在前置校验即返回，不会触达真实注册流程。
#include "ccu/hcomm/ccu_launch.h"

extern "C" CcuResult asccomm_ccu_kernel_register_start(CcuInsHandle ins_handle)
{
    (void)ins_handle;
    return CCU_E_PARA;
}

extern "C" CcuResult asccomm_ccu_kernel_register(
    CcuInsHandle ins_handle, uint32_t die_id, const char* kernel_func_name, const void* kernel_func,
    const void** kernel_args, uint32_t arg_num, ccu_kernel_handle* kernel_handle)
{
    (void)ins_handle;
    (void)die_id;
    (void)kernel_func_name;
    (void)kernel_func;
    (void)kernel_args;
    (void)arg_num;
    (void)kernel_handle;
    return CCU_E_PARA;
}

extern "C" CcuResult asccomm_ccu_kernel_register_end(CcuInsHandle ins_handle)
{
    (void)ins_handle;
    return CCU_E_PARA;
}

extern "C" CcuResult asccomm_ccu_get_task_args_num(ccu_kernel_handle kernel_handle, uint32_t* task_args_num)
{
    (void)kernel_handle;
    (void)task_args_num;
    return CCU_E_PARA;
}

extern "C" CcuResult asccomm_ccu_kernel_launch(
    ccu_launch_stream stream, ccu_kernel_handle kernel_handle, const void* task_args, uint32_t arg_num)
{
    (void)stream;
    (void)kernel_handle;
    (void)task_args;
    (void)arg_num;
    return CCU_E_PARA;
}
