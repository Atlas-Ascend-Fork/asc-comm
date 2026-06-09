/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "orion_adapter_hal.h"

#include "dlhal_function_v2.h"
#include "ascend_hal_error.h"
#include "log.h"

namespace Hccl
{
    
HcclResult HrtHalDrvQueryProcessHostPid(int pid, unsigned int *chipId, unsigned int *vfid,
    unsigned int *hostPid, unsigned int *cpType)
{
    CHK_PTR_NULL(hostPid);
    // 和底软确认，chipId、vfid、hostPid、cpType不需要校验空指针，如果传入空指针表示当前不获取该值
    drvError_t ret = DlHalFunctionV2::GetInstance().dlHalDrvQueryProcessHostPid(pid,
        chipId, vfid, hostPid, cpType);
    CHK_PRT_RET(ret != DRV_ERROR_NONE, HCCL_ERROR("errNo[0x%016llx] HrtHalDrvQueryProcessHostPid fail,"
        "return[%d], para: pid[%d].", HCCL_ERROR_CODE(HCCL_E_DRV), ret, pid), HCCL_E_DRV);
    HCCL_INFO("HrtHalDrvQueryProcessHostPid pid[%d] hostPid[%u]", pid, *hostPid);
    return HCCL_SUCCESS;
}

}