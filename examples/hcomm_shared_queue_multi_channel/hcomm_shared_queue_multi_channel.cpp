/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file hcomm_shared_queue_multi_channel.cpp
 * \brief Hcomm multi-channel shared-queue host control-plane sample
 */

#include <arpa/inet.h>
#include <dlfcn.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_comm.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"

#define HCCLCHECK(ret)                                                                          \
    do {                                                                                        \
        const HcclResult checkRet = (ret);                                                      \
        if (checkRet != HCCL_SUCCESS) {                                                         \
            std::fprintf(stderr, "%s:%d: %s return %d.\n", __FILE__, __LINE__, #ret, checkRet); \
            return checkRet;                                                                    \
        }                                                                                       \
    } while (0)

#define ACLCHECK(ret)                                                                           \
    do {                                                                                        \
        const aclError checkRet = (ret);                                                        \
        if (checkRet != ACL_SUCCESS) {                                                          \
            std::fprintf(stderr, "%s:%d: %s return %d.\n", __FILE__, __LINE__, #ret, checkRet); \
            return HCCL_E_RUNTIME;                                                              \
        }                                                                                       \
    } while (0)

struct PeerLink {
    uint32_t peerRank;
    HcclChannelDesc desc;
};

struct PeerChannel {
    uint32_t peerRank;
    ChannelHandle channel;
};

struct EndpointGroup {
    EndpointDesc localEndpoint;
    std::vector<PeerLink> peerLinks;
};

extern "C" void run_hcomm_shared_queue_multi_channel(
    ChannelHandle channel, uint8_t* localAddr, uint8_t* remoteAddr, uint8_t* remoteNotifyAddr, uint64_t bytes,
    aclrtStream stream);

namespace {

constexpr const char* RECV_MEM_TAG = "recv_buffer";
constexpr const char* SHARED_QUEUE_TAG_PREFIX = "hcomm_shared_queue_multi_channel";
constexpr const char* DEFAULT_ROOT_INFO_IP_PORT = "tcp://127.0.0.1:8899";
constexpr int ROOT_INFO_CONNECT_RETRY_TIMES = 300;
constexpr int ROOT_INFO_CONNECT_RETRY_INTERVAL_MS = 100;
constexpr uint64_t NOTIFY_VALUE = 1;
constexpr uint64_t NOTIFY_BYTES = sizeof(uint64_t);
constexpr uint32_t NOTIFY_WAIT_TIMEOUT_MS = 30000;
constexpr uint32_t NOTIFY_POLL_INTERVAL_MS = 1;
constexpr uint32_t TRANSFER_CYCLES = 2;

bool ParseIpPort(const std::string& ipPort, std::string* host, uint16_t* port)
{
    if (host == nullptr || port == nullptr) {
        return false;
    }

    constexpr const char* TCP_PREFIX = "tcp://";
    std::string value = ipPort;
    if (value.compare(0, std::strlen(TCP_PREFIX), TCP_PREFIX) == 0) {
        value = value.substr(std::strlen(TCP_PREFIX));
    }

    const std::size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        return false;
    }

    const std::string portText = value.substr(separator + 1);
    char* end = nullptr;
    errno = 0;
    const long parsedPort = std::strtol(portText.c_str(), &end, 10);
    if (errno != 0 || end == nullptr || *end != '\0' || parsedPort <= 0 || parsedPort > 65535) {
        return false;
    }

    *host = value.substr(0, separator);
    *port = static_cast<uint16_t>(parsedPort);
    return true;
}

bool SendAll(int fd, const void* data, std::size_t size)
{
    const auto* buffer = static_cast<const char*>(data);
    std::size_t sent = 0;
    while (sent < size) {
        const ssize_t count = send(fd, buffer + sent, size - sent, 0);
        if (count <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

bool RecvAll(int fd, void* data, std::size_t size)
{
    auto* buffer = static_cast<char*>(data);
    std::size_t received = 0;
    while (received < size) {
        const ssize_t count = recv(fd, buffer + received, size - received, 0);
        if (count <= 0) {
            return false;
        }
        received += static_cast<std::size_t>(count);
    }
    return true;
}

int CreateListenSocket(const std::string& host, uint16_t port)
{
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::fprintf(stderr, "socket failed, errno=%d.\n", errno);
        return -1;
    }

    int reuseAddress = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        address.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::fprintf(stderr, "bind %s:%u failed, errno=%d.\n", host.c_str(), port, errno);
        close(fd);
        return -1;
    }
    if (listen(fd, 64) != 0) {
        std::fprintf(stderr, "listen failed, errno=%d.\n", errno);
        close(fd);
        return -1;
    }
    return fd;
}

int ConnectToRoot(const std::string& host, uint16_t port)
{
    for (int retry = 0; retry < ROOT_INFO_CONNECT_RETRY_TIMES; ++retry) {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            return -1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
            hostent* hostEntry = gethostbyname(host.c_str());
            if (hostEntry == nullptr || hostEntry->h_addr_list == nullptr || hostEntry->h_addr_list[0] == nullptr) {
                close(fd);
                return -1;
            }
            std::memcpy(&address.sin_addr, hostEntry->h_addr_list[0], sizeof(address.sin_addr));
        }

        if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0) {
            return fd;
        }
        close(fd);
        std::this_thread::sleep_for(std::chrono::milliseconds(ROOT_INFO_CONNECT_RETRY_INTERVAL_MS));
    }
    return -1;
}

HcclResult ExchangeRootInfo(uint32_t rank, uint32_t rankSize, const std::string& ipPort, HcclRootInfo* rootInfo)
{
    if (rootInfo == nullptr) {
        return HCCL_E_PTR;
    }

    std::string host;
    uint16_t port = 0;
    if (!ParseIpPort(ipPort, &host, &port)) {
        std::fprintf(stderr, "invalid ipport: %s.\n", ipPort.c_str());
        return HCCL_E_PARA;
    }

    if (rank == 0) {
        const int listenFd = CreateListenSocket(host, port);
        if (listenFd < 0) {
            return HCCL_E_INTERNAL;
        }

        for (uint32_t peer = 1; peer < rankSize; ++peer) {
            const int connectionFd = accept(listenFd, nullptr, nullptr);
            if (connectionFd < 0) {
                std::fprintf(stderr, "accept failed, errno=%d.\n", errno);
                close(listenFd);
                return HCCL_E_INTERNAL;
            }
            if (!SendAll(connectionFd, rootInfo, sizeof(*rootInfo))) {
                std::fprintf(stderr, "send root info failed.\n");
                close(connectionFd);
                close(listenFd);
                return HCCL_E_INTERNAL;
            }
            close(connectionFd);
        }
        close(listenFd);
        return HCCL_SUCCESS;
    }

    const int fd = ConnectToRoot(host, port);
    if (fd < 0) {
        std::fprintf(stderr, "connect ipport failed: %s.\n", ipPort.c_str());
        return HCCL_E_INTERNAL;
    }
    if (!RecvAll(fd, rootInfo, sizeof(*rootInfo))) {
        std::fprintf(stderr, "receive root info failed.\n");
        close(fd);
        return HCCL_E_INTERNAL;
    }
    close(fd);
    return HCCL_SUCCESS;
}

const CommLink* FindUbcCtpLink(const CommLink* links, uint32_t linkNum)
{
    for (uint32_t i = 0; i < linkNum; ++i) {
        if (links[i].linkAttr.linkProtocol == COMM_PROTOCOL_UBC_CTP) {
            return &links[i];
        }
    }
    return nullptr;
}

HcclResult BuildPeerLink(
    uint32_t peerRank, const CommLink& link, HcclMemHandle* memHandles, uint32_t memHandleNum, PeerLink* peerLink)
{
    if (peerLink == nullptr) {
        return HCCL_E_PTR;
    }

    HcclChannelDesc desc;
    HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = link.linkAttr.linkProtocol;
    desc.localEndpoint = link.srcEndpointDesc;
    desc.remoteEndpoint = link.dstEndpointDesc;
    desc.memHandles = memHandles;
    desc.memHandleNum = memHandleNum;

    peerLink->peerRank = peerRank;
    peerLink->desc = desc;
    return HCCL_SUCCESS;
}

bool IsSameEndpoint(const EndpointDesc& lhs, const EndpointDesc& rhs)
{
    return lhs.protocol == rhs.protocol && lhs.commAddr.type == rhs.commAddr.type &&
           std::memcmp(lhs.commAddr.eid, rhs.commAddr.eid, sizeof(lhs.commAddr.eid)) == 0;
}

HcclResult FindNearestAllPeerLinks(
    HcclComm comm, HcclMemHandle* memHandles, uint32_t memHandleNum, std::vector<PeerLink>* peerLinks)
{
    if (peerLinks == nullptr) {
        return HCCL_E_PTR;
    }

    uint32_t selfRank = 0;
    HCCLCHECK(HcclGetRankId(comm, &selfRank));

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));

    std::unordered_set<uint32_t> addedPeers;
    for (uint32_t layerIndex = 0; layerIndex < layerNum; ++layerIndex) {
        uint32_t* ranks = nullptr;
        uint32_t rankNum = 0;
        HCCLCHECK(HcclRankGraphGetRanksByLayer(comm, layers[layerIndex], &ranks, &rankNum));

        for (uint32_t rankIndex = 0; rankIndex < rankNum; ++rankIndex) {
            const uint32_t peerRank = ranks[rankIndex];
            if (peerRank == selfRank || addedPeers.count(peerRank) != 0) {
                continue;
            }

            CommLink* links = nullptr;
            uint32_t linkNum = 0;
            HCCLCHECK(HcclRankGraphGetLinks(comm, layers[layerIndex], selfRank, peerRank, &links, &linkNum));

            const CommLink* link = FindUbcCtpLink(links, linkNum);
            if (link == nullptr) {
                continue;
            }

            PeerLink peerLink;
            HCCLCHECK(BuildPeerLink(peerRank, *link, memHandles, memHandleNum, &peerLink));
            peerLinks->push_back(peerLink);
            addedPeers.insert(peerRank);
        }
    }
    return HCCL_SUCCESS;
}

