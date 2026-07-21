/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_launch.h"

#include <algorithm>
#include <vector>

#include "ccu_dataplane_abi.h"
#include "ccu_assist_v1.h"
#include "ccu_instance_mgr.h"
#include "ccu_kernel_mgr.h"
#include "ccu_log.h"
#include "ccu_provider_internal.h"
#include "ccu_res.h"
#include "rt_external_base.h"
#include "rt_external_kernel.h"

namespace {

CcuResult GetInstanceDevice(CcuInsHandle insHandle, uint32_t &deviceLogicId)
{
    const uint32_t encoded = static_cast<uint32_t>(insHandle >> 32U);
    if (encoded == 0 || encoded > hcomm::MAX_MODULE_DEVICE_NUM)
        return CCU_E_PARA;
    deviceLogicId = encoded - 1U;
    return CCU_SUCCESS;
}

class RegistrationContextGuard {
public:
    explicit RegistrationContextGuard(hcomm::CcuInstance *instance)
    {
        hcomm::CcuSetCurrentInstance(instance);
    }

    ~RegistrationContextGuard()
    {
        hcomm::CcuSetCurrentInstance(nullptr);
    }
};

CcuResult LaunchTask(const AscCommCcuLaunchContext &context, const AscCommCcuTask &task, AscCommCcuTaskInfo &taskInfo)
{
    rtCcuTaskInfo_t runtimeTask{};
    runtimeTask.dieId = task.dieId;
    runtimeTask.missionId = task.missionId;
    runtimeTask.instStartId = task.instructionStartId;
    runtimeTask.instCnt = task.instructionCount;
    runtimeTask.key = task.missionKey;
    runtimeTask.argSize = task.argumentSize;
    runtimeTask.timeout = task.timeoutSeconds;
    std::copy(std::begin(task.arguments), std::end(task.arguments), std::begin(runtimeTask.args));
    if (rtCCULaunch(&runtimeTask, context.stream) != RT_ERROR_NONE)
        return CCU_E_RUNTIME;
    if (rtGetTaskIdAndStreamID(&taskInfo.taskId, &taskInfo.streamId) != RT_ERROR_NONE)
        return CCU_E_RUNTIME;
    taskInfo.task = task;
    return CCU_SUCCESS;
}

AscCommCcuTask ConvertTask(const hcomm::CcuTaskParam &source, const AscCommCcuLaunchContext &context)
{
    AscCommCcuTask task{};
    task.dieId = source.dieId;
    task.missionId = source.missionId;
    task.instructionStartId = source.instStartId;
    task.instructionCount = source.instCnt;
    task.missionKey = source.key;
    task.argumentSize = source.argSize;
    task.timeoutSeconds = context.timeoutSeconds;
    std::copy(std::begin(source.args), std::end(source.args), std::begin(task.arguments));
    return task;
}

} // namespace

CcuResult CcuDataPlaneQueryInstanceResourceRequest(uint32_t, AscCommCcuResourceRequest *request)
{
    CCU_CHK_PTR_NULL(request);
    return hcomm::CcuKernelMgr::GetTranslatorResourceRequest(*request);
}

CcuResult CcuDataPlaneInstanceCreate(const AscCommCcuInstanceCreateParam *param, CcuInsHandle *insHandle)
{
    CCU_CHK_PTR_NULL(param);
    CCU_CHK_PTR_NULL(insHandle);
    if (param->abiVersion != ASCCOMM_CCU_DATAPLANE_ABI_VERSION
        || param->structSize < sizeof(AscCommCcuInstanceCreateParam))
        return CCU_E_NOT_SUPPORT;
    if (param->resourceEntryCount != 0 && param->resourceEntries == nullptr)
        return CCU_E_PTR;
    CCU_CHK_RET(hcomm::CcuRetainHostServices(param->hostServices));
    const CcuResult result = hcomm::CcuInstanceMgr::GetInstance(param->deviceLogicId).Create(*param, *insHandle);
    if (result != CCU_SUCCESS)
        hcomm::CcuReleaseHostServices();
    return result;
}

CcuResult CcuDataPlaneInstanceDestroy(CcuInsHandle insHandle, uint32_t deviceLogicId)
{
    const CcuResult result = hcomm::CcuInstanceMgr::GetInstance(deviceLogicId).Destroy(insHandle);
    if (result == CCU_SUCCESS)
        hcomm::CcuReleaseHostServices();
    return result;
}

