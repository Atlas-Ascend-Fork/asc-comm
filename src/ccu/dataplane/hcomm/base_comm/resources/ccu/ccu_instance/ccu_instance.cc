/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_instance.h"

#include <algorithm>

#include "log.h"
#include "ccu_log.h"

#include "ccu_res_pack.h"
#include "ccu_kernel_mgr.h"

namespace hcomm {

CcuInstance::~CcuInstance()
{
    // 主动释放资源保证时序，不得随意调整顺序
    for (auto &kernelHandle : kernelHandles_) {
        if (kernelHandle != 0) {
            (void)CcuKernelMgr::GetInstance(devLogicId_).UnRegister(kernelHandle);
            kernelHandle = 0;
        }
    }
    kernelHandles_.clear();

    resPack_ = nullptr;
}

CcuResult CcuInstance::Init(const AscCommCcuInstanceCreateParam &param)
{
    if (param.instanceType >= static_cast<uint32_t>(CcuInstanceType::CCU_UNUSED)) {
        HCCL_ERROR("[CcuInstance][%s] failed, CcuInstanceType[%d] is invalid.", __func__, param.instanceType);
        return CcuResult::CCU_E_PARA;
    }
    devLogicId_ = static_cast<int32_t>(param.deviceLogicId);
    for (uint32_t die = 0; die < ASCCOMM_CCU_MAX_DIE_NUM; ++die) {
        dieEnabled_[die] = param.dieEnabled[die] != 0;
        xnBaseAddress_[die] = param.xnBaseAddress[die];
        missionKey_[die] = param.missionKey[die];
        resourceSpaceToken_[die] = param.resourceSpaceToken[die];
        for (uint32_t destination = 0; destination < ASCCOMM_CCU_MAX_DIE_NUM; ++destination) {
            loopChannelId_[die][destination] = param.loopChannelId[die][destination];
        }
    }
    deviceMemoryToken_ = param.deviceMemoryToken;

    if (!resPack_) {
        resPack_.reset(new (std::nothrow) CcuResPack(static_cast<CcuInstanceType>(param.instanceType)));
        CCU_CHK_PTR_NULL(resPack_);
        CCU_CHK_RET(resPack_->Init(devLogicId_));
    }
    CCU_CHK_RET(static_cast<CcuResult>(CcuKernelMgr::GetInstance(devLogicId_).Init()));
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::Reset()
{
    if (!resPack_) {
        return CcuResult::CCU_SUCCESS;
    }

    untranslatedKernelHandles_.clear();
    CCU_CHK_RET(resPack_->Reset());
    return CcuResult::CCU_SUCCESS;
}

CcuResPack *CcuInstance::GetResPack()
{
    return resPack_.get();
}

CcuResult CcuInstance::SaveKernel(const CcuKernelHandle kernelHandle)
{
    kernelHandles_.push_back(kernelHandle);
    untranslatedKernelHandles_.push_back(kernelHandle);
    return CcuResult::CCU_SUCCESS;
}

const std::vector<CcuKernelHandle> &CcuInstance::GetUntranslatedKernels()
{
    return untranslatedKernelHandles_;
}

CcuResult CcuInstance::BeginRegister()
{
    if (registerState_ == RegisterState::REGISTERING) {
        HCCL_ERROR("[CcuInstance][%s] failed, previous register round is not ended, "
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
        HCCL_ERROR("[CcuInstance][%s] failed, HcommCcuKernelRegister must be called between "
                   "HcommCcuKernelRegisterStart and HcommCcuKernelRegisterEnd.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstance::EndRegister()
{
    if (registerState_ == RegisterState::IDLE) {
        HCCL_ERROR("[CcuInstance][%s] failed, HcommCcuKernelRegisterEnd is called without a matching "
                   "HcommCcuKernelRegisterStart.",
            __func__);
        return CcuResult::CCU_E_INTERNAL;
    }
    if (registerState_ == RegisterState::REGISTER_ABORTED) {
        HCCL_WARNING("[CcuInstance][%s] previous register round was aborted due to error, "
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

int32_t CcuInstance::GetDeviceLogicId() const
{
    return devLogicId_;
}

bool CcuInstance::GetDieEnabled(uint32_t dieId) const
{
    return dieId < dieEnabled_.size() && dieEnabled_[dieId];
}

uint64_t CcuInstance::GetXnBaseAddress(uint32_t dieId) const
{
    return dieId < xnBaseAddress_.size() ? xnBaseAddress_[dieId] : 0;
}

uint32_t CcuInstance::GetMissionKey(uint32_t dieId) const
{
    return dieId < missionKey_.size() ? missionKey_[dieId] : 0;
}

uint32_t CcuInstance::GetLoopChannelId(uint32_t sourceDieId, uint32_t destinationDieId) const
{
    return sourceDieId < loopChannelId_.size() && destinationDieId < loopChannelId_[sourceDieId].size()
               ? loopChannelId_[sourceDieId][destinationDieId]
               : 0;
}

const AscCommCcuTokenInfo &CcuInstance::GetResourceSpaceToken(uint32_t dieId) const
{
    static const AscCommCcuTokenInfo empty{};
    return dieId < resourceSpaceToken_.size() ? resourceSpaceToken_[dieId] : empty;
}

const AscCommCcuTokenInfo &CcuInstance::GetDeviceMemoryToken() const
{
    return deviceMemoryToken_;
}

} // namespace hcomm
