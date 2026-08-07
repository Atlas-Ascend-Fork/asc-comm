/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/instance/ccu_res_pack.h"

#include "hcomm/common/ccu_log.h"
#include "ccu_res_desc.h"

namespace asccomm {

CcuResPack::~CcuResPack()
{
    if (resHandle_ == 0) {
        return;
    }

    auto ret = hcomm::CcuReleaseResHandle(devLogicId_, resHandle_);
    if (ret != HcclResult::HCCL_SUCCESS) {
        HCCL_ERROR("[CcuResPack][%s] failed, resHandle[%p] devLogicId[%d].", __func__, resHandle_, devLogicId_);
    }
    resHandle_ = 0;
}

CcuResult CcuResPack::Init()
{
    devLogicId_ = HcclGetThreadDeviceId();
    if (!resDescs_.empty()) {
        std::vector<hcomm::CcuResDesc> internalDescs;
        internalDescs.reserve(resDescs_.size());
        for (const auto& snapshot : resDescs_) {
            hcomm::CcuResDesc desc;
            desc.dieId = snapshot.dieId;
            desc.resNum = snapshot.resNum;
            internalDescs.push_back(desc);
        }
        std::vector<const hcomm::CcuResDesc*> descPtrs;
        descPtrs.reserve(internalDescs.size());
        for (const auto& desc : internalDescs) {
            descPtrs.push_back(&desc);
        }
        CCU_CHK_RET(hcomm::CcuAllocResHandleByResDescs(
            devLogicId_, descPtrs.data(), static_cast<uint32_t>(descPtrs.size()), resHandle_));
        CCU_CHK_RET(Reset());
        return CcuResult::CCU_SUCCESS;
    }

    if (insType_ == CcuInstanceType::CCU_UNUSED) {
        HCCL_ERROR("[CcuResPack][%s] failed, error ccu instance type[%d].", __func__, static_cast<int32_t>(insType_));
        return CcuResult::CCU_E_PARA;
    }

    // 根据通信域算子展开模式申请资源
    // 如果资源不足，返回HCCL_E_UNAVAIL，表示需要回退
    auto ret = hcomm::CcuAllocResHandleByInsType(devLogicId_, insType_, resHandle_);
    if (ret == CcuResult::CCU_E_UNAVAIL) {
        HCCL_RUN_WARNING(
            "[%s] failed but passed, resource is not enough, "
            "devLogicId[%d], ccuInsType[%d].",
            __func__, devLogicId_, insType_);
        return ret;
    }
    CCU_CHK_RET(ret);
    CCU_CHK_RET(Reset());
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuResPack::Reset()
{
    if (!resHandle_) {
        return CcuResult::CCU_SUCCESS;
    }

    CCU_CHK_RET(hcomm::CcuCheckResource(devLogicId_, resHandle_, resRepo_));
    return CcuResult::CCU_SUCCESS;
}

CcuResRepository& CcuResPack::GetCcuResRepo() { return resRepo_; }

} // namespace asccomm
