/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
#include <cstdint>
#include <vector>

#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hcomm_primitives.h>
#include <hcomm/hcomm_ccu_launch_api.h>

#include "alg_resource.h"
#include "ccu/hcomm/ccu_launch.h"
#include "ccu/hcomm/ccu_resource_api.h"
#include "ccu_kernel.h"

namespace ops_hccl_ag {
constexpr uint64_t UB_MAX_DATA_SIZE = 256 * 1024 * 1024;

static HcclResult GetDataTypeSize(HcclDataType data_type_, uint32_t& dataTypeSize)
{
    if (data_type_ != HCCL_DATA_TYPE_FP32) {
        return HCCL_E_NOT_SUPPORT;
    }
    dataTypeSize = sizeof(float);
    return HCCL_SUCCESS;
}

static HcclResult LaunchCcuKernelSlice(
    const AlgResourceCtx& resCtx, uint64_t inputAddr, uint64_t outputAddr, uint64_t token, uint64_t dataSize,
    uint32_t myRank, uint64_t sliceCount, uint64_t dataTypeSize)
{
    uint64_t sliceSize = sliceCount * dataTypeSize;

    uint64_t currentRankSliceInputOffset = 0;
    uint64_t currentRankSliceOutputOffset = dataSize * myRank;

    loop_group_config config{};
    config.ms_interleave = ccu_ms_interleave;
    config.loop_count = CCU_MS_LOCAL_COPY_LOOP_COUNT;
    config.mem_slice = ccu_ms_size * CCU_LOCAL_COPY_MS_PER_LOOP;
    GroupOpSize goSize = CalGoSize(sliceSize, config);

    std::vector<uint64_t> task_args = {
        inputAddr,
        outputAddr,
        token,
        currentRankSliceInputOffset,
        currentRankSliceOutputOffset,
        sliceSize,
        goSize.addr_offset,
        goSize.loop_param_,
        goSize.parallel_param,
        goSize.residual,
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
    uint32_t dataTypeSize = 0;
    RETURN_IF_HCCL_FAIL(GetDataTypeSize(param_.data_type_, dataTypeSize));
    uint64_t dataSize = param_.count_ * dataTypeSize;
    uint64_t count_ = param_.count_;

    if (count_ == 0) {
        return HCCL_SUCCESS;
    }

    if (param_.rankSize == 1) {
        RETURN_IF_HCCL_FAIL(static_cast<HcclResult>(
            HcommLocalCopyOnThread(resCtx.threads[0], param_.outputPtr, param_.inputPtr, dataSize)));
        return HCCL_SUCCESS;
    }

    uint64_t maxDataCountPerLoop = UB_MAX_DATA_SIZE / dataTypeSize;
    uint64_t loop_count = count_ / maxDataCountPerLoop + static_cast<uint64_t>(count_ % maxDataCountPerLoop != 0);
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

        RETURN_IF_HCCL_FAIL(LaunchCcuKernelSlice(
            resCtx, inputAddr, outputAddr, token, dataSize, param_.myRank, sliceCount, dataTypeSize));

        processedDataCount += sliceCount;
    }

    return HCCL_SUCCESS;
}

} // namespace ops_hccl_ag
