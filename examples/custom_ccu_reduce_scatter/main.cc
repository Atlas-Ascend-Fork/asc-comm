/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
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
#include <hccl/hccl_res.h>
#include <hccl/hccl_types.h>
#include "alg_resource.h"
#include "exec_op.h"

using namespace ops_hccl_rs;

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
    uint32_t rankId;
    uint32_t rankSize;
    uint64_t recvCount;
};

constexpr uint64_t DEFAULT_RECV_COUNT = 6553600;
constexpr uint64_t PRINT_ELEMENT_LIMIT = 16;

static uint64_t GetRecvCount()
{
    const char* value = std::getenv("COUNT");
    if (value == nullptr || value[0] == '\0') {
        return DEFAULT_RECV_COUNT;
    }
    try {
        uint64_t count = std::stoull(value);
        return count == 0 ? DEFAULT_RECV_COUNT : count;
    } catch (const std::exception&) {
        std::cerr << "invalid COUNT: " << value << ", fallback to " << DEFAULT_RECV_COUNT << std::endl;
        return DEFAULT_RECV_COUNT;
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

static HcclResult HcclReduceScatterCustom(
    void* sendBuf, void* recvBuf, uint64_t recvCount, HcclDataType dataType, HcclReduceOp reduceOp, HcclComm comm,
    aclrtStream stream)
{
    if (sendBuf == nullptr || recvBuf == nullptr || comm == nullptr || stream == nullptr) {
        return HCCL_E_PTR;
    }
    if (dataType != HCCL_DATA_TYPE_FP32 || reduceOp != HCCL_REDUCE_SUM) {
        return HCCL_E_NOT_SUPPORT;
    }

    OpParam param;
    RETURN_IF_HCCL_FAIL(HcclGetRankId(comm, &param.myRank));
    RETURN_IF_HCCL_FAIL(HcclGetRankSize(comm, &param.rankSize));
    if (param.rankSize == 0 || param.rankSize > MAX_RANK_SIZE) {
        return HCCL_E_NOT_SUPPORT;
    }

    param.stream = stream;
    param.inputPtr = sendBuf;
    param.outputPtr = recvBuf;
    param.recvCount = recvCount;
    param.dataType = dataType;
    param.reduceOp = reduceOp;

    AlgResourceCtx resCtxHost;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param, resCtxHost));
    RETURN_IF_HCCL_FAIL(ExecOp(param, resCtxHost));

    return HCCL_SUCCESS;
}

