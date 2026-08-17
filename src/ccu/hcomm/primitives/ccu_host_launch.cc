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

CcuResult PhyDieMaskToDieId(uint64_t phyDieMask, uint32_t& dieId)
{
    if (phyDieMask == 0 || (phyDieMask & (phyDieMask - 1)) != 0) {
        HCCL_ERROR("[%s] phyDieMask[0x%llx] must contain exactly one bit.", __func__, phyDieMask);
        return CcuResult::CCU_E_PARA;
    }
    dieId = 0;
    while ((phyDieMask >>= 1) != 0) {
        ++dieId;
    }
    constexpr uint32_t maxDieNum = 2;
    if (dieId >= maxDieNum) {
        HCCL_ERROR("[%s] dieId[%u] is out of range.", __func__, dieId);
        return CcuResult::CCU_E_PARA;
    }
    return CcuResult::CCU_SUCCESS;
}

} // namespace

CcuResult hcomm_ccu_host_kernel_launch(
    const void* kernelFunc, const HcommCcuHostKernelLaunchCfg* cfg, const HcommCcuHostKernelArgs* args)
{
    CCU_CHK_PTR_NULL(kernelFunc);
    CCU_CHK_PTR_NULL(cfg);
    CCU_CHK_PTR_NULL(args);
    CCU_CHK_PTR_NULL(cfg->kernelName);
    CCU_CHK_PTR_NULL(cfg->launchContext);

    if (args->kernelArgNum != 0) {
        CCU_CHK_PTR_NULL(args->kernelArgs);
    }
    if (args->taskArgNum != 0) {
        CCU_CHK_PTR_NULL(args->taskArgs);
    }

    uint32_t dieId = 0;
    CCU_CHK_RET(PhyDieMaskToDieId(cfg->ccuSchd.phyDieMask, dieId));

    CCU_CHK_PTR_NULL(cfg->registerContext);
    CcuResult ret = hcomm_ccu_kernel_register_start(cfg->ccuIns, cfg->registerContext);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] hcomm_ccu_kernel_register_start failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    CcuKernelHandle kernelHandle{0};
    ret = hcomm_ccu_kernel_register(
        cfg->ccuIns, dieId, cfg->kernelName, kernelFunc, args->kernelArgs, args->kernelArgNum,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] hcomm_ccu_kernel_register failed, ccuRet[%d].", __func__, ret);
        (void)hcomm_ccu_kernel_register_end(cfg->ccuIns);
        return ret;
    }

    ret = hcomm_ccu_kernel_register_end(cfg->ccuIns);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] hcomm_ccu_kernel_register_end failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    ret = hcomm_ccu_kernel_launch(cfg->launchContext, kernelHandle, args->taskArgs, args->taskArgNum);
    if (ret != CCU_SUCCESS) {
        HCCL_ERROR("[%s] hcomm_ccu_kernel_launch failed, ccuRet[%d].", __func__, ret);
        return ret;
    }

    return CcuResult::CCU_SUCCESS;
}
