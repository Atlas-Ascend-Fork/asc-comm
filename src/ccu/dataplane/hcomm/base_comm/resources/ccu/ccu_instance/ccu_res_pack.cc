/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_res_pack.h"

#include "ccu_log.h"

namespace hcomm {
CcuResPack::~CcuResPack()
{
    if (resourceHandle_ != nullptr) {
        const HcclResult result = CcuReleaseResHandle(deviceLogicId_, resourceHandle_);
        if (result != HCCL_SUCCESS) {
            HCCL_ERROR("[CcuResPack][%s] failed, resourceHandle[%p] deviceLogicId[%d].",
                __func__, resourceHandle_, deviceLogicId_);
        }
        resourceHandle_ = nullptr;
    }
}

CcuResult CcuResPack::Init(int32_t deviceLogicId)
{
    deviceLogicId_ = deviceLogicId;
    if (instanceType_ >= CCU_UNUSED)
        return CCU_E_PARA;
    CCU_CHK_RET(CcuAllocResHandleByInsType(deviceLogicId_, instanceType_, resourceHandle_));
    return Reset();
}

CcuResult CcuResPack::Reset()
{
    if (resourceHandle_ == nullptr)
        return CCU_SUCCESS;
    return CcuCheckResource(deviceLogicId_, resourceHandle_, resRepo_);
}

CcuResRepository &CcuResPack::GetCcuResRepo()
{
    return resRepo_;
}

} // namespace hcomm
