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

using namespace ops_hccl_a2avc;

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

static HcclResult HcclAllToAllVCCustom(
    void* sendBuf, const uint64_t* sendCountMatrix, HcclDataType sendType, void* recvBuf, HcclDataType recvType,
    HcclComm comm, aclrtStream stream)
{
    if (sendBuf == nullptr || recvBuf == nullptr || sendCountMatrix == nullptr || comm == nullptr ||
        stream == nullptr) {
        return HCCL_E_PTR;
    }
    if (sendType != HCCL_DATA_TYPE_FP32 || recvType != HCCL_DATA_TYPE_FP32) {
        return HCCL_E_NOT_SUPPORT;
    }

    OpParam param_;
    RETURN_IF_HCCL_FAIL(HcclGetRankId(comm, &param_.myRank));
    RETURN_IF_HCCL_FAIL(HcclGetRankSize(comm, &param_.rankSize));
    if (param_.rankSize == 0 || param_.rankSize > MAX_RANK_SIZE) {
        return HCCL_E_NOT_SUPPORT;
    }
    param_.stream = stream;
    param_.inputPtr = sendBuf;
    param_.outputPtr = recvBuf;
    param_.sendCounts.resize(param_.rankSize);
    param_.sendDispls.resize(param_.rankSize);
    param_.recvCounts.resize(param_.rankSize);
    param_.recvDispls.resize(param_.rankSize);
    uint64_t sendOffset = 0;
    uint64_t recvOffset = 0;
    for (uint32_t peer = 0; peer < param_.rankSize; peer++) {
        param_.sendCounts[peer] = sendCountMatrix[param_.myRank * param_.rankSize + peer];
        param_.recvCounts[peer] = sendCountMatrix[peer * param_.rankSize + param_.myRank];
        param_.sendDispls[peer] = sendOffset;
        param_.recvDispls[peer] = recvOffset;
        sendOffset += param_.sendCounts[peer];
        recvOffset += param_.recvCounts[peer];
    }
    param_.data_type_ = sendType;

    AlgResourceCtx resources;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param_, resources));
    return ExecOp(param_, resources);
}

static int Sample(void* arg)
{
    auto* ctx = static_cast<ThreadContext*>(arg);
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(ctx->device)));

    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->devCount, ctx->rootInfo, ctx->device, &comm));
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    std::vector<uint64_t> countMatrix(ctx->devCount * ctx->devCount);
    for (uint32_t src_ = 0; src_ < ctx->devCount; src_++) {
        for (uint32_t dst_ = 0; dst_ < ctx->devCount; dst_++) {
            countMatrix[src_ * ctx->devCount + dst_] = src_ + dst_ + 1;
        }
    }
    std::vector<uint64_t> sendCounts(ctx->devCount);
    std::vector<uint64_t> recvCounts(ctx->devCount);
    std::vector<uint64_t> sendDispls(ctx->devCount);
    std::vector<uint64_t> recvDispls(ctx->devCount);
    uint64_t sendTotal = 0;
    uint64_t recvTotal = 0;
    for (uint32_t peer = 0; peer < ctx->devCount; peer++) {
        sendCounts[peer] = countMatrix[ctx->device * ctx->devCount + peer];
        recvCounts[peer] = countMatrix[peer * ctx->devCount + ctx->device];
        sendDispls[peer] = sendTotal;
        recvDispls[peer] = recvTotal;
        sendTotal += sendCounts[peer];
        recvTotal += recvCounts[peer];
    }

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    const size_t sendSize = sendTotal * sizeof(float);
    const size_t recvSize = recvTotal * sizeof(float);
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));

    void* hostBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuf, sendSize));
    auto* input = static_cast<float*>(hostBuf);
    for (uint32_t dst_ = 0; dst_ < ctx->devCount; dst_++) {
        for (uint64_t i = 0; i < sendCounts[dst_]; i++) {
            input[sendDispls[dst_] + i] = static_cast<float>(ctx->device * 1000 + dst_ * 100 + i);
        }
    }
    ACLCHECK(aclrtMemcpy(sendBuf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuf));

    HCCLCHECK(HcclAllToAllVCCustom(
        sendBuf, countMatrix.data(), HCCL_DATA_TYPE_FP32, recvBuf, HCCL_DATA_TYPE_FP32, comm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    std::this_thread::sleep_for(std::chrono::seconds(ctx->device));
    void* resultHost = nullptr;
    ACLCHECK(aclrtMallocHost(&resultHost, recvSize));
    ACLCHECK(aclrtMemcpy(resultHost, recvSize, recvBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
    auto* output = static_cast<float*>(resultHost);
    bool passed = true;
    std::cout << "rankId: " << ctx->device << ", output: [";
    for (uint32_t src_ = 0; src_ < ctx->devCount; src_++) {
        for (uint64_t i = 0; i < recvCounts[src_]; i++) {
            const float expected_ = static_cast<float>(src_ * 1000 + ctx->device * 100 + i);
            const float actual = output[recvDispls[src_] + i];
            std::cout << " " << actual;
            passed = passed && (actual == expected_);
        }
    }
    std::cout << " ]" << std::endl;
    std::cout << "rankId: " << ctx->device << (passed ? " PASS" : " FAIL") << std::endl;

    ACLCHECK(aclrtFreeHost(resultHost));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
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
