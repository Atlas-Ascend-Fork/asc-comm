/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 *
 * AllGather Mesh1D Mem2Mem CCU kernel, ported from
 * asc-devkit/pr_4261 ccu_kernel_all_gather_mesh1d_mem2mem, rewritten against
 * asc-comm's ccu:: free-function representation API (same style as ccu_direct).
 */
#ifndef ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_KERNEL_H
#define ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_KERNEL_H

#include <algorithm>
#include <cstdint>
#include <ios>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "log.h"

namespace ccu = ::AscendC::ccu;

namespace ccu_ag_mem2mem {

// Group-copy tiling constants. Must stay consistent between CalGoSize (host)
// and the kernel-side GroupCopy resource config.
constexpr uint64_t CCU_MS_INTERLEAVE = 8;
constexpr uint64_t CCU_MS_SIZE = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

struct LoopGroupConfig {
    uint32_t msInterleave;
    uint32_t loopCount;
    uint64_t memSlice;
};

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
    ccu::Variable tmpRepeatNum;
    ccu::Variable inputRepeatStride;
    ccu::Variable outputRepeatStride;
    ccu::Variable normalSliceSize;
    ccu::Variable lastSliceSize;
    ccu::Variable isInputOutputEqual;
    std::vector<ccu::Event> events;

    GroupOpSizeVars goSize;
    LoopGroupConfig moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(std::string loopStr) {
        loopMap.emplace(loopStr, CcuLoopEntity());
    }
    bool IsLoopEntityRegistered(std::string loopStr) {
        return loopMap.count(loopStr) != 0;
    }
};

CcuResult CcuAllGatherMesh1DMem2MemKernel(CcuKernelArg arg);

}  // namespace ccu_ag_mem2mem

#endif  // ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_KERNEL_H
