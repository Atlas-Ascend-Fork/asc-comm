/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>

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
    uint32_t devCount;
};

static HcclResult HcclReduceScatterCustom(
    void* sendBuf, void* recvBuf, uint64_t recvCount, HcclDataType data_type_, HcclReduceOp reduce_op, HcclComm comm,
    aclrtStream stream)
{
    if (sendBuf == nullptr || recvBuf == nullptr || comm == nullptr || stream == nullptr) {
        return HCCL_E_PTR;
    }
    if (data_type_ != HCCL_DATA_TYPE_FP32 || reduce_op != HCCL_REDUCE_SUM) {
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
    param_.recvCount = recvCount;
    param_.data_type_ = data_type_;
    param_.reduce_op = reduce_op;

    AlgResourceCtx resCtxHost;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param_, resCtxHost));
    RETURN_IF_HCCL_FAIL(ExecOp(param_, resCtxHost));

    return HCCL_SUCCESS;
}

int Sample(void* arg)
{
    ThreadContext* ctx = (ThreadContext*)arg;
    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    uint32_t device = ctx->device;
    const uint64_t recvCount = ctx->devCount;
    uint64_t sendCount = recvCount * ctx->devCount;
    size_t sendSize = sendCount * sizeof(float);
    size_t recvSize = recvCount * sizeof(float);
    // 设置当前线程使用的 device
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    // 初始化 HCCL 通信域
    HcclComm hcclComm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->devCount, ctx->rootInfo, device, &hcclComm));

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
        tmpHostBuff[i] = static_cast<float>(device * 100 + i);
    }
    std::cout << "rankId: " << device << ", input: [";
    for (uint64_t i = 0; i < sendCount; ++i) {
        std::cout << " " << tmpHostBuff[i];
    }
    std::cout << " ]" << std::endl;

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
    std::this_thread::sleep_for(std::chrono::seconds(ctx->device));
    void* resultBuff;
    ACLCHECK(aclrtMallocHost(&resultBuff, recvSize));
    ACLCHECK(aclrtMemcpy(resultBuff, recvSize, recvBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
    float* tmpResBuff = static_cast<float*>(resultBuff);
    bool passed = true;
    std::cout << "rankId: " << ctx->device << ", output: [";
    for (uint32_t i = 0; i < recvCount; ++i) {
        std::cout << " " << tmpResBuff[i];
        float expected_ = static_cast<float>(
            100 * ctx->devCount * (ctx->devCount - 1) / 2 + ctx->devCount * (ctx->device * recvCount + i));
        passed = passed && (tmpResBuff[i] == expected_);
    }
    std::cout << " ]" << std::endl;
    std::cout << "rankId: " << ctx->device << (passed ? " PASS" : " FAIL") << std::endl;
    ACLCHECK(aclrtFreeHost(resultBuff));

    // 释放资源
    HCCLCHECK(HcclCommDestroy(hcclComm)); // 销毁通信域
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

    int32_t rootRank = 0;
    ACLCHECK(aclrtSetDevice(rootRank));
    // 生成 root 节点信息，各线程使用同一份 RootInfo
    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    HcclRootInfo* rootInfo = (HcclRootInfo*)rootInfoBuf;
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    // 启动线程执行集合通信操作
    std::vector<std::thread> threads(devCount);
    std::vector<int> results(devCount, 1);
    std::vector<ThreadContext> args_(devCount);
    for (uint32_t i = 0; i < devCount; i++) {
        args_[i].rootInfo = rootInfo;
        args_[i].device = i;
        args_[i].devCount = devCount;
        threads[i] = std::thread([&args_, &results, i]() { results[i] = Sample(static_cast<void*>(&args_[i])); });
    }
    for (uint32_t i = 0; i < devCount; i++) {
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
