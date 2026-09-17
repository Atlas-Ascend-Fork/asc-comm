/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms of conditions of
 * CANN Open Software License Agreement Version 2.0 ("License").
 * Please refer to the License for details.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>

#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hcomm_primitives.h>
#include <hcomm/hcomm_ccu_launch_api.h>
#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"
#include "alg_resource.h"
#include "ccu_kernel.h"

namespace ops_hccl_transfer {
namespace {

constexpr uint64_t DATA_TYPE_SIZE = sizeof(float);

} // namespace

HcclResult ExecOp(const OpParam& param, const AlgResourceCtx& resourceCtx)
{
    if (param.rankSize != 2U || resourceCtx.threads.empty() || resourceCtx.ccuKernels.empty()) {
        return HCCL_E_PARA;
    }

    const uint64_t dataSize = ELEMENT_COUNT * DATA_TYPE_SIZE;
    uint64_t inputToken = 0;
    uint64_t outputToken = 0;
    if (asccomm_ccu_get_mem_token(reinterpret_cast<uint64_t>(param.inputPtr), dataSize, &inputToken) != CCU_SUCCESS ||
        asccomm_ccu_get_mem_token(reinterpret_cast<uint64_t>(param.outputPtr), dataSize, &outputToken) !=
            CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    const std::vector<uint64_t> taskArgs = {
        reinterpret_cast<uint64_t>(param.inputPtr),
        reinterpret_cast<uint64_t>(param.outputPtr),
        inputToken,
        outputToken,
        dataSize,
    };

    HcommCcuLaunchContextPod launchContext{};
    if (HcommCcuGetLaunchContext(resourceCtx.threads[0], &launchContext) != HCCL_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    return asccomm_ccu_kernel_launch(
               &launchContext, resourceCtx.ccuKernels[0], taskArgs.data(), taskArgs.size()) == CCU_SUCCESS
               ? HCCL_SUCCESS
               : HCCL_E_INTERNAL;
}

} // namespace ops_hccl_transfer
