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
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <string>
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
    uint32_t rankId;
    uint32_t rankSize;
    uint64_t count;
};

constexpr uint64_t DEFAULT_COUNT = 6553600;
constexpr uint64_t PRINT_ELEMENT_LIMIT = 16;

static uint64_t GetCount()
{
    const char* value = std::getenv("COUNT");
    if (value == nullptr || value[0] == '\0') {
        return DEFAULT_COUNT;
    }
    try {
        uint64_t count = std::stoull(value);
        return count == 0 ? DEFAULT_COUNT : count;
    } catch (const std::exception&) {
        std::cerr << "invalid COUNT: " << value << ", fallback to " << DEFAULT_COUNT << std::endl;
        return DEFAULT_COUNT;
    }
}

static std::vector<uint32_t> ParseDevices(const char* value)
{
    std::vector<uint32_t> devices;
    if (value == nullptr || value[0] == '\0') {
        return devices;
    }

    std::stringstream stream(value);
    std::string item;
    while (std::getline(stream, item, ',')) {
        if (!item.empty()) {
            devices.push_back(static_cast<uint32_t>(std::stoul(item)));
        }
    }
    return devices;
}

static std::vector<uint32_t> GetDevices(uint32_t availableDeviceCount)
{
    std::vector<uint32_t> devices = ParseDevices(std::getenv("DEVICES"));
    if (devices.empty()) {
        devices.resize(availableDeviceCount);
        for (uint32_t i = 0; i < availableDeviceCount; ++i) {
            devices[i] = i;
        }
        return devices;
    }

    const std::set<uint32_t> uniqueDevices(devices.begin(), devices.end());
    if (uniqueDevices.size() != devices.size()) {
        throw std::invalid_argument("DEVICES contains duplicate device IDs");
    }
    for (uint32_t device : devices) {
        if (device >= availableDeviceCount) {
            throw std::invalid_argument("DEVICES contains unavailable device ID: " + std::to_string(device));
        }
    }
    return devices;
}

static void PrintFloatPreview(const std::string& label, uint32_t rankId, const float* data, uint64_t count)
{
    const uint64_t previewCount = std::min(count, PRINT_ELEMENT_LIMIT);
    std::cout << "rankId: " << rankId << ", " << label << " count: " << count << ", preview: [";
    for (uint64_t i = 0; i < previewCount; ++i) {
        std::cout << " " << data[i];
    }
    if (previewCount < count) {
        std::cout << " ...";
    }
    std::cout << " ]" << std::endl;
}

static HcclResult HcclBroadcastCustom(
    void* buffer, uint64_t count, HcclDataType dataType, uint32_t rootRank, HcclComm comm, aclrtStream stream)
{
    if (buffer == nullptr || comm == nullptr || stream == nullptr) {
        return HCCL_E_PTR;
    }
    if (dataType != HCCL_DATA_TYPE_FP32) {
        return HCCL_E_NOT_SUPPORT;
    }

    OpParam param;
    RETURN_IF_HCCL_FAIL(HcclGetRankId(comm, &param.myRank));
    RETURN_IF_HCCL_FAIL(HcclGetRankSize(comm, &param.rankSize));
    if (param.rankSize == 0 || param.rankSize > MAX_RANK_SIZE || rootRank >= param.rankSize) {
        return HCCL_E_NOT_SUPPORT;
    }
    param.stream = stream;
    param.buffer = buffer;
    param.count = count;
    param.dataType = dataType;
    param.rootRank = rootRank;

    AlgResourceCtx resources;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param, resources));
    return ExecOp(param, resources);
}

static int Sample(void* arg)
{
    auto* ctx = static_cast<ThreadContext*>(arg);
    constexpr uint32_t rootRank = 0;
    const uint64_t count = ctx->count;
    if (count > std::numeric_limits<size_t>::max() / sizeof(float)) {
        std::cerr << "rankId: " << ctx->rankId << ", COUNT is too large: " << count << std::endl;
        return 1;
    }
    const size_t dataSize = count * sizeof(float);

    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(ctx->device)));
    HcclComm comm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->rankSize, ctx->rootInfo, ctx->rankId, &comm));
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    void* buffer = nullptr;
    ACLCHECK(aclrtMalloc(&buffer, dataSize, ACL_MEM_MALLOC_HUGE_ONLY));
    void* hostBuffer = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuffer, dataSize));
    auto* input = static_cast<float*>(hostBuffer);
    for (uint64_t i = 0; i < count; i++) {
        input[i] = ctx->rankId == rootRank ? static_cast<float>(i) : -1.0F;
    }
    PrintFloatPreview("input", ctx->rankId, input, count);
    ACLCHECK(aclrtMemcpy(buffer, dataSize, hostBuffer, dataSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuffer));

    HCCLCHECK(HcclBroadcastCustom(buffer, count, HCCL_DATA_TYPE_FP32, rootRank, comm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    std::this_thread::sleep_for(std::chrono::seconds(ctx->rankId));
    void* resultHost = nullptr;
    ACLCHECK(aclrtMallocHost(&resultHost, dataSize));
    ACLCHECK(aclrtMemcpy(resultHost, dataSize, buffer, dataSize, ACL_MEMCPY_DEVICE_TO_HOST));
    auto* output = static_cast<float*>(resultHost);
    bool passed = true;
    for (uint64_t i = 0; i < count; i++) {
        passed = passed && (output[i] == static_cast<float>(i));
    }
    PrintFloatPreview("output", ctx->rankId, output, count);
    std::cout << "rankId: " << ctx->rankId << (passed ? " PASS" : " FAIL") << std::endl;

    ACLCHECK(aclrtFreeHost(resultHost));
    HcclResult destroyRet = HcclCommDestroy(comm);
    if (destroyRet != HCCL_SUCCESS) {
        std::cerr << "rankId: " << ctx->rankId << ", HcclCommDestroy warning, retcode: " << destroyRet << std::endl;
    }
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

    std::vector<uint32_t> devices;
    try {
        devices = GetDevices(deviceCount);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        ACLCHECK(aclFinalize());
        return 1;
    }
    if (devices.empty()) {
        std::cerr << "No NPU device available" << std::endl;
        ACLCHECK(aclFinalize());
        return 1;
    }
    uint32_t rankSize = static_cast<uint32_t>(devices.size());
    uint64_t count = GetCount();
    std::cout << "Use devices: [";
    for (uint32_t device : devices) {
        std::cout << " " << device;
    }
    std::cout << " ], rankSize: " << rankSize << std::endl;
    std::cout << "Broadcast count: " << count << std::endl;

    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(devices[0])));
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    auto* rootInfo = static_cast<HcclRootInfo*>(rootInfoBuf);
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    std::vector<std::thread> threads(rankSize);
    std::vector<ThreadContext> contexts(rankSize);
    std::vector<int> results(rankSize, 1);
    for (uint32_t rank = 0; rank < rankSize; rank++) {
        contexts[rank] = {rootInfo, devices[rank], rank, rankSize, count};
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