extern "C" CcuResult HcommCcuKernelRegisterStart(CcuInsHandle insHandle)
{
    uint32_t deviceLogicId = 0;
    CCU_CHK_RET(GetInstanceDevice(insHandle, deviceLogicId));
    auto *instance = hcomm::CcuInstanceMgr::GetInstance(deviceLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(instance);
    const CcuResult beginResult = instance->BeginRegister();
    if (beginResult != CCU_SUCCESS)
        return beginResult;
    const CcuResult result = instance->Reset();
    if (result != CCU_SUCCESS) {
        (void)instance->EndRegister();
    }
    return result;
}

static CcuResult CcuKernelTryRegister(hcomm::CcuInstance *instance, hcomm::CcuResPack *resourcePack,
    uint32_t deviceLogicId, const char *kernelFuncName, const void *kernelFunc, const void **kernelArgs,
    uint32_t argNum, CcuKernelHandle &newHandle)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    auto &kernelManager = hcomm::CcuKernelMgr::GetInstance(deviceLogicId);
    CCU_CHK_RET(kernelManager.Register(*resourcePack, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle));
    CCU_CHK_RET(instance->SaveKernel(newHandle));
    CCU_EXCEPTION_HANDLE_END
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegister(CcuInsHandle insHandle, uint32_t dieId, const char *kernelFuncName,
    const void *kernelFunc, const void **kernelArgs, uint32_t argNum, CcuKernelHandle *kernelHandle)
{
    CCU_CHK_PTR_NULL(kernelFunc);
    CCU_CHK_PTR_NULL(kernelHandle);
    if (argNum != 0)
        CCU_CHK_PTR_NULL(kernelArgs);
    (void)dieId;

    uint32_t deviceLogicId = 0;
    CCU_CHK_RET(GetInstanceDevice(insHandle, deviceLogicId));
    auto *instance = hcomm::CcuInstanceMgr::GetInstance(deviceLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(instance);
    RegistrationContextGuard context(instance);
    CCU_CHK_RET(instance->CheckRegistering());
    auto *resourcePack = instance->GetResPack();
    CCU_CHK_PTR_NULL(resourcePack);

    CcuKernelHandle newHandle = 0;
    CcuResult registerResult = CcuKernelTryRegister(
        instance, resourcePack, deviceLogicId, kernelFuncName, kernelFunc, kernelArgs, argNum, newHandle);
    if (registerResult != CCU_SUCCESS) {
        instance->AbortRegister();
        if (CCU_CHK_RES_UNAVAIL(registerResult)) {
            HCCL_WARNING("[%s] register kernel resource unavailable[%d], current register round aborted.", __func__,
                registerResult);
            return CCU_E_UNAVAIL;
        }
        HCCL_ERROR(
            "[%s] failed, register kernel failed[%d], current register round aborted.", __func__, registerResult);
        return registerResult;
    }
    *kernelHandle = newHandle;
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle)
{
    uint32_t deviceLogicId = 0;
    CCU_CHK_RET(GetInstanceDevice(insHandle, deviceLogicId));
    auto *instance = hcomm::CcuInstanceMgr::GetInstance(deviceLogicId).Get(insHandle);
    CCU_CHK_PTR_NULL(instance);
    RegistrationContextGuard context(instance);

    CcuResult result = CCU_SUCCESS;
    CCU_EXCEPTION_HANDLE_BEGIN
    result = hcomm::CcuKernelMgr::GetInstance(deviceLogicId)
                 .Translate(instance->GetUntranslatedKernels(), *instance->GetResPack());
    CCU_EXCEPTION_HANDLE_END
    const CcuResult endResult = instance->EndRegister();
    if (result != CCU_SUCCESS)
        return result;
    return endResult;
}

extern "C" CcuResult HcommCcuKernelLaunch(
    ThreadHandle threadHandle, CcuKernelHandle kernelHandle, const void *taskArgs, uint32_t argNum)
{
    const auto *hostServices = hcomm::CcuGetHostServices();
    if (hostServices == nullptr)
        return CCU_E_NOT_INITIALIZED;

    AscCommCcuLaunchContext launchContext{};
    CCU_CHK_RET(hostServices->getLaunchContext(
        hostServices->userData, static_cast<AscCommCcuThreadHandle>(threadHandle), &launchContext));
    CCU_CHK_PTR_NULL(launchContext.stream);
    if (kernelHandle == 0)
        return CCU_E_PARA;
    if (argNum != 0 && taskArgs == nullptr)
        return CCU_E_PTR;

    auto *kernel = hcomm::CcuKernelMgr::GetInstance(launchContext.deviceLogicId).GetKernel(kernelHandle);
    CCU_CHK_PTR_NULL(kernel);
    std::vector<hcomm::CcuTaskParam> taskParams;
    AscCommCcuLaunchResult result{};
    CCU_EXCEPTION_HANDLE_BEGIN
    CCU_CHK_RET(kernel->GeneTaskParams(static_cast<const uint64_t *>(taskArgs), argNum, taskParams));
    if (taskParams.size() > ASCCOMM_CCU_MAX_LAUNCH_TASKS)
        return CCU_E_INTERNAL;
    result.abiVersion = ASCCOMM_CCU_DATAPLANE_ABI_VERSION;
    result.structSize = sizeof(AscCommCcuLaunchResult);
    result.taskCount = 0;
    for (const auto &taskParam : taskParams) {
        const AscCommCcuTask task = ConvertTask(taskParam, launchContext);
        CCU_CHK_RET(LaunchTask(launchContext, task, result.tasks[result.taskCount]));
        ++result.taskCount;
    }
    CCU_EXCEPTION_HANDLE_END
    for (uint32_t index = 0; index < result.taskCount; ++index) {
        const auto &taskInfo = result.tasks[index];
        CCU_CHK_RET(hostServices->reportTask(hostServices->userData, static_cast<AscCommCcuThreadHandle>(threadHandle),
            taskInfo.streamId, taskInfo.taskId, kernelHandle, &taskInfo.task));
    }
    return CCU_SUCCESS;
}

extern "C" CcuResult HcommCcuGetMemToken(uint64_t srcVa, uint64_t size, uint64_t *tokenInfo)
{
    CCU_CHK_PTR_NULL(tokenInfo);
    if (srcVa == 0 || size == 0)
        return CCU_E_PARA;
    uint64_t tokenId = 0;
    uint64_t tokenValue = 0;
    CCU_CHK_RET(hcomm::CcuQueryMemoryToken(srcVa, size, tokenId, tokenValue));
    *tokenInfo = hcomm::CcuRep::GetToken(tokenId, tokenValue, 1);
    return CCU_SUCCESS;
}
