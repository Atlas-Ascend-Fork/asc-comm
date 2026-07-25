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
#include <ccu/ccu_types.h>
#include <hccl/hcomm_primitives.h>
#include "ccu_launch.h"
#include "ccu_res.h"
#include "alg_resource.h"

namespace ops_hccl_a2av {

constexpr uint64_t MAX_TRANSFER_BYTES = 256ULL * 1024 * 1024;

static uint64_t GetBufferElements(const std::vector<uint64_t> &counts,
                                  const std::vector<uint64_t> &displs)
{
    uint64_t elements = 0;
    for (uint32_t i = 0; i < counts.size(); i++) {
        if (counts[i] != 0) {
            elements = std::max(elements, displs[i] + counts[i]);
        }
    }
    return elements;
}

HcclResult ExecOp(const OpParam &param, const AlgResourceCtx &resCtx)
{
    constexpr uint64_t dataTypeSize = sizeof(float);
    const uint64_t inputSize = GetBufferElements(param.sendCounts, param.sendDispls) * dataTypeSize;
    const uint64_t outputSize = GetBufferElements(param.recvCounts, param.recvDispls) * dataTypeSize;
    if (param.rankSize == 1) {
        if (param.sendCounts[0] != param.recvCounts[0]) {
            return HCCL_E_PARA;
        }
        if (param.sendCounts[0] == 0) {
            return HCCL_SUCCESS;
        }
        auto *src = static_cast<char *>(param.inputPtr) + param.sendDispls[0] * dataTypeSize;
        auto *dst = static_cast<char *>(param.outputPtr) + param.recvDispls[0] * dataTypeSize;
        return static_cast<HcclResult>(HcommLocalCopyOnThread(
            resCtx.threads[0], dst, src, param.sendCounts[0] * dataTypeSize));
    }

    uint64_t inputToken = 0;
    uint64_t outputToken = 0;
    const uint64_t inputAddr = reinterpret_cast<uint64_t>(param.inputPtr);
    const uint64_t outputAddr = reinterpret_cast<uint64_t>(param.outputPtr);
    if ((inputSize != 0 && HcommCcuGetMemToken(inputAddr, inputSize, &inputToken) != CCU_SUCCESS) ||
        (outputSize != 0 && HcommCcuGetMemToken(outputAddr, outputSize, &outputToken) != CCU_SUCCESS)) {
        return HCCL_E_INTERNAL;
    }

    std::vector<uint64_t> taskArgs = {inputAddr, outputAddr, inputToken, outputToken};
    for (uint32_t rankIdx = 0; rankIdx < param.rankSize; rankIdx++) {
        const uint64_t sendBytes = param.sendCounts[rankIdx] * dataTypeSize;
        if (sendBytes > MAX_TRANSFER_BYTES) {
            return HCCL_E_NOT_SUPPORT;
        }
        taskArgs.push_back(sendBytes);
        taskArgs.push_back(param.sendDispls[rankIdx] * dataTypeSize);
        taskArgs.push_back(param.recvDispls[rankIdx] * dataTypeSize);
    }

    CcuResult launchRet = HcommCcuKernelLaunch(resCtx.threads[0], resCtx.ccuKernels[0],
        taskArgs.data(), taskArgs.size());
    return launchRet == CCU_SUCCESS ? HCCL_SUCCESS : HCCL_E_INTERNAL;
}

} // namespace ops_hccl_a2av
