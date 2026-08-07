/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

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

extern "C" aclError LaunchUbmemDataCopy(void* dst, void* src, uint32_t byteSize, aclrtStream stream);

namespace {

constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr size_t CHANNEL_MEMORY_BYTES = 2UL * 1024UL * 1024UL;
constexpr uint32_t VERIFY_BYTES = 4UL * 1024UL;
constexpr uint8_t PATH_MODE_ONE = 2;
constexpr uint8_t PATH_MODE_MULTI = 3;
constexpr CommProtocol TARGET_PROTOCOL = COMM_PROTOCOL_UB_MEM;
constexpr uint32_t PATH_COUNT = 2;
constexpr time_t SOCKET_IO_TIMEOUT_SECONDS = 30;
constexpr uint8_t TARGET_PATH_MODES[PATH_COUNT] = {PATH_MODE_ONE, PATH_MODE_MULTI};
static_assert(VERIFY_BYTES % PATH_COUNT == 0, "VERIFY_BYTES must be divisible by PATH_COUNT");

struct Options {
    uint32_t rankSize = 0;
    uint32_t rank = 0;
    uint32_t device = 0;
    std::string ipPort;
};

struct CommResources {
    HcclComm comm = nullptr;
    void* channelMemory = nullptr;
    aclrtStream streams[PATH_COUNT] = {nullptr, nullptr};
    void* verifyBuffer = nullptr;
};

int ExchangeRootInfo(uint32_t rank, uint32_t rankSize, const std::string& ipPort, HcclRootInfo& rootInfo);
int ReportError(uint32_t rank, const char* operation, int result);
int BuildChannelDescs(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle& memHandle, HcclChannelDesc* descs);
int TransferAndVerifyShards(
    uint32_t rank, uint32_t peerRank, HcclComm comm, const ChannelHandle* channels, CommResources& resources);
int CleanupCommRes(CommResources& resources, int status);

int TestDualPath(const Options& options, const HcclRootInfo& rootInfo)
{
    CommResources resources;
    HcclMemHandle memHandle = nullptr;
    std::vector<uint32_t> peerRanks;

    // Initialize the communication domain and prepare data in the local HCCL Buffer.
    int result = HcclCommInitRootInfo(options.rankSize, &rootInfo, options.rank, &resources.comm);
    if (result != HCCL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "HcclCommInitRootInfo", result));
    }

    void* localHcclBuffer = nullptr;
    uint64_t localHcclBufferSize = 0;
    result = HcclGetHcclBuffer(resources.comm, &localHcclBuffer, &localHcclBufferSize);
    if (result != HCCL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "HcclGetHcclBuffer", result));
    }
    if (localHcclBuffer == nullptr || localHcclBufferSize < VERIFY_BYTES) {
        std::cerr << "rank " << options.rank << ": local HCCL buffer is too small, size=" << localHcclBufferSize
                  << std::endl;
        return CleanupCommRes(resources, HCCL_E_INTERNAL);
    }
    result = aclrtMemset(localHcclBuffer, VERIFY_BYTES, static_cast<int32_t>(options.rank), VERIFY_BYTES);
    if (result != ACL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "aclrtMemset local HCCL buffer", result));
    }

    // Register the memory resource exchanged when the channels are created.
    result = aclrtMalloc(&resources.channelMemory, CHANNEL_MEMORY_BYTES, ACL_MEM_MALLOC_HUGE_FIRST);
    if (result != ACL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "aclrtMalloc channel memory", result));
    }
    result = aclrtMemset(resources.channelMemory, CHANNEL_MEMORY_BYTES, 0, CHANNEL_MEMORY_BYTES);
    if (result != ACL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "aclrtMemset channel memory", result));
    }
    std::string memoryTag = "link_select_rank_" + std::to_string(options.rank);
    CommMem channelMemory{COMM_MEM_TYPE_DEVICE, resources.channelMemory, CHANNEL_MEMORY_BYTES};
    result = HcclCommMemReg(resources.comm, memoryTag.c_str(), &channelMemory, &memHandle);
    if (result != HCCL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "HcclCommMemReg", result));
    }

    // Acquire all peer channels in one batch.
    peerRanks.reserve(options.rankSize - 1);
    std::vector<HcclChannelDesc> allChannelDescs;
    allChannelDescs.reserve((options.rankSize - 1) * PATH_COUNT);
    for (uint32_t peerRank = 0; peerRank < options.rankSize; ++peerRank) {
        if (peerRank == options.rank) {
            continue;
        }

        HcclChannelDesc peerChannelDescs[PATH_COUNT];
        result = BuildChannelDescs(resources.comm, options.rank, peerRank, memHandle, peerChannelDescs);
        if (result != HCCL_SUCCESS) {
            return CleanupCommRes(resources, result);
        }

        for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
            allChannelDescs.push_back(peerChannelDescs[pathIndex]);
        }
        peerRanks.push_back(peerRank);
    }
    const uint32_t channelDescNum = static_cast<uint32_t>(allChannelDescs.size());
    std::vector<ChannelHandle> channels(channelDescNum, 0);
    result =
        HcclChannelAcquire(resources.comm, COMM_ENGINE_AIV, allChannelDescs.data(), channelDescNum, channels.data());
    if (result != HCCL_SUCCESS) {
        std::cerr << "rank " << options.rank << ": HcclChannelAcquire failed, ret=" << result << std::endl;
        return CleanupCommRes(resources, result);
    }
    for (size_t peerIndex = 0; peerIndex < peerRanks.size(); ++peerIndex) {
        const uint32_t peerRank = peerRanks[peerIndex];
        const ChannelHandle* peerChannels = channels.data() + peerIndex * PATH_COUNT;
        if (peerChannels[0] == peerChannels[1]) {
            std::cerr << "rank " << options.rank << ": duplicate channel handles for rank " << peerRank << std::endl;
            return CleanupCommRes(resources, HCCL_E_INTERNAL);
        }
    }

    for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
        result = aclrtCreateStream(&resources.streams[pathIndex]);
        if (result != ACL_SUCCESS) {
            return CleanupCommRes(resources, ReportError(options.rank, "aclrtCreateStream", result));
        }
    }
    result = aclrtMalloc(&resources.verifyBuffer, VERIFY_BYTES, ACL_MEM_MALLOC_HUGE_FIRST);
    if (result != ACL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "aclrtMalloc verify buffer", result));
    }

    // Process peers serially; for each peer, submit both path shards before synchronizing either stream.
    for (size_t peerIndex = 0; peerIndex < peerRanks.size(); ++peerIndex) {
        const uint32_t peerRank = peerRanks[peerIndex];
        result = TransferAndVerifyShards(
            options.rank, peerRank, resources.comm, channels.data() + peerIndex * PATH_COUNT, resources);
        if (result != HCCL_SUCCESS) {
            return CleanupCommRes(resources, result);
        }
    }

    // Keep every rank's HCCL Buffer valid until all remote reads have completed.
    result = HcclBarrier(resources.comm, resources.streams[0]);
    if (result != HCCL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "HcclBarrier", result));
    }
    result = aclrtSynchronizeStream(resources.streams[0]);
    if (result != ACL_SUCCESS) {
        return CleanupCommRes(resources, ReportError(options.rank, "aclrtSynchronizeStream after HcclBarrier", result));
    }
    return CleanupCommRes(resources, HCCL_SUCCESS);
}

