/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @brief 定义 Host 侧 CCU 资源上下文及资源初始化接口。
 *
 * 资源上下文保存 CCL Buffer、CCU Thread、Channel 和已注册 Kernel 的句柄。
 * alg_resource.cc 负责申请这些资源，并将其保存到 HCCL EngineCtx 或从中恢复。
 */
#ifndef ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_ALG_RESOURCE_H
#define ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_ALG_RESOURCE_H

#include <cstdint>
#include <vector>

#include <acl/acl_rt.h>
#include <ccu/hcomm/ccu_api_types.h>
#include <hcomm/hcomm_primitives.h>
#include <hcomm/hcomm_types.h>

namespace CcuAgMem2mem {

using ccu_kernel_handle = uint64_t;

struct CommBuffer {
    void* addr_{nullptr};
    uint64_t size{0};
};

struct AlgResourceCtx {
    CommBuffer cclMem;
    uint32_t notifyNumOnMainThread{0};
    std::vector<ThreadHandle> threads;
    std::vector<ChannelHandle> kernelChannels;
    std::vector<uint32_t> ccuKernelNum;
    std::vector<ccu_kernel_handle> ccuKernels;

    std::vector<char> Serialize() const;
    void Deserialize(std::vector<char>& data);
};

HcclResult ConvertCcuToHccl(CcuResult result);

HcclResult InitAlgResourceCtx(
    HcclComm comm, const char* tag, uint32_t myRank, uint32_t rankSize, aclrtStream stream, AlgResourceCtx& resCtx);

} // namespace CcuAgMem2mem

#define RETURN_IF_HCCL_FAIL(call)                                \
    do {                                                         \
        const HcclResult result = static_cast<HcclResult>(call); \
        if (result != HCCL_SUCCESS) {                            \
            return result;                                       \
        }                                                        \
    } while (0)

#endif // ASC_COMM_EXAMPLE_CCU_ALLGATHER_MESH1D_MEM2MEM_ALG_RESOURCE_H
