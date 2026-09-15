/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @brief 实现 NHR 调度及 CCU Thread、Channel、Kernel、EngineCtx 资源管理。
 *
 * 本文件根据 Rank 数量生成 NHR 通信步骤，批量申请所需的 UBC_CTP Channel，
 * 注册 NHR1D Mem2Mem AllGather Kernel，并在 HCCL EngineCtx 中序列化或恢复
 * 资源句柄。
 */
#include "alg_resource.h"

#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>

#include <hccl/hccl_comm.h>
#include <hccl/hccl_rank_graph.h>
#include <hccl/hcomm_primitives.h>

#include <ccu/hcomm/ccu_launch.h>
#include "../../../common/ccu_register_context.h"
#include <ccu/hcomm/ccu_resource_api.h>
#include <hccl/hccl_ccu_res.h>
#include "../../../common/ccu_instance.h"

#include "ccu_kernel.h"

namespace CcuAgNhr1dMem2mem {
namespace {

constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;

class BinaryStream {
public:
    static constexpr std::ios_base::openmode DEFAULT_IOS_MODE = std::ios_base::in | std::ios_base::out;

    explicit BinaryStream(std::ios_base::openmode mode = DEFAULT_IOS_MODE) : stream_(mode | std::ios_base::binary) {}

    explicit BinaryStream(std::vector<char>& buffer, std::ios_base::openmode mode = DEFAULT_IOS_MODE)
        : stream_(mode | std::ios_base::binary)
    {
        stream_.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        stream_.seekg(0);
    }

    template <typename t>
    BinaryStream& operator<<(const t& value)
    {
        stream_.write(reinterpret_cast<const char*>(&value), sizeof(t));
        return *this;
    }

    template <typename t>
    BinaryStream& operator<<(const std::vector<t>& values)
    {
        const size_t size = values.size();
        *this << size;
        for (const auto& value : values) {
            *this << value;
        }
        return *this;
    }

    template <typename t>
    BinaryStream& operator>>(t& value)
    {
        stream_.read(reinterpret_cast<char*>(&value), sizeof(t));
        return *this;
    }

    template <typename t>
    BinaryStream& operator>>(std::vector<t>& values)
    {
        size_t size = 0;
        *this >> size;
        values.resize(size);
        for (auto& value : values) {
            *this >> value;
        }
        return *this;
    }

