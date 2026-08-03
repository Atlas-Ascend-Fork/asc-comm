/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_res.h"

#include "hcomm/resource/instance/ccu_instance_mgr.h"
#include "hcomm/common/ccu_log.h"
#include "hcom_common.h"

CcuResult HcommCcuInsCreateLegacy(const CcuInstanceType insType, CcuInsHandle *ccuInsHandle)
{
    CCU_CHK_PTR_NULL(ccuInsHandle);

    const int32_t devLogicId = HcclGetThreadDeviceId();
    CCU_CHK_RET(hcomm::CcuInstanceMgr::GetInstance(devLogicId).Create(insType, *ccuInsHandle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult HcommCcuInsCreate(const HcommCcuResDescHandle*, uint32_t, CcuInsHandle*)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult HcommCcuInsCreateDefault(const uint32_t*, uint32_t, CcuInsHandle*)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult HcommCcuInsDestroy(CcuInsHandle) { return CcuResult::CCU_E_NOT_SUPPORT; }

CcuResult HcommCcuInsQueryResDesc(CcuInsHandle, HcommCcuResDescHandle)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult HcommCcuInsResDescCreate(uint32_t, HcommCcuResDescHandle*) { return CcuResult::CCU_E_NOT_SUPPORT; }

CcuResult HcommCcuInsResDescDestroy(HcommCcuResDescHandle) { return CcuResult::CCU_E_NOT_SUPPORT; }

CcuResult HcommCcuInsResDescQueryDieId(HcommCcuResDescHandle, uint32_t*) { return CcuResult::CCU_E_NOT_SUPPORT; }

CcuResult HcommCcuInsResDescSetNum(HcommCcuResDescHandle, HcommCcuResType, uint32_t)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult HcommCcuInsResDescQueryNum(HcommCcuResDescHandle, HcommCcuResType, uint32_t*)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult HcommCcuQueryRemainResDesc(HcommCcuResDescHandle) { return CcuResult::CCU_E_NOT_SUPPORT; }

CcuResult HcommCcuKernelQueryResReq(const void*, const void**, uint32_t, HcommCcuResDescHandle)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}