int ReportError(uint32_t rank, const char* operation, int result)
{
    std::cerr << "rank " << rank << ": " << operation << " failed, ret=" << result << std::endl;
    return result;
}

int BuildChannelDescs(HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle& memHandle, HcclChannelDesc* descs)
{
    uint32_t* layers = nullptr;
    uint32_t layerCount = 0;
    int result = HcclRankGraphGetLayers(comm, &layers, &layerCount);
    if (result != HCCL_SUCCESS) {
        return ReportError(rank, "HcclRankGraphGetLayers", result);
    }

    for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex) {
        CommTopo topo = COMM_TOPO_RESERVED;
        result = HcclRankGraphGetTopoTypeByLayer(comm, layers[layerIndex], &topo);
        if (result != HCCL_SUCCESS) {
            return ReportError(rank, "HcclRankGraphGetTopoTypeByLayer", result);
        }
        CommLink* links = nullptr;
        uint32_t linkCount = 0;
        result = HcclRankGraphGetLinks(comm, layers[layerIndex], rank, peerRank, &links, &linkCount);
        if (result != HCCL_SUCCESS) {
            return ReportError(rank, "HcclRankGraphGetLinks", result);
        }
        for (uint32_t linkIndex = 0; linkIndex < linkCount; ++linkIndex) {
            if (links[linkIndex].linkAttr.linkProtocol != TARGET_PROTOCOL) {
                continue;
            }
            const CommLink& link = links[linkIndex];
            for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
                HcclChannelDesc& desc = descs[pathIndex];
                result = HcclChannelDescInit(&desc, 1);
                if (result != HCCL_SUCCESS) {
                    return ReportError(rank, "HcclChannelDescInit", result);
                }
                desc.remoteRank = peerRank;
                desc.localEndpoint.protocol = link.srcEndpointDesc.protocol;
                desc.localEndpoint.commAddr = link.srcEndpointDesc.commAddr;
                desc.localEndpoint.loc = link.srcEndpointDesc.loc;
                desc.remoteEndpoint.protocol = link.dstEndpointDesc.protocol;
                desc.remoteEndpoint.commAddr = link.dstEndpointDesc.commAddr;
                desc.remoteEndpoint.loc = link.dstEndpointDesc.loc;
                desc.channelProtocol = link.linkAttr.linkProtocol;
                desc.notifyNum = CHANNEL_NOTIFY_NUM;
                desc.memHandles = &memHandle;
                desc.memHandleNum = 1;
                desc.ubMemAttr.pathMode = TARGET_PATH_MODES[pathIndex];
            }
            std::cout << "rank " << rank << ": peer=" << peerRank << ", rank_graph_topo=" << static_cast<int32_t>(topo)
                      << ", protocol=UB_MEM, path_modes=" << static_cast<uint32_t>(TARGET_PATH_MODES[0]) << "/"
                      << static_cast<uint32_t>(TARGET_PATH_MODES[1]) << std::endl;
            return HCCL_SUCCESS;
        }
    }

    std::cerr << "rank " << rank << ": no UB_MEM link found to rank " << peerRank << std::endl;
    return HCCL_E_NOT_FOUND;
}

