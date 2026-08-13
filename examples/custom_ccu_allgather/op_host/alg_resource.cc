/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>
#include <vector>

#include <ccu/hcomm/ccu_types.h>
#include <hccl/hccl_ccu_res.h>
#include <hccl/hccl_rank_graph.h>
#include <hccl/hccl_res.h>

#include "../../common/ccu_instance.h"
#include "../../common/ccu_user_resource_context.h"
#include "alg_resource.h"
#include "ccu/hcomm/ccu_launch.h"
#include "ccu_kernel.h"

namespace ops_hccl_ag {
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

static HcclResult AllocThreadAndChannelResource(
    HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost, std::vector<ChannelHandle>& kernelChannels,
    uint32_t& kernelDie)
{
    ThreadHandle thread;
    constexpr uint32_t notifyNumOnMainThread = 0;
    RETURN_IF_HCCL_FAIL(
        HcclThreadAcquireWithStream(comm, CommEngine::COMM_ENGINE_CCU, param.stream, notifyNumOnMainThread, &thread));
    resCtxHost.threads.push_back(thread);

    if (param.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    uint32_t channelNum = param.rankSize - 1;
    kernelChannels.resize(channelNum);

    uint32_t channelIndex = 0;
    bool hasKernelDie = false;
    for (uint32_t remoteRank = 0; remoteRank < param.rankSize; remoteRank++) {
        if (remoteRank == param.myRank) {
            continue;
        }

        uint32_t netLayer = 0;
        uint32_t listSize = 0;
        CommLink* linkList = nullptr;
        RETURN_IF_HCCL_FAIL(HcclRankGraphGetLinks(comm, netLayer, param.myRank, remoteRank, &linkList, &listSize));

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
                uint32_t channelDie = 0;
                RETURN_IF_HCCL_FAIL(asccomm_examples::QueryEndpointDie(comm, param.myRank, link.srcEndpointDesc,
                    channelDie));
                RETURN_IF_HCCL_FAIL(asccomm_examples::MergeKernelDie(channelDie, hasKernelDie, kernelDie));
                protocolExists = true;
                break;
            }
        }
        if (!protocolExists) {
            return HCCL_E_NOT_FOUND;
        }
        RETURN_IF_HCCL_FAIL(
            HcclChannelAcquire(comm, CommEngine::COMM_ENGINE_CCU, &desc, 1, &kernelChannels[channelIndex]));
        channelIndex++;
    }

    return HCCL_SUCCESS;
}

static HcclResult RegisterAllGatherKernel(
    HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost,
    const std::vector<ChannelHandle>& kernelChannels, uint32_t kernelDie)
{
    CcuKernelInfo kernelInfo;
    if (strcpy_s(kernelInfo.kernelFuncName, sizeof(kernelInfo.kernelFuncName),
        "CcuAllGatherMesh1DMem2MemKernel") != EOK) {
        return HCCL_E_INTERNAL;
    }
    kernelInfo.kernelFunc = reinterpret_cast<void*>(CcuAllGatherMesh1DMem2MemKernel);

    auto kernelArg = std::make_shared<CcuKernelArgAllGatherMesh1DMem2Mem>();
    kernelArg->rankSize = param.rankSize;
    kernelArg->rankId = param.myRank;
    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernelArg->channels[i] = kernelChannels[i];
    }
    kernelArg->channelCount = static_cast<uint32_t>(kernelChannels.size());
    kernelInfo.setKernelArg(kernelArg);

    resCtxHost.ccuKernels.resize(1);

    asccomm_examples::CcuRegisterContextStorage hcommContextStorage;
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuRegisterContext(comm, hcommContextStorage));
    asccomm_examples::CcuUserResourceContextStorage contextStorage;
    RETURN_IF_HCCL_FAIL(asccomm_examples::BuildDefaultUserCcuRegisterContext(hcommContextStorage, contextStorage));
    CcuInsHandle insHandle = static_cast<CcuInsHandle>(contextStorage.context.instanceHandle);
    CcuResult regStartRet = HcommCcuKernelRegisterStart(insHandle, &contextStorage.context);
    if (regStartRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    CcuKernelHandle kernelHandle;
    const void* kernelArgs[] = {kernelInfo.kernelArg};

    constexpr uint32_t kernelArgNum = 1;
    CcuResult regRet = HcommCcuKernelRegister(
        insHandle, kernelDie, kernelInfo.kernelFuncName, reinterpret_cast<void*>(kernelInfo.kernelFunc), kernelArgs,
        kernelArgNum, &kernelHandle);
    if (regRet != CCU_SUCCESS) {
        (void)HcommCcuKernelRegisterEnd(insHandle);
        return HCCL_E_INTERNAL;
    }
    resCtxHost.ccuKernels[0] = kernelHandle;

    CcuResult regEndRet = HcommCcuKernelRegisterEnd(insHandle);
    if (regEndRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    return HCCL_SUCCESS;
}

HcclResult AllocAlgResource(HcclComm comm, const OpParam& param, AlgResourceCtx& resCtxHost)
{
    std::vector<ChannelHandle> kernelChannels;
    uint32_t kernelDie = 0;
    RETURN_IF_HCCL_FAIL(AllocThreadAndChannelResource(comm, param, resCtxHost, kernelChannels, kernelDie));

    if (param.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    RETURN_IF_HCCL_FAIL(RegisterAllGatherKernel(comm, param, resCtxHost, kernelChannels, kernelDie));
    return HCCL_SUCCESS;
}

} // namespace ops_hccl_ag
