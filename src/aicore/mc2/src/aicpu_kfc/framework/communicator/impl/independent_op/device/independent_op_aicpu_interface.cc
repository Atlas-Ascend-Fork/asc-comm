/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "independent_op_aicpu_interface.h"
#include "aicpu_indop_process.h"
#ifndef MC2_SERVER_ONLY
#include "framework/aicpu_hccl_process.h"
#endif

extern "C" {
__attribute__((visibility("default"))) uint32_t RunAicpuIndOpCommInit(void *args)
{
    CHK_PRT_RET(args == nullptr, HCCL_ERROR("[%s]args is null.", __func__), HCCL_E_PARA);

    CommAicpuParam *commAicpuParam = reinterpret_cast<CommAicpuParam *>(args);
    DevType devType = static_cast<DevType>(commAicpuParam->deviceType);
    if (devType == DevType::DEV_TYPE_950) {
        HCCL_INFO("[RunAicpuIndOpCommInit] group[%s], deviceLogicId[%u], devicePhyId[%u], deviceType[%u]",
                commAicpuParam->hcomId, commAicpuParam->deviceLogicId, commAicpuParam->devicePhyId, commAicpuParam->deviceType);
        return AicpuIndopProcess::AicpuIndOpCommInit(commAicpuParam);
    }
#ifdef MC2_SERVER_ONLY
    HCCL_ERROR("[RunAicpuIndOpCommInit] mc2_server does not support non-950 deviceType[%u].", devType);
    return HCCL_E_NOT_SUPPORT;
#else
    return AicpuHcclProcess::AicpuIndOpCommInit(commAicpuParam);
#endif
}

}
