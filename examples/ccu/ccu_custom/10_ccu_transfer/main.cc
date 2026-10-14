/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <acl/acl_rt.h>
#include <hccl/hccl_comm.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_types.h>
#include "alg_resource.h"
#include "ccu_kernel.h"
#include "exec_op.h"

using namespace ops_hccl_transfer;

#define ACLCHECK(ret)                                                                          \
    do {                                                                                       \
        if (ret != ACL_SUCCESS) {                                                              \
            printf("acl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, ret); \
            return ret;                                                                        \
        }                                                                                      \
    } while (0)

#define HCCLCHECK(ret)                                                                          \
    do {                                                                                        \
        if (ret != HCCL_SUCCESS) {                                                              \
            printf("hccl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, ret); \
            return ret;                                                                         \
        }                                                                                       \
    } while (0)

namespace {

constexpr uint32_t RANK_SIZE = 2;

struct ThreadContext {
    HcclRootInfo* rootInfo = nullptr;
    uint32_t rank = 0;
};

void PrintVector(const std::string& name, uint32_t rank, const std::vector<float>& values)
{
    std::cout << "rank " << rank << " " << name << ": [";
    for (uint32_t i = 0; i < values.size(); i++) {
        std::cout << (i == 0 ? "" : ", ") << values[i];
    }
    std::cout << "]" << std::endl;
}

bool IsEqual(const std::vector<float>& actual, const std::vector<float>& expected)
{
    if (actual.size() != expected.size()) {
        return false;
    }
    for (uint32_t i = 0; i < actual.size(); i++) {
        if (std::fabs(actual[i] - expected[i]) > 1e-6F) {
            return false;
        }
    }
    return true;
}

std::vector<float> GetInput(uint32_t rank)
{
    return {1.0F + rank * 4.0F, 2.0F + rank * 4.0F, 3.0F + rank * 4.0F, 4.0F + rank * 4.0F};
}

std::vector<float> GetExpected(uint32_t rank)
{
#if SCENARIO_NUM == 1
    return GetInput(rank);
#elif SCENARIO_NUM == 2
    return GetInput(1U - rank);
#else
    return {6.0F, 8.0F, 10.0F, 12.0F};
#endif
}

int RunSample(ThreadContext* context)
{
    const uint32_t rank = context->rank;
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(rank)));

    HcclComm comm{};
    HCCLCHECK(HcclCommInitRootInfo(RANK_SIZE, context->rootInfo, rank, &comm));

    aclrtStream stream{};
    ACLCHECK(aclrtCreateStream(&stream));

    void* inputPtr = nullptr;
    void* outputPtr = nullptr;
    const size_t bufferSize = ELEMENT_COUNT * sizeof(float);
    ACLCHECK(aclrtMalloc(&inputPtr, bufferSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&outputPtr, bufferSize, ACL_MEM_MALLOC_HUGE_ONLY));

    const std::vector<float> input = GetInput(rank);
    const std::vector<float> expected = GetExpected(rank);
    ACLCHECK(aclrtMemcpy(inputPtr, bufferSize, input.data(), bufferSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtMemset(outputPtr, bufferSize, 0, bufferSize));
    PrintVector("input", rank, input);

    uint32_t myRank = 0;
    uint32_t rankSize = 0;
    HCCLCHECK(HcclGetRankId(comm, &myRank));
    HCCLCHECK(HcclGetRankSize(comm, &rankSize));
    if (myRank != rank || rankSize != RANK_SIZE) {
        std::cerr << "rank id or rank size is unexpected" << std::endl;
        return HCCL_E_INTERNAL;
    }

    OpParam param{};
    param.stream = stream;
    param.inputPtr = inputPtr;
    param.outputPtr = outputPtr;
    param.myRank = myRank;
    param.rankSize = rankSize;

    AlgResourceCtx resourceCtx;
    HCCLCHECK(AllocAlgResource(comm, param, resourceCtx));
    HCCLCHECK(ExecOp(param, resourceCtx));
    ACLCHECK(aclrtSynchronizeStream(stream));

    std::vector<float> output(input.size(), 0.0F);
    ACLCHECK(aclrtMemcpy(output.data(), bufferSize, outputPtr, bufferSize, ACL_MEMCPY_DEVICE_TO_HOST));
    PrintVector("output", rank, output);

    const bool pass = IsEqual(output, expected);
    std::cout << "rank " << rank << " check: " << (pass ? "PASS" : "FAIL") << std::endl;
    if (!pass) {
        return HCCL_E_INTERNAL;
    }

    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(inputPtr));
    ACLCHECK(aclrtFree(outputPtr));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(static_cast<int32_t>(rank)));
    return HCCL_SUCCESS;
}

} // namespace

int main()
{
    ACLCHECK(aclInit(nullptr));

    uint32_t deviceCount = 0;
    ACLCHECK(aclrtGetDeviceCount(&deviceCount));
    if (deviceCount < RANK_SIZE) {
        std::cerr << "this sample requires at least 2 NPU devices" << std::endl;
        ACLCHECK(aclFinalize());
        return 1;
    }

    ACLCHECK(aclrtSetDevice(0));
    void* rootInfoPtr = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoPtr, sizeof(HcclRootInfo)));
    auto* rootInfo = static_cast<HcclRootInfo*>(rootInfoPtr);
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    std::vector<ThreadContext> contexts(RANK_SIZE);
    std::vector<std::thread> threads;
    for (uint32_t rank = 0; rank < RANK_SIZE; rank++) {
        contexts[rank].rootInfo = rootInfo;
        contexts[rank].rank = rank;
        threads.emplace_back(RunSample, &contexts[rank]);
    }
    for (auto& thread : threads) {
        thread.join();
    }

    ACLCHECK(aclrtFreeHost(rootInfoPtr));
    ACLCHECK(aclFinalize());
    return 0;
}
