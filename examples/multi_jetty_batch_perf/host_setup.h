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
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"
#include "multi_jetty_batch_perf_common.h"

namespace multi_jetty_batch_perf {
// Host-only diagnostics: flush before each potentially blocking call.
inline void TraceHostCall(const char* phase, const char* call, int line, int result = 0)
{
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (std::strcmp(phase, "END") == 0) {
        std::fprintf(stderr, "HOST_CALL | EpochMs=%lld | PID=%ld | Phase=END | Line=%d | Ret=%d | Call=%s\n",
            static_cast<long long>(milliseconds), static_cast<long>(getpid()), line, result, call);
    } else {
        std::fprintf(stderr, "HOST_CALL | EpochMs=%lld | PID=%ld | Phase=%s | Line=%d | Call=%s\n",
            static_cast<long long>(milliseconds), static_cast<long>(getpid()), phase, line, call);
    }
    std::fflush(stderr);
}
} // namespace multi_jetty_batch_perf

#define CHECK_ACL(expression)                                                                    \
    do {                                                                                         \
        multi_jetty_batch_perf::TraceHostCall("BEGIN", #expression, __LINE__);                  \
        const aclError result = (expression);                                                    \
        multi_jetty_batch_perf::TraceHostCall("END", #expression, __LINE__, result);            \
        if (result != ACL_SUCCESS) {                                                             \
            std::cerr << "ACL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result \
                      << std::endl;                                                              \
            return result;                                                                       \
        }                                                                                        \
    } while (0)

#define CHECK_HCCL(expression)                                                                    \
    do {                                                                                          \
        multi_jetty_batch_perf::TraceHostCall("BEGIN", #expression, __LINE__);                  \
        const HcclResult result = (expression);                                                   \
        multi_jetty_batch_perf::TraceHostCall("END", #expression, __LINE__, result);            \
        if (result != HCCL_SUCCESS) {                                                             \
            std::cerr << "HCCL failure at " << __FILE__ << ':' << __LINE__ << ", ret=" << result \
                      << std::endl;                                                               \
            return result;                                                                        \
        }                                                                                         \
    } while (0)


namespace multi_jetty_batch_perf {
constexpr uint32_t kBarrierWaitSeconds = 60U, kBarrierPollMilliseconds = 100U;
constexpr CommProtocol kProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendTag = "multiJettyBatchPerfSend";
constexpr const char* kRecvTag = "multiJettyBatchPerfRecv";
bool FileReady(const std::string& path, size_t minimumBytes = 1U)
{
    struct stat status {};
    return stat(path.c_str(), &status) == 0 && status.st_size >= static_cast<off_t>(minimumBytes);
}

HcclResult WriteRootInfo(const std::string& path, const HcclRootInfo& rootInfo)
{
    const std::string temporaryPath = path + ".tmp";
    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
    output.close();
    if (!output || std::rename(temporaryPath.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfo(const std::string& path, HcclRootInfo* rootInfo)
{
    const uint32_t maximumPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMilliseconds;
    for (uint32_t poll = 0U; poll < maximumPolls; ++poll) {
        if (FileReady(path, sizeof(*rootInfo))) {
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char*>(rootInfo), sizeof(*rootInfo));
            if (input.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
                return HCCL_SUCCESS;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMilliseconds));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitComm(uint32_t rank, uint32_t rankCount, const std::string& rootPath, HcclComm* comm)
{
    HcclRootInfo rootInfo {};
    HcclResult result = HCCL_SUCCESS;
    if (rank == kRootRank) {
        CHECK_HCCL(HcclGetRootInfo(&rootInfo));
        result = WriteRootInfo(rootPath, rootInfo);
    } else {
        result = ReadRootInfo(rootPath, &rootInfo);
    }
    if (result != HCCL_SUCCESS) {
        return result;
    }
    return HcclCommInitRootInfo(rankCount, &rootInfo, rank, comm);
}

std::string SyncDirectory()
{
    const char* value = std::getenv("MULTI_JETTY_BATCH_PERF_SYNC_DIR");
    return value == nullptr ? "/tmp/multi_jetty_batch_perf" : value;
}

HcclResult HostBarrier(uint32_t rank, uint32_t rankCount, const char* name)
{
    const std::string directory = SyncDirectory();
    if (mkdir(directory.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = directory + "/" + name + ".";
    std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
    marker << '1';
    marker.close();
    const uint32_t maximumPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMilliseconds;
    for (uint32_t poll = 0U; poll < maximumPolls; ++poll) {
        bool ready = true;
        for (uint32_t peer = 0U; peer < rankCount; ++peer) {
            ready = ready && FileReady(prefix + std::to_string(peer));
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMilliseconds));
    }
    for (uint32_t peer = 0U; peer < rankCount; ++peer) {
        const std::string path = prefix + std::to_string(peer);
        std::cerr << "HOST_BARRIER_TIMEOUT | Rank=" << rank << " | Barrier=" << name
                  << " | Peer=" << peer << " | Present=" << FileReady(path)
                  << " | Path=" << path << std::endl;
    }
    return HCCL_E_TIMEOUT;
}

HcclResult BuildChannelDesc(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, HcclChannelDesc& desc)
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
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles,
    std::vector<ChannelHandle>& channels, std::vector<uint64_t>& remoteRecvAddresses,
    std::vector<uint32_t>& remoteRecvIndices)
{
    const uint32_t channelCount = static_cast<uint32_t>(channels.size());
    if (channelCount == 0U || remoteRecvAddresses.size() != channelCount ||
        remoteRecvIndices.size() != channelCount) {
        return HCCL_E_PARA;
    }
    std::vector<HcclChannelDesc> descs(channelCount);
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(BuildChannelDesc(comm, rank, peer, handles, descs[channel]));
    }
    CHECK_HCCL(HcclChannelAcquire(
        comm, COMM_ENGINE_AIV, descs.data(), channelCount, channels.data()));
    for (uint32_t channel = 0U; channel < channelCount; ++channel) {
        CHECK_HCCL(ResolveRemoteRecv(
            comm, channels[channel], remoteRecvAddresses[channel], remoteRecvIndices[channel]));
    }
    return HCCL_SUCCESS;
}

} // namespace multi_jetty_batch_perf
