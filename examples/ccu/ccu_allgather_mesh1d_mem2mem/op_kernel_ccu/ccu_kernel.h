/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <ccu/hcomm/ccu_api_types.h>
#include <ccu/hcomm/ccu_variable.hpp>

namespace ccu = ::AscendC::ccu;

namespace CcuAgMem2mem {

#define RETURN_IF_CCU_FAIL(call)     \
    do {                             \
        CcuResult result = (call);   \
        if (result != CCU_SUCCESS) { \
            return result;           \
        }                            \
    } while (0)

constexpr uint32_t CCU_MAX_RANK_SIZE = 16;

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE]{};
    uint32_t channelCount{0};
};

// Group-copy tiling constants. Must stay consistent between CalGoSize (host)
// and the kernel-side GroupCopy resource config.
constexpr uint64_t ccu_ms_interleave = 8;
constexpr uint64_t ccu_ms_size = 4096;
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

struct loop_group_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

struct LoopGroupResource {
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
};

struct GroupOpSizeVars {
    ccu::variable addr_offset;
    ccu::variable loop_param_;
    ccu::variable parallel_param;
    ccu::variable residual;
};

struct CcuLoopEntity {
    std::unique_ptr<ccu::func> body[2];
    std::unique_ptr<ccu::loop> loops[2];
    ccu::variable loop_param_[2];
};

struct CcuKernelArgAllGatherMesh1DMem2Mem : public CcuKernelArgBase {
    uint32_t rankSize;
    uint32_t rankId;
};

struct AllGatherMesh1DMem2MemContext {
    const CcuKernelArgAllGatherMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    ccu::variable inputSliceStride;
    ccu::variable outputSliceStride;
    ccu::variable chunkSize;
    std::vector<ccu::event> events;

    GroupOpSizeVars goSize;
    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(const std::string& loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(const std::string& loopStr) const { return loopMap.count(loopStr) != 0; }
};

CcuResult CcuAllGatherMesh1DMem2MemKernel(ccu_kernel_arg arg);

} // namespace CcuAgMem2mem

#endif // ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_KERNEL_H
