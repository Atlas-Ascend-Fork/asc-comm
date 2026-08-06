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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>

#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "simt_notify_atomic_common.h"

#define ACLCHECK(ret)                                                                          \
    do {                                                                                       \
        if (ret != ACL_SUCCESS) {                                                              \
            std::printf("ACL error %s:%d, ret=%d\n", __FILE__, __LINE__, ret);                \
            return ret;                                                                        \
        }                                                                                      \
    } while (0)

#define HCCLCHECK(ret)                                                                         \
    do {                                                                                       \
        if (ret != HCCL_SUCCESS) {                                                             \
            std::printf("HCCL error %s:%d, ret=%d\n", __FILE__, __LINE__, ret);               \
            return ret;                                                                        \
        }                                                                                      \
    } while (0)

extern void LaunchSimtSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup);
extern void LaunchAivSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup);

namespace {
using namespace simt_notify_atomic;

constexpr uint32_t kWaitSeconds = 60U;
constexpr uint32_t kPollMs = 100U;
constexpr uint32_t kTimingItemCount = 5U;
constexpr uint32_t kPostCycles = 0U;
constexpr uint32_t kDrainStatus = 1U;
constexpr uint32_t kBurstWqes = 2U;
constexpr uint32_t kSqDepth = 3U;
constexpr uint32_t kCqDepth = 4U;
constexpr uint32_t kPerfModeAivWrite = 7U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char *kSendBufTag = "simtNotifyAtomicPerfSendBuf";
constexpr const char *kRecvBufTag = "simtNotifyAtomicPerfRecvBuf";

enum class PerfPath {
    SIMT,
    AIV,
};

bool ParsePath(const char *text, PerfPath &path)
{
    if (std::strcmp(text, "simt") == 0) {
        path = PerfPath::SIMT;
    } else if (std::strcmp(text, "aiv") == 0) {
        path = PerfPath::AIV;
    } else {
        return false;
    }
    return true;
}

bool ParseMode(const char *text, uint32_t &mode)
{
    if (std::strcmp(text, "notify") == 0) {
        mode = kModeNotify;
    } else if (std::strcmp(text, "faa") == 0) {
        mode = kModeFaa;
    } else if (std::strcmp(text, "cas") == 0) {
        mode = kModeCas;
    } else if (std::strcmp(text, "write") == 0) {
        mode = kPerfModeAivWrite;
    } else {
        return false;
    }
    return true;
}

const char *ModeName(uint32_t mode)
{
    if (mode == kPerfModeAivWrite) {
        return "write";
    }
    if (mode == kModeNotify) {
        return "notify";
    }
    return mode == kModeFaa ? "faa" : "cas";
}

bool FileReady(const std::string &path, size_t bytes)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(bytes);
}

