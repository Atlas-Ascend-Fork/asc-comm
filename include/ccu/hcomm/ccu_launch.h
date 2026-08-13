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

#include "hccl/hccl_types.h"
#include "ccu/hcomm/ccu_types.h"
#include "hccl/hccl_res.h"
#include "hcomm/hcomm_ccu_launch.h"
#include "hcomm/hcomm_ccu_res.h"

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

extern CcuResult HcommCcuKernelRegisterStart(
    CcuInsHandle insHandle, const HcommCcuRegisterContextPod *context);

extern CcuResult HcommCcuKernelRegister(
    CcuInsHandle insHandle, uint32_t dieId, const char* kernelFuncName, const void* kernelFunc, const void** kernelArgs,
    uint32_t argNum, CcuKernelHandle* kernelHandle);

extern CcuResult HcommCcuKernelRegisterEnd(CcuInsHandle insHandle);

extern CcuResult HcommCcuGetTaskArgsNum(CcuKernelHandle kernelHandle, uint32_t* taskArgsNum);

extern CcuResult HcommCcuKernelLaunch(
    const HcommCcuLaunchContextPod *launchContext, CcuKernelHandle kernelHandle, const void* taskArgs, uint32_t argNum);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // CCU_LAUNCH_H