    void dump(std::vector<char>& result)
    {
        const std::string buffer = stream_.str();
        result.assign(buffer.begin(), buffer.end());
    }

private:
    std::stringstream stream_;
};

uint32_t GetNhrStepNum(uint32_t rankSize)
{
    uint32_t stepNum = 0;
    for (uint32_t value = rankSize - 1; value != 0; value >>= 1, ++stepNum) {
    }
    return stepNum;
}

void AddPeer(uint32_t peerRank, std::vector<uint32_t>& peerRanks, std::map<uint32_t, uint32_t>& rank2ChannelIdx)
{
    if (rank2ChannelIdx.count(peerRank) != 0) {
        return;
    }
    rank2ChannelIdx[peerRank] = static_cast<uint32_t>(peerRanks.size());
    peerRanks.push_back(peerRank);
}

HcclResult BuildNhrSchedule(
    uint32_t myRank, uint32_t rankSize, std::vector<NhrStepInfo>& steps, std::vector<uint32_t>& peerRanks,
    std::map<uint32_t, uint32_t>& rank2ChannelIdx)
{
    if (rankSize < 2 || myRank >= rankSize) {
        return HCCL_E_PARA;
    }

    const uint32_t stepNum = GetNhrStepNum(rankSize);
    for (uint32_t step = 0; step < stepNum; ++step) {
        NhrStepInfo stepInfo;
        stepInfo.step = step;

        const uint32_t deltaRank = 1U << (stepNum - 1 - step);
        stepInfo.toRank = (myRank + deltaRank) % rankSize;
        stepInfo.fromRank = (myRank + rankSize - deltaRank) % rankSize;

        const uint32_t sliceNumerator = rankSize - 1 + deltaRank;
        const uint32_t sliceDenominator = 1U << (stepNum - step);
        const uint32_t sliceNum = sliceNumerator / sliceDenominator;
        const uint32_t deltaSliceIndex = 1U << (stepNum - step);
        uint32_t txSliceIdx = myRank;
        for (uint32_t index = 0; index < sliceNum; ++index) {
            stepInfo.txSliceIdxs.push_back(txSliceIdx);
            txSliceIdx = (txSliceIdx + rankSize - deltaSliceIndex) % rankSize;
        }
        steps.push_back(stepInfo);

        AddPeer(stepInfo.fromRank, peerRanks, rank2ChannelIdx);
        AddPeer(stepInfo.toRank, peerRanks, rank2ChannelIdx);
    }

    std::ostringstream output;
    output << "rank " << myRank << " NHR schedule:";
    for (const auto& step : steps) {
        output << " step" << step.step << "(to=" << step.toRank << ",from=" << step.fromRank << ",tx=[";
        for (uint32_t index = 0; index < step.txSliceIdxs.size(); ++index) {
            if (index != 0) {
                output << ",";
            }
            output << step.txSliceIdxs[index];
        }
        output << "])";
    }
    std::cout << output.str() << std::endl;
    return HCCL_SUCCESS;
}

HcclResult GetChannelForCcu(
    HcclComm comm, uint32_t myRank, const std::vector<uint32_t>& peerRanks, std::vector<ChannelHandle>& kernelChannels,
    uint32_t& kernelDie)
{
    const uint32_t channel_num = static_cast<uint32_t>(peerRanks.size());
    std::vector<HcclChannelDesc> channelDescs(channel_num);
    kernelChannels.resize(channel_num);

    uint32_t* netLayers = nullptr;
    uint32_t netLayerNum = 0;
    RETURN_IF_HCCL_FAIL(HcclRankGraphGetLayers(comm, &netLayers, &netLayerNum));

    bool hasKernelDie = false;
    for (uint32_t channelIndex = 0; channelIndex < channel_num; ++channelIndex) {
        const uint32_t remoteRank = peerRanks[channelIndex];
        HcclChannelDesc& desc = channelDescs[channelIndex];
        RETURN_IF_HCCL_FAIL(HcclChannelDescInit(&desc, 1));

        bool found = false;
        for (uint32_t layerIndex = 0; layerIndex < netLayerNum && !found; ++layerIndex) {
            uint32_t listSize = 0;
            CommLink* linkList = nullptr;
            RETURN_IF_HCCL_FAIL(
                HcclRankGraphGetLinks(comm, netLayers[layerIndex], myRank, remoteRank, &linkList, &listSize));
            for (uint32_t linkIndex = 0; linkIndex < listSize; ++linkIndex) {
                const CommLink& link = linkList[linkIndex];
                if (link.linkAttr.linkProtocol != CommProtocol::COMM_PROTOCOL_UBC_CTP) {
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
                RETURN_IF_HCCL_FAIL(asccomm_examples::QueryEndpointDie(comm, myRank, link.srcEndpointDesc, channelDie));
                RETURN_IF_HCCL_FAIL(asccomm_examples::MergeKernelDie(channelDie, hasKernelDie, kernelDie));
                found = true;
                break;
            }
        }
        if (!found) {
            std::cerr << "no UBC_CTP link from rank " << myRank << " to rank " << remoteRank << "\n";
            return HCCL_E_NOT_FOUND;
        }
    }

    RETURN_IF_HCCL_FAIL(
        HcclChannelAcquire(comm, CommEngine::COMM_ENGINE_CCU, channelDescs.data(), channel_num, kernelChannels.data()));
    return HCCL_SUCCESS;
}

HcclResult RegisterCcuKernel(
    HcclComm comm, uint32_t myRank, uint32_t rankSize, const std::vector<ChannelHandle>& channels,
    const std::vector<NhrStepInfo>& steps, const std::map<uint32_t, uint32_t>& rank2ChannelIdx, uint32_t kernelDie,
    AlgResourceCtx& resCtx)
{
    auto kernel_arg = std::make_shared<CcuKernelArgAllGatherNhr1DMem2Mem>();
    kernel_arg->rankSize = rankSize;
    kernel_arg->rankId = myRank;
    kernel_arg->stepInfoVector = steps;
    kernel_arg->rank2ChannelIdx = rank2ChannelIdx;
    for (uint32_t index = 0; index < channels.size(); ++index) {
        kernel_arg->channels[index] = channels[index];
    }
    kernel_arg->channelCount = static_cast<uint32_t>(channels.size());

    resCtx.ccuKernels.resize(1);
    CcuInsHandle ins_handle{0};
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuInstance(comm, ins_handle));
    HcommCcuRegisterContextHandle context_{nullptr};
    RETURN_IF_HCCL_FAIL(asccomm_examples::QueryCcuRegisterContext(comm, context_));
    RETURN_IF_HCCL_FAIL(ConvertCcuToHccl(asccomm_ccu_kernel_register_start(ins_handle, context_)));

    ccu_kernel_handle kernelHandle = 0;
    const void* kernel_args[] = {kernel_arg.get()};
    constexpr uint32_t kernel_arg_num = 1;
    CcuResult reg_ret = asccomm_ccu_kernel_register(
        ins_handle, kernelDie, "CcuAllGatherNhr1DMem2MemKernel",
        reinterpret_cast<void*>(CcuAllGatherNhr1DMem2MemKernel), kernel_args, kernel_arg_num, &kernelHandle);

    CcuResult regEndRet = asccomm_ccu_kernel_register_end(ins_handle);
    RETURN_IF_HCCL_FAIL(ConvertCcuToHccl(reg_ret));
    RETURN_IF_HCCL_FAIL(ConvertCcuToHccl(regEndRet));
    resCtx.ccuKernels[0] = kernelHandle;
    resCtx.ccuKernelNum = {1};
    return HCCL_SUCCESS;
}

HcclResult AllocAlgResource(
    HcclComm comm, uint32_t myRank, uint32_t rankSize, aclrtStream stream, AlgResourceCtx& resCtx)
{
    void* cclBufferAddr = nullptr;
    uint64_t cclBufferSize = 0;
    RETURN_IF_HCCL_FAIL(HcclGetHcclBuffer(comm, &cclBufferAddr, &cclBufferSize));
    resCtx.cclMem = CommBuffer{cclBufferAddr, cclBufferSize};

    ThreadHandle thread = 0;
    RETURN_IF_HCCL_FAIL(
        HcclThreadAcquireWithStream(comm, CommEngine::COMM_ENGINE_CCU, stream, resCtx.notifyNumOnMainThread, &thread));
    resCtx.threads.push_back(thread);

    if (rankSize == 1) {
        resCtx.ccuKernelNum = {0};
        return HCCL_SUCCESS;
    }

    std::vector<NhrStepInfo> steps;
    std::vector<uint32_t> peerRanks;
    std::map<uint32_t, uint32_t> rank2ChannelIdx;
    RETURN_IF_HCCL_FAIL(BuildNhrSchedule(myRank, rankSize, steps, peerRanks, rank2ChannelIdx));

    uint32_t kernelDie = 0;
    RETURN_IF_HCCL_FAIL(GetChannelForCcu(comm, myRank, peerRanks, resCtx.kernelChannels, kernelDie));
    RETURN_IF_HCCL_FAIL(
        RegisterCcuKernel(comm, myRank, rankSize, resCtx.kernelChannels, steps, rank2ChannelIdx, kernelDie, resCtx));
    return HCCL_SUCCESS;
}

} // namespace

std::vector<char> AlgResourceCtx::Serialize() const
{
    BinaryStream stream;
    stream << cclMem;
    stream << notifyNumOnMainThread;
    stream << threads;
    stream << kernelChannels;
    stream << ccuKernelNum;
    stream << ccuKernels;

    std::vector<char> result;
    stream.dump(result);
    return result;
}

void AlgResourceCtx::Deserialize(std::vector<char>& data)
{
    BinaryStream stream(data);
    stream >> cclMem;
    stream >> notifyNumOnMainThread;
    stream >> threads;
    stream >> kernelChannels;
    stream >> ccuKernelNum;
    stream >> ccuKernels;
}

HcclResult ConvertCcuToHccl(CcuResult result)
{
    switch (result) {
        case CCU_SUCCESS:
            return HCCL_SUCCESS;
        case CCU_E_PARA:
            return HCCL_E_PARA;
        case CCU_E_PTR:
            return HCCL_E_PTR;
        case CCU_E_INTERNAL:
            return HCCL_E_INTERNAL;
        case CCU_E_NOT_SUPPORT:
            return HCCL_E_NOT_SUPPORT;
        case CCU_E_NOT_FOUND:
            return HCCL_E_NOT_FOUND;
        case CCU_E_UNAVAIL:
            return HCCL_E_UNAVAIL;
        default:
            return HCCL_E_INTERNAL;
    }
}

HcclResult InitAlgResourceCtx(
    HcclComm comm, const char* tag, uint32_t myRank, uint32_t rankSize, aclrtStream stream, AlgResourceCtx& resCtx)
{
    void* ctx = nullptr;
    uint64_t size = 0;
    if (HcclEngineCtxGet(comm, tag, CommEngine::COMM_ENGINE_CCU, &ctx, &size) == HCCL_SUCCESS) {
        std::vector<char> data(static_cast<char*>(ctx), static_cast<char*>(ctx) + size);
        resCtx.Deserialize(data);
        return HCCL_SUCCESS;
    }

    RETURN_IF_HCCL_FAIL(AllocAlgResource(comm, myRank, rankSize, stream, resCtx));
    const std::vector<char> data = resCtx.Serialize();
    void* newCtx = nullptr;
    RETURN_IF_HCCL_FAIL(HcclEngineCtxCreate(comm, tag, CommEngine::COMM_ENGINE_CCU, data.size(), &newCtx));
    std::memcpy(newCtx, data.data(), data.size());
    return HCCL_SUCCESS;
}

} // namespace CcuAgNhr1dMem2mem