void GroupPeerLinks(const std::vector<PeerLink>& peerLinks, std::vector<EndpointGroup>* groups)
{
    for (const PeerLink& peerLink : peerLinks) {
        EndpointGroup* matchedGroup = nullptr;
        for (EndpointGroup& group : *groups) {
            if (IsSameEndpoint(group.localEndpoint, peerLink.desc.localEndpoint)) {
                matchedGroup = &group;
                break;
            }
        }
        if (matchedGroup == nullptr) {
            groups->push_back(EndpointGroup{peerLink.desc.localEndpoint, {}});
            matchedGroup = &groups->back();
        }
        matchedGroup->peerLinks.push_back(peerLink);
    }
}

HcclResult AcquireSharedChannels(
    HcclComm comm, const std::vector<HcclChannelDesc>& descs, const std::string& tag,
    std::vector<ChannelHandle>* channels)
{
    HcclChannelConfig config = nullptr;
    HcclResult ret = HcclChannelConfigCreate(&config);
    if (ret == HCCL_SUCCESS) {
        ret = HcclChannelConfigSetInt(config, HCCL_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE, 1);
    }
    if (ret == HCCL_SUCCESS) {
        ret = HcclChannelConfigSetStr(config, HCCL_CHANNEL_CONFIG_TYPE_SHARED_QUEUE_TAG, tag.c_str());
    }
    if (ret == HCCL_SUCCESS) {
        ret = HcclChannelAcquireWithConfig(
            comm, COMM_ENGINE_AIV, descs.data(), static_cast<uint32_t>(descs.size()), config, channels->data());
    }
    const HcclResult destroyRet = HcclChannelConfigDestroy(config);
    return ret != HCCL_SUCCESS ? ret : destroyRet;
}

