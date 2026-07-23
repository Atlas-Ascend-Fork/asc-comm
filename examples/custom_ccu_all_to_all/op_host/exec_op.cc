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
using namespace ops_hccl_a2a;

namespace ops_hccl_a2a {
constexpr uint64_t UB_MAX_DATA_SIZE = 256*1024*1024; // UB 协议单次传输最大字节数

static constexpr uint64_t SetBits(uint16_t end)
{
    return ((uint64_t(1) << (end + 1)) - uint64_t(1));
}

static uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loopNumBitNum = 12;
    return SetBits(loopNumBitNum);
}

static uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum       = 7;
    constexpr uint16_t repeatNumShiftBit  = 55;
    constexpr uint16_t repeatLoopBitNum   = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum    = 7;
    constexpr uint16_t totalLoopShiftBit  = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit)
           | ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit)
           | ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

static std::vector<uint64_t> CalGoSize(uint64_t size, const LoopGroupConfig &config)
{
    uint64_t loopSize = config.loopCount * config.memSlice;
    uint64_t maxSize  = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.memSlice;
    uint64_t p = size - m * loopSize - n * config.memSlice;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loopCount - 1;
        p = config.memSlice;
    }

    uint64_t offset      = config.memSlice * config.loopCount * m;
    uint64_t loopIterNum = m;

    uint64_t loopExtendNum = 0;
    uint64_t tailSize      = 0;
    uint64_t LoopNumTwo    = 2;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize      = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = GetParallelParam(n - 1, 0, 1);
        tailSize      = config.memSlice;
    } else if (n == 0 && p != 0) {
        loopExtendNum = GetParallelParam(0, 0, 1);
        tailSize      = p;
    } else {
        loopExtendNum = GetParallelParam(n - 1, 1, LoopNumTwo);
        tailSize      = p;
    }

    return {offset, loopIterNum, loopExtendNum, tailSize};
}

static HcclResult LaunchCcuKernelSlice(const AlgResourceCtx &resCtx,
                                        uint64_t inputAddr, uint64_t outputAddr,
                                        uint64_t inputToken, uint64_t outputToken,
                                        uint64_t rankSliceSize, uint64_t sliceCount,
                                        uint64_t dataTypeSize)
{
    uint64_t sliceSize = sliceCount * dataTypeSize;

    LoopGroupConfig config{};
    config.msInterleave = CCU_MS_INTERLEAVE;
    config.loopCount    = CCU_MS_LOCAL_COPY_LOOP_COUNT;
    config.memSlice     = CCU_MS_SIZE * CCU_LOCAL_COPY_MS_PER_LOOP;
    auto goSize         = CalGoSize(sliceSize, config);

    std::vector<uint64_t> taskArgs = {
        inputAddr,
        outputAddr,
        inputToken,
        outputToken,
        rankSliceSize,
        sliceSize,
        goSize[0],
        goSize[1],
        goSize[2],
        goSize[3],
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
    const uint64_t rankSliceSize = param.perCount * dataTypeSize;
    const uint64_t inputSize = rankSliceSize * param.rankSize;
    const uint64_t outputSize = inputSize;
    const uint64_t count = param.perCount;

    if (count == 0) { // 数据量为 0，直接返回
        return HcclResult::HCCL_SUCCESS;
    }

    if (param.rankSize == 1) { // 单卡场景直接本地拷贝
        RETURN_IF_HCCL_FAIL(static_cast<HcclResult>(HcommLocalCopyOnThread(
            resCtx.threads[0], param.outputPtr, param.inputPtr, outputSize)));
        return HCCL_SUCCESS;
    }

    uint64_t maxDataSizePerLoop = UB_MAX_DATA_SIZE;
    uint64_t maxDataCountPerLoop = maxDataSizePerLoop / dataTypeSize;
    uint64_t loopCount = count / maxDataCountPerLoop + static_cast<uint64_t>(count % maxDataCountPerLoop != 0); // 计算分片处理次数
    uint64_t processedDataCount = 0;

    uint64_t inputToken = 0;
    uint64_t outputToken = 0;
    uint64_t baseInputAddr = reinterpret_cast<uint64_t>(param.inputPtr);
    uint64_t baseOutputAddr = reinterpret_cast<uint64_t>(param.outputPtr);
    CcuResult tokenRet = HcommCcuGetMemToken(baseInputAddr, inputSize, &inputToken);
    if (tokenRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    tokenRet = HcommCcuGetMemToken(baseOutputAddr, outputSize, &outputToken);
    if (tokenRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    for (uint64_t loop = 0; loop < loopCount; loop++) {
        uint64_t sliceCount = std::min(maxDataCountPerLoop, count - loop * maxDataCountPerLoop);
        uint64_t inputAddr = baseInputAddr + processedDataCount * dataTypeSize;
        uint64_t outputAddr = baseOutputAddr + processedDataCount * dataTypeSize;

        RETURN_IF_HCCL_FAIL(LaunchCcuKernelSlice(resCtx, inputAddr, outputAddr, inputToken, outputToken,
            rankSliceSize, sliceCount, dataTypeSize));

        processedDataCount += sliceCount;
    }

    return HCCL_SUCCESS;
}
} // namespace ops_hccl_a2a


