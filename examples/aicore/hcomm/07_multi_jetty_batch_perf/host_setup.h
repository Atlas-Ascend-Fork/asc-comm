/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"
#include "multi_jetty_batch_perf_common.h"

#define CHECK_ACL(expression)                                                                                 \
    do {                                                                                                      \
        const aclError result = (expression);                                                                 \
        if (result != ACL_SUCCESS) {                                                                          \
            std::cerr << "ACL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result << std::endl; \
            return result;                                                                                    \
        }                                                                                                     \
    } while (0)

#define CHECK_HCCL(expression)                                                                                 \
    do {                                                                                                       \
        const HcclResult result = (expression);                                                                \
        if (result != HCCL_SUCCESS) {                                                                          \
            std::cerr << "HCCL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result << std::endl; \
            return result;                                                                                     \
        }                                                                                                      \
    } while (0)

namespace multi_jetty_batch_perf {
constexpr CommProtocol kProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendTag = "multiJettyBatchPerfSend";
constexpr const char* kRecvTag = "multiJettyBatchPerfRecv";

HcclResult BuildChannelDesc(HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, HcclChannelDesc& desc)
{
    CHECK_HCCL(HcclChannelDescInit(&desc, 1U));
    desc.remoteRank = peer;
    desc.channelProtocol = kProtocol;
    desc.notifyNum = 0U;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;
    uint32_t* layers = nullptr;
    uint32_t layerCount = 0U;
    CHECK_HCCL(HcclRankGraphGetLayers(comm, &layers, &layerCount));
    for (uint32_t layer = 0U; layer < layerCount; ++layer) {
        CommLink* links = nullptr;
        uint32_t linkCount = 0U;
        CHECK_HCCL(HcclRankGraphGetLinks(comm, layers[layer], rank, peer, &links, &linkCount));
        for (uint32_t link = 0U; link < linkCount; ++link) {
            if (links[link].linkAttr.linkProtocol == kProtocol) {
                desc.localEndpoint = links[link].srcEndpointDesc;
                desc.remoteEndpoint = links[link].dstEndpointDesc;
                return HCCL_SUCCESS;
            }
        }
    }
    return HCCL_E_NOT_SUPPORT;
}

HcclResult ResolveRemoteRecv(
    HcclComm comm, ChannelHandle channel, uint64_t& remoteRecvAddress, uint32_t& remoteRecvIndex)
{
    uint32_t memoryCount = 0U;
    CommMem* remoteMemories = nullptr;
    char** tags = nullptr;
    CHECK_HCCL(HcclChannelGetRemoteMems(comm, channel, &memoryCount, &remoteMemories, &tags));
    for (uint32_t memory = 0U; memory < memoryCount; ++memory) {
        if (tags[memory] != nullptr && std::strcmp(tags[memory], kRecvTag) == 0) {
            remoteRecvAddress = reinterpret_cast<uint64_t>(remoteMemories[memory].addr);
            remoteRecvIndex = memory;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

HcclResult AcquireChannels(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, std::vector<ChannelHandle>& channels,
    std::vector<uint64_t>& remoteRecvAddresses, std::vector<uint32_t>& remoteRecvIndices)
{
    const uint32_t channelCount = static_cast<uint32_t>(channels.size());
    if (channelCount == 0U || remoteRecvAddresses.size() != channelCount || remoteRecvIndices.size() != channelCount) {
        return HCCL_E_PARA;
    }
    std::vector<HcclChannelDesc> descs(channelCount);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(BuildChannelDesc(comm, rank, peer, handles, descs[channel]));
    }
    CHECK_HCCL(HcclChannelAcquire(comm, COMM_ENGINE_AIV, descs.data(), channelCount, channels.data()));
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(
            ResolveRemoteRecv(comm, channels[channel], remoteRecvAddresses[channel], remoteRecvIndices[channel]));
    }
    return HCCL_SUCCESS;
}

} // namespace multi_jetty_batch_perf
