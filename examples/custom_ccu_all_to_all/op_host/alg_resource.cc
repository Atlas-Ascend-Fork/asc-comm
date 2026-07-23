/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <vector>
#include <memory>
#include <ccu/ccu_types.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_rank_graph.h>
#include "alg_resource.h"
#include "ccu_kernel.h"
#include "ccu_launch.h"

extern "C" HcclResult HcclCommQueryCcuIns(HcclComm comm, CcuInsHandle *insHandles, uint32_t *insNum);

namespace ops_hccl_a2a {
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

static HcclResult AllocThreadAndChannelResource(HcclComm comm, const OpParam &param, AlgResourceCtx &resCtxHost,
                                                std::vector<ChannelHandle> &kernelChannels)
{
    ThreadHandle thread;
    constexpr uint32_t notifyNumOnMainThread = 0;
    RETURN_IF_HCCL_FAIL(HcclThreadAcquireWithStream(comm, CommEngine::COMM_ENGINE_CCU, param.stream,
        notifyNumOnMainThread, &thread));
    resCtxHost.threads.push_back(thread);

    if (param.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    uint32_t channelNum = param.rankSize - 1;
    kernelChannels.resize(channelNum);

    uint32_t channelIndex = 0;
    for(uint32_t remoteRank = 0; remoteRank < param.rankSize; remoteRank++) {
        if (remoteRank == param.myRank) {
            continue;
        }

        uint32_t netLayer = 0, listSize = 0;
        CommLink *linkList = nullptr;
        RETURN_IF_HCCL_FAIL(HcclRankGraphGetLinks(comm, netLayer, param.myRank, remoteRank, &linkList,
                                      &listSize)); // 获取 srcRank 和 dstRank 间的 link 信息

        HcclChannelDesc desc;
        RETURN_IF_HCCL_FAIL(HcclChannelDescInit(&desc, 1));
        CommProtocol protocol = CommProtocol::COMM_PROTOCOL_UBC_CTP;
        bool protocolExists = false;
        for (uint32_t idx = 0; idx < listSize; idx++) {
            CommLink link = linkList[idx];
            if (link.linkAttr.linkProtocol == protocol) {
                desc.remoteRank = remoteRank;
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
            return HCCL_E_NOT_FOUND;
        }
        RETURN_IF_HCCL_FAIL(HcclChannelAcquire(comm, CommEngine::COMM_ENGINE_CCU, &desc, 1,
            &kernelChannels[channelIndex])); // 获取 channel handle
        channelIndex++;
    }

    return HCCL_SUCCESS;
}

static HcclResult RegisterAllToAllKernel(HcclComm comm, const OpParam &param, AlgResourceCtx &resCtxHost,
                                          const std::vector<ChannelHandle> &kernelChannels)
{
    CcuKernelInfo kernelInfo;
    strcpy_s(kernelInfo.kernelFuncName, sizeof(kernelInfo.kernelFuncName), "CcuAllToAllMesh1DMem2MemKernel");
    kernelInfo.kernelFunc = reinterpret_cast<void *>(CcuAllToAllMesh1DMem2MemKernel);

    auto kernelArg = std::make_shared<CcuKernelArgAllToAllMesh1DMem2Mem>();
    kernelArg->rankSize = param.rankSize;
    kernelArg->rankId = param.myRank;
    kernelInfo.setKernelArg(kernelArg);

    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernelArg->channels[i] = kernelChannels[i];  // 将 channel handle 保存到 kernel 参数
    }
    kernelArg->channelCount = static_cast<uint32_t>(kernelChannels.size());

    CcuInsHandle insHandle{0};
    uint32_t insNum = 0;
    RETURN_IF_HCCL_FAIL(HcclCommQueryCcuIns(comm, &insHandle, &insNum));
    if (insNum != 1) {
        return HCCL_E_INTERNAL;
    }

    resCtxHost.ccuKernels.resize(1); // 只注册一个 kernel

    CcuResult regStartRet = HcommCcuKernelRegisterStart(insHandle);
    if (regStartRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    CcuKernelHandle kernelHandle;
    const void *kernelArgs[] = {kernelInfo.kernelArg};

    constexpr uint32_t dieId = 0; // 预留接口，暂无含义
    constexpr uint32_t kernelArgNum = 1;
    CcuResult regRet = HcommCcuKernelRegister(insHandle, dieId, kernelInfo.kernelFuncName,
                                                reinterpret_cast<void*>(kernelInfo.kernelFunc),
                                                kernelArgs, kernelArgNum, &kernelHandle); // 注册 kernel

    if (regRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    resCtxHost.ccuKernels[0] = kernelHandle;

    CcuResult regEndRet = HcommCcuKernelRegisterEnd(insHandle);
    if (regEndRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }
    
    return HCCL_SUCCESS;
}

HcclResult AllocAlgResource(HcclComm comm, const OpParam &param, AlgResourceCtx &resCtxHost)
{
    std::vector<ChannelHandle> kernelChannels;
    RETURN_IF_HCCL_FAIL(AllocThreadAndChannelResource(comm, param, resCtxHost, kernelChannels));

    if (param.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    RETURN_IF_HCCL_FAIL(RegisterAllToAllKernel(comm, param, resCtxHost, kernelChannels));
    return HCCL_SUCCESS;
}

} // namespace ops_hccl_a2a


