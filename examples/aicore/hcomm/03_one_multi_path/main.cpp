/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "utils/check.h"
#include "utils/hccl_comm_init.h"
#include "utils/process_manager.h"
#include "utils/rank_sync.h"
#include "utils/arg_parser.h"

extern "C" aclError LaunchUbmemDataCopy(void* dst, void* src, uint32_t byteSize, aclrtStream stream);

namespace {

constexpr uint32_t CHANNEL_NOTIFY_NUM = 3;
constexpr size_t CHANNEL_MEMORY_BYTES = 2UL * 1024UL * 1024UL;
constexpr uint32_t VERIFY_BYTES = 4UL * 1024UL;
constexpr uint8_t PATH_MODE_ONE = 1;
constexpr uint8_t PATH_MODE_MULTI = 2;
constexpr CommProtocol TARGET_PROTOCOL = COMM_PROTOCOL_UB_MEM;
constexpr uint32_t PATH_COUNT = 2;
constexpr uint8_t TARGET_PATH_MODES[PATH_COUNT] = {PATH_MODE_ONE, PATH_MODE_MULTI};
static_assert(VERIFY_BYTES % PATH_COUNT == 0, "VERIFY_BYTES must be divisible by PATH_COUNT");
// 收尾barrier的tag：本样例只有这一个host侧同步点。
constexpr uint32_t kTagFinalRelease = examples::kTagUserBase;

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

int ReportError(uint32_t rank, const char* operation, int result);
int BuildChannelDescs(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle& memHandle, HcclChannelDesc* descs);
int TransferAndVerifyShards(
    uint32_t rank, uint32_t peerRank, HcclComm comm, const ChannelHandle* channels, CommResources& resources);
int CleanupCommRes(CommResources& resources, int status);

int TestDualPath(const Options& options, examples::RankSyncContext& sync)
{
    CommResources resources;
    HcclMemHandle memHandle = nullptr;
    std::vector<uint32_t> peerRanks;

    // Initialize the communication domain and prepare data in the local HCCL Buffer.
    int result = examples::InitCommByRootInfo(sync, &resources.comm);
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

    // Keep every rank's HCCL Buffer valid until all remote reads have completed. Each rank has
    // already synchronized its own streams above, so the host-side barrier suffices: once every
    // rank passes it, all remote reads in the group are done and freeing is safe.
    if (!sync.Barrier(kTagFinalRelease)) {
        return CleanupCommRes(resources, ReportError(options.rank, "Barrier", HCCL_E_INTERNAL));
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

} // namespace

// 单个rank的完整流程，单机与多机模式共用。由ProcessGroup（单机）或main的5参数分支
// （多机）调用。
int RunOneMultiPathRank(uint32_t rankSize, uint32_t rank, const std::string& ipPort, uint32_t device)
{
    Options options{rankSize, rank, device, ipPort};

    examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(ipPort, rankSize) :
                                                  examples::RankSyncContext::Connect(rank, rankSize, ipPort);
    if (!sync.Ok()) {
        std::cerr << "[rank " << rank << "] control channel setup failed" << std::endl;
        return 1;
    }

    EXAMPLES_ACLCHECK(aclInit(nullptr));
    uint32_t deviceCount = 0;
    EXAMPLES_ACLCHECK(aclrtGetDeviceCount(&deviceCount));
    if (options.device >= deviceCount) {
        std::cerr << "device " << options.device << " is out of range, device_count=" << deviceCount << std::endl;
        return 1;
    }
    EXAMPLES_ACLCHECK(aclrtSetDevice(static_cast<int32_t>(options.device)));

    int status = HCCL_E_INTERNAL;
    {
        std::cout << "[rank " << options.rank << "] one_multi_path | channels_per_peer=" << PATH_COUNT
                  << ", channel_descs=" << (options.rankSize - 1) * PATH_COUNT << std::endl;
        status = TestDualPath(options, sync);
        if (status == HCCL_SUCCESS) {
            std::cout << "[rank " << options.rank << "] one_multi_path | dual-path validation | PASS" << std::endl;
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

// 单机模式：one_multi_path <tcp://ip:port> <nranks>，父进程fork全部rank进程，
// device固定为rank对应的NPU。多机模式：one_multi_path <tcp://ip:port> <rank_size> <rank> <device>，
// 每台机器各自启动本机的rank进程，endpoint统一指向rank 0所在机器。
int main(int argc, char* argv[])
{
    // 单机：endpoint + nranks。
    if (argc == 3) {
        uint32_t uRankSize = 0;
        if (!examples::ParseUint32(argv[2], uRankSize, 2)) {
            std::cerr << "invalid nranks: " << argv[2] << ", expected an integer >= 2" << std::endl;
            return 1;
        }
        const std::string endpoint = argv[1];

        std::vector<examples::ArgsOf<decltype(RunOneMultiPathRank)>> perRankArgs;
        perRankArgs.reserve(uRankSize);
        for (uint32_t rank = 0; rank < uRankSize; ++rank) {
            perRankArgs.emplace_back(uRankSize, rank, endpoint, rank);
        }

        examples::ProcessGroup group;
        if (!group.Launch(uRankSize, RunOneMultiPathRank, std::move(perRankArgs))) {
            return 1;
        }
        const bool pass = group.WaitAll();
        if (group.Interrupted()) {
            std::cerr << "RESULT | Example=one_multi_path | Status=INTERRUPTED" << std::endl;
            return 1;
        }
        std::cout << "RESULT | Example=one_multi_path | Status=" << (pass ? "PASS" : "FAIL") << std::endl;
        return pass ? 0 : 1;
    }

    // 多机：每rank一进程，参数为 <endpoint> <rank_size> <rank> <device>。
    if (argc != 5) {
        std::cerr << "Usage: " << argv[0] << " <tcp://ip:port> <nranks>    (single machine)" << std::endl;
        std::cerr << "       " << argv[0] << " <tcp://ip:port> <rank_size> <rank> <device>    (per-rank, multi machine)"
                  << std::endl;
        return 1;
    }

    uint32_t uRankSize = 0;
    uint32_t uRank = 0;
    uint32_t uDevice = 0;
    if (!examples::ParseUint32(argv[2], uRankSize, 2) || !examples::ParseUint32(argv[3], uRank) ||
        !examples::ParseUint32(argv[4], uDevice) || uRank >= uRankSize) {
        std::cerr << "invalid arguments: rank_size must be at least 2, rank must be in [0, rank_size), and device "
                     "must be a non-negative integer"
                  << std::endl;
        return 1;
    }
    return RunOneMultiPathRank(uRankSize, uRank, argv[1], uDevice);
}