HcclResult AcquireEndpointGroup(
    HcclComm comm, uint32_t selfRank, std::size_t groupIndex, const EndpointGroup& group,
    std::vector<PeerChannel>* peerChannels)
{
    std::vector<HcclChannelDesc> descs;
    descs.reserve(group.peerLinks.size());
    for (const PeerLink& peerLink : group.peerLinks) {
        descs.push_back(peerLink.desc);
    }

    const std::string tag =
        std::string(SHARED_QUEUE_TAG_PREFIX) + "_" + std::to_string(selfRank) + "_" + std::to_string(groupIndex);
    std::vector<ChannelHandle> channels(descs.size(), 0);
    HCCLCHECK(AcquireSharedChannels(comm, descs, tag, &channels));

    for (std::size_t i = 0; i < group.peerLinks.size(); ++i) {
        peerChannels->push_back(PeerChannel{group.peerLinks[i].peerRank, channels[i]});
    }
    return HCCL_SUCCESS;
}

HcclResult CreateSharedQueueMultiPeerChannels(
    HcclComm comm, HcclMemHandle* memHandles, uint32_t memHandleNum, std::vector<PeerChannel>* peerChannels)
{
    if (peerChannels == nullptr) {
        return HCCL_E_PTR;
    }

    std::vector<PeerLink> peerLinks;
    HCCLCHECK(FindNearestAllPeerLinks(comm, memHandles, memHandleNum, &peerLinks));
    if (peerLinks.empty()) {
        return HCCL_E_NOT_FOUND;
    }

    std::vector<EndpointGroup> groups;
    GroupPeerLinks(peerLinks, &groups);

    uint32_t selfRank = 0;
    HCCLCHECK(HcclGetRankId(comm, &selfRank));
    peerChannels->reserve(peerLinks.size());
    for (std::size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {
        HCCLCHECK(AcquireEndpointGroup(comm, selfRank, groupIndex, groups[groupIndex], peerChannels));
    }

    std::printf(
        "rank %u: created %zu channels in %zu shared-queue groups.\n", selfRank, peerChannels->size(), groups.size());
    return HCCL_SUCCESS;
}

HcclResult FillDeviceBuffer(void* buffer, uint64_t bytes, uint8_t value)
{
    std::vector<uint8_t> host(bytes, value);
    ACLCHECK(aclrtMemcpy(buffer, bytes, host.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE));
    return HCCL_SUCCESS;
}

HcclResult WaitForNotifyValue(void* notifyAddr, uint64_t expected)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(NOTIFY_WAIT_TIMEOUT_MS);
    uint64_t value = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        ACLCHECK(aclrtMemcpy(&value, sizeof(value), notifyAddr, sizeof(value), ACL_MEMCPY_DEVICE_TO_HOST));
        if (value >= expected) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(NOTIFY_POLL_INTERVAL_MS));
    }
    std::fprintf(
        stderr, "wait for incoming Hcomm notify timed out, expected=%llu, actual=%llu.\n",
        static_cast<unsigned long long>(expected), static_cast<unsigned long long>(value));
    return HCCL_E_TIMEOUT;
}

