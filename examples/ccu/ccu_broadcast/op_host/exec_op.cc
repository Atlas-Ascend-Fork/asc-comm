/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <ccu/hcomm/ccu_api_types.h>
#include <hcomm/hcomm_ccu_launch_api.h>
#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"
#include "alg_resource.h"

namespace ops_hccl_bcast {

constexpr uint64_t MAX_TRANSFER_BYTES = 256ULL * 1024 * 1024;

HcclResult ExecOp(const OpParam& param_, const AlgResourceCtx& resCtx)
{
    const uint64_t dataSize = param_.count_ * sizeof(float);
    if (dataSize == 0 || param_.rankSize == 1) {
        return HCCL_SUCCESS;
    }
    if (dataSize > MAX_TRANSFER_BYTES) {
        return HCCL_E_NOT_SUPPORT;
    }

    const uint64_t bufferAddr = reinterpret_cast<uint64_t>(param_.buffer);
    uint64_t token = 0;
    if (asccomm_ccu_get_mem_token(bufferAddr, dataSize, &token) != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    std::vector<uint64_t> task_args = {bufferAddr, token, dataSize};
    HcommCcuLaunchContextPod launch_context{};
    if (HcommCcuGetLaunchContext(resCtx.threads[0], &launch_context) != HCCL_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    CcuResult launchRet =
        asccomm_ccu_kernel_launch(&launch_context, resCtx.ccuKernels[0], task_args.data(), task_args.size());
    return launchRet == CCU_SUCCESS ? HCCL_SUCCESS : HCCL_E_INTERNAL;
}

} // namespace ops_hccl_bcast
