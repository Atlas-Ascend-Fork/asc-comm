/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <hccl/hccl_res_expt.h>
#include <hccl/hccl_rank_graph.h>
#include <hcomm/hcomm_res.h>
#include "asccomm_cpu_primitives.h"
#include "log.h"
#include "common.h"
#include "device_type.h"
#include "kernel_loader.h"
#include "aicpu_ts_custom_alltoallv.h"

constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

HcclResult FillChannelDesc(HcclComm comm, uint32_t srcRank, uint32_t dstRank, HcclChannelDesc& desc)
{
    uint32_t netLayer = 0;
    uint32_t listSize = 0;
    CommLink* linkList = nullptr;
    CHK_RET(HcclRankGraphGetLinks(comm, netLayer, srcRank, dstRank, &linkList, &listSize));

    CHK_RET(HcclChannelDescInit(&desc, 1));
    CommProtocol protocol = CommProtocol::COMM_PROTOCOL_UBC_CTP;
    bool protocolExists = false;
    for (uint32_t idx = 0; idx < listSize; idx++) {
        CommLink link = linkList[idx];
        if (link.linkAttr.linkProtocol == protocol) {
            desc.remoteRank = dstRank;
            desc.notifyNum = CHANNEL_NOTIFY_NUM;
            desc.channelProtocol = link.linkAttr.linkProtocol;
            desc.localEndpoint.protocol = link.srcEndpointDesc.protocol;
            desc.localEndpoint.commAddr = link.srcEndpointDesc.commAddr;
            desc.localEndpoint.loc = link.srcEndpointDesc.loc;
            desc.remoteEndpoint.protocol = link.dstEndpointDesc.protocol;
            desc.remoteEndpoint.commAddr = link.dstEndpointDesc.commAddr;
            desc.remoteEndpoint.loc = link.dstEndpointDesc.loc;
            protocolExists = true;
            break;
        }
    }
    if (!protocolExists) {
        HCCL_ERROR("[FillChannelDesc] Protocol %d not found between rank %u and rank %u", protocol, srcRank, dstRank);
        return HCCL_E_NOT_FOUND;
    }
    return HCCL_SUCCESS;
}

HcclResult HcclMemcpyCtxHostToDevice(
    HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost, void** resCtxSequence, uint64_t* ctxSize)
{
    // 序列化
    std::vector<char> seq = resCtxHost.Serialize();
    uint64_t size = seq.size();

    void* ctx = nullptr;

    // 创建Context, aicpu和host dpu申请device内存
    CHK_RET(HcclEngineCtxCreate(comm, param.tag, COMM_ENGINE_AICPU_TS, size, &ctx));
    // 从Host内存拷贝到Device Context内存上
    CHK_RET(HcclEngineCtxCopy(comm, COMM_ENGINE_AICPU_TS, param.tag, seq.data(), size, 0));
    // 将内存强转为AlgResourceCtx结构体
    *resCtxSequence = ctx;
    *ctxSize = size;
    HCCL_INFO("Memcpy hostCtx to device success.");
    return HCCL_SUCCESS;
}

HcclResult HcclGetThreadAICPU(
    HcclComm comm, const OpParam& param, std::vector<ThreadHandle>& threadHandles, ThreadHandle* originalMainThread)
{
    CHK_PTR_NULL(originalMainThread);
    // Mesh算法所需资源(与HcclAllocAlgResourceAICPU的线程规划一致)
    uint32_t threadNum = param.rankSize > 1 ? param.rankSize - 1 : 1;
    uint32_t slaveThreadNum = threadNum - 1;
    uint32_t notifyNumOnMainThread = slaveThreadNum;
    uint32_t maxNotifyNum = notifyNumOnMainThread;
    threadHandles.resize(threadNum);
    // maxNotifyNum需要再增加一个用于host-device同步
    CHK_RET(HcclThreadAcquire(comm, COMM_ENGINE_AICPU_TS, threadNum, maxNotifyNum + 1, threadHandles.data()));
    // AICPU acquire returns the ThreadEntity address handle directly.
    *originalMainThread = threadHandles[0];
    return HCCL_SUCCESS;
}

