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

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ccu/ccu_event.hpp>
#include <ccu/ccu_primitives.hpp>
#include <ccu/ccu_variable.hpp>

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

enum TaskArgIndex : uint32_t {
    TASK_INPUT_ADDR = 0,
    TASK_OUTPUT_ADDR,
    TASK_TOKEN,
    TASK_INPUT_SLICE_STRIDE,
    TASK_OUTPUT_SLICE_STRIDE,
    TASK_CHUNK_SIZE,
    TASK_GO_ADDR_OFFSET,
    TASK_GO_LOOP_PARAM,
    TASK_GO_PARALLEL_PARAM,
    TASK_GO_RESIDUAL,
};

constexpr uint32_t TASK_ARG_NUM = 10;
static_assert(TASK_GO_RESIDUAL + 1 == TASK_ARG_NUM);

struct LoopGroupConfig {
    uint32_t msInterleave;
    uint32_t loopCount;
    uint64_t memSlice;
};

struct LoopGroupResource {
    ccu::Array<ccu::Event> completedEvent{0};
    ccu::Array<ccu::CcuBuffer> ccuBuf{0};
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
    uint32_t rankSize;
    uint32_t rankId;
};

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::Variable input;
    std::vector<ccu::Variable> output;
    std::vector<ccu::Variable> token;
    ccu::Variable inputSliceStride;
    ccu::Variable outputSliceStride;
    ccu::Variable chunkSize;
    std::vector<ccu::Event> events;

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

CcuResult CcuAllGatherMesh1DMem2MemKernel(CcuKernelArg arg);

}  // namespace ccu_ag_mem2mem

#endif  // ASC_COMM_EXAMPLE_CCU_AG_MEM2MEM_KERNEL_H
