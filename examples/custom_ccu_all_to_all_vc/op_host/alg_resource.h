/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef OPS_HCCL_A2AVC_ALG_RESOURCE_H
#define OPS_HCCL_A2AVC_ALG_RESOURCE_H

#include <vector>
#include <acl/acl_rt.h>
#include <hccl/hccl_types.h>
#include <hccl/hcomm_primitives.h>

namespace ops_hccl_a2avc {

using CcuKernelHandle = uint64_t;
constexpr uint32_t MAX_RANK_SIZE = 16;

struct AlgResourceCtx {
    std::vector<ThreadHandle> threads;
    std::vector<CcuKernelHandle> ccuKernels;
};

struct OpParam {
    aclrtStream stream;
    void *inputPtr = nullptr;
    void *outputPtr = nullptr;
    std::vector<uint64_t> sendCounts;
    std::vector<uint64_t> sendDispls;
    std::vector<uint64_t> recvCounts;
    std::vector<uint64_t> recvDispls;
    HcclDataType dataType;
    uint32_t myRank = 0;
    uint32_t rankSize = 0;
};

HcclResult AllocAlgResource(HcclComm comm, const OpParam &param, AlgResourceCtx &resCtxHost);

} // namespace ops_hccl_a2avc

#define RETURN_IF_HCCL_FAIL(call)                      \
    do {                                                \
        HcclResult ret = static_cast<HcclResult>(call); \
        if (ret != HCCL_SUCCESS) {                      \
            return ret;                                 \
        }                                               \
    } while (0)

#endif
