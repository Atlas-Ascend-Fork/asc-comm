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
using namespace ops_hccl_rs;

namespace ops_hccl_rs {
constexpr uint64_t UB_MAX_DATA_SIZE = 256 * 1024 * 1024; // UB 协议单次传输最大字节数

static constexpr uint64_t set_bits(uint16_t end) { return ((uint64_t(1) << (end + 1)) - uint64_t(1)); }

static uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loop_num_bit_num = 12;
    return set_bits(loop_num_bit_num);
}

static uint64_t get_parallel_param(uint64_t repeat_num, uint64_t repeat_loop_index, uint64_t total_loop_num)
{
    constexpr uint16_t repeat_bit_num = 7;
    constexpr uint16_t repeat_num_shift_bit = 55;
    constexpr uint16_t repeat_loop_bit_num = 7;
    constexpr uint16_t repeat_loop_shift_bit = 48;
    constexpr uint16_t total_loop_bit_num = 7;
    constexpr uint16_t total_loop_shift_bit = 41;
    return ((repeat_num & set_bits(repeat_bit_num)) << repeat_num_shift_bit) |
           ((repeat_loop_index & set_bits(repeat_loop_bit_num)) << repeat_loop_shift_bit) |
           ((total_loop_num & set_bits(total_loop_bit_num)) << total_loop_shift_bit);
}

static std::vector<uint64_t> CalGoSize(uint64_t size, const loop_group_config& config)
{
    uint64_t loopSize = config.loop_count * config.mem_slice;
    uint64_t maxSize = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.mem_slice;
    uint64_t p = size - m * loopSize - n * config.mem_slice;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loop_count - 1;
        p = config.mem_slice;
    }

    uint64_t offset = config.mem_slice * config.loop_count * m;
    uint64_t loop_iter_num_ = m;

    uint64_t loopExtendNum = 0;
    uint64_t tailSize = 0;
    uint64_t LoopNumTwo = 2;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = get_parallel_param(n - 1, 0, 1);
        tailSize = config.mem_slice;
    } else if (n == 0 && p != 0) {
        loopExtendNum = get_parallel_param(0, 0, 1);
        tailSize = p;
    } else {
        loopExtendNum = get_parallel_param(n - 1, 1, LoopNumTwo);
        tailSize = p;
    }

    return {offset, loop_iter_num_, loopExtendNum, tailSize};
}

static HcclResult LaunchCcuKernelSlice(
    const AlgResourceCtx& resCtx, uint64_t inputAddr, uint64_t outputAddr, uint64_t inputToken, uint64_t outputToken,
    uint64_t rankSliceSize, uint64_t sliceCount, uint64_t dataTypeSize)
{
    uint64_t sliceSize = sliceCount * dataTypeSize;

    loop_group_config config{};
    config.ms_interleave = ccu_ms_interleave;
    config.loop_count = CCU_MS_LOCAL_COPY_LOOP_COUNT;
    config.mem_slice = ccu_ms_size * CCU_LOCAL_COPY_MS_PER_LOOP;
    auto goSize = CalGoSize(sliceSize, config);

    std::vector<uint64_t> task_args = {
        inputAddr, outputAddr, inputToken, outputToken, rankSliceSize,
        sliceSize, goSize[0],  goSize[1],  goSize[2],   goSize[3],
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
    const uint64_t rankSliceSize = param_.recvCount * dataTypeSize;
    const uint64_t inputSize = rankSliceSize * param_.rankSize;
    const uint64_t outputSize = rankSliceSize;
    const uint64_t count_ = param_.recvCount;

    if (count_ == 0) { // 数据量为 0，直接返回
        return HcclResult::HCCL_SUCCESS;
    }

    if (param_.rankSize == 1) { // 单卡场景直接本地拷贝
        RETURN_IF_HCCL_FAIL(static_cast<HcclResult>(
            HcommLocalCopyOnThread(resCtx.threads[0], param_.outputPtr, param_.inputPtr, outputSize)));
        return HCCL_SUCCESS;
    }

    uint64_t maxDataSizePerLoop = UB_MAX_DATA_SIZE;
    uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    uint64_t loop_count =
        count_ / maxDataCountPerLoop + static_cast<uint64_t>(count_ % maxDataCountPerLoop != 0); // 计算分片处理次数
    uint64_t processedDataCount = 0;

    uint64_t inputToken = 0;
    uint64_t outputToken = 0;
    uint64_t baseInputAddr = reinterpret_cast<uint64_t>(param_.inputPtr);
    uint64_t baseOutputAddr = reinterpret_cast<uint64_t>(param_.outputPtr);
    CcuResult tokenRet = asccomm_ccu_get_mem_token(baseInputAddr, inputSize, &inputToken);
    if (tokenRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    tokenRet = asccomm_ccu_get_mem_token(baseOutputAddr, outputSize, &outputToken);
    if (tokenRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    for (uint64_t loop = 0; loop < loop_count; loop++) {
        uint64_t sliceCount = std::min(maxDataCountPerLoop, count_ - loop * maxDataCountPerLoop);
        uint64_t inputAddr = baseInputAddr + processedDataCount * dataTypeSize;
        uint64_t outputAddr = baseOutputAddr + processedDataCount * dataTypeSize;

        RETURN_IF_HCCL_FAIL(LaunchCcuKernelSlice(
            resCtx, inputAddr, outputAddr, inputToken, outputToken, rankSliceSize, sliceCount, dataTypeSize));

        processedDataCount += sliceCount;
    }

    return HCCL_SUCCESS;
}
} // namespace ops_hccl_rs