int Sample(void* arg)
{
    ThreadContext* ctx = (ThreadContext*)arg;
    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    uint32_t device = ctx->device;
    uint32_t rankId = ctx->rankId;
    const uint64_t recvCount = ctx->recvCount;
    if (recvCount > std::numeric_limits<uint64_t>::max() / ctx->rankSize ||
        recvCount * ctx->rankSize > std::numeric_limits<size_t>::max() / sizeof(float)) {
        std::cerr << "rankId: " << rankId << ", COUNT is too large: " << recvCount << std::endl;
        return 1;
    }
    uint64_t sendCount = recvCount * ctx->rankSize;
    size_t sendSize = sendCount * sizeof(float);
    size_t recvSize = recvCount * sizeof(float);
    // 设置当前线程使用的 device
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    // 初始化 HCCL 通信域
    HcclComm hcclComm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->rankSize, ctx->rootInfo, rankId, &hcclComm));

    // 创建任务流
    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    // 申请 Device 侧输入和输出内存
    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));

    // 申请 Host 内存保存输入数据，并初始化为 [0, 1, ..., sendCount - 1]
    void* hostBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuf, sendSize));
    float* tmpHostBuff = static_cast<float*>(hostBuf);
    for (uint64_t i = 0; i < sendCount; ++i) {
        tmpHostBuff[i] = static_cast<float>(rankId * 100 + i);
    }
    PrintFloatPreview("input", rankId, tmpHostBuff, sendCount);

    // 将 Host 输入数据拷贝到 Device
    ACLCHECK(aclrtMemcpy(sendBuf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
    // 释放 Host 侧输入内存
    ACLCHECK(aclrtFreeHost(hostBuf));

    // 输入按 rank 分段，rank r 接收所有 rank 输入中第 r 段的逐元素和。
    HCCLCHECK(
        HcclReduceScatterCustom(sendBuf, recvBuf, recvCount, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, hcclComm, stream));
    // 等待 stream 中任务执行完成
    ACLCHECK(aclrtSynchronizeStream(stream));

    // 将 Device 侧结果拷贝回 Host，并打印结果
    std::this_thread::sleep_for(std::chrono::seconds(rankId));
    void* resultBuff;
    ACLCHECK(aclrtMallocHost(&resultBuff, recvSize));
    ACLCHECK(aclrtMemcpy(resultBuff, recvSize, recvBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
    float* tmpResBuff = static_cast<float*>(resultBuff);
    bool passed = true;
    for (uint64_t i = 0; i < recvCount; ++i) {
        float expected = static_cast<float>(
            100 * ctx->rankSize * (ctx->rankSize - 1) / 2 + ctx->rankSize * (rankId * recvCount + i));
        passed = passed && (tmpResBuff[i] == expected);
    }
    PrintFloatPreview("output", rankId, tmpResBuff, recvCount);
    std::cout << "rankId: " << rankId << (passed ? " PASS" : " FAIL") << std::endl;
    ACLCHECK(aclrtFreeHost(resultBuff));

    // 释放资源
    HcclResult destroyRet = HcclCommDestroy(hcclComm); // 销毁通信域
    if (destroyRet != HCCL_SUCCESS) {
        std::cerr << "rankId: " << rankId << ", HcclCommDestroy warning, retcode: " << destroyRet << std::endl;
    }
    if (sendBuf) {
        ACLCHECK(aclrtFree(sendBuf)); // 释放 Device 侧输入内存
    }
    if (recvBuf) {
        ACLCHECK(aclrtFree(recvBuf)); // 释放 Device 侧输出内存
    }
    ACLCHECK(aclrtDestroyStream(stream)); // 销毁任务流
    ACLCHECK(aclrtResetDevice(device));   // 重置 device
    return passed ? 0 : 1;
}

int main()
{
    // 初始化 ACL
    ACLCHECK(aclInit(NULL));
    // 查询 device 数量
    uint32_t devCount;
    ACLCHECK(aclrtGetDeviceCount(&devCount));
    std::cout << "Found " << devCount << " NPU device(s) available" << std::endl;

    std::vector<uint32_t> devices;
    try {
        devices = GetDevices(devCount);
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
    uint64_t recvCount = GetRecvCount();
    std::cout << "Use devices: [";
    for (uint32_t device : devices) {
        std::cout << " " << device;
    }
    std::cout << " ], rankSize: " << rankSize << std::endl;
    std::cout << "ReduceScatter recvCount: " << recvCount << ", sendCount per rank: " << recvCount * rankSize
              << std::endl;

    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(devices[0])));
    // 生成 root 节点信息，各线程使用同一份 RootInfo
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    HcclRootInfo* rootInfo = (HcclRootInfo*)rootInfoBuf;
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    // 启动线程执行集合通信操作
    std::vector<std::thread> threads(rankSize);
    std::vector<int> results(rankSize, 1);
    std::vector<ThreadContext> args(rankSize);
    for (uint32_t i = 0; i < rankSize; i++) {
        args[i].rootInfo = rootInfo;
        args[i].device = devices[i];
        args[i].rankId = i;
        args[i].rankSize = rankSize;
        args[i].recvCount = recvCount;
        threads[i] = std::thread([&args, &results, i]() { results[i] = Sample(static_cast<void*>(&args[i])); });
    }
    for (uint32_t i = 0; i < rankSize; i++) {
        threads[i].join();
    }

    // 释放资源
    ACLCHECK(aclrtFreeHost(rootInfoBuf)); // 释放 Host 内存
    ACLCHECK(aclFinalize());              // ACL 去初始化
    for (int result : results) {
        if (result != 0) {
            return 1;
        }
    }
    return 0;
}
