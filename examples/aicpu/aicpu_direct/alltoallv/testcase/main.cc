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
#include <hccl/hccl_types.h>
#include <aicpu_ts_custom_alltoallv.h>

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

struct ThreadContext {
    HcclRootInfo* rootInfo;
    uint32_t device;
    uint32_t devCount;
};

int Sample(void* arg)
{
    auto* ctx = static_cast<ThreadContext*>(arg);
    const uint32_t device = ctx->device;
    const uint32_t rankSize = ctx->devCount;
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    HcclComm hcclComm;
    HCCLCHECK(HcclCommInitRootInfo(rankSize, ctx->rootInfo, device, &hcclComm));
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    std::vector<uint64_t> sendCounts(rankSize);
    std::vector<uint64_t> recvCounts(rankSize);
    std::vector<uint64_t> sendDispls(rankSize);
    std::vector<uint64_t> recvDispls(rankSize);
    uint64_t sendTotal = 0;
    uint64_t recvTotal = 0;
    for (uint32_t peer = 0; peer < rankSize; peer++) {
        sendCounts[peer] = device + peer + 1;
        recvCounts[peer] = peer + device + 1;
        sendDispls[peer] = sendTotal;
        recvDispls[peer] = recvTotal;
        sendTotal += sendCounts[peer];
        recvTotal += recvCounts[peer];
    }

    const size_t sendSize = sendTotal * sizeof(float);
    const size_t recvSize = recvTotal * sizeof(float);
    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));

    void* hostBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuf, sendSize));
    auto* input = static_cast<float*>(hostBuf);
    for (uint32_t dst = 0; dst < rankSize; dst++) {
        for (uint64_t i = 0; i < sendCounts[dst]; i++) {
            input[sendDispls[dst] + i] = static_cast<float>(device * 1000 + dst * 100 + i);
        }
    }
    std::cout << "rankId: " << device << ", input: [";
    for (uint64_t i = 0; i < sendTotal; i++) {
        std::cout << " " << input[i];
    }
    std::cout << " ]" << std::endl;
    ACLCHECK(aclrtMemcpy(sendBuf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuf));

    HCCLCHECK(AlltoAllVCustom(
        sendBuf, sendCounts.data(), sendDispls.data(), HCCL_DATA_TYPE_FP32, recvBuf, recvCounts.data(),
        recvDispls.data(), HCCL_DATA_TYPE_FP32, hcclComm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    // 第二次调用复用同一通信域的engine context。
    HCCLCHECK(AlltoAllVCustom(
        sendBuf, sendCounts.data(), sendDispls.data(), HCCL_DATA_TYPE_FP32, recvBuf, recvCounts.data(),
        recvDispls.data(), HCCL_DATA_TYPE_FP32, hcclComm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    std::this_thread::sleep_for(std::chrono::seconds(device));
    void* resultHost = nullptr;
    ACLCHECK(aclrtMallocHost(&resultHost, recvSize));
    ACLCHECK(aclrtMemcpy(resultHost, recvSize, recvBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
    auto* output = static_cast<float*>(resultHost);
    bool passed = true;
    std::cout << "rankId: " << device << ", output: [";
    for (uint32_t src = 0; src < rankSize; src++) {
        for (uint64_t i = 0; i < recvCounts[src]; i++) {
            const float actual = output[recvDispls[src] + i];
            const float expected = static_cast<float>(src * 1000 + device * 100 + i);
            std::cout << " " << actual;
            passed = passed && (actual == expected);
        }
    }
    std::cout << " ]" << std::endl;
    if (!passed) {
        std::cerr << "rankId: " << device << ", result verification failed" << std::endl;
    }

    ACLCHECK(aclrtFreeHost(resultHost));
    HCCLCHECK(HcclCommDestroy(hcclComm));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(device));
    return passed ? 0 : 1;
}

int main()
{
    ACLCHECK(aclInit(nullptr));
    uint32_t devCount = 0;
    ACLCHECK(aclrtGetDeviceCount(&devCount));
    std::cout << "Found " << devCount << " NPU device(s) available" << std::endl;

    ACLCHECK(aclrtSetDevice(0));
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    auto* rootInfo = static_cast<HcclRootInfo*>(rootInfoBuf);
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    std::vector<std::thread> threads(devCount);
    std::vector<ThreadContext> contexts(devCount);
    std::vector<int> results(devCount, 1);
    for (uint32_t rank = 0; rank < devCount; rank++) {
        contexts[rank] = {rootInfo, rank, devCount};
        threads[rank] =
            std::thread([&contexts, &results, rank]() { results[rank] = Sample(static_cast<void*>(&contexts[rank])); });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    ACLCHECK(aclrtFreeHost(rootInfoBuf));
    ACLCHECK(aclFinalize());
    for (uint32_t rank = 0; rank < devCount; rank++) {
        std::cout << "rankId: " << rank << ", test result: " << results[rank] << std::endl;
        if (results[rank] != 0) {
            return results[rank];
        }
    }
    std::cout << "AlltoAllVCustom test completed successfully" << std::endl;
    return 0;
}