HcclResult VerifyDeviceBuffer(void* buffer, uint64_t bytes, uint8_t expected)
{
    std::vector<uint8_t> host(bytes, 0);
    ACLCHECK(aclrtMemcpy(host.data(), bytes, buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST));

    for (uint64_t i = 0; i < bytes; ++i) {
        if (host[i] != expected) {
            std::fprintf(
                stderr, "verify failed at byte %llu, expected %u, actual %u.\n", static_cast<unsigned long long>(i),
                expected, host[i]);
            return HCCL_E_INTERNAL;
        }
    }
    return HCCL_SUCCESS;
}

HcclResult GetRemoteMemByTag(
    HcclComm comm, ChannelHandle channel, const char* memTag, uint64_t minBytes, uint8_t** remoteAddr)
{
    if (remoteAddr == nullptr) {
        return HCCL_E_PTR;
    }
    *remoteAddr = nullptr;

    uint32_t memNum = 0;
    CommMem* remoteMems = nullptr;
    char** memTags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags));

    for (uint32_t i = 0; i < memNum; ++i) {
        if (memTags[i] != nullptr && std::strcmp(memTags[i], memTag) == 0) {
            if (remoteMems[i].addr == nullptr || remoteMems[i].size < minBytes) {
                return HCCL_E_PARA;
            }
            *remoteAddr = static_cast<uint8_t*>(remoteMems[i].addr);
            return HCCL_SUCCESS;
        }
    }

    std::fprintf(stderr, "remote mem tag %s not found.\n", memTag);
    return HCCL_E_NOT_FOUND;
}