int TransferAndVerifyShards(
    uint32_t rank, uint32_t peerRank, HcclComm comm, const ChannelHandle* channels, CommResources& resources)
{
    constexpr uint32_t SHARD_BYTES = VERIFY_BYTES / PATH_COUNT;
    void* remoteBuffers[PATH_COUNT] = {nullptr, nullptr};
    uint64_t remoteBufferSizes[PATH_COUNT] = {0, 0};
    for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
        int result = HcclChannelGetHcclBuffer(
            comm, channels[pathIndex], &remoteBuffers[pathIndex], &remoteBufferSizes[pathIndex]);
        if (result != HCCL_SUCCESS) {
            return ReportError(rank, "HcclChannelGetHcclBuffer", result);
        }
        const uint32_t shardOffset = pathIndex * SHARD_BYTES;
        const uint64_t requiredSize = static_cast<uint64_t>(shardOffset) + SHARD_BYTES;
        if (remoteBuffers[pathIndex] == nullptr || remoteBufferSizes[pathIndex] < requiredSize) {
            std::cerr << "rank " << rank << ": remote HCCL buffer from rank " << peerRank
                      << " is too small, path_mode=" << static_cast<uint32_t>(TARGET_PATH_MODES[pathIndex])
                      << ", size=" << remoteBufferSizes[pathIndex] << std::endl;
            return HCCL_E_INTERNAL;
        }
    }

    int result = aclrtMemset(resources.verifyBuffer, VERIFY_BYTES, 0, VERIFY_BYTES);
    if (result != ACL_SUCCESS) {
        return ReportError(rank, "aclrtMemset verify buffer", result);
    }

    auto* localBuffer = static_cast<uint8_t*>(resources.verifyBuffer);
    for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
        const uint32_t shardOffset = pathIndex * SHARD_BYTES;
        auto* remoteBuffer = static_cast<uint8_t*>(remoteBuffers[pathIndex]);
        result = LaunchUbmemDataCopy(
            localBuffer + shardOffset, remoteBuffer + shardOffset, SHARD_BYTES, resources.streams[pathIndex]);
        if (result != ACL_SUCCESS) {
            return ReportError(rank, "LaunchUbmemDataCopy", result);
        }
    }

    for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
        result = aclrtSynchronizeStream(resources.streams[pathIndex]);
        if (result != ACL_SUCCESS) {
            return ReportError(rank, "aclrtSynchronizeStream", result);
        }
    }

    std::vector<uint8_t> hostData(VERIFY_BYTES);
    result =
        aclrtMemcpy(hostData.data(), hostData.size(), resources.verifyBuffer, VERIFY_BYTES, ACL_MEMCPY_DEVICE_TO_HOST);
    if (result != ACL_SUCCESS) {
        return ReportError(rank, "aclrtMemcpy verify data", result);
    }

    const uint8_t expected = static_cast<uint8_t>(peerRank);
    for (size_t offset = 0; offset < hostData.size(); ++offset) {
        if (hostData[offset] != expected) {
            std::cerr << "rank " << rank << ": data verification failed for rank " << peerRank << ", offset=" << offset
                      << ", actual=" << static_cast<uint32_t>(hostData[offset])
                      << ", expected=" << static_cast<uint32_t>(expected) << std::endl;
            return HCCL_E_INTERNAL;
        }
    }

    std::cout << "rank " << rank << ": dual-path data copy from remote rank " << peerRank
              << " passed, bytes=" << VERIFY_BYTES << std::endl;
    return HCCL_SUCCESS;
}

