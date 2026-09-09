/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

#include <acl/acl_rt.h>
#include <hccl/hccl_comm.h>
#include <hccl/hccl_types.h>
#include "alg_resource.h"
#include "exec_op.h"

using namespace ops_hccl_bcast;

#define ACLCHECK(call)                                                    \
    do {                                                                  \
        auto ret = (call);                                                \
        if (ret != ACL_SUCCESS) {                                         \
            printf("ACL error %s:%d, ret=%d\n", __FILE__, __LINE__, ret); \
            return ret;                                                   \
        }                                                                 \
    } while (0)
#define HCCLCHECK(call)                                                    \
    do {                                                                   \
        auto ret = (call);                                                 \
        if (ret != HCCL_SUCCESS) {                                         \
            printf("HCCL error %s:%d, ret=%d\n", __FILE__, __LINE__, ret); \
            return ret;                                                    \
        }                                                                  \
    } while (0)

struct ThreadContext {
    HcclRootInfo* rootInfo;
    uint32_t device;
    uint32_t devCount;
};

static HcclResult HcclBroadcastCustom(
    void* buffer, uint64_t count_, HcclDataType data_type_, uint32_t rootRank, HcclComm comm, aclrtStream stream)
{
    if (buffer == nullptr || comm == nullptr || stream == nullptr) {
        return HCCL_E_PTR;
    }
    if (data_type_ != HCCL_DATA_TYPE_FP32) {
        return HCCL_E_NOT_SUPPORT;
    }

    OpParam param_;
    RETURN_IF_HCCL_FAIL(HcclGetRankId(comm, &param_.myRank));
    RETURN_IF_HCCL_FAIL(HcclGetRankSize(comm, &param_.rankSize));
    if (param_.rankSize == 0 || param_.rankSize > MAX_RANK_SIZE || rootRank >= param_.rankSize) {
        return HCCL_E_NOT_SUPPORT;
    }
    param_.stream = stream;
    param_.buffer = buffer;
    param_.count_ = count_;
    param_.data_type_ = data_type_;
    param_.rootRank = rootRank;

    AlgResourceCtx resources;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param_, resources));
    return ExecOp(param_, resources);
}

static int Sample(void* arg)
{
    auto* ctx = static_cast<ThreadContext*>(arg);
    constexpr uint32_t rootRank = 0;
    const uint64_t count_ = ctx->devCount;
    const size_t dataSize = count_ * sizeof(float);

    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(ctx->device)));
    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->devCount, ctx->rootInfo, ctx->device, &comm));
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    void* buffer = nullptr;
    ACLCHECK(aclrtMalloc(&buffer, dataSize, ACL_MEM_MALLOC_HUGE_ONLY));
    void* hostBuffer = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuffer, dataSize));
    auto* input = static_cast<float*>(hostBuffer);
    for (uint64_t i = 0; i < count_; i++) {
        input[i] = ctx->device == rootRank ? static_cast<float>(i) : -1.0F;
    }
    ACLCHECK(aclrtMemcpy(buffer, dataSize, hostBuffer, dataSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuffer));

    HCCLCHECK(HcclBroadcastCustom(buffer, count_, HCCL_DATA_TYPE_FP32, rootRank, comm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    std::this_thread::sleep_for(std::chrono::seconds(ctx->device));
    void* resultHost = nullptr;
    ACLCHECK(aclrtMallocHost(&resultHost, dataSize));
    ACLCHECK(aclrtMemcpy(resultHost, dataSize, buffer, dataSize, ACL_MEMCPY_DEVICE_TO_HOST));
    auto* output = static_cast<float*>(resultHost);
    bool passed = true;
    std::cout << "rankId: " << ctx->device << ", output: [";
    for (uint64_t i = 0; i < count_; i++) {
        std::cout << " " << output[i];
        passed = passed && (output[i] == static_cast<float>(i));
    }
    std::cout << " ]" << std::endl;
    std::cout << "rankId: " << ctx->device << (passed ? " PASS" : " FAIL") << std::endl;

    ACLCHECK(aclrtFreeHost(resultHost));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(buffer));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(ctx->device));
    return passed ? 0 : 1;
}

int main()
{
    ACLCHECK(aclInit(nullptr));
    uint32_t deviceCount = 0;
    ACLCHECK(aclrtGetDeviceCount(&deviceCount));
    std::cout << "Found " << deviceCount << " NPU device(s) available" << std::endl;

    ACLCHECK(aclrtSetDevice(0));
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    auto* rootInfo = static_cast<HcclRootInfo*>(rootInfoBuf);
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    std::vector<std::thread> threads(deviceCount);
    std::vector<ThreadContext> contexts(deviceCount);
    std::vector<int> results(deviceCount, 1);
    for (uint32_t rank = 0; rank < deviceCount; rank++) {
        contexts[rank] = {rootInfo, rank, deviceCount};
        threads[rank] = std::thread([&contexts, &results, rank]() { results[rank] = Sample(&contexts[rank]); });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    ACLCHECK(aclrtFreeHost(rootInfoBuf));
    ACLCHECK(aclFinalize());
    for (int result : results) {
        if (result != 0) {
            return 1;
        }
    }
    return 0;
}