const PeerChannel* FindPeerChannel(const std::vector<PeerChannel>& peerChannels, uint32_t peer)
{
    for (const PeerChannel& peerChannel : peerChannels) {
        if (peerChannel.peerRank == peer) {
            return &peerChannel;
        }
    }
    return nullptr;
}

uint8_t GetTransferValue(uint32_t rank, uint32_t round)
{
    return static_cast<uint8_t>((rank * 31U + round * 17U) % 255U + 1U);
}

using HcclBarrierFunc = HcclResult (*)(HcclComm, aclrtStream);

HcclBarrierFunc ResolveHcommBarrier()
{
    static void* hcommHandle = nullptr;
    static HcclBarrierFunc barrierFunc = nullptr;
    if (barrierFunc != nullptr) {
        return barrierFunc;
    }

    hcommHandle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
    if (hcommHandle == nullptr) {
        std::fprintf(stderr, "dlopen libhcomm.so failed: %s.\n", dlerror());
        return nullptr;
    }

    (void)dlerror();
    void* symbol = dlsym(hcommHandle, "HcclBarrier");
    const char* error = dlerror();
    if (error != nullptr || symbol == nullptr) {
        std::fprintf(
            stderr, "resolve HcclBarrier from libhcomm.so failed: %s.\n", error == nullptr ? "not found" : error);
        return nullptr;
    }

    barrierFunc = reinterpret_cast<HcclBarrierFunc>(symbol);
    Dl_info symbolInfo{};
    if (dladdr(symbol, &symbolInfo) != 0 && symbolInfo.dli_fname != nullptr) {
        std::printf("resolved HcclBarrier from %s.\n", symbolInfo.dli_fname);
        std::fflush(stdout);
    }
    return barrierFunc;
}

HcclResult RunHcclBarrier(HcclComm comm, aclrtStream stream, uint32_t rank, const std::string& location)
{
    HcclBarrierFunc barrierFunc = ResolveHcommBarrier();
    if (barrierFunc == nullptr) {
        return HCCL_E_NOT_FOUND;
    }

    std::printf("rank %u: enter HcclBarrier at %s.\n", rank, location.c_str());
    std::fflush(stdout);
    const HcclResult barrierRet = barrierFunc(comm, stream);
    if (barrierRet != HCCL_SUCCESS) {
        std::fprintf(stderr, "rank %u: HcclBarrier failed at %s, ret=%d.\n", rank, location.c_str(), barrierRet);
        return barrierRet;
    }

    std::printf("rank %u: HcclBarrier enqueued at %s.\n", rank, location.c_str());
    std::fflush(stdout);
    const aclError syncRet = aclrtSynchronizeStream(stream);
    if (syncRet != ACL_SUCCESS) {
        std::fprintf(
            stderr, "rank %u: HcclBarrier stream synchronization failed at %s, ret=%d.\n", rank, location.c_str(),
            syncRet);
        return HCCL_E_RUNTIME;
    }
    std::printf("rank %u: HcclBarrier completed at %s.\n", rank, location.c_str());
    std::fflush(stdout);
    return HCCL_SUCCESS;
}

