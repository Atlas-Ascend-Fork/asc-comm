/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_host_launch.h"

#include "hcomm/common/ccu_log.h"

namespace {

CcuResult phy_die_mask_to_die_id(uint64_t phy_die_mask, uint32_t& die_id)
{
    if (phy_die_mask == 0 || (phy_die_mask & (phy_die_mask - 1)) != 0) {
        HCCL_ERROR("[%s] phyDieMask[0x%llx] must contain exactly one bit.", __func__, phy_die_mask);
        return CcuResult::CCU_E_PARA;
    }
    die_id = 0;
    while ((phy_die_mask >>= 1) != 0) {
        ++die_id;
    }
    constexpr uint32_t max_die_num = 2;
    if (die_id >= max_die_num) {
        HCCL_ERROR("[%s] dieId[%u] is out of range.", __func__, die_id);
        return CcuResult::CCU_E_PARA;
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult asccomm_ccu_host_kernel_launch(
    const void* kernel_func, const asccomm_ccu_host_kernel_launch_cfg* cfg, const asccomm_ccu_host_kernel_args* args)
{
    CCU_CHK_PTR_NULL(kernel_func);
    CCU_CHK_PTR_NULL(cfg);
    CCU_CHK_PTR_NULL(args);
    CCU_CHK_PTR_NULL(cfg->kernel_name);
    CCU_CHK_PTR_NULL(cfg->launch_context);

    if (args->kernel_arg_num != 0) {
        CCU_CHK_PTR_NULL(args->kernel_args);
    }
    if (args->task_arg_num != 0) {
        CCU_CHK_PTR_NULL(args->task_args);
    }

    uint32_t die_id = 0;
    CCU_CHK_RET(phy_die_mask_to_die_id(cfg->ccu_schd.phy_die_mask, die_id));

    CCU_CHK_PTR_NULL(cfg->register_context);
    CcuResult ret = asccomm_ccu_kernel_register_start(cfg->ccu_ins, cfg->register_context);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] asccomm_ccu_kernel_register_start failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    ccu_kernel_handle kernel_handle{0};
    ret = asccomm_ccu_kernel_register(
        cfg->ccu_ins, die_id, cfg->kernel_name, kernel_func, args->kernel_args, args->kernel_arg_num,
        &kernel_handle);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] asccomm_ccu_kernel_register failed, ccuRet[%d].", __func__, ret);
        (void)asccomm_ccu_kernel_register_end(cfg->ccu_ins);
        return ret;
    }

    ret = asccomm_ccu_kernel_register_end(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] asccomm_ccu_kernel_register_end failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    ret = asccomm_ccu_kernel_launch(cfg->launch_context, kernel_handle, args->task_args, args->task_arg_num);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] asccomm_ccu_kernel_launch failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    return CcuResult::CCU_SUCCESS;
}
