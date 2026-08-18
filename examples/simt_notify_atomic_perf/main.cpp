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

#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "simt_notify_atomic_common.h"

#define ACLCHECK(ret)                                                                          \
    do {                                                                                       \
        if ((ret) != ACL_SUCCESS) {                                                            \
            std::printf("acl interface failed at %s:%d, ret=%d\n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                      \
        }                                                                                      \
    } while (0)

#define HCCLCHECK(ret)                                                                          \
    do {                                                                                        \
        if ((ret) != HCCL_SUCCESS) {                                                            \
            std::printf("hccl interface failed at %s:%d, ret=%d\n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                       \
        }                                                                                       \
    } while (0)

extern void LaunchSimtNotifyPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t laneCount);
extern void LaunchSimtFaaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup, uint32_t laneCount);
extern void LaunchSimtCasPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t iterations,
    uint32_t warmup, uint32_t laneCount);
extern void LaunchSimtLastCommitPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t api, uint32_t laneCount);

namespace {
using namespace simt_notify_atomic_perf;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtNotifyAtomicPerfSendBuf";
constexpr const char* kRecvBufTag = "simtNotifyAtomicPerfRecvBuf";

// Rank 0通过HcclGetRootInfo生成root info并写入文件，其余rank从文件读取后调用
// HcclCommInitRootInfo创建通信域，从而摆脱rank table依赖。
bool IsRootInfoFileReady(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(sizeof(HcclRootInfo));
}

HcclResult WriteRootInfoFile(const std::string& path, const HcclRootInfo& rootInfo)
{
    const std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cout << "Open root info tmp file failed: " << tmpPath << std::endl;
        return HCCL_E_OPEN_FILE_FAILURE;
    }
    ofs.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
    ofs.close();
    if (!ofs) {
        std::cout << "Write root info file failed: " << tmpPath << std::endl;
        return HCCL_E_SYSCALL;
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::cout << "Rename root info file failed: " << tmpPath << " -> " << path << std::endl;
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfoFile(const std::string& path, HcclRootInfo* rootInfo)
{
    const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t i = 0U; i < maxPollTimes; ++i) {
        if (!IsRootInfoFileReady(path)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
            continue;
        }
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            return HCCL_E_OPEN_FILE_FAILURE;
        }
        ifs.read(reinterpret_cast<char*>(rootInfo), sizeof(*rootInfo));
        if (ifs.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    std::cout << "Wait root info file timeout: " << path << std::endl;
    return HCCL_E_TIMEOUT;
}

HcclResult InitCommByRootInfo(uint32_t rank, uint32_t nranks, const std::string& rootInfoFile, HcclComm* comm)
{
    HcclRootInfo rootInfo{};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == 0U) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        ret = WriteRootInfoFile(rootInfoFile, rootInfo);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }
    } else {
        ret = ReadRootInfoFile(rootInfoFile, &rootInfo);
        if (ret != HCCL_SUCCESS) {
            return ret;
        }
    }
    HCCLCHECK(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
    return HCCL_SUCCESS;
}

std::string SyncDir()
{
    const char* value = std::getenv("SIMT_NOTIFY_ATOMIC_SYNC_DIR");
    return value == nullptr ? "/tmp/simt_notify_atomic_perf" : value;
}

bool MarkerReady(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size != 0;
}

HcclResult HostBarrier(int rank, int nranks, const char* name)
{
    const std::string dir = SyncDir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = dir + "/" + name + ".";
    {
        std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
        marker << '1';
        if (!marker) {
            return HCCL_E_SYSCALL;
        }
    }
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
        bool ready = true;
        for (int i = 0; i < nranks; ++i) {
            ready = ready && MarkerReady(prefix + std::to_string(i));
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    return HCCL_E_TIMEOUT;
}

size_t AlignUp(size_t value, size_t alignment) { return (value + alignment - 1U) / alignment * alignment; }

uint32_t LaneOperationCount(uint32_t total, uint32_t lane, uint32_t laneCount)
{
    return total <= lane ? 0U : (total - 1U - lane) / laneCount + 1U;
}

size_t BufferBytes(const std::string& api, uint32_t payloadBytes, uint32_t laneCount)
{
    if (api == "notify") {
        return (AlignUp(payloadBytes, sizeof(uint64_t)) + sizeof(uint64_t)) * laneCount;
    }
    return sizeof(uint64_t) * laneCount;
}

aclError InitBuffers(void* sendBuf, void* recvBuf, size_t bytes, const std::string& api, uint32_t laneCount)
{
    std::vector<uint8_t> send(bytes, api == "notify" ? 0x5AU : 0U);
    std::vector<uint8_t> recv(bytes, 0U);
    if (api == "faa" || api == "cas") {
        uint64_t initial = api == "faa" ? kFaaInitial : kCasInitial;
        for (uint32_t lane = 0U; lane < laneCount; ++lane) {
            std::memcpy(recv.data() + lane * sizeof(uint64_t), &initial, sizeof(initial));
        }
    }
    aclError ret = aclrtMemcpy(sendBuf, bytes, send.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ? aclrtMemcpy(recvBuf, bytes, recv.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE) : ret;
}

HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle* memHandles, ChannelHandle& channel,
    uint32_t& remoteRecvIdx)
{
    HcclChannelDesc desc;
    HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = kTargetCommProtocol;
    desc.notifyNum = 0;
    desc.memHandles = memHandles;
    desc.memHandleNum = 2U;

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0U;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));
    if (layerNum == 0U || layers == nullptr) {
        return HCCL_E_INTERNAL;
    }
    CommLink* links = nullptr;
    uint32_t linkNum = 0U;
    HCCLCHECK(HcclRankGraphGetLinks(comm, layers[0], rank, peerRank, &links, &linkNum));
    bool found = false;
    for (uint32_t i = 0U; i < linkNum; ++i) {
        if (links[i].linkAttr.linkProtocol == kTargetCommProtocol) {
            desc.localEndpoint = links[i].srcEndpointDesc;
            desc.remoteEndpoint = links[i].dstEndpointDesc;
            found = true;
            break;
        }
    }
    if (!found) {
        return HCCL_E_NOT_SUPPORT;
    }
    HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &desc, 1U, &channel));

    uint32_t memNum = 0U;
    CommMem* remoteMems = nullptr;
    char** tags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &tags));
    for (uint32_t i = 0U; i < memNum; ++i) {
        if (tags[i] != nullptr && std::strcmp(tags[i], kRecvBufTag) == 0) {
            remoteRecvIdx = i;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

bool CheckReceiver(
    void* recvBuf, size_t bytes, const std::string& api, uint32_t payloadBytes, uint32_t iterations, uint32_t warmup,
    uint32_t laneCount)
{
    std::vector<uint8_t> actual(bytes, 0U);
    if (aclrtMemcpy(actual.data(), bytes, recvBuf, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    if (api == "notify") {
        size_t stride = AlignUp(payloadBytes, sizeof(uint64_t)) + sizeof(uint64_t);
        for (uint32_t lane = 0U; lane < laneCount; ++lane) {
            uint32_t operationCount =
                LaneOperationCount(iterations, lane, laneCount) + LaneOperationCount(warmup, lane, laneCount);
            if (operationCount == 0U) {
                continue;
            }
            for (uint32_t i = 0U; i < payloadBytes; ++i) {
                if (actual[lane * stride + i] != 0x5AU) {
                    return false;
                }
            }
            uint64_t signal = 0U;
            std::memcpy(
                &signal, actual.data() + lane * stride + AlignUp(payloadBytes, sizeof(uint64_t)), sizeof(signal));
            if (signal != kNotifySignal) {
                return false;
            }
        }
        return true;
    }

    for (uint32_t lane = 0U; lane < laneCount; ++lane) {
        uint64_t value = 0U;
        std::memcpy(&value, actual.data() + lane * sizeof(uint64_t), sizeof(value));
        uint32_t operationCount =
            LaneOperationCount(iterations, lane, laneCount) + LaneOperationCount(warmup, lane, laneCount);
        uint64_t expected = api == "faa" ? kFaaInitial + static_cast<uint64_t>(operationCount) * kFaaAdd :
                                           (operationCount == 0U ? kCasInitial : kCasSwap);
        if (value != expected) {
            std::cout << "receiver lane " << lane << " actual=" << value << " expected=" << expected << std::endl;
            return false;
        }
    }
    return true;
}

bool ValidApi(const std::string& api) { return api == "notify" || api == "faa" || api == "cas"; }

uint32_t ApiIndex(const std::string& api) { return api == "notify" ? 0U : (api == "faa" ? 1U : 2U); }

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 10) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> <notify|faa|cas>"
                     " <iterations> <warmup> <payload_bytes> <lanes> <immediate|last>"
                  << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string api = argv[4];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t laneCount = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const std::string commitMode = argv[9];
    if (nranks < 2 || !ValidApi(api) || iterations == 0U || payloadBytes == 0U ||
        (laneCount != 1U && laneCount != 32U && laneCount != 1024U) ||
        (commitMode != "immediate" && commitMode != "last") || (commitMode == "immediate" && laneCount != 1U)) {
        std::cout << "Invalid argument." << std::endl;
        if (commitMode == "immediate" && laneCount != 1U) {
            std::cout << "Multiple lanes are supported only with --commit-mode last." << std::endl;
        }
        return 1;
    }
    const size_t bufferBytes = BufferBytes(api, payloadBytes, laneCount);
    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    HCCLCHECK(InitCommByRootInfo(static_cast<uint32_t>(rank), static_cast<uint32_t>(nranks), rootInfoFile, &comm));

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, api, laneCount));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));

    uint32_t peer = rank == kSenderRank ? kReceiverRank : kSenderRank;
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));

    void* timingBuf = nullptr;
    uint64_t timing[kTimingWords] = {};
    bool pass = true;
    constexpr uint32_t kLocalSendIdx = 1U;
    if (rank == kSenderRank) {
        ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        if (commitMode == "last") {
            LaunchSimtLastCommitPerf(
                stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, iterations, warmup,
                ApiIndex(api), laneCount);
        } else if (api == "notify") {
            LaunchSimtNotifyPerf(
                stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, iterations, warmup, laneCount);
        } else if (api == "faa") {
            LaunchSimtFaaPerf(stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, iterations, warmup, laneCount);
        } else {
            LaunchSimtCasPerf(stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, iterations, warmup, laneCount);
        }
        ACLCHECK(aclrtSynchronizeStream(stream));
        ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));
        pass = static_cast<int64_t>(timing[kDrainStatusIndex]) == 0 && timing[kCompletedIndex] == iterations;
        double issueUs = static_cast<double>(timing[kIssueCyclesIndex]) / 1000.0;
        double averageNs = static_cast<double>(timing[kIssueCyclesIndex]) / iterations;
        double completionUs = static_cast<double>(timing[kCompletionCyclesIndex]) / 1000.0;
        uint32_t dataBytes = api == "notify" ? payloadBytes : sizeof(uint64_t);
        double completionBandwidth = timing[kCompletionCyclesIndex] == 0U ?
                                         0.0 :
                                         static_cast<double>(dataBytes) * iterations / timing[kCompletionCyclesIndex];
        std::cout << std::fixed << std::setprecision(6) << "RESULT | Path=SIMT-t" << laneCount << " | API=" << api
                  << " | DataSize/B=" << dataBytes << " | WqeCount=" << iterations << " | Warmup=" << warmup
                  << " | CommitMode=" << (commitMode == "last" ? "Last" : "Immediate") << " | IssueTime/us=" << issueUs
                  << " | AverageIssue/ns=" << averageNs << " | CompletionTime/us=" << completionUs
                  << " | CompletionBandwidth/GB/s=" << completionBandwidth
                  << " | DrainStatus=" << static_cast<int64_t>(timing[kDrainStatusIndex])
                  << " | Completed=" << timing[kCompletedIndex] << std::endl;
    }

    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    if (rank == kReceiverRank) {
        pass = CheckReceiver(recvBuf, bufferBytes, api, payloadBytes, iterations, warmup, laneCount);
    }
    if (!pass && (rank == kSenderRank || rank == kReceiverRank)) {
        std::cout << "FAIL | Rank=" << rank << " | API=" << api << " | Path=SIMT-t" << laneCount << std::endl;
    }

    if (timingBuf != nullptr) {
        ACLCHECK(aclrtFree(timingBuf));
    }
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
