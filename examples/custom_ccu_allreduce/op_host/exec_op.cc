/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <algorithm>
#include <ccu/ccu_types.h>
#include <hccl/hcomm_primitives.h>
#include "ccu_launch.h"
#include "ccu_res.h"
#include "alg_resource.h"
#include "ccu_kernel.h"
using namespace ops_hccl_ar;

namespace ops_hccl_ar {
constexpr uint64_t UB_MAX_DATA_SIZE = 256*1024*1024; // UB 协议单次传输最大字节数

static HcclResult LaunchCcuKernelSlice(const AlgResourceCtx &resCtx,
                                        uint64_t inputAddr, uint64_t outputAddr,
                                        uint64_t token, uint64_t sliceCount,
                                        uint64_t dataTypeSize)
{
    uint64_t sliceSize = sliceCount * dataTypeSize;

    std::vector<uint64_t> taskArgs = {
        inputAddr,
        outputAddr,
        token,
        sliceSize,
    };

    CcuResult launchRet = HcommCcuKernelLaunch(resCtx.threads[0], resCtx.ccuKernels[0],
                                                taskArgs.data(), taskArgs.size());
    if (launchRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    return HCCL_SUCCESS;
}

HcclResult ExecOp(const OpParam &param, const AlgResourceCtx &resCtx)
{
    constexpr uint64_t dataTypeSize = sizeof(float);
    uint64_t dataSize = param.count * dataTypeSize;
    uint64_t count = param.count;

    if (count == 0) { // 数据量为 0，直接返回
        return HcclResult::HCCL_SUCCESS;
    }

    if (param.rankSize == 1) { // 单卡场景直接本地拷贝
        RETURN_IF_HCCL_FAIL(static_cast<HcclResult>(HcommLocalCopyOnThread(resCtx.threads[0], param.outputPtr, param.inputPtr, dataSize)));
        return HCCL_SUCCESS;
    }

    uint64_t maxDataSizePerLoop = UB_MAX_DATA_SIZE;
    uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    uint64_t loopCount = count / maxDataCountPerLoop + static_cast<uint64_t>(count % maxDataCountPerLoop != 0); // 计算分片处理次数
    uint64_t processedDataCount = 0;

    uint64_t token = 0;
    uint64_t baseInputAddr = reinterpret_cast<uint64_t>(param.inputPtr);
    uint64_t baseOutputAddr = reinterpret_cast<uint64_t>(param.outputPtr);
    if (param.inputPtr != nullptr) {
        HcommCcuGetMemToken(baseInputAddr, static_cast<uint64_t>(dataSize), &token);
    } else if (param.outputPtr != nullptr) {
        HcommCcuGetMemToken(baseOutputAddr, static_cast<uint64_t>(dataSize), &token);
    }

    for (uint64_t loop = 0; loop < loopCount; loop++) {
        uint64_t sliceCount = std::min(maxDataCountPerLoop, count - loop * maxDataCountPerLoop);
        uint64_t inputAddr = baseInputAddr + processedDataCount * dataTypeSize;
        uint64_t outputAddr = baseOutputAddr + processedDataCount * dataTypeSize;

        RETURN_IF_HCCL_FAIL(LaunchCcuKernelSlice(resCtx, inputAddr, outputAddr, token, sliceCount, dataTypeSize));

        processedDataCount += sliceCount;
    }

    return HCCL_SUCCESS;
}
} // namespace ops_hccl_ar


