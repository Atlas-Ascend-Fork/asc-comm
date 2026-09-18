/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_KERNEL_REDUCE_SCATTER_MESH_1D_MEM2MEM_H
#define HCCL_CCU_KERNEL_REDUCE_SCATTER_MESH_1D_MEM2MEM_H

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <ccu/hcomm/ccu_api_types.h>
#include <ccu/hcomm/ccu_variable.hpp>
#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_rs {

#define RETURN_IF_CCU_FAIL(call)  \
    do {                          \
        CcuResult ret = (call);   \
        if (ret != CCU_SUCCESS) { \
            return ret;           \
        }                         \
    } while (0)

constexpr uint64_t ccu_ms_interleave = 8;
constexpr uint64_t ccu_ms_size = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t channelCount;
};

struct ccu_kernel_info {
    char kernel_func_name[64];
    void* kernel_func;
    void* kernel_arg;

private:
    std::shared_ptr<CcuKernelArgBase> kernelArgSmartPtr;

public:
    template <typename t>
    void setKernelArg(std::shared_ptr<t> arg)
    {
        kernelArgSmartPtr = std::static_pointer_cast<CcuKernelArgBase>(arg);
        kernel_arg = static_cast<void*>(arg.get());
    }
};

struct loop_group_config {
    uint32_t ms_interleave;
    uint32_t loop_count;
    uint64_t mem_slice;
};

struct LoopGroupResource {
    ccu::array<ccu::event> completed_event{0};
    ccu::array<ccu::ccu_buffer> ccuBuf{0};
    uint32_t eventCount;
    uint32_t bufCount;
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

struct CcuKernelArgReduceScatterMesh1DMem2Mem : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
};

struct ReduceScatterMesh1DMem2MemContext {
    const CcuKernelArgReduceScatterMesh1DMem2Mem* arg;

    ccu::variable input;
    ccu::variable inputToken;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> outputToken;
    ccu::variable rankSliceSize;
    ccu::variable sliceSize;
    ccu::event event;

    GroupOpSizeVars goSize;
    loop_group_config moConfig;
    LoopGroupResource moRes;
    bool resourceAllocated = false;

    std::map<std::string, CcuLoopEntity> loopMap;

    void CreateLoopEntity(std::string loopStr) { loopMap.emplace(loopStr, CcuLoopEntity()); }

    bool IsLoopEntityRegistered(std::string loopStr) { return loopMap.count(loopStr) != 0; }
};

CcuResult CcuReduceScatterMesh1DMem2MemKernel(ccu_kernel_arg arg);

} // namespace ops_hccl_rs

#endif // HCCL_CCU_KERNEL_REDUCE_SCATTER_MESH_1D_MEM2MEM_H