void RecordCleanupError(const char* operation, int result, int& firstError)
{
    if (result == 0) {
        return;
    }

    std::cerr << operation << " failed during cleanup, ret=" << result << std::endl;
    if (firstError == 0) {
        firstError = result;
    }
}

int CleanupCommRes(CommResources& resources, int status)
{
    int cleanupStatus = 0;
    for (uint32_t pathIndex = 0; pathIndex < PATH_COUNT; ++pathIndex) {
        if (resources.streams[pathIndex] != nullptr) {
            const int cleanupResult = aclrtDestroyStream(resources.streams[pathIndex]);
            RecordCleanupError("aclrtDestroyStream", cleanupResult, cleanupStatus);
        }
    }

    bool commDestroyed = true;
    if (resources.comm != nullptr) {
        const int cleanupResult = HcclCommDestroy(resources.comm);
        commDestroyed = cleanupResult == HCCL_SUCCESS;
        RecordCleanupError("HcclCommDestroy", cleanupResult, cleanupStatus);
    }
    if (resources.verifyBuffer != nullptr) {
        const int cleanupResult = aclrtFree(resources.verifyBuffer);
        RecordCleanupError("aclrtFree verify buffer", cleanupResult, cleanupStatus);
    }
    if (resources.channelMemory != nullptr && commDestroyed) {
        const int cleanupResult = aclrtFree(resources.channelMemory);
        RecordCleanupError("aclrtFree channel memory", cleanupResult, cleanupStatus);
    }
    return status != HCCL_SUCCESS ? status : cleanupStatus;
}

bool ParseNonNegativeInt(const char* text, int& value)
{
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* parseEnd = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &parseEnd, 10);
    if (errno == ERANGE || parseEnd == text || *parseEnd != '\0' || parsed < 0 ||
        parsed > std::numeric_limits<int>::max()) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseIpPort(const std::string& ipPort, std::string& host, uint16_t& port)
{
    constexpr const char* TCP_PREFIX = "tcp://";
    std::string value = ipPort;
    if (value.compare(0, std::strlen(TCP_PREFIX), TCP_PREFIX) == 0) {
        value = value.substr(std::strlen(TCP_PREFIX));
    }

    size_t separator = value.rfind(':');
    if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        return false;
    }

    const std::string portText = value.substr(separator + 1);
    char* parseEnd = nullptr;
    errno = 0;
    const long parsedPort = std::strtol(portText.c_str(), &parseEnd, 10);
    if (errno == ERANGE || parseEnd == portText.c_str() || *parseEnd != '\0' || parsedPort <= 0 ||
        parsedPort > UINT16_MAX) {
        return false;
    }
    host = value.substr(0, separator);
    if (host.front() == '[') {
        if (host.size() < 3 || host.back() != ']') {
            return false;
        }
        host = host.substr(1, host.size() - 2);
    }
    port = static_cast<uint16_t>(parsedPort);
    return true;
}

