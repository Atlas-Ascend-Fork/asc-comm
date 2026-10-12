/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <memory>

#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_rank_graph.h>
#include "alg_resource.h"
#include "ccu_kernel.h"
#include "ccu/hcomm/ccu_launch.h"
#include "../../../common/ccu_instance.h"

namespace ops_hccl_transfer {
namespace {

constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

HcclResult AllocChannelResource(
    HcclComm comm, const OpParam& param, std::vector<ChannelHandle>& channels, uint32_t& kernelDie)
{
    if (param.rankSize != 2U) {
        return HCCL_E_PARA;
    }

    channels.resize(param.rankSize - 1U);
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
        const CommProtocol protocol = CommProtocol::COMM_PROTOCOL_UBC_CTP;
        bool protocolExists = false;
        for (uint32_t i = 0; i < listSize; i++) {
            const CommLink& link = linkList[i];
            if (link.linkAttr.linkProtocol != protocol) {
                continue;
            }

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
            RETURN_IF_HCCL_FAIL(
                asccomm_examples::QueryEndpointDie(comm, param.myRank, link.srcEndpointDesc, channelDie));
            RETURN_IF_HCCL_FAIL(asccomm_examples::MergeKernelDie(channelDie, hasKernelDie, kernelDie));
            protocolExists = true;
            break;
        }
        if (!protocolExists) {
            return HCCL_E_NOT_FOUND;
        }

        RETURN_IF_HCCL_FAIL(HcclChannelAcquire(comm, CommEngine::COMM_ENGINE_CCU, &desc, 1, &channels[0]));
    }
    return HCCL_SUCCESS;
}

HcclResult RegisterTransferKernel(
    HcclComm comm, const OpParam& param, AlgResourceCtx& resourceCtx, const std::vector<ChannelHandle>& channels,
    uint32_t kernelDie)
{
    ccu_kernel_info kernelInfo{};
    if (strcpy_s(kernelInfo.kernel_func_name, sizeof(kernelInfo.kernel_func_name), "CcuTransferKernel") != EOK) {
        return HCCL_E_INTERNAL;
    }
    kernelInfo.kernel_func = reinterpret_cast<void*>(CcuTransferKernel);

    auto kernelArg = std::make_shared<CcuTransferKernelArg>();
    kernelArg->rankSize = param.rankSize;
    kernelArg->rankId = param.myRank;
    kernelArg->channelCount = static_cast<uint32_t>(channels.size());
    for (uint32_t i = 0; i < channels.size(); i++) {
        kernelArg->channels[i] = channels[i];
    }
    kernelInfo.setKernelArg(kernelArg);

    void* instanceHandle = nullptr;
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuInstance(comm, instanceHandle));
    if (asccomm_ccu_kernel_register_start(instanceHandle) != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    ccu_kernel_handle kernelHandle = 0;
    const void* kernelArgs[] = {kernelInfo.kernel_arg};
    constexpr uint32_t kernelArgNum = 1;
    const CcuResult ret = asccomm_ccu_kernel_register(
        instanceHandle, kernelDie, kernelInfo.kernel_func_name, kernelInfo.kernel_func, kernelArgs, kernelArgNum,
        &kernelHandle);
    if (ret != CCU_SUCCESS) {
        (void)asccomm_ccu_kernel_register_end(instanceHandle);
        return HCCL_E_INTERNAL;
    }
    if (asccomm_ccu_kernel_register_end(instanceHandle) != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    resourceCtx.ccuKernels.push_back(kernelHandle);
    return HCCL_SUCCESS;
}

} // namespace

HcclResult AllocAlgResource(HcclComm comm, const OpParam& param, AlgResourceCtx& resourceCtx)
{
    ThreadHandle thread{};
    constexpr uint32_t notifyNumOnMainThread = 0;
    RETURN_IF_HCCL_FAIL(
        HcclThreadAcquireWithStream(comm, CommEngine::COMM_ENGINE_CCU, param.stream, notifyNumOnMainThread, &thread));
    resourceCtx.threads.push_back(thread);

    std::vector<ChannelHandle> channels;
    uint32_t kernelDie = 0;
    RETURN_IF_HCCL_FAIL(AllocChannelResource(comm, param, channels, kernelDie));
    RETURN_IF_HCCL_FAIL(RegisterTransferKernel(comm, param, resourceCtx, channels, kernelDie));
    return HCCL_SUCCESS;
}

} // namespace ops_hccl_transfer
