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
#include <ccu/hcomm/ccu_api_types.h>
#include <hccl/hccl_res.h>
#include <hccl/hccl_rank_graph.h>
#include "alg_resource.h"
#include "ccu_kernel.h"
#include "ccu/hcomm/ccu_launch.h"
#include "../../../common/ccu_instance.h"
#include "../../../common/ccu_register_context.h"

namespace ops_hccl_rs {
constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

static HcclResult AllocThreadAndChannelResource(
    HcclComm comm, const OpParam& param_, AlgResourceCtx& resCtxHost, std::vector<ChannelHandle>& kernelChannels,
    uint32_t& kernelDie)
{
    ThreadHandle thread;
    constexpr uint32_t notifyNumOnMainThread = 0;
    RETURN_IF_HCCL_FAIL(
        HcclThreadAcquireWithStream(comm, CommEngine::COMM_ENGINE_CCU, param_.stream, notifyNumOnMainThread, &thread));
    resCtxHost.threads.push_back(thread);

    if (param_.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    uint32_t channel_num = param_.rankSize - 1;
    kernelChannels.resize(channel_num);

    uint32_t channelIndex = 0;
    bool hasKernelDie = false;
    for (uint32_t remoteRank = 0; remoteRank < param_.rankSize; remoteRank++) {
        if (remoteRank == param_.myRank) {
            continue;
        }

        uint32_t netLayer = 0, listSize = 0;
        CommLink* linkList = nullptr;
        RETURN_IF_HCCL_FAIL(HcclRankGraphGetLinks(
            comm, netLayer, param_.myRank, remoteRank, &linkList,
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
                uint32_t channelDie = 0;
                RETURN_IF_HCCL_FAIL(
                    asccomm_examples::QueryEndpointDie(comm, param_.myRank, link.srcEndpointDesc, channelDie));
                RETURN_IF_HCCL_FAIL(asccomm_examples::MergeKernelDie(channelDie, hasKernelDie, kernelDie));
                protocolExists = true;
                break;
            }
        }
        if (!protocolExists) {
            return HCCL_E_NOT_FOUND;
        }
        RETURN_IF_HCCL_FAIL(HcclChannelAcquire(
            comm, CommEngine::COMM_ENGINE_CCU, &desc, 1,
            &kernelChannels[channelIndex])); // 获取 channel_ handle
        channelIndex++;
    }

    return HCCL_SUCCESS;
}

static HcclResult RegisterReduceScatterKernel(
    HcclComm comm, const OpParam& param_, AlgResourceCtx& resCtxHost, const std::vector<ChannelHandle>& kernelChannels,
    uint32_t kernelDie)
{
    ccu_kernel_info kernelInfo;
    if (strcpy_s(
            kernelInfo.kernel_func_name, sizeof(kernelInfo.kernel_func_name), "CcuReduceScatterMesh1DMem2MemKernel") !=
        EOK) {
        return HCCL_E_INTERNAL;
    }
    kernelInfo.kernel_func = reinterpret_cast<void*>(CcuReduceScatterMesh1DMem2MemKernel);

    auto kernel_arg = std::make_shared<CcuKernelArgReduceScatterMesh1DMem2Mem>();
    kernel_arg->rankSize = param_.rankSize;
    kernel_arg->rankId = param_.myRank;
    kernelInfo.setKernelArg(kernel_arg);

    for (uint32_t i = 0; i < kernelChannels.size(); ++i) {
        kernel_arg->channels[i] = kernelChannels[i]; // 将 channel_ handle 保存到 kernel 参数
    }
    kernel_arg->channelCount = static_cast<uint32_t>(kernelChannels.size());

    resCtxHost.ccuKernels.resize(1); // 只注册一个 kernel

    CcuInsHandle ins_handle{0};
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuInstance(comm, ins_handle));
    HcommCcuRegisterContextHandle context_{nullptr};
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuRegisterContext(comm, context_));
    CcuResult regStartRet = asccomm_ccu_kernel_register_start(ins_handle, context_);
    if (regStartRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    ccu_kernel_handle kernelHandle;
    const void* kernel_args[] = {kernelInfo.kernel_arg};

    constexpr uint32_t kernel_arg_num = 1;
    CcuResult reg_ret = asccomm_ccu_kernel_register(
        ins_handle, kernelDie, kernelInfo.kernel_func_name, reinterpret_cast<void*>(kernelInfo.kernel_func),
        kernel_args, kernel_arg_num, &kernelHandle); // 注册 kernel

    if (reg_ret != CCU_SUCCESS) {
        (void)asccomm_ccu_kernel_register_end(ins_handle);
        return HCCL_E_INTERNAL;
    }
    resCtxHost.ccuKernels[0] = kernelHandle;

    CcuResult regEndRet = asccomm_ccu_kernel_register_end(ins_handle);
    if (regEndRet != CCU_SUCCESS) {
        return HCCL_E_INTERNAL;
    }

    return HCCL_SUCCESS;
}

HcclResult AllocAlgResource(HcclComm comm, const OpParam& param_, AlgResourceCtx& resCtxHost)
{
    std::vector<ChannelHandle> kernelChannels;
    uint32_t kernelDie = 0;
    RETURN_IF_HCCL_FAIL(AllocThreadAndChannelResource(comm, param_, resCtxHost, kernelChannels, kernelDie));

    if (param_.rankSize == 1) {
        return HCCL_SUCCESS;
    }

    RETURN_IF_HCCL_FAIL(RegisterReduceScatterKernel(comm, param_, resCtxHost, kernelChannels, kernelDie));
    return HCCL_SUCCESS;
}

} // namespace ops_hccl_rs