HcclResult HcclGetChannelAICPU(
    HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost, std::vector<ChannelHandle>& channelHandles)
{
    uint32_t channelNum = param.rankSize - 1;
    std::vector<HcclChannelDesc> descs(channelNum);
    channelHandles.resize(channelNum);
    std::vector<uint32_t> remoteRanks(channelNum);

    uint32_t idx = 0;
    for (uint32_t remoteRank = 0; remoteRank < param.rankSize; remoteRank++) {
        if (remoteRank == param.myRank) {
            continue;
        }
        CHK_RET(FillChannelDesc(comm, param.myRank, remoteRank, descs[idx]));
        remoteRanks[idx] = remoteRank;
        idx++;
    }

    CHK_RET(HcclChannelAcquire(comm, COMM_ENGINE_AICPU_TS, descs.data(), channelNum, channelHandles.data()));

    for (uint32_t i = 0; i < channelNum; i++) {
        ChannelInfo channel;
        channel.remoteRank = remoteRanks[i];
        channel.notifyNum = CHANNEL_NOTIFY_NUM;
        void* cclBuf;
        uint64_t cclBufSize;
        CHK_RET(HcclChannelGetHcclBuffer(comm, channelHandles[i], &cclBuf, &cclBufSize));
        channel.remoteCclMem = CommBuffer{cclBuf, cclBufSize};
        resCtxHost.channels.push_back(channel);
    }
    for (size_t i = 0; i < channelHandles.size(); ++i) {
        resCtxHost.channels[i].handle = channelHandles[i];
    }
    return HCCL_SUCCESS;
}

HcclResult HcclAllocAlgResourceAICPU(HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost)
{
    void* cclBufferAddr;
    uint64_t cclBufferSize;
    // 从通信域获取CCL buffer
    CHK_RET(HcclGetHcclBuffer(comm, &cclBufferAddr, &cclBufferSize));
    resCtxHost.cclMem = CommBuffer{cclBufferAddr, cclBufferSize};
    uint32_t threadNum = param.rankSize > 1 ? param.rankSize - 1 : 1;
    resCtxHost.slaveThreadNum = threadNum - 1;
    resCtxHost.notifyNumOnMainThread = resCtxHost.slaveThreadNum;
    resCtxHost.notifyNumPerThread = std::vector<uint32_t>(resCtxHost.slaveThreadNum, 1);
    // 申请thread/channel句柄
    std::vector<ThreadHandle> threadHandles;
    std::vector<ChannelHandle> channelHandles;
    ThreadHandle originalMainThread = 0;
    CHK_RET(HcclGetThreadAICPU(comm, param, threadHandles, &originalMainThread));
    CHK_RET(HcclGetChannelAICPU(comm, param, resCtxHost, channelHandles));

    // helper返回的线程/通道句柄已经是POD地址句柄；仅保留原始主线程句柄供控制面导出。
    resCtxHost.threads = threadHandles;
    resCtxHost.mainThreadHandle = originalMainThread;
    return HCCL_SUCCESS;
}

// aicpu主线程handle及其notify数量，缓存于COMM_ENGINE_CPU_TS(通信域host内存)context，供复用
struct AicpuMainThreadCache {
    ThreadHandle thread;
    uint32_t notifyNum;
};

bool IsDataTypeSupported(HcclDataType dataType)
{
    switch (dataType) {
        case HCCL_DATA_TYPE_INT8:
        case HCCL_DATA_TYPE_INT16:
        case HCCL_DATA_TYPE_INT32:
        case HCCL_DATA_TYPE_FP16:
        case HCCL_DATA_TYPE_FP32:
        case HCCL_DATA_TYPE_INT64:
        case HCCL_DATA_TYPE_UINT64:
        case HCCL_DATA_TYPE_UINT8:
        case HCCL_DATA_TYPE_UINT16:
        case HCCL_DATA_TYPE_UINT32:
        case HCCL_DATA_TYPE_FP64:
        case HCCL_DATA_TYPE_BFP16:
            return true;
        default:
            return false;
    }
}

