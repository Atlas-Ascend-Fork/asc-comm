/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H
#define HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ccu/hcomm/ccu_array.hpp>
#include <ccu/hcomm/ccu_buffer.hpp>
#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_func.hpp>
#include <ccu/hcomm/ccu_loop.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <ccu/hcomm/ccu_types.h>
#include <ccu/hcomm/ccu_variable.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_ag {

#define RETURN_IF_CCU_FAIL(call)  \
    do {                          \
        CcuResult ret = (call);   \
        if (ret != CCU_SUCCESS) { \
            return ret;           \
        }                         \
    } while (0)

constexpr uint64_t CCU_MAX_RANK_SIZE = 16;
constexpr uint64_t CCU_MS_INTERLEAVE = 8;
constexpr uint64_t CCU_MS_SIZE = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

struct LoopGroupConfig {
    uint32_t msInterleave;
    uint32_t loopCount;
    uint64_t memSlice;
};

struct GroupOpSize {
    uint64_t addrOffset;
    uint64_t loopParam;
    uint64_t parallelParam;
    uint64_t residual;
};

constexpr uint64_t SetBits(uint16_t start, uint16_t end)
{
    return ((uint64_t(1) << (end - start + 1)) - uint64_t(1)) << start;
}

constexpr uint64_t SetBits(uint16_t end)
{
    return (uint64_t(1) << (end + 1)) - uint64_t(1);
}

constexpr uint64_t GetMaxLoopIterNum()
{
    constexpr uint16_t loopNumBitNum = 12;
    return SetBits(loopNumBitNum);
}

constexpr uint64_t GetLoopParam(uint64_t loopCtxId, uint64_t gsaOffset, uint64_t loopIterNum)
{
    constexpr uint16_t ctxIdBitNum = 8;
    constexpr uint16_t ctxIdShiftBit = 45;
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 13;
    constexpr uint16_t loopNumBitNum = 13;
    constexpr uint16_t loopNumShiftBit = 0;
    return ((loopCtxId & SetBits(ctxIdBitNum)) << ctxIdShiftBit) | ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit) |
           ((loopIterNum & SetBits(loopNumBitNum)) << loopNumShiftBit);
}

constexpr uint64_t GetParallelParam(uint64_t repeatNum, uint64_t repeatLoopIndex, uint64_t totalLoopNum)
{
    constexpr uint16_t repeatBitNum = 7;
    constexpr uint16_t repeatNumShiftBit = 55;
    constexpr uint16_t repeatLoopBitNum = 7;
    constexpr uint16_t repeatLoopShiftBit = 48;
    constexpr uint16_t totalLoopBitNum = 7;
    constexpr uint16_t totalLoopShiftBit = 41;
    return ((repeatNum & SetBits(repeatBitNum)) << repeatNumShiftBit) |
           ((repeatLoopIndex & SetBits(repeatLoopBitNum)) << repeatLoopShiftBit) |
           ((totalLoopNum & SetBits(totalLoopBitNum)) << totalLoopShiftBit);
}

constexpr uint64_t GetOffsetParam(uint64_t gsaOffset, uint64_t msOffset, uint64_t ckeOffset)
{
    constexpr uint16_t gsaBitNum = 32;
    constexpr uint16_t gsaShiftBit = 21;
    constexpr uint16_t msBitNum = 11;
    constexpr uint16_t msShiftBit = 10;
    constexpr uint16_t ckeBitNum = 10;
    constexpr uint16_t ckeShiftBit = 0;
    return ((gsaOffset & SetBits(gsaBitNum)) << gsaShiftBit) | ((msOffset & SetBits(msBitNum)) << msShiftBit) |
           ((ckeOffset & SetBits(ckeBitNum)) << ckeShiftBit);
}

inline GroupOpSize CalGoSize(uint64_t size, const LoopGroupConfig& config)
{
    uint64_t loopSize = config.loopCount * config.memSlice;
    uint64_t maxSize = loopSize * (GetMaxLoopIterNum() + 1);

    uint64_t m = size / loopSize;
    uint64_t n = (size - m * loopSize) / config.memSlice;
    uint64_t p = size - m * loopSize - n * config.memSlice;

    if (size == maxSize) {
        m = GetMaxLoopIterNum();
        n = config.loopCount - 1;
        p = config.memSlice;
    }

    uint64_t loopExtendNum = 0;
    uint64_t tailSize = 0;
    constexpr uint64_t loopNumTwo = 2;

    if (n == 0 && p == 0) {
        loopExtendNum = 0;
        tailSize = 0;
    } else if (n != 0 && p == 0) {
        loopExtendNum = GetParallelParam(n - 1, 0, 1);
        tailSize = config.memSlice;
    } else if (n == 0 && p != 0) {
        loopExtendNum = GetParallelParam(0, 0, 1);
        tailSize = p;
    } else {
        loopExtendNum = GetParallelParam(n - 1, 1, loopNumTwo);
        tailSize = p;
    }

    return {config.memSlice * config.loopCount * m, m, loopExtendNum, tailSize};
}

struct LoopGroupResource {
    ccu::Array<ccu::Event> completedEvent{0};
    ccu::Array<ccu::CcuBuffer> ccuBuf{0};
    uint32_t eventCount;
    uint32_t bufCount;
};

struct GroupOpSizeVars {
    ccu::Variable addrOffset;
    ccu::Variable loopParam;
    ccu::Variable parallelParam;
    ccu::Variable residual;
};

struct CcuLoopEntity {
    std::unique_ptr<ccu::Func> body[2];
    std::unique_ptr<ccu::Loop> loops[2];
    ccu::Variable loopParam[2];
};

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t channelCount;
};

struct CcuKernelInfo {
    char kernelFuncName[64];
    void* kernelFunc;
    void* kernelArg;

private:
    std::shared_ptr<CcuKernelArgBase> kernelArgSmartPtr;

public:
    template <typename T>
    void setKernelArg(std::shared_ptr<T> arg)
    {
        kernelArgSmartPtr = std::static_pointer_cast<CcuKernelArgBase>(arg);
        kernelArg = static_cast<void*>(arg.get());
    }
};

struct CcuKernelArgAllGatherMesh1DMem2Mem : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
};

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::Variable input;
    std::vector<ccu::Variable> output;
    std::vector<ccu::Variable> token;
    ccu::Variable currentRankSliceInputOffset;
    ccu::Variable currentRankSliceOutputOffset;
    ccu::Variable sliceSize;
    ccu::Event event;

    GroupOpSizeVars goSize;
    LoopGroupConfig moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string& loopStr)
    {
        loopMap.emplace(loopStr, CcuLoopEntity());
    }

    bool IsLoopEntityRegistered(const std::string& loopStr)
    {
        return loopMap.count(loopStr) != 0;
    }
};

CcuResult CcuAllGatherMesh1DMem2MemKernel(CcuKernelArg arg);

} // namespace ops_hccl_ag

#endif // HCCL_CCU_KERNEL_ALL_GATHER_MESH_1D_MEM2MEM_H
