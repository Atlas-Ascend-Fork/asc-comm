/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/instance/ccu_instance.h"

#include <algorithm>

#include "log.h"
#include "hcomm/common/ccu_log.h"

#include "hcomm/resource/instance/ccu_res_pack.h"
#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/instance/ccu_var_event_res_mgr.h"

namespace hcomm {

CcuInstance::~CcuInstance()
{
    // 主动释放资源保证时序，不得随意调整顺序
    for (auto& kernelHandle : kernelHandles_) {
        if (kernelHandle != 0) {
            (void)CcuKernelMgr::GetInstance(devLogicId_).UnRegister(kernelHandle);
            kernelHandle = 0;
        }
    }
    kernelHandles_.clear();

    (void)CcuVarEventResMgr::GetInstance(devLogicId_).ReleaseByInstance(insHandle_);

    resPack_ = nullptr;
}

CcuResult CcuInstance::Init()
{
    if (insType_ >= CcuInstanceType::CCU_UNUSED) {
        HCCL_ERROR("[CcuInstance][%s] failed, CcuInstanceType[%d] is invalid.", __func__, insType_);
        return CcuResult::CCU_E_PARA;
    }

    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::LoadRegisterContext(const HcommCcuRegisterContextPod &context)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    if (!resPack_) {
        resPack_.reset(new (std::nothrow) CcuResPack());
        CCU_CHK_PTR_NULL(resPack_);
    }
    CCU_CHK_RET(resPack_->Load(context));
    devLogicId_ = context.deviceLogicId;
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::Reset()
{
    if (!resPack_) {
        return CcuResult::CCU_E_UNAVAIL;
    }

    untranslatedKernelHandles_.clear();
    CCU_CHK_RET(resPack_->Reset());

    CCU_CHK_RET(CcuVarEventResMgr::GetInstance(devLogicId_).ExcludeAllocatedFromRepo(insHandle_));
    return CcuResult::CCU_SUCCESS;
}

CcuResPack* CcuInstance::GetResPack() { return resPack_.get(); }

void CcuInstance::SetHandle(CcuInsHandle insHandle)
{
    insHandle_ = insHandle;
}

CcuResult CcuInstance::SaveKernel(const CcuKernelHandle kernelHandle)
{
    kernelHandles_.push_back(kernelHandle);
    untranslatedKernelHandles_.push_back(kernelHandle);
    return CcuResult::CCU_SUCCESS;
}

const std::vector<CcuKernelHandle>& CcuInstance::GetUntranslatedKernels() { return untranslatedKernelHandles_; }

CcuResult CcuInstance::BeginRegister()
{
    if (registerState_ == RegisterState::REGISTERING) {
        HCCL_ERROR(
            "[CcuInstance][%s] failed, previous register round is not ended, "
            "HcommCcuKernelRegisterEnd is missing before a new HcommCcuKernelRegisterStart.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    registerState_ = RegisterState::REGISTERING;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::CheckRegistering() const
{
    if (registerState_ != RegisterState::REGISTERING) {
        HCCL_ERROR(
            "[CcuInstance][%s] failed, HcommCcuKernelRegister must be called between "
            "HcommCcuKernelRegisterStart and HcommCcuKernelRegisterEnd.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::EndRegister()
{
    if (registerState_ == RegisterState::IDLE) {
        HCCL_ERROR(
            "[CcuInstance][%s] failed, HcommCcuKernelRegisterEnd is called without a matching "
            "HcommCcuKernelRegisterStart.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (registerState_ == RegisterState::REGISTER_ABORTED) {
        HCCL_WARNING(
            "[CcuInstance][%s] previous register round was aborted due to error, "
            "close it to keep Start/End paired, no kernel will be translated.",
            __func__);
    }
    registerState_ = RegisterState::IDLE;
    return CcuResult::CCU_SUCCESS;
}

void CcuInstance::AbortRegister()
{
    for (auto kernelHandle : untranslatedKernelHandles_) {
        if (kernelHandle == 0) {
            continue;
        }
        (void)CcuKernelMgr::GetInstance(devLogicId_).UnRegister(kernelHandle);
        auto it = std::find(kernelHandles_.begin(), kernelHandles_.end(), kernelHandle);
        if (it != kernelHandles_.end()) {
            kernelHandles_.erase(it);
        }
    }
    untranslatedKernelHandles_.clear();
    registerState_ = RegisterState::REGISTER_ABORTED;
}

} // namespace hcomm
