/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 *
 * AllGather NHR1D Mem2Mem CCU kernel. The NHR scheduling and data forwarding
 * are ported from asc-devkit/pr_4261
 * ccu_kernel_all_gather_nhr1d_mem2mem.{h,cc}. The implementation is adapted to
 * asc-comm's direct-launch ccu:: free-function representation API.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_AG_NHR1D_MEM2MEM_KERNEL_H
#define ASC_COMM_EXAMPLE_CCU_AG_NHR1D_MEM2MEM_KERNEL_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "log.h"

namespace ccu = ::AscendC::ccu;

namespace ccu_ag_nhr1d_mem2mem {

// Group-copy tiling constants. These must stay consistent with host CalGoSize.
constexpr uint64_t CCU_MS_INTERLEAVE = 8;
constexpr uint64_t CCU_MS_SIZE = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;

struct NhrStepInfo {
    uint32_t step = 0;
    uint32_t toRank = 0;
    uint32_t fromRank = 0;
    std::vector<uint32_t> txSliceIdxs;
};

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

// Static registration-time arguments. stepInfoVector and rank2ChannelIdx are
// generated on the host with the same modulo NHR schedule as PR-4261.
struct CcuKernelArgAllGatherNhr1DMem2Mem : public CcuKernelArgBase {
    uint32_t rankSize;
    uint32_t rankId;
    std::vector<NhrStepInfo> stepInfoVector;
    std::map<uint32_t, uint32_t> rank2ChannelIdx;
};

struct AllGatherNhr1DMem2MemContext {
    const CcuKernelArgAllGatherNhr1DMem2Mem *arg;

    ccu::Variable input;
    std::vector<ccu::Variable> output;
    std::vector<ccu::Variable> token;
    ccu::Variable sliceSize;
    ccu::Event transferEvent;

    GroupOpSizeVars goSize;
    LoopGroupConfig moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;
    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string &loopStr)
    {
        loopMap.emplace(loopStr, CcuLoopEntity());
    }

    bool IsLoopEntityRegistered(const std::string &loopStr) const
    {
        return loopMap.count(loopStr) != 0;
    }
};

CcuResult CcuAllGatherNhr1DMem2MemKernel(CcuKernelArg arg);

}  // namespace ccu_ag_nhr1d_mem2mem

#endif  // ASC_COMM_EXAMPLE_CCU_AG_NHR1D_MEM2MEM_KERNEL_H