HcclResult AlltoAllVCustom(
    const void* sendBuf, const uint64_t* sendCounts, const uint64_t* sendDispls, HcclDataType sendType, void* recvBuf,
    const uint64_t* recvCounts, const uint64_t* recvDispls, HcclDataType recvType, HcclComm comm, aclrtStream stream)
{
    CHK_PTR_NULL(sendBuf);
    CHK_PTR_NULL(sendCounts);
    CHK_PTR_NULL(sendDispls);
    CHK_PTR_NULL(recvBuf);
    CHK_PTR_NULL(recvCounts);
    CHK_PTR_NULL(recvDispls);
    CHK_PTR_NULL(comm);
    CHK_PTR_NULL(stream);

    if (!IsDataTypeSupported(sendType) || !IsDataTypeSupported(recvType)) {
        HCCL_ERROR(
            "[AlltoAllVCustom] Unsupported data type, send type [%d], recv type [%d]", static_cast<int32_t>(sendType),
            static_cast<int32_t>(recvType));
        return HCCL_E_NOT_SUPPORT;
    }
    if (sendType != recvType) {
        HCCL_ERROR("[AlltoAllVCustom] Send and recv data types must match");
        return HCCL_E_NOT_SUPPORT;
    }

    OpParam param;
    int ret = sprintf_s(param.tag, sizeof(param.tag), "%s", "hccl_custom_alltoallv");
    if (ret <= 0) {
        HCCL_ERROR("[AlltoAllVCustom] Failed to fill param.tag");
        return HCCL_E_INTERNAL;
    }
    CHK_RET(GetDeviceType(&param.devType));
    if (param.devType != DEVICE_TYPE_A5) {
        HCCL_ERROR("[AlltoAllVCustom] Not Support Device Type [%u]", param.devType);
        return HCCL_E_INTERNAL;
    }

    CHK_RET(HcclGetCommName(comm, param.commName));
    HCCL_INFO("[AlltoAllVCustom] commName: %s", param.commName);
    param.inputPtr = sendBuf;
    param.outputPtr = recvBuf;
    param.dataType = sendType;
    param.opType = HcclCMDType::HCCL_CMD_ALLTOALLV;

    // ==============================================
    // STEP 1: 解析拓扑信息
    // ==============================================
    CHK_RET(HcclGetRankId(comm, &param.myRank));
    CHK_RET(HcclGetRankSize(comm, &param.rankSize));
    if (param.rankSize == 0 || param.rankSize > MAX_RANK_SIZE) {
        HCCL_ERROR("[AlltoAllVCustom] Unsupported rank size [%u], maximum is [%u]", param.rankSize, MAX_RANK_SIZE);
        return HCCL_E_NOT_SUPPORT;
    }
    if (sendCounts[param.myRank] != recvCounts[param.myRank]) {
        HCCL_ERROR("[AlltoAllVCustom] Local send and recv counts must match");
        return HCCL_E_NOT_SUPPORT;
    }
    for (uint32_t rank = 0; rank < param.rankSize; rank++) {
        param.sendCounts[rank] = sendCounts[rank];
        param.sendDispls[rank] = sendDispls[rank];
        param.recvCounts[rank] = recvCounts[rank];
        param.recvDispls[rank] = recvDispls[rank];
    }

    // ==============================================
    // STEP 2: 申请host同步thread (从stream转换)
    // 该thread与stream绑定，每次调用都必须重新申请，不能随context复用
    // ==============================================
    CHK_RET(HcclThreadAcquireWithStream(comm, COMM_ENGINE_CPU_TS, stream, 1, &param.cpuThread));
    // 将host cpu thread导出到aicpu引擎，供device侧通知host使用(每次调用刷新)
    CHK_RET(HcclThreadExportToCommEngine(comm, 1, &param.cpuThread, COMM_ENGINE_AICPU_TS, &param.cpuThreadOnAicpu));

    // ==============================================
    // STEP 3: 创建/复用资源
    // ==============================================
    CommEngine engine = CommEngine::COMM_ENGINE_AICPU_TS;

    void* ctx = nullptr;
    uint64_t size = 0;
    if (HcclEngineCtxGet(comm, param.tag, engine, &ctx, &size) == HCCL_SUCCESS) {
        // device资源已经存在，复用
        HCCL_INFO("[AlltoAllVCustom] Engine context already exists");
        param.resCtxDevice = ctx;
        param.ctxSize = size;
        // aicpu主thread为comm级资源(非stream绑定)可复用；从通信域host内存context取回其句柄，重新导出到cpu引擎
        void* hostCtx = nullptr;
        uint64_t hostCtxSize = sizeof(AicpuMainThreadCache);
        CHK_RET(HcclEngineCtxGet(comm, param.tag, COMM_ENGINE_CPU_TS, &hostCtx, &hostCtxSize));
        auto* mainThreadCache = static_cast<AicpuMainThreadCache*>(hostCtx);
        CHK_RET(HcclThreadExportToCommEngine(
            comm, 1, &mainThreadCache->thread, COMM_ENGINE_CPU_TS, &param.aicpuThreadOnCpu));
        param.aicpuRecordCpuIdx = mainThreadCache->notifyNum;
    } else {
        // 不存在，新创建Context
        HCCL_INFO("[AlltoAllVCustom] Creating engine context");
        AlgResourceCtx resCtxHost;

        // 申请资源Thread和Channel (comm级资源，可随context复用)
        CHK_RET(HcclAllocAlgResourceAICPU(comm, param, resCtxHost));

        // 使用threads[0]作为主AICPU thread，同时负责算法执行和host/device同步
        // 将aicpu主thread导出到cpu引擎，供host侧通知device使用
        CHK_RET(HcclThreadExportToCommEngine(
            comm, 1, &resCtxHost.mainThreadHandle, COMM_ENGINE_CPU_TS, &param.aicpuThreadOnCpu));
        param.aicpuRecordCpuIdx = resCtxHost.notifyNumOnMainThread;

        // 序列化并拷贝到device context
        CHK_RET(HcclMemcpyCtxHostToDevice(comm, param, resCtxHost, &param.resCtxDevice, &param.ctxSize));
        // 将aicpu主线程句柄缓存到通信域host内存context(COMM_ENGINE_CPU_TS)，供后续复用
        void* hostCtx = nullptr;
        uint64_t hostCtxSize = sizeof(AicpuMainThreadCache);
        CHK_RET(HcclEngineCtxCreate(comm, param.tag, COMM_ENGINE_CPU_TS, hostCtxSize, &hostCtx));
        auto* mainThreadCache = static_cast<AicpuMainThreadCache*>(hostCtx);
        mainThreadCache->thread = resCtxHost.mainThreadHandle;
        mainThreadCache->notifyNum = resCtxHost.notifyNumOnMainThread;
    }

    // STEP 4: 下发 AICPU Kernel
    // ==============================================
    CHK_RET(LoadAICPUKernel("libcustom_alltoallv_aicpu_kernel.json", g_binKernelHandle));
    CHK_RET(
        asccomm_cpu_thread_notify_record_on_thread(param.cpuThread, param.aicpuThreadOnCpu, param.aicpuRecordCpuIdx));

    aclrtFuncHandle funcHandle;
    aclrtArgsHandle argsHandle;
    ACLCHECK(aclrtBinaryGetFunction(g_binKernelHandle, "LaunchCustomAlltoAllVAicpuKernel", &funcHandle));
    ACLCHECK(aclrtKernelArgsInit(funcHandle, &argsHandle));
    aclrtParamHandle paraHandle;
    ACLCHECK(aclrtKernelArgsAppend(argsHandle, &param, sizeof(OpParam), &paraHandle));
    ACLCHECK(aclrtKernelArgsFinalize(argsHandle));

    constexpr uint16_t notifyWaitTime = 1836;
    aclrtLaunchKernelAttr attr;
    attr.id = ACL_RT_LAUNCH_KERNEL_ATTR_TIMEOUT;
    attr.value.timeout = notifyWaitTime;
    aclrtLaunchKernelCfg cfg;
    cfg.numAttrs = 1;
    cfg.attrs = &attr;
    constexpr uint32_t numBlocks = 1;
    ACLCHECK(aclrtLaunchKernelWithConfig(funcHandle, numBlocks, stream, &cfg, argsHandle, nullptr));

    CHK_RET(asccomm_cpu_thread_notify_wait_on_thread(param.cpuThread, 0, CUSTOM_TIMEOUT));
    return HCCL_SUCCESS;

    return HCCL_SUCCESS;
}
