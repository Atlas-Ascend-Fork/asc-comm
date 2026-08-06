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
 * \file main.cpp
 * \brief Hcomm SIMT Notify/Atomic样例Host侧流程：通信域创建、内存注册、P2P通道创建与结果校验
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
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"
#include "hccl/hccl_rank_graph.h"

#include "simt_notify_atomic_common.h"

#define ACLCHECK(ret)                                                                            \
    do {                                                                                         \
        if ((ret) != ACL_SUCCESS) {                                                              \
            printf("acl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                        \
        }                                                                                        \
    } while (0)

#define HCCLCHECK(ret)                                                                            \
    do {                                                                                          \
        if ((ret) != HCCL_SUCCESS) {                                                              \
            printf("hccl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                         \
        }                                                                                         \
    } while (0)

extern void LaunchSimtNotifyPerf(void* stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup);

namespace {

using namespace simt_notify_atomic;

constexpr uint32_t kBarrierWaitSeconds = 60;
constexpr uint32_t kBarrierPollMs = 100;

// SIMT Notify/Atomic只在UBC_CTP/URMA协议上实现，其余链路协议无对应实现。
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;

// 两块内存注册到通信域时使用的tag。两个rank使用相同tag，远端内存才能按tag查回。
constexpr const char* kSendBufTag = "simtNotifyAtomicSendBuf";
constexpr const char* kRecvBufTag = "simtNotifyAtomicRecvBuf";

// Directory holding the barrier marker files. run.sh creates a fresh one per run so stale
// markers from an earlier run cannot let the barrier pass immediately.
std::string SyncDir()
{
    const char* dirEnv = std::getenv("SIMT_NOTIFY_ATOMIC_SYNC_DIR");
    return dirEnv != nullptr ? std::string(dirEnv) : std::string("/tmp/simt_notify_atomic_sync");
}

bool IsMarkerFileReady(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= 1;
}

HcclResult WriteMarkerFile(const std::string& path, const std::string& payload = "1")
{
    const std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        std::cout << "Open marker tmp file failed: " << tmpPath << std::endl;
        return HCCL_E_OPEN_FILE_FAILURE;
    }
    ofs << payload;
    ofs.close();
    if (!ofs) {
        std::cout << "Write marker file failed: " << tmpPath << std::endl;
        return HCCL_E_SYSCALL;
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        std::cout << "Rename marker file failed: " << tmpPath << " -> " << path << std::endl;
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

// Keeps the check deterministic: a rank must not read its recv buffer until the rank
// writing into it has finished. HCCL is already up at this point, but this example
// deliberately avoids issuing collectives so only WriteNbi touches the QP.
HcclResult HostFileBarrier(int rank, int nranks, const std::string& tag, const std::string& payload = "1")
{
    const std::string dir = SyncDir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        std::cout << "Create sync dir failed: " << dir << std::endl;
        return HCCL_E_SYSCALL;
    }
    const std::string markerPrefix = dir + "/" + tag + ".";
    HcclResult ret = WriteMarkerFile(markerPrefix + std::to_string(rank), payload);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000 / kBarrierPollMs;
    for (uint32_t poll = 0; poll < maxPollTimes; ++poll) {
        bool ready = true;
        for (int i = 0; i < nranks; ++i) {
            if (!IsMarkerFileReady(markerPrefix + std::to_string(i))) {
                ready = false;
                break;
            }
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    std::cout << "Wait host barrier timeout: " << tag << std::endl;
    return HCCL_E_TIMEOUT;
}

size_t AlignUp(size_t value, size_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

aclError InitBuffers(void* sendBuf, void* recvBuf, size_t bufferBytes)
{
    std::vector<uint8_t> send(bufferBytes, 0x5AU);
    std::vector<uint8_t> recv(bufferBytes, 0U);
    aclError ret = aclrtMemcpy(
        sendBuf, bufferBytes, send.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        return ret;
    }
    return aclrtMemcpy(
        recvBuf, bufferBytes, recv.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
}

// Builds the P2P channel to peerRank over kTargetCommProtocol and reports the index of the
// remote buffer tagged kRecvBufTag in the channel's remote buffer table.
//
// The kernel resolves buffer base addresses through AscendC::simt::LocalBufferAddr and
// RemoteBufferAddr, both of which take an index into the channel's buffer table. That table is
// built from the memHandles handed to HcclChannelAcquire below. Local table index 0 is reserved
// for the CCL buffer, so explicitly registered buffers start at index 1. The remote index is
// looked up by tag because the remote table may hold entries this example did not register.
HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle* memHandles, uint32_t memHandleNum,
    ChannelHandle& channel, uint32_t& remoteRecvIdx)
{
    HcclChannelDesc channelDesc;
    HCCLCHECK(HcclChannelDescInit(&channelDesc, 1));
    channelDesc.remoteRank = peerRank;
    channelDesc.channelProtocol = kTargetCommProtocol;
    channelDesc.notifyNum = 0;
    channelDesc.memHandles = memHandles;
    channelDesc.memHandleNum = memHandleNum;

    uint32_t* netLayers = nullptr;
    uint32_t netLayerNum = 0;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &netLayers, &netLayerNum));
    if (netLayerNum == 0 || netLayers == nullptr) {
        std::cout << "[rank " << rank << "] no net layer reported" << std::endl;
        return HCCL_E_INTERNAL;
    }

    CommLink* links = nullptr;
    uint32_t linkNum = 0;
    HCCLCHECK(HcclRankGraphGetLinks(comm, netLayers[0], rank, peerRank, &links, &linkNum));
    if (linkNum == 0 || links == nullptr) {
        std::cout << "[rank " << rank << "] no link to peer " << peerRank << std::endl;
        return HCCL_E_INTERNAL;
    }

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
        std::cout << "[rank " << rank << "] no UBC_CTP link to peer " << peerRank
                  << ", SIMT Notify/Atomic requires the URMA path" << std::endl;
        return HCCL_E_NOT_SUPPORT;
    }

    HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &channel));
    if (channel == 0) {
        std::cout << "[rank " << rank << "] channel acquire returned an invalid handle" << std::endl;
        return HCCL_E_INTERNAL;
    }

    uint32_t memNum = 0;
    CommMem* remoteMems = nullptr;
    char** memTags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &memTags));
    if (memNum == 0 || remoteMems == nullptr || memTags == nullptr) {
        std::cout << "[rank " << rank << "] no remote memory reported on the channel" << std::endl;
        return HCCL_E_INTERNAL;
    }
    for (uint32_t i = 0; i < memNum; ++i) {
        if (memTags[i] != nullptr && std::strcmp(memTags[i], kRecvBufTag) == 0) {
            remoteRecvIdx = i;
            return HCCL_SUCCESS;
        }
    }
    std::cout << "[rank " << rank << "] remote memory " << kRecvBufTag << " not found on the channel" << std::endl;
    return HCCL_E_INTERNAL;
}

bool CheckReceiver(void* recvBuf, uint32_t payloadBytes, size_t bufferBytes)
{
    std::vector<uint8_t> actual(bufferBytes, 0U);
    if (aclrtMemcpy(actual.data(), bufferBytes, recvBuf, bufferBytes,
        ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    for (uint32_t i = 0U; i < payloadBytes; ++i) {
        if (actual[i] != 0x5AU) {
            std::cout << "receiver payload byte " << i
                      << " actual=" << static_cast<uint32_t>(actual[i])
                      << " expected=90" << std::endl;
            return false;
        }
    }
    uint64_t signal = 0U;
    std::memcpy(&signal, actual.data() + AlignUp(payloadBytes, sizeof(uint64_t)), sizeof(signal));
    if (signal != kNotifySignal) {
        std::cout << "receiver signal actual=" << signal
                  << " expected=" << kNotifySignal << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 10) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <rank_table_file> simt notify"
                     " <iterations> <warmup> <payload_bytes> <lanes>"
                  << std::endl;
        return 1;
    }

    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const char* rankTableFile = argv[3];
    if (nranks < 2) {
        std::cout << "nranks must be at least 2." << std::endl;
        return 1;
    }

    if (std::strcmp(argv[4], "simt") != 0 || std::strcmp(argv[5], "notify") != 0) {
        std::cout << "This minimal benchmark currently supports only simt/notify" << std::endl;
        return 1;
    }
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const uint32_t laneCount = static_cast<uint32_t>(std::strtoul(argv[9], nullptr, 10));
    if (iterations == 0U || payloadBytes == 0U || laneCount != 1U) {
        std::cout << "iterations and payload_bytes must be positive; this minimal benchmark requires lanes=1"
                  << std::endl;
        return 1;
    }

    const size_t bufferBytes = AlignUp(payloadBytes, sizeof(uint64_t)) + sizeof(uint64_t);

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    HcclComm hcclComm = nullptr;
    HCCLCHECK(HcclCommInitClusterInfo(rankTableFile, rank, &hcclComm));

    void* sendBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes));

    // Both ranks register both buffers under the same tags, so the sender can look up the
    // receiver's recv buffer by tag and the buffer tables stay symmetric.
    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle memHandles[2] = {nullptr, nullptr};
    constexpr uint32_t kSendMemHandleIdx = 0U;
    constexpr uint32_t kLocalSendIdx = 1U;
    HCCLCHECK(HcclCommMemReg(hcclComm, kSendBufTag, &sendMem, &memHandles[kSendMemHandleIdx]));
    HCCLCHECK(HcclCommMemReg(hcclComm, kRecvBufTag, &recvMem, &memHandles[1]));

    const uint32_t peerRank =
        rank == kSenderRank ? static_cast<uint32_t>(kReceiverRank) : static_cast<uint32_t>(kSenderRank);
    ChannelHandle channel = 0;
    uint32_t remoteRecvIdx = 0;
    HCCLCHECK(
        AcquirePeerChannel(hcclComm, static_cast<uint32_t>(rank), peerRank, memHandles, 2U, channel, remoteRecvIdx));

    HCCLCHECK(HostFileBarrier(rank, nranks, "ready"));

    void* timingBuf = nullptr;
    constexpr size_t kTimingBytes = 3U * sizeof(uint64_t);
    bool senderDrainPass = true;
    if (rank == kSenderRank) {
        ACLCHECK(aclrtMalloc(&timingBuf, kTimingBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    }

    // Only kSenderRank drives traffic. Every other rank just holds its registered buffers
    // open so the sender's WQEs have a valid target.
    if (rank == kSenderRank) {
        std::cout << "[rank " << rank << "] 路径=SIMT"
                  << " 接口=notify"
                  << "，发送到 rank " << kReceiverRank << std::endl;
        uint64_t timing[3] = {};
        LaunchSimtNotifyPerf(
            stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf,
            payloadBytes, iterations, warmup);
        ACLCHECK(aclrtSynchronizeStream(stream));
        ACLCHECK(aclrtMemcpy(
            timing, kTimingBytes, timingBuf, kTimingBytes, ACL_MEMCPY_DEVICE_TO_HOST));
        senderDrainPass = static_cast<int64_t>(timing[1]) == 0;
        const double totalUs = static_cast<double>(timing[0]) / 1000.0;
        const double averageNs = static_cast<double>(timing[0]) / iterations;
        const double packetRate = totalUs == 0.0 ? 0.0 :
            static_cast<double>(iterations) / totalUs;
        const double issueBandwidth = averageNs == 0.0 ? 0.0 :
            static_cast<double>(payloadBytes) / averageNs;
        const uint32_t doorbellCount = iterations;
        std::cout << std::fixed << std::setprecision(6)
                  << "RESULT | Path="
                  << "SIMT-t1"
                  << " | API=notify"
                  << " | DataSize/B=" << payloadBytes
                  << " | Npus=" << nranks
                  << " | WqeCount=" << iterations
                  << " | DoorbellCount=" << doorbellCount
                  << " | Warmup=" << warmup
                  << " | TotalTime/us=" << totalUs
                  << " | AverageWqe/ns=" << averageNs
                  << " | PacketRate/Mpps=" << packetRate
                  << " | IssueBandwidth/GB/s=" << issueBandwidth
                  << " | DrainStatus=" << static_cast<int64_t>(timing[1])
                  << " | Completed=" << timing[2] << std::endl;
    }

    // Separates the sender's kernel from the receiver's read. kSenderRank only arrives here
    // after aclrtSynchronizeStream, so kReceiverRank cannot sample its recv buffer while the
    // WQEs are still in flight. Without this the receiver races ahead straight from the
    // "ready" barrier and reads the memset-zeroed buffer, which looks like nothing landed.
    HCCLCHECK(HostFileBarrier(rank, nranks, "sent"));

    bool pass = true;
    if (rank == kSenderRank) {
        pass = senderDrainPass;
    } else if (rank == kReceiverRank) {
        pass = CheckReceiver(recvBuf, payloadBytes, bufferBytes);
    }
    if (rank == kSenderRank || rank == kReceiverRank) {
        std::cout << "[rank " << rank << "] SIMT notify"
                  << " 性能用例" << (pass ? "通过" : "失败") << std::endl;
    }

    if (timingBuf != nullptr) {
        ACLCHECK(aclrtFree(timingBuf));
    }
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(hcclComm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