bool SendAll(int fd, const void* data, size_t size)
{
    const char* buffer = static_cast<const char*>(data);
    size_t sent = 0;
    while (sent < size) {
        const ssize_t count = send(fd, buffer + sent, size - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            std::cerr << "send failed, errno=" << errno << std::endl;
            return false;
        }
        if (count == 0) {
            std::cerr << "send returned 0 before all data was sent" << std::endl;
            return false;
        }
        sent += static_cast<size_t>(count);
    }
    return true;
}

bool RecvAll(int fd, void* data, size_t size)
{
    char* buffer = static_cast<char*>(data);
    size_t received = 0;
    while (received < size) {
        const ssize_t count = recv(fd, buffer + received, size - received, 0);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            std::cerr << "recv failed, errno=" << errno << std::endl;
            return false;
        }
        if (count == 0) {
            std::cerr << "peer closed the connection before all data was received" << std::endl;
            return false;
        }
        received += static_cast<size_t>(count);
    }
    return true;
}

bool SetSocketTimeouts(int fd)
{
    timeval timeout{};
    timeout.tv_sec = SOCKET_IO_TIMEOUT_SECONDS;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        std::cerr << "setsockopt SO_RCVTIMEO failed, errno=" << errno << std::endl;
        return false;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0) {
        std::cerr << "setsockopt SO_SNDTIMEO failed, errno=" << errno << std::endl;
        return false;
    }
    return true;
}

int CreateListenSocket(const std::string& host, uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    const std::string portText = std::to_string(port);
    addrinfo* addresses = nullptr;
    const int resolveResult = getaddrinfo(host.c_str(), portText.c_str(), &hints, &addresses);
    if (resolveResult != 0) {
        std::cerr << "getaddrinfo for listen address failed: " << gai_strerror(resolveResult) << std::endl;
        return -1;
    }

    int listenFd = -1;
    int lastError = 0;
    for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) {
            lastError = errno;
            continue;
        }
        if (!SetSocketTimeouts(fd)) {
            lastError = errno;
            close(fd);
            continue;
        }
        int reuseAddress = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress, sizeof(reuseAddress)) != 0) {
            lastError = errno;
            close(fd);
            continue;
        }
        if (bind(fd, address->ai_addr, address->ai_addrlen) != 0 || listen(fd, 64) != 0) {
            lastError = errno;
            close(fd);
            continue;
        }
        listenFd = fd;
        break;
    }
    freeaddrinfo(addresses);
    if (listenFd < 0) {
        std::cerr << "listen on " << host << ":" << port << " failed, errno=" << lastError << std::endl;
    }
    return listenFd;
}

int ConnectToRoot(const std::string& host, uint16_t port)
{
    constexpr int RETRY_TIMES = 300;
    constexpr int RETRY_INTERVAL_MS = 100;

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const std::string portText = std::to_string(port);
    addrinfo* addresses = nullptr;
    const int resolveResult = getaddrinfo(host.c_str(), portText.c_str(), &hints, &addresses);
    if (resolveResult != 0) {
        std::cerr << "getaddrinfo for root address failed: " << gai_strerror(resolveResult) << std::endl;
        return -1;
    }

    for (int retry = 0; retry < RETRY_TIMES; ++retry) {
        for (const addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
            int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
            if (fd < 0) {
                continue;
            }
            if (!SetSocketTimeouts(fd)) {
                close(fd);
                continue;
            }
            if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
                freeaddrinfo(addresses);
                return fd;
            }
            close(fd);
        }
        if (retry + 1 < RETRY_TIMES) {
            std::this_thread::sleep_for(std::chrono::milliseconds(RETRY_INTERVAL_MS));
        }
    }
    freeaddrinfo(addresses);
    return -1;
}