HcclResult SynchronizeRanks(
    HcclComm comm, aclrtStream stream, uint32_t rank, uint32_t round, uint32_t totalRounds, const char* phase)
{
    const std::string location =
        "round=" + std::to_string(round + 1) + "/" + std::to_string(totalRounds) + ", phase=" + phase;
    return RunHcclBarrier(comm, stream, rank, location);
}

HcclResult RunAllPeerTransfers(
    HcclComm comm, uint32_t rankSize, uint32_t rank, const std::vector<PeerChannel>& peerChannels, uint8_t* sendAddr,
    uint8_t* recvAddr, uint8_t* localNotifyAddr, uint64_t remoteMemBytes, uint64_t bytes, aclrtStream stream)
{
    if (peerChannels.size() != rankSize - 1) {
        std::fprintf(
            stderr, "rank %u: expected %u peer channels, but created %zu.\n", rank, rankSize - 1, peerChannels.size());
        return HCCL_E_NOT_FOUND;
    }

    const uint32_t roundsPerCycle = rankSize - 1;
    const uint32_t totalRounds = TRANSFER_CYCLES * roundsPerCycle;
    for (uint32_t cycle = 0; cycle < TRANSFER_CYCLES; ++cycle) {
        for (uint32_t offset = 1; offset < rankSize; ++offset) {
            const uint32_t round = cycle * roundsPerCycle + offset - 1;
            const uint32_t sendPeer = (rank + offset) % rankSize;
            const uint32_t recvPeer = (rank + rankSize - offset) % rankSize;
            const PeerChannel* peerChannel = FindPeerChannel(peerChannels, sendPeer);
            if (peerChannel == nullptr) {
                std::fprintf(stderr, "rank %u: peer %u channel not found.\n", rank, sendPeer);
                return HCCL_E_NOT_FOUND;
            }

            const uint8_t sendValue = GetTransferValue(rank, round);
            const uint8_t expectedValue = GetTransferValue(recvPeer, round);
            HCCLCHECK(FillDeviceBuffer(sendAddr, bytes, sendValue));
            HCCLCHECK(FillDeviceBuffer(recvAddr, bytes, 0));
            ACLCHECK(aclrtMemset(localNotifyAddr, NOTIFY_BYTES, 0, NOTIFY_BYTES));
            HCCLCHECK(SynchronizeRanks(comm, stream, rank, round, totalRounds, "before-transfer"));

            uint8_t* remoteAddr = nullptr;
            HCCLCHECK(GetRemoteMemByTag(comm, peerChannel->channel, RECV_MEM_TAG, remoteMemBytes, &remoteAddr));
            uint8_t* remoteNotifyAddr = remoteAddr + remoteMemBytes - NOTIFY_BYTES;
            run_hcomm_shared_queue_multi_channel(
                peerChannel->channel, sendAddr, remoteAddr, remoteNotifyAddr, bytes, stream);
            ACLCHECK(aclrtSynchronizeStream(stream));
            HCCLCHECK(WaitForNotifyValue(localNotifyAddr, NOTIFY_VALUE));
            HCCLCHECK(VerifyDeviceBuffer(recvAddr, bytes, expectedValue));

            std::printf(
                "rank %u: round %u/%u pass, send peer %u, receive peer %u, channel %llu.\n", rank, round + 1,
                totalRounds, sendPeer, recvPeer, static_cast<unsigned long long>(peerChannel->channel));
            HCCLCHECK(SynchronizeRanks(comm, stream, rank, round, totalRounds, "after-transfer"));
        }
    }
    return HCCL_SUCCESS;
}

