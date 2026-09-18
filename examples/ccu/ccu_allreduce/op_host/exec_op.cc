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
#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hcomm_primitives.h>
#include <hcomm/hcomm_ccu_launch_api.h>
#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"
#include "alg_resource.h"
#include "ccu_kernel.h"
using namespace ops_hccl_ar;

namespace ops_hccl_ar {
constexpr uint64_t UB_MAX_DATA_SIZE = 256 * 1024 * 1024; // UB 协议单次传输最大字节数

static HcclResult LaunchCcuKernelSlice(
    const AlgResourceCtx& resCtx, uint64_t inputAddr, uint64_t outputAddr, uint64_t token, uint64_t sliceCount,
    uint64_t dataTypeSize)
{
    uint64_t sliceSize = sliceCount * dataTypeSize;

    std::vector<uint64_t> task_args = {
        inputAddr,
        outputAddr,
        token,
        sliceSize,
    };

    HcommCcuLaunchContextPod launch_context{};
    if (HcommCcuGetLaunchContext(resCtx.threads[0], &launch_context) != HCCL_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    CcuResult launchRet =
        asccomm_ccu_kernel_launch(&launch_context, resCtx.ccuKernels[0], task_args.data(), task_args.size());
    if (launchRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    return HCCL_SUCCESS;
}

HcclResult ExecOp(const OpParam& param_, const AlgResourceCtx& resCtx)
{
    constexpr uint64_t dataTypeSize = sizeof(float);
    uint64_t dataSize = param_.count_ * dataTypeSize;
    uint64_t count_ = param_.count_;

    if (count_ == 0) { // 数据量为 0，直接返回
        return HcclResult::HCCL_SUCCESS;
    }

    if (param_.rankSize == 1) { // 单卡场景直接本地拷贝
        RETURN_IF_HCCL_FAIL(static_cast<HcclResult>(
            HcommLocalCopyOnThread(resCtx.threads[0], param_.outputPtr, param_.inputPtr, dataSize)));
        return HCCL_SUCCESS;
    }

    uint64_t maxDataSizePerLoop = UB_MAX_DATA_SIZE;
    uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    uint64_t loop_count =
        count_ / maxDataCountPerLoop + static_cast<uint64_t>(count_ % maxDataCountPerLoop != 0); // 计算分片处理次数
    uint64_t processedDataCount = 0;

    uint64_t token = 0;
    uint64_t baseInputAddr = reinterpret_cast<uint64_t>(param_.inputPtr);
    uint64_t baseOutputAddr = reinterpret_cast<uint64_t>(param_.outputPtr);
    if (param_.inputPtr != nullptr) {
        asccomm_ccu_get_mem_token(baseInputAddr, static_cast<uint64_t>(dataSize), &token);
    } else if (param_.outputPtr != nullptr) {
        asccomm_ccu_get_mem_token(baseOutputAddr, static_cast<uint64_t>(dataSize), &token);
    }

    for (uint64_t loop = 0; loop < loop_count; loop++) {
        uint64_t sliceCount = std::min(maxDataCountPerLoop, count_ - loop * maxDataCountPerLoop);
        uint64_t inputAddr = baseInputAddr + processedDataCount * dataTypeSize;
        uint64_t outputAddr = baseOutputAddr + processedDataCount * dataTypeSize;

        RETURN_IF_HCCL_FAIL(LaunchCcuKernelSlice(resCtx, inputAddr, outputAddr, token, sliceCount, dataTypeSize));

        processedDataCount += sliceCount;
    }

    return HCCL_SUCCESS;
}
} // namespace ops_hccl_ar