HcclResult WriteBytes(const std::string &path, const void *data, size_t bytes)
{
    const std::string tmp = path + ".tmp";
    std::ofstream output(tmp, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(data), static_cast<std::streamsize>(bytes));
    output.close();
    if (!output || std::rename(tmp.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfo(const std::string &path, HcclRootInfo *rootInfo)
{
    const uint32_t maxPoll = kWaitSeconds * 1000U / kPollMs;
    for (uint32_t poll = 0U; poll < maxPoll; ++poll) {
        if (FileReady(path, sizeof(*rootInfo))) {
            std::ifstream input(path, std::ios::binary);
            input.read(reinterpret_cast<char *>(rootInfo), sizeof(*rootInfo));
            if (input.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
                return HCCL_SUCCESS;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitComm(int rank, int nranks, const std::string &rootInfoFile, HcclComm *comm)
{
    HcclRootInfo rootInfo {};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == 0) {
        ret = HcclGetRootInfo(&rootInfo);
        if (ret == HCCL_SUCCESS) {
            ret = WriteBytes(rootInfoFile, &rootInfo, sizeof(rootInfo));
        }
    } else {
        ret = ReadRootInfo(rootInfoFile, &rootInfo);
    }
    return ret == HCCL_SUCCESS ? HcclCommInitRootInfo(nranks, &rootInfo, rank, comm) : ret;
}

std::string SyncDir()
{
    const char *dir = std::getenv("ASC_COMM_SIMT_NOTIFY_ATOMIC_PERF_SYNC_DIR");
    return dir != nullptr ? std::string(dir) :
        std::string("/tmp/asc_comm_simt_notify_atomic_perf_sync");
}

HcclResult Barrier(int rank, int nranks, const std::string &tag)
{
    const std::string dir = SyncDir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = dir + "/" + tag + ".";
    const char marker = '1';
    HcclResult ret = WriteBytes(prefix + std::to_string(rank), &marker, 1U);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }

    const uint32_t maxPoll = kWaitSeconds * 1000U / kPollMs;
    for (uint32_t poll = 0U; poll < maxPoll; ++poll) {
        bool ready = true;
        for (int peer = 0; peer < nranks; ++peer) {
            ready = ready && FileReady(prefix + std::to_string(peer), 1U);
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kPollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult AcquirePeerChannel(HcclComm comm, uint32_t rank, uint32_t peerRank,
    HcclMemHandle *memHandles, uint32_t memHandleNum, ChannelHandle &channel,
    uint32_t &remoteRecvIdx)
{
    HcclChannelDesc channelDesc;
    HCCLCHECK(HcclChannelDescInit(&channelDesc, 1));
    channelDesc.remoteRank = peerRank;
    channelDesc.channelProtocol = kTargetCommProtocol;
    channelDesc.notifyNum = 0;
    channelDesc.memHandles = memHandles;
    channelDesc.memHandleNum = memHandleNum;

    uint32_t *netLayers = nullptr;
    uint32_t netLayerNum = 0;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &netLayers, &netLayerNum));
    if (netLayerNum == 0 || netLayers == nullptr) {
        return HCCL_E_INTERNAL;
    }

    CommLink *links = nullptr;
    uint32_t linkNum = 0;
    HCCLCHECK(HcclRankGraphGetLinks(comm, netLayers[0], rank, peerRank, &links, &linkNum));
    bool linkFound = false;
    for (uint32_t i = 0; i < linkNum; ++i) {
        if (links[i].linkAttr.linkProtocol == kTargetCommProtocol) {
            channelDesc.localEndpoint = links[i].srcEndpointDesc;
            channelDesc.remoteEndpoint = links[i].dstEndpointDesc;
            linkFound = true;
            break;
        }
    }
    if (!linkFound) {
        return HCCL_E_NOT_SUPPORT;
    }

    HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel));
    uint32_t memNum = 0;
    CommMem *remoteMems = nullptr;
    char **memTags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags));
    for (uint32_t i = 0; i < memNum; ++i) {
        if (memTags[i] != nullptr && std::strcmp(memTags[i], kRecvBufTag) == 0) {
            remoteRecvIdx = i;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

aclError InitBuffers(void *sendBuf, void *recvBuf, size_t bufferBytes, uint32_t mode)
{
    std::vector<uint8_t> send(bufferBytes, 0x5AU);
    std::vector<uint8_t> recv(bufferBytes, 0U);
    if (mode == kModeFaa || mode == kModeCas) {
        const uint64_t initial = mode == kModeFaa ? kFaaInitial : kCasInitial;
        std::memcpy(recv.data(), &initial, sizeof(initial));
    }
    aclError ret = aclrtMemcpy(
        sendBuf, bufferBytes, send.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ? aclrtMemcpy(recvBuf, bufferBytes, recv.data(),
        bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE) : ret;
}

aclError RunPerf(PerfPath path, void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timingBuf, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, bool &pass)
{
    if (path == PerfPath::SIMT) {
        LaunchSimtSubmitPerf(stream, channel, sendBufIdx, recvBufIdx, timingBuf,
            mode, payloadBytes, iterations, warmup);
    } else {
        LaunchAivSubmitPerf(stream, channel, sendBufIdx, recvBufIdx, timingBuf,
            mode, payloadBytes, iterations, warmup);
    }
    aclError ret = aclrtSynchronizeStream(stream);
    if (ret != ACL_SUCCESS) {
        return ret;
    }

    uint64_t timing[kTimingItemCount] = {0U};
    ret = aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing),
        ACL_MEMCPY_DEVICE_TO_HOST);
    if (ret != ACL_SUCCESS) {
        return ret;
    }

    const char *pathName = path == PerfPath::SIMT ? "SIMT" : "AIV-UB";
    if (timing[kDrainStatus] != 0U) {
        std::cout << "Drain失败 | 路径=" << pathName
                  << " | 接口=" << ModeName(mode)
                  << " | 数据(B)=" << payloadBytes
                  << " | 状态=" << timing[kDrainStatus] << std::endl;
        pass = false;
        return ACL_SUCCESS;
    }

    // Ascend 950 system counter runs at 1 GHz: one cycle is one nanosecond.
    const double averageNs = static_cast<double>(timing[kPostCycles]) / iterations;
    const double issueGbps = static_cast<double>(payloadBytes) / averageNs;
    std::cout << std::fixed << std::setprecision(3)
              << "RESULT | 路径=" << pathName
              << " | 接口=" << ModeName(mode)
              << " | 数据(B)=" << payloadBytes
              << " | 次数=" << iterations
              << " | SQDepth(BB)=" << timing[kSqDepth]
              << " | CQDepth(CQE)=" << timing[kCqDepth]
              << " | Burst(WQE)=" << timing[kBurstWqes]
              << " | 平均WQE下发(ns/次)=" << averageNs
              << " | Burst下发带宽(GB/s)=" << issueGbps << std::endl;
    return ACL_SUCCESS;
}
} // namespace

int main(int argc, char *argv[])
{
    if (argc != 9) {
        std::cout << "用法：" << argv[0]
                  << " <rank> <nranks> <root_info_file> <simt|aiv>"
                     " <notify|faa|cas|write> <iterations> <warmup> <payload_bytes>\n";
        return 1;
    }

    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    PerfPath path = PerfPath::SIMT;
    uint32_t mode = 0U;
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    if (nranks < 2 || !ParsePath(argv[4], path) || !ParseMode(argv[5], mode) ||
        iterations == 0U || payloadBytes == 0U ||
        (path == PerfPath::SIMT && mode == kPerfModeAivWrite) ||
        ((mode == kModeFaa || mode == kModeCas) && payloadBytes != sizeof(uint64_t))) {
        std::cout << "参数无效：卡数至少为2；FAA/CAS固定8B；write仅支持AIV路径。\n";
        return 1;
    }

    const size_t bufferBytes = AlignUp(payloadBytes, sizeof(uint64_t)) + sizeof(uint64_t);
    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    HcclComm hcclComm = nullptr;
    HCCLCHECK(InitComm(rank, nranks, argv[3], &hcclComm));

    void *sendBuf = nullptr;
    void *recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, mode));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle memHandles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(hcclComm, kSendBufTag, &sendMem, &memHandles[0]));
    HCCLCHECK(HcclCommMemReg(hcclComm, kRecvBufTag, &recvMem, &memHandles[1]));

    constexpr uint32_t kLocalSendIdx = 1U;
    const uint32_t peerRank = rank == kSenderRank ? static_cast<uint32_t>(kReceiverRank) :
        static_cast<uint32_t>(kSenderRank);
    ChannelHandle channel = 0;
    uint32_t remoteRecvIdx = 0;
    HCCLCHECK(AcquirePeerChannel(hcclComm, static_cast<uint32_t>(rank), peerRank,
        memHandles, 2U, channel, remoteRecvIdx));
    HCCLCHECK(Barrier(rank, nranks, "ready"));

    bool pass = true;
    if (rank == kSenderRank) {
        void *timingBuf = nullptr;
        ACLCHECK(aclrtMalloc(&timingBuf,
            kTimingItemCount * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(RunPerf(path, stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf,
            mode, payloadBytes, iterations, warmup, pass));
        ACLCHECK(aclrtFree(timingBuf));
    }

    HCCLCHECK(Barrier(rank, nranks, "completed"));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(hcclComm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