HcclResult RunSharedQueueMultiChannel(
    uint32_t rankSize, uint32_t rank, uint32_t device, uint64_t bytes, const std::string& ipPort)
{
    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(device)));

    HcclRootInfo rootInfo{};
    if (rank == 0) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
    }
    HCCLCHECK(ExchangeRootInfo(rank, rankSize, ipPort, &rootInfo));

    HcclCommConfig commConfig{};
    HcclCommConfigInit(&commConfig);

    HcclComm comm = nullptr;
    const HcclResult initRet = HcclCommInitRootInfoConfig(rankSize, &rootInfo, rank, &commConfig, &comm);
    if (initRet != HCCL_SUCCESS) {
        std::fprintf(stderr, "rank %u: HcclCommInitRootInfoConfig failed, ret=%d.\n", rank, initRet);
        return initRet;
    }

    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    const uint64_t alignedBytes = (bytes + NOTIFY_BYTES - 1) / NOTIFY_BYTES * NOTIFY_BYTES;
    const uint64_t remoteMemBytes = alignedBytes + NOTIFY_BYTES;
    const uint64_t commBufferBytes = alignedBytes + remoteMemBytes;
    void* commBufferAddr = nullptr;
    ACLCHECK(aclrtMalloc(&commBufferAddr, commBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));

    auto* sendAddr = static_cast<uint8_t*>(commBufferAddr);
    auto* recvAddr = sendAddr + alignedBytes;
    auto* recvNotifyAddr = recvAddr + alignedBytes;

    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvAddr, remoteMemBytes};
    HcclMemHandle recvMemHandle = nullptr;
    HCCLCHECK(HcclCommMemReg(comm, RECV_MEM_TAG, &recvMem, &recvMemHandle));

    std::vector<PeerChannel> peerChannels;
    HCCLCHECK(CreateSharedQueueMultiPeerChannels(comm, &recvMemHandle, 1, &peerChannels));

    HCCLCHECK(RunAllPeerTransfers(
        comm, rankSize, rank, peerChannels, sendAddr, recvAddr, recvNotifyAddr, remoteMemBytes, bytes, stream));

    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtFree(commBufferAddr));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclrtResetDevice(static_cast<int32_t>(device)));
    ACLCHECK(aclFinalize());

    std::printf(
        "rank %u pass, exercised %zu peer channels for %u cycles, bytes %llu.\n", rank, peerChannels.size(),
        TRANSFER_CYCLES, static_cast<unsigned long long>(bytes));
    return HCCL_SUCCESS;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 5 && argc != 6) {
        std::fprintf(stderr, "Usage: %s <rank_size> <rank> <device> <bytes> [ipport]\n", argv[0]);
        return 1;
    }

    int argIndex = 1;
    const int rankSizeArg = std::atoi(argv[argIndex++]);
    const int rankArg = std::atoi(argv[argIndex++]);
    const int deviceArg = std::atoi(argv[argIndex++]);
    const uint64_t bytes = static_cast<uint64_t>(std::strtoull(argv[argIndex++], nullptr, 10));
    const std::string ipPort = argc == 6 ? argv[argIndex] : DEFAULT_ROOT_INFO_IP_PORT;

    if (rankSizeArg < 2 || rankArg < 0 || rankArg >= rankSizeArg || deviceArg < 0 || bytes == 0) {
        std::fprintf(stderr, "invalid arguments.\n");
        return 1;
    }

    const HcclResult ret = RunSharedQueueMultiChannel(rankSizeArg, rankArg, deviceArg, bytes, ipPort);
    if (ret != HCCL_SUCCESS) {
        std::fprintf(stderr, "sample failed, ret %d.\n", ret);
        return static_cast<int>(ret);
    }
    return 0;
}
