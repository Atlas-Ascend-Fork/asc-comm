/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_KERNEL_ALL_REDUCE_MESH_1D_MEM2MEM_H
#define HCCL_CCU_KERNEL_ALL_REDUCE_MESH_1D_MEM2MEM_H

#include <cstdint>
#include <memory>
#include <vector>
#include <ccu/hcomm/ccu_api_types.h>
#include <ccu/hcomm/ccu_variable.hpp>
#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_ar {

#define RETURN_IF_CCU_FAIL(call)  \
    do {                          \
        CcuResult ret = (call);   \
        if (ret != CCU_SUCCESS) { \
            return ret;           \
        }                         \
    } while (0)

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

struct CcuKernelArgAllReduceMesh1DMem2Mem : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
};

struct AllReduceMesh1DMem2MemContext {
    const CcuKernelArgAllReduceMesh1DMem2Mem* arg;

    ccu::variable input;
    std::vector<ccu::variable> output;
    std::vector<ccu::variable> token;
    ccu::variable sliceSize;
    ccu::event event;
};

CcuResult CcuAllReduceMesh1DMem2MemKernel(ccu_kernel_arg arg);

} // namespace ops_hccl_ar

#endif // HCCL_CCU_KERNEL_ALL_REDUCE_MESH_1D_MEM2MEM_H
