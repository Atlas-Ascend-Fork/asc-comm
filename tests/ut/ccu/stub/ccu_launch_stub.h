/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_LAUNCH_STUB_H
#define CCU_LAUNCH_STUB_H

#include "ccu/hcomm/ccu_launch.h"

struct CcuLaunchStubCalls {
    uint32_t registerStart;
    uint32_t registerKernel;
    uint32_t registerEnd;
    uint32_t getTaskArgsNum;
    uint32_t kernelLaunch;
};

struct CcuLaunchStubObservations {
    CcuLaunchStubCalls calls{};
    CcuInsHandle lastInsHandle{};
    uint32_t lastDieId{};
    const char* lastKernelName{};
    const void* lastKernelFunc{};
    const void* lastKernelArg{};
    ccu_kernel_handle lastKernelHandle{};
    uint32_t lastTaskArgsNum{};
    ccu_launch_stream lastStream{};
    const void* lastTaskArgs{};
    uint32_t lastLaunchArgNum{};
};

void ResetCcuLaunchStub();
void SetCcuLaunchStubResults(
    CcuResult registerStart, CcuResult registerKernel, CcuResult registerEnd, CcuResult getTaskArgsNum,
    CcuResult kernelLaunch);
void SetCcuLaunchStubTaskArgsNum(uint32_t taskArgsNum);
CcuLaunchStubObservations GetCcuLaunchStubObservations();

#endif // CCU_LAUNCH_STUB_H