int ExchangeRootInfo(uint32_t rank, uint32_t rankSize, const std::string& ipPort, HcclRootInfo& rootInfo)
{
    std::string host;
    uint16_t port = 0;
    if (!ParseIpPort(ipPort, host, port)) {
        std::cerr << "invalid ipport: " << ipPort << std::endl;
        return -1;
    }

    if (rank == 0) {
        int listenFd = CreateListenSocket(host, port);
        if (listenFd < 0) {
            return -1;
        }
        for (uint32_t peer = 1; peer < rankSize; ++peer) {
            int connectionFd = -1;
            do {
                connectionFd = accept(listenFd, nullptr, nullptr);
            } while (connectionFd < 0 && errno == EINTR);
            if (connectionFd < 0) {
                std::cerr << "accept failed, errno=" << errno << std::endl;
                close(listenFd);
                return -1;
            }
            if (!SetSocketTimeouts(connectionFd)) {
                close(connectionFd);
                close(listenFd);
                return -1;
            }
            if (!SendAll(connectionFd, &rootInfo, sizeof(rootInfo))) {
                std::cerr << "send root info failed" << std::endl;
                close(connectionFd);
                close(listenFd);
                return -1;
            }
            close(connectionFd);
        }
        close(listenFd);
        return 0;
    }

    int fd = ConnectToRoot(host, port);
    if (fd < 0) {
        std::cerr << "connect ipport failed: " << ipPort << std::endl;
        return -1;
    }
    if (!RecvAll(fd, &rootInfo, sizeof(rootInfo))) {
        std::cerr << "recv root info failed" << std::endl;
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 5) {
        std::cerr << "invalid arguments" << std::endl;
        return 1;
    }

    int rankSize = 0;
    int rank = 0;
    int device = 0;
    if (!ParseNonNegativeInt(argv[1], rankSize) || !ParseNonNegativeInt(argv[2], rank) ||
        !ParseNonNegativeInt(argv[4], device) || rankSize < 2 || rank >= rankSize) {
        std::cerr << "invalid arguments: rank_size must be at least 2, rank must be in [0, rank_size), and device "
                     "must be a non-negative integer"
                  << std::endl;
        return 1;
    }
    Options options{
        static_cast<uint32_t>(rankSize), static_cast<uint32_t>(rank), static_cast<uint32_t>(device), argv[3]};

    ACLCHECK(aclInit(nullptr));
    uint32_t deviceCount = 0;
    ACLCHECK(aclrtGetDeviceCount(&deviceCount));
    if (options.device >= deviceCount) {
        std::cerr << "device " << options.device << " is out of range, device_count=" << deviceCount << std::endl;
        return 1;
    }
    ACLCHECK(aclrtSetDevice(static_cast<int32_t>(options.device)));

    HcclRootInfo rootInfo{};
    if (options.rank == 0) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
    }

    int status = HCCL_E_INTERNAL;
    if (ExchangeRootInfo(options.rank, options.rankSize, options.ipPort, rootInfo) == 0) {
        std::cout << "rank " << options.rank << "/" << options.rankSize << ": device=" << options.device
                  << ", channels_per_peer=" << PATH_COUNT << ", channel_descs=" << (options.rankSize - 1) * PATH_COUNT
                  << std::endl;
        status = TestDualPath(options, rootInfo);
        if (status == HCCL_SUCCESS) {
            std::cout << "rank " << options.rank << ": dual-path validation passed" << std::endl;
        }
    }

    int cleanupStatus = 0;
    RecordCleanupError("aclrtResetDevice", aclrtResetDevice(static_cast<int32_t>(options.device)), cleanupStatus);
    RecordCleanupError("aclFinalize", aclFinalize(), cleanupStatus);
    if (status == HCCL_SUCCESS) {
        status = cleanupStatus;
    }
    return status == HCCL_SUCCESS ? 0 : 1;
}
