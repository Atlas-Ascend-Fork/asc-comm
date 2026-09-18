/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdio>
#include <iostream>
#include <thread>
#include <vector>

#include <acl/acl_rt.h>
#include <hccl/hccl_comm.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_types.h>

#include "alg_resource.h"
#include "exec_op.h"

using namespace ops_hccl_ag;

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

static HcclResult HcclAllGatherCustom(
    void* sendBuf, void* recvBuf, uint64_t sendCount, HcclDataType data_type_, HcclComm comm, aclrtStream stream)
{
    OpParam param_;
    RETURN_IF_HCCL_FAIL(HcclGetRankId(comm, &param_.myRank));
    RETURN_IF_HCCL_FAIL(HcclGetRankSize(comm, &param_.rankSize));

    param_.stream = stream;
    param_.inputPtr = sendBuf;
    param_.outputPtr = recvBuf;
    param_.count_ = sendCount;
    param_.data_type_ = data_type_;

    AlgResourceCtx resCtxHost;
    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, param_, resCtxHost));
    RETURN_IF_HCCL_FAIL(ExecOp(param_, resCtxHost));

    return HCCL_SUCCESS;
}

int Sample(void* arg)
{
    ThreadContext* ctx = static_cast<ThreadContext*>(arg);
    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    uint32_t device = ctx->device;
    uint64_t sendCount = ctx->devCount;
    uint64_t recvCount = sendCount * ctx->devCount;
    size_t sendSize = sendCount * sizeof(float);
    size_t recvSize = recvCount * sizeof(float);

    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    HcclComm hcclComm;
    HCCLCHECK(HcclCommInitRootInfo(ctx->devCount, ctx->rootInfo, device, &hcclComm));

    aclrtStream stream;
    ACLCHECK(aclrtCreateStream(&stream));

    ACLCHECK(aclrtMalloc(&sendBuf, sendSize, ACL_MEM_MALLOC_HUGE_ONLY));
    ACLCHECK(aclrtMalloc(&recvBuf, recvSize, ACL_MEM_MALLOC_HUGE_ONLY));

    void* hostBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&hostBuf, sendSize));
    float* tmpHostBuff = static_cast<float*>(hostBuf);
    for (uint64_t i = 0; i < sendCount; ++i) {
        tmpHostBuff[i] = static_cast<float>(device);
    }
    std::cout << "rankId: " << device << ", input: [";
    for (uint64_t i = 0; i < sendCount; ++i) {
        std::cout << " " << tmpHostBuff[i];
    }
    std::cout << " ]" << std::endl;

    ACLCHECK(aclrtMemcpy(sendBuf, sendSize, hostBuf, sendSize, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(aclrtFreeHost(hostBuf));

    HCCLCHECK(HcclAllGatherCustom(sendBuf, recvBuf, sendCount, HCCL_DATA_TYPE_FP32, hcclComm, stream));
    ACLCHECK(aclrtSynchronizeStream(stream));

    void* resultBuff = nullptr;
    ACLCHECK(aclrtMallocHost(&resultBuff, recvSize));
    ACLCHECK(aclrtMemcpy(resultBuff, recvSize, recvBuf, recvSize, ACL_MEMCPY_DEVICE_TO_HOST));
    float* tmpResBuff = static_cast<float*>(resultBuff);
    std::cout << "rankId: " << ctx->device << ", output: [";
    for (uint32_t i = 0; i < recvCount; ++i) {
        std::cout << " " << tmpResBuff[i];
    }
    std::cout << " ]" << std::endl;
    ACLCHECK(aclrtFreeHost(resultBuff));

    HCCLCHECK(HcclCommDestroy(hcclComm));
    if (sendBuf != nullptr) {
        ACLCHECK(aclrtFree(sendBuf));
    }
    if (recvBuf != nullptr) {
        ACLCHECK(aclrtFree(recvBuf));
    }
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(device));
    return 0;
}

int main()
{
    ACLCHECK(aclInit(nullptr));

    uint32_t devCount = 0;
    ACLCHECK(aclrtGetDeviceCount(&devCount));
    std::cout << "Found " << devCount << " NPU device(s) available" << std::endl;

    int32_t rootRank = 0;
    ACLCHECK(aclrtSetDevice(rootRank));

    void* rootInfoBuf = nullptr;
    ACLCHECK(aclrtMallocHost(&rootInfoBuf, sizeof(HcclRootInfo)));
    HcclRootInfo* rootInfo = static_cast<HcclRootInfo*>(rootInfoBuf);
    HCCLCHECK(HcclGetRootInfo(rootInfo));

    std::vector<std::thread> threads(devCount);
    std::vector<ThreadContext> args_(devCount);
    for (uint32_t i = 0; i < devCount; i++) {
        args_[i].rootInfo = rootInfo;
        args_[i].device = i;
        args_[i].devCount = devCount;
        threads[i] = std::thread(Sample, static_cast<void*>(&args_[i]));
    }
    for (uint32_t i = 0; i < devCount; i++) {
        threads[i].join();
    }

    ACLCHECK(aclrtFreeHost(rootInfoBuf));
    ACLCHECK(aclFinalize());
    return 0;
}
