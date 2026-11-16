/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software; you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_launch_stub.h"

namespace {

ccu_result g_registerStartResult = ccu_result::CCU_SUCCESS;
ccu_result g_registerKernelResult = ccu_result::CCU_SUCCESS;
ccu_result g_registerEndResult = ccu_result::CCU_SUCCESS;
ccu_result g_getTaskArgsNumResult = ccu_result::CCU_SUCCESS;
ccu_result g_kernelLaunchResult = ccu_result::CCU_SUCCESS;
uint32_t g_taskArgsNum = 0;
CcuLaunchStubObservations g_observations{};

} // namespace

void ResetCcuLaunchStub()
{
    g_registerStartResult = ccu_result::CCU_SUCCESS;
    g_registerKernelResult = ccu_result::CCU_SUCCESS;
    g_registerEndResult = ccu_result::CCU_SUCCESS;
    g_getTaskArgsNumResult = ccu_result::CCU_SUCCESS;
    g_kernelLaunchResult = ccu_result::CCU_SUCCESS;
    g_taskArgsNum = 0;
    g_observations = {};
}

void SetCcuLaunchStubResults(
    ccu_result registerStart, ccu_result registerKernel, ccu_result registerEnd, ccu_result getTaskArgsNum,
    ccu_result kernelLaunch)
{
    g_registerStartResult = registerStart;
    g_registerKernelResult = registerKernel;
    g_registerEndResult = registerEnd;
    g_getTaskArgsNumResult = getTaskArgsNum;
    g_kernelLaunchResult = kernelLaunch;
}

void SetCcuLaunchStubTaskArgsNum(uint32_t taskArgsNum) { g_taskArgsNum = taskArgsNum; }

CcuLaunchStubObservations GetCcuLaunchStubObservations() { return g_observations; }

extern "C" ccu_result asccomm_ccu_kernel_register_start(void* insHandle)
{
    ++g_observations.calls.registerStart;
    g_observations.lastInsHandle = insHandle;
    return g_registerStartResult;
}

extern "C" ccu_result asccomm_ccu_kernel_register(
    void* insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, ccu_kernel_handle* kernelHandle)
{
    ++g_observations.calls.registerKernel;
    g_observations.lastInsHandle = insHandle;
    g_observations.lastDieId = dieId;
    g_observations.lastKernelName = kernelFuncName;
    g_observations.lastKernelFunc = kernelFunc;
    if (kernelArgs != nullptr && argNum > 0) {
        g_observations.lastKernelArg = kernelArgs[0];
    }
    if (kernelHandle == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    if (g_registerKernelResult != ccu_result::CCU_SUCCESS) {
        return g_registerKernelResult;
    }

    // The wrapper registers a trampoline returning ccu_result. Execute it here to cover the
    // void-kernel adaptation without depending on the real kernel representation stack.
    if (argNum != 1U || kernelArgs == nullptr) {
        return ccu_result::CCU_E_PARA;
    }
    using CcuKernelFunc = ccu_result (*)(ccu_kernel_arg);
    auto kernel = reinterpret_cast<CcuKernelFunc>(const_cast<void*>(kernelFunc));
    ccu_result ret = kernel(const_cast<void*>(kernelArgs[0]));
    if (ret != ccu_result::CCU_SUCCESS) {
        return ret;
    }
    *kernelHandle = 0x5100U;
    g_observations.lastKernelHandle = *kernelHandle;
    return ccu_result::CCU_SUCCESS;
}

extern "C" ccu_result asccomm_ccu_kernel_register_end(void* insHandle)
{
    ++g_observations.calls.registerEnd;
    g_observations.lastInsHandle = insHandle;
    return g_registerEndResult;
}

extern "C" ccu_result asccomm_ccu_get_task_args_num(ccu_kernel_handle kernelHandle, uint32_t* taskArgsNum)
{
    ++g_observations.calls.getTaskArgsNum;
    g_observations.lastKernelHandle = kernelHandle;
    if (taskArgsNum == nullptr) {
        return ccu_result::CCU_E_PTR;
    }
    if (g_getTaskArgsNumResult != ccu_result::CCU_SUCCESS) {
        return g_getTaskArgsNumResult;
    }
    *taskArgsNum = g_taskArgsNum;
    g_observations.lastTaskArgsNum = *taskArgsNum;
    return ccu_result::CCU_SUCCESS;
}

extern "C" ccu_result asccomm_ccu_kernel_launch(
    ccu_launch_stream stream, ccu_kernel_handle kernelHandle, const void* taskArgs, uint32_t argNum)
{
    ++g_observations.calls.kernelLaunch;
    g_observations.lastStream = stream;
    g_observations.lastKernelHandle = kernelHandle;
    g_observations.lastTaskArgs = taskArgs;
    g_observations.lastLaunchArgNum = argNum;
    return g_kernelLaunchResult;
}
