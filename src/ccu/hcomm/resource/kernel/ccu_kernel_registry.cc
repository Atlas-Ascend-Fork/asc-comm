/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_registry.h"

#include <algorithm>

#include "log.h"
#include "hcomm/common/ccu_log.h"

#include "hcomm/resource/common/asc_ccu_res_snapshot.h"
#include "hcomm/resource/kernel/ccu_kernel_mgr.h"
#include "hcomm/resource/common/ccu_var_event_res_mgr.h"

namespace hcomm {

CcuKernelRegistry::~CcuKernelRegistry()
{
    // 主动释放资源保证时序，不得随意调整顺序
    for (auto& kernelHandle : kernelHandles_) {
        if (kernelHandle != 0) {
            (void)CcuKernelMgr::GetInstance(devLogicId_).UnRegister(kernelHandle);
            kernelHandle = 0;
        }
    }
    kernelHandles_.clear();

    // 必须先于资源快照释放：CcuVarEventResMgr 持有指向快照内资源仓的借用指针。
    (void)CcuVarEventResMgr::GetInstance(devLogicId_).ReleaseByInstance(insHandle_);

    resSnapshot_ = nullptr;
}

CcuResult CcuKernelRegistry::Init(int32_t deviceLogicId)
{
    // 先于 LoadRegisterContext 落定设备号：实例可能在未加载上下文的情况下被销毁，
    // 此时析构仍需按正确设备定位 CcuKernelMgr。
    devLogicId_ = deviceLogicId;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistry::LoadRegisterContext(const HcommCcuRegisterContextPod &context)
{
    CCU_EXCEPTION_HANDLE_BEGIN
    if (!resSnapshot_) {
        resSnapshot_.reset(new (std::nothrow) AscCcuResSnapshot());
        CCU_CHK_PTR_NULL(resSnapshot_);
    }
    // 上下文的设备号必须与创建时传入的一致，否则说明调用方把上下文用在了错误的实例上。
    if (context.deviceLogicId != devLogicId_) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, context deviceLogicId[%d] mismatches instance deviceLogicId[%d].", __func__,
            context.deviceLogicId, devLogicId_);
        return CcuResult::CCU_E_PARA;
    }
    CCU_CHK_RET(resSnapshot_->Load(context));
    CCU_EXCEPTION_HANDLE_END
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistry::Reset()
{
    if (!resSnapshot_) {
        return CcuResult::CCU_E_UNAVAIL;
    }

    untranslatedKernelHandles_.clear();
    CCU_CHK_RET(resSnapshot_->Reset());

    // 刷新余量池后，把已被 Variable/Event 预约占用的区间从池中排除。
    CCU_CHK_RET(CcuVarEventResMgr::GetInstance(devLogicId_).ExcludeAllocatedFromRepo(insHandle_));
    return CcuResult::CCU_SUCCESS;
}

AscCcuResSnapshot* CcuKernelRegistry::GetResSnapshot() { return resSnapshot_.get(); }

void CcuKernelRegistry::SetHandle(CcuInsHandle insHandle)
{
    insHandle_ = insHandle;
}

CcuResult CcuKernelRegistry::SaveKernel(const CcuKernelHandle kernelHandle)
{
    kernelHandles_.push_back(kernelHandle);
    untranslatedKernelHandles_.push_back(kernelHandle);
    return CcuResult::CCU_SUCCESS;
}

const std::vector<CcuKernelHandle>& CcuKernelRegistry::GetUntranslatedKernels() { return untranslatedKernelHandles_; }

CcuResult CcuKernelRegistry::BeginRegister()
{
    if (registerState_ == RegisterState::REGISTERING) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, previous register round is not ended, "
            "HcommCcuKernelRegisterEnd is missing before a new HcommCcuKernelRegisterStart.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    registerState_ = RegisterState::REGISTERING;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistry::CheckRegistering() const
{
    if (registerState_ != RegisterState::REGISTERING) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, HcommCcuKernelRegister must be called between "
            "HcommCcuKernelRegisterStart and HcommCcuKernelRegisterEnd.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistry::EndRegister()
{
    if (registerState_ == RegisterState::IDLE) {
        HCCL_ERROR(
            "[CcuKernelRegistry][%s] failed, HcommCcuKernelRegisterEnd is called without a matching "
            "HcommCcuKernelRegisterStart.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (registerState_ == RegisterState::REGISTER_ABORTED) {
        HCCL_WARNING(
            "[CcuKernelRegistry][%s] previous register round was aborted due to error, "
            "close it to keep Start/End paired, no kernel will be translated.",
            __func__);
    }
    registerState_ = RegisterState::IDLE;
    return CcuResult::CCU_SUCCESS;
}

void CcuKernelRegistry::AbortRegister()
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
