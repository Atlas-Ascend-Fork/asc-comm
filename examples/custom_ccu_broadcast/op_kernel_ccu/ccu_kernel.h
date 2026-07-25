/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_CCU_KERNEL_BROADCAST_MESH_1D_H
#define HCCL_CCU_KERNEL_BROADCAST_MESH_1D_H

#include <cstdint>
#include <memory>
#include <vector>
#include <ccu/ccu_types.h>
#include <ccu/ccu_variable.hpp>
#include <ccu/ccu_event.hpp>
#include <ccu/ccu_primitives.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_bcast {

#define RETURN_IF_CCU_FAIL(call)  \
    do {                           \
        CcuResult ret = (call);    \
        if (ret != CCU_SUCCESS) {  \
            return ret;            \
        }                          \
    } while (0)

constexpr uint64_t CCU_MAX_RANK_SIZE = 16;

struct CcuKernelArgBase {
    ChannelHandle channels[CCU_MAX_RANK_SIZE];
    uint32_t channelCount;
};

struct CcuKernelInfo {
    char kernelFuncName[64];
    void *kernelFunc;
    void *kernelArg;

private:
    std::shared_ptr<CcuKernelArgBase> kernelArgSmartPtr;

public:
    template<typename T>
    void setKernelArg(std::shared_ptr<T> arg)
    {
        kernelArgSmartPtr = std::static_pointer_cast<CcuKernelArgBase>(arg);
        kernelArg = static_cast<void *>(arg.get());
    }
};

struct CcuKernelArgBroadcastMesh1D : public CcuKernelArgBase {
    uint64_t rankSize;
    uint32_t rankId;
    uint32_t rootId;
};

struct BroadcastMesh1DContext {
    const CcuKernelArgBroadcastMesh1D *arg;
    ccu::Variable localBuffer;
    ccu::Variable localToken;
    ccu::Variable dataSize;
    std::vector<ccu::Variable> remoteBuffer;
    std::vector<ccu::Variable> remoteToken;
    ccu::Event event;
};

CcuResult CcuBroadcastMesh1DKernel(CcuKernelArg arg);

} // namespace ops_hccl_bcast

#endif
