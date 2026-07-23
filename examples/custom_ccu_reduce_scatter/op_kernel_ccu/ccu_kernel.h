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
#include <ccu/ccu_types.h>
#include <ccu/ccu_variable.hpp>
#include <ccu/ccu_event.hpp>
#include <ccu/ccu_primitives.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_rs {

#define RETURN_IF_CCU_FAIL(call)      \
    do {                               \
        CcuResult ret = (call);        \
        if (ret != CCU_SUCCESS) {      \
            return ret;                \
        }                              \
    } while (0)

constexpr uint64_t CCU_MS_INTERLEAVE = 8;
constexpr uint64_t CCU_MS_SIZE = 4096;
constexpr uint32_t CCU_LOCAL_COPY_MS_PER_LOOP = 8;
constexpr uint32_t CCU_MS_LOCAL_COPY_LOOP_COUNT = 8;
constexpr uint64_t CCU_MAX_RANK_SIZE = 16;

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t      channelCount;
};

struct CcuKernelInfo {
    char kernelFuncName[64];
    void* kernelFunc;
    void *kernelArg;

private:
    std::shared_ptr<CcuKernelArgBase> kernelArgSmartPtr;

public:
    template<typename T>
    void setKernelArg(std::shared_ptr<T> arg) {
        kernelArgSmartPtr = std::static_pointer_cast<CcuKernelArgBase>(arg);
        kernelArg = static_cast<void*>(arg.get());
    }
};

struct LoopGroupConfig {
    uint32_t msInterleave;
    uint32_t loopCount;
    uint64_t memSlice;
};

struct LoopGroupResource {
    ccu::Array<ccu::Event>     completedEvent{0};
    ccu::Array<ccu::CcuBuffer> ccuBuf{0};
    uint32_t  eventCount;
    uint32_t  bufCount;
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
    ccu::Variable              loopParam[2];
};

struct CcuKernelArgReduceScatterMesh1DMem2Mem : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
};

struct ReduceScatterMesh1DMem2MemContext {
    const CcuKernelArgReduceScatterMesh1DMem2Mem* arg;

    ccu::Variable input;
    ccu::Variable inputToken;
    std::vector<ccu::Variable> output;
    std::vector<ccu::Variable> outputToken;
    ccu::Variable rankSliceSize;
    ccu::Variable sliceSize;
    ccu::Event event;

    GroupOpSizeVars goSize;
    LoopGroupConfig   moConfig;
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

CcuResult CcuReduceScatterMesh1DMem2MemKernel(CcuKernelArg arg);

} // namespace ops_hccl_rs

#endif // HCCL_CCU_KERNEL_REDUCE_SCATTER_MESH_1D_MEM2MEM_H


