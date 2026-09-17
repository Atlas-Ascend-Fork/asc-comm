/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms of conditions of
 * CANN Open Software License Agreement Version 2.0 ("License").
 * Please refer to the License for details.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef OPS_HCCL_CCU_TRANSFER_KERNEL_H
#define OPS_HCCL_CCU_TRANSFER_KERNEL_H

#include <memory>

#include <ccu/hcomm/ccu_api_types.h>
#include <ccu/hcomm/ccu_buffer.hpp>
#include <ccu/hcomm/ccu_event.hpp>
#include <ccu/hcomm/ccu_primitives.hpp>
#include <ccu/hcomm/ccu_variable.hpp>
#include <hccl/hcomm_primitives.h>

namespace ccu = ::AscendC::ccu;

namespace ops_hccl_transfer {

constexpr uint64_t ELEMENT_COUNT = 4;

#define RETURN_IF_CCU_FAIL(call)  \
    do {                          \
        CcuResult ret = (call);   \
        if (ret != CCU_SUCCESS) { \
            return ret;           \
        }                         \
    } while (0)

struct CcuTransferKernelArg {
    ChannelHandle channels[2];
    uint32_t channelCount;
    uint32_t rankId;
    uint32_t rankSize;
};

struct ccu_kernel_info {
    char kernel_func_name[64];
    void* kernel_func;
    void* kernel_arg;

private:
    std::shared_ptr<CcuTransferKernelArg> kernelArgSmartPtr;

public:
    template <typename T>
    void setKernelArg(std::shared_ptr<T> arg)
    {
        kernelArgSmartPtr = std::static_pointer_cast<CcuTransferKernelArg>(arg);
        kernel_arg = static_cast<void*>(arg.get());
    }
};

CcuResult CcuTransferKernel(ccu_kernel_arg arg);

} // namespace ops_hccl_transfer

#endif // OPS_HCCL_CCU_TRANSFER_KERNEL_H
