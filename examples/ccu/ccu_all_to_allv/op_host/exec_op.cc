/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <vector>
#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hcomm_primitives.h>
#include <hcomm/hcomm_ccu_launch_api.h>
#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"
#include "alg_resource.h"

namespace ops_hccl_a2av {

constexpr uint64_t MAX_TRANSFER_BYTES = 256ULL * 1024 * 1024;

static uint64_t GetBufferElements(const std::vector<uint64_t>& counts, const std::vector<uint64_t>& displs)
{
    uint64_t elements = 0;
    for (uint32_t i = 0; i < counts.size(); i++) {
        if (counts[i] != 0) {
            elements = std::max(elements, displs[i] + counts[i]);
        }
    }
    return elements;
}

HcclResult ExecOp(const OpParam& param_, const AlgResourceCtx& resCtx)
{
    constexpr uint64_t dataTypeSize = sizeof(float);
    const uint64_t inputSize = GetBufferElements(param_.sendCounts, param_.sendDispls) * dataTypeSize;
    const uint64_t outputSize = GetBufferElements(param_.recvCounts, param_.recvDispls) * dataTypeSize;
    if (param_.rankSize == 1) {
        if (param_.sendCounts[0] != param_.recvCounts[0]) {
            return HCCL_E_PARA;
        }
        if (param_.sendCounts[0] == 0) {
            return HCCL_SUCCESS;
        }
        auto* src_ = static_cast<char*>(param_.inputPtr) + param_.sendDispls[0] * dataTypeSize;
        auto* dst_ = static_cast<char*>(param_.outputPtr) + param_.recvDispls[0] * dataTypeSize;
        return static_cast<HcclResult>(
            HcommLocalCopyOnThread(resCtx.threads[0], dst_, src_, param_.sendCounts[0] * dataTypeSize));
    }

    uint64_t inputToken = 0;
    uint64_t outputToken = 0;
    const uint64_t inputAddr = reinterpret_cast<uint64_t>(param_.inputPtr);
    const uint64_t outputAddr = reinterpret_cast<uint64_t>(param_.outputPtr);
    if ((inputSize != 0 && asccomm_ccu_get_mem_token(inputAddr, inputSize, &inputToken) != CCU_SUCCESS) ||
        (outputSize != 0 && asccomm_ccu_get_mem_token(outputAddr, outputSize, &outputToken) != CCU_SUCCESS)) {
        return HCCL_E_INTERNAL;
    }

    std::vector<uint64_t> task_args = {inputAddr, outputAddr, inputToken, outputToken};
    for (uint32_t rankIdx = 0; rankIdx < param_.rankSize; rankIdx++) {
        const uint64_t sendBytes = param_.sendCounts[rankIdx] * dataTypeSize;
        if (sendBytes > MAX_TRANSFER_BYTES) {
            return HCCL_E_NOT_SUPPORT;
        }
        task_args.push_back(sendBytes);
        task_args.push_back(param_.sendDispls[rankIdx] * dataTypeSize);
        task_args.push_back(param_.recvDispls[rankIdx] * dataTypeSize);
    }

    HcommCcuLaunchContextPod launch_context{};
    if (HcommCcuGetLaunchContext(resCtx.threads[0], &launch_context) != HCCL_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    CcuResult launchRet =
        asccomm_ccu_kernel_launch(&launch_context, resCtx.ccuKernels[0], task_args.data(), task_args.size());
    return launchRet == CCU_SUCCESS ? HCCL_SUCCESS : HCCL_E_INTERNAL;
}

} // namespace ops_hccl_a2av
