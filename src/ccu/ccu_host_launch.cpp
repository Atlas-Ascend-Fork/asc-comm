/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/ccu_host_launch.h"

#include <algorithm>
#include <iterator>
#include <vector>

#include "env_config/env_config.h"
#include "rt_external_kernel.h"

int32_t HcclGetThreadDeviceId();

namespace hcomm {
constexpr uint32_t CCU_SQE_ARGS_LEN = 13;

struct CcuTaskParam {
    uint8_t dieId;
    uint8_t missionId;
    uint16_t timeout;
    uint32_t instStartId;
    uint32_t instCnt;
    uint32_t key;
    uint32_t argSize;
    uint64_t args[CCU_SQE_ARGS_LEN];
};

class CcuKernel {
public:
    CcuResult GeneTaskParams(const uint64_t *taskArgs, uint32_t argsNum,
        std::vector<CcuTaskParam> &taskParams);
};

class CcuKernelMgr {
public:
    static CcuKernelMgr &GetInstance(int32_t deviceLogicId);
    CcuKernel *GetKernel(CcuKernelHandle kernelHandle);
};
} // namespace hcomm

namespace {
bool IsCcuKernelLaunchApiAvailable()
{
    auto registerStart = HcommCcuKernelRegisterStart;
    auto registerKernel = HcommCcuKernelRegister;
    auto registerEnd = HcommCcuKernelRegisterEnd;
    auto getTaskArgsNum = HcommCcuGetTaskArgsNum;
    return registerStart != nullptr && registerKernel != nullptr &&
        registerEnd != nullptr && getTaskArgsNum != nullptr;
}

using VoidPackedCcuKernel = void (*)(void *);

struct VoidKernelRegisterCtx {
    const void *kernelFunc;
    void *packedArgs;
};

CcuResult VoidKernelTrampoline(CcuKernelArg arg)
{
    auto *ctx = static_cast<VoidKernelRegisterCtx *>(arg);
    if (ctx == nullptr || ctx->kernelFunc == nullptr || ctx->packedArgs == nullptr) {
        return CCU_E_PTR;
    }

    auto fn = reinterpret_cast<VoidPackedCcuKernel>(const_cast<void *>(ctx->kernelFunc));
    fn(ctx->packedArgs);
    return CCU_SUCCESS;
}

CcuResult GetSingleTaskParam(CcuKernelHandle kernelHandle, const void *taskArgs, uint32_t argNum,
    hcomm::CcuTaskParam &taskParam)
{
    if (kernelHandle == 0) {
        return CCU_E_PARA;
    }
    if (argNum > hcomm::CCU_SQE_ARGS_LEN) {
        return CCU_E_NOT_SUPPORT;
    }
    if (argNum > 0 && taskArgs == nullptr) {
        return CCU_E_PTR;
    }

    try {
        const uint32_t devLogicId = static_cast<uint32_t>(HcclGetThreadDeviceId());
        auto &kernelMgr = hcomm::CcuKernelMgr::GetInstance(devLogicId);
        auto *kernel = kernelMgr.GetKernel(kernelHandle);
        if (kernel == nullptr) {
            return CCU_E_PTR;
        }

        std::vector<hcomm::CcuTaskParam> taskParams{};
        CcuResult ret = kernel->GeneTaskParams(static_cast<const uint64_t *>(taskArgs), argNum, taskParams);
        if (ret != CCU_SUCCESS) {
            return ret;
        }
        if (taskParams.size() != 1) {
            return CCU_E_NOT_SUPPORT;
        }

        taskParam = taskParams.front();
    } catch (...) {
        return CCU_E_INTERNAL;
    }

    return CCU_SUCCESS;
}

CcuResult LaunchSingleCcuTask(const hcomm::CcuTaskParam &param, aclrtStream stream)
{
    if (stream == nullptr) {
        return CCU_E_PTR;
    }

    const uint32_t execTimeOutSec = Hccl::EnvConfig::GetInstance().GetRtsConfig().GetExecTimeOut();
    rtCcuTaskInfo_t taskInfo{};
    taskInfo.dieId = param.dieId;
    taskInfo.missionId = param.missionId;
    taskInfo.instStartId = param.instStartId;
    taskInfo.instCnt = param.instCnt;
    taskInfo.key = param.key;
    taskInfo.argSize = param.argSize;
    taskInfo.timeout = execTimeOutSec;
    std::copy(std::begin(param.args), std::end(param.args), std::begin(taskInfo.args));

    auto rtRet = rtCCULaunch(&taskInfo, stream);
    if (rtRet != RT_ERROR_NONE) {
        return CCU_E_RUNTIME;
    }

    return CCU_SUCCESS;
}

} // namespace


extern "C" CcuResult asccomm_ccu_host_kernel_launch(const void *kernel_func,
    const asccomm_launch_kernel_cfg *cfg, void *args)
{
    if (kernel_func == nullptr || cfg == nullptr || args == nullptr) {
        return CCU_E_PTR;
    }
    if (!IsCcuKernelLaunchApiAvailable()) {
        return CCU_E_NOT_SUPPORT;
    }

    CcuResult ret = HcommCcuKernelRegisterStart(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    CcuKernelHandle kernelHandle = 0;
    const uint32_t dieId = static_cast<uint32_t>(cfg->ccu_schd.phy_die_mask);

    VoidKernelRegisterCtx registerCtx {
        kernel_func,
        args,
    };

    const void *kernelArgs[] = {&registerCtx};

    // kernelFunc do not support func that return void , only support return CcuResult
    ret = HcommCcuKernelRegister(
        cfg->ccu_ins,
        dieId,
        nullptr,
        reinterpret_cast<const void *>(VoidKernelTrampoline),
        kernelArgs,
        1,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        (void)HcommCcuKernelRegisterEnd(cfg->ccu_ins);
        return ret;
    }

    ret = HcommCcuKernelRegisterEnd(cfg->ccu_ins);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    uint32_t taskArgsNum = 0;
    ret = HcommCcuGetTaskArgsNum(kernelHandle, &taskArgsNum);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    hcomm::CcuTaskParam taskParam{};
    ret = GetSingleTaskParam(kernelHandle, args, taskArgsNum, taskParam);
    if (ret != CCU_SUCCESS) {
        return ret;
    }

    return LaunchSingleCcuTask(taskParam, cfg->stream);
}
