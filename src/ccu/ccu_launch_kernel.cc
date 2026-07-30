/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_launch_kernel.h"

namespace {
CcuResult CheckLaunchArgs(const void *kernelFunc, const AscCommCcuLaunchCfg *cfg,
    const AscCommCcuLaunchArgs *args)
{
    if (kernelFunc == nullptr || cfg == nullptr || args == nullptr) {
        return CCU_E_PTR;
    }
    if (args->kernelArgNum != 0 && (args->kernelArgs == nullptr || args->kernelArgs[0] == nullptr)) {
        return CCU_E_PTR;
    }
    if (args->taskArgNum != 0 && args->taskArgs == nullptr) {
        return CCU_E_PTR;
    }
    return CCU_SUCCESS;
}

bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto launchKernel = HcommCcuKernelLaunch;
    return registerStart != nullptr && registerKernel != nullptr &&
        registerEnd != nullptr && launchKernel != nullptr;
}
} // namespace

extern "C" CcuResult AscCommCcuLaunchKernel(const void *kernelFunc,
    const AscCommCcuLaunchCfg *cfg, const AscCommCcuLaunchArgs *args)
{
    CcuResult ret = CheckLaunchArgs(kernelFunc, cfg, args);
    if (ret != CCU_SUCCESS) {
        return ret;
    }
    if (!IsCcuKernelLaunchApiAvailable()) {
        return CCU_E_NOT_SUPPORT;
    }

    ret = HcommCcuKernelRegisterStart(cfg->ccuIns);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    CcuKernelHandle kernelHandle = 0;
    const uint32_t dieId = static_cast<uint32_t>(cfg->ccuSchd.phyDieMask);
    ret = HcommCcuKernelRegister(cfg->ccuIns, dieId, cfg->kernelName, kernelFunc,
        args->kernelArgs, args->kernelArgNum, &kernelHandle);
    if (ret != CCU_SUCCESS) {
        (void)HcommCcuKernelRegisterEnd(cfg->ccuIns);
        return ret;
    }

    ret = HcommCcuKernelRegisterEnd(cfg->ccuIns);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    return HcommCcuKernelLaunch(cfg->thread, kernelHandle, args->taskArgs, args->taskArgNum);
}
