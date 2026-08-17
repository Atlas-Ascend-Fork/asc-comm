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

extern void LaunchSimtNotifyAtomic(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode);

namespace {

using namespace simt_notify_atomic;

constexpr uint32_t kBarrierWaitSeconds = 60;
constexpr uint32_t kBarrierPollMs = 100;

// SIMT Notify/Atomic只在UBC_CTP/URMA协议上实现，其余链路协议无对应实现。
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;

// 两块内存注册到通信域时使用的tag。两个rank使用相同tag，远端内存才能按tag查回。
constexpr const char* kSendBufTag = "simtNotifyAtomicSendBuf";
constexpr const char* kRecvBufTag = "simtNotifyAtomicRecvBuf";

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
    const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000 / kBarrierPollMs;
    for (uint32_t i = 0; i < maxPollTimes; ++i) {
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

// Unlike a defaulted environment variable, an explicit command-line argument that does not
// match any mode is a typo rather than a request for the default, so it is rejected.
bool ParseMode(const char* arg, uint32_t& mode)
{
    const std::string name(arg);
    if (name == "notify") {
        mode = kModeNotify;
        return true;
    }
    if (name == "faa") {
        mode = kModeFaa;
        return true;
    }
    if (name == "cas") {
        mode = kModeCas;
        return true;
    }
    if (name == "single") {
        mode = kModeSingle;
        return true;
    }
    if (name == "batch_last") {
        mode = kModeBatchLast;
        return true;
    }
    if (name == "multi_lane") {
        mode = kModeMultiLane;
        return true;
    }
    if (name == "notify_immediate_repeat") {
        mode = kModeNotifyImmediateRepeat;
        return true;
    }
    return false;
}

const char* ModeName(uint32_t mode)
{
    if (mode == kModeNotify) {
        return "notify";
    }
    if (mode == kModeFaa) {
        return "faa";
    }
    if (mode == kModeCas) {
        return "cas";
    }
    if (mode == kModeBatchLast) {
        return "batch_last";
    }
    if (mode == kModeMultiLane) {
        return "multi_lane";
    }
    if (mode == kModeNotifyImmediateRepeat) {
        return "notify_immediate_repeat";
    }
    return "single";
}

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

// Fills the sender's buffer with the values the receiver will check for, and clears the
// recv buffer so a slot that never lands reads back as 0 rather than as stale data.
// Both ranks call this: the sender needs the payload, the receiver needs the clean slate.
aclError InitBuffers(void* sendBuf, void* recvBuf, uint32_t mode)
{
    uint64_t send[kSlotCount] = {};
    uint64_t recv[kSlotCount] = {};
    send[kNotifyDataSlot] = kNotifyData;
    recv[kFaaTargetSlot] = kFaaInitial;
    recv[kCasTargetSlot] = kCasInitial;
    aclError ret = aclrtMemcpy(sendBuf, kBufferBytes, send, kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        return ret;
    }
    return aclrtMemcpy(recvBuf, kBufferBytes, recv, kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
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

bool CheckWords(void* deviceBuf, const uint64_t expected[kSlotCount], const char* side)
{
    uint64_t actual[kSlotCount] = {};
    if (aclrtMemcpy(actual, kBufferBytes, deviceBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    bool pass = true;
    for (uint32_t slot = 0; slot < kSlotCount; ++slot) {
        if (actual[slot] != expected[slot]) {
            pass = false;
            std::cout << side << " slot " << slot << " actual=" << actual[slot] << " expected=" << expected[slot]
                      << std::endl;
        }
    }
    return pass;
}

bool CheckSender(void* sendBuf, uint32_t mode)
{
    uint64_t expected[kSlotCount] = {};
    expected[kNotifyDataSlot] = kNotifyData;
    if (mode == kModeFaa) {
        expected[kFaaFetchSlot] = kFaaInitial;
    } else if (mode == kModeCas) {
        expected[kCasFetchSlot] = kCasInitial;
    } else if (mode == kModeSingle || mode == kModeBatchLast || mode == kModeMultiLane) {
        expected[kFaaFetchSlot] = kFaaInitial;
        expected[kCasFetchSlot] = kCasInitial;
    }
    return CheckWords(sendBuf, expected, "sender fetch/drain");
}

bool CheckReceiver(void* recvBuf, uint32_t mode)
{
    uint64_t expectedNotify[kSlotCount] = {};
    uint64_t expectedFaa[kSlotCount] = {};
    uint64_t expectedCas[kSlotCount] = {};
    uint64_t expectedCombined[kSlotCount] = {};
    uint64_t expectedMultiLane[kSlotCount] = {};
    expectedNotify[kNotifyDataSlot] = kNotifyData;
    expectedNotify[kNotifySignalSlot] = kNotifySignal;
    expectedNotify[kFaaTargetSlot] = kFaaInitial;
    expectedNotify[kCasTargetSlot] = kCasInitial;
    expectedFaa[kFaaTargetSlot] = kFaaInitial + kFaaAdd;
    expectedFaa[kCasTargetSlot] = kCasInitial;
    expectedCas[kFaaTargetSlot] = kFaaInitial;
    expectedCas[kCasTargetSlot] = kCasSwap;
    expectedCombined[kNotifyDataSlot] = kNotifyData;
    expectedCombined[kNotifySignalSlot] = kNotifySignal;
    expectedCombined[kFaaTargetSlot] = kFaaInitial + kFaaAdd;
    expectedCombined[kCasTargetSlot] = kCasSwap;
    for (uint32_t slot = 0U; slot < kSlotCount; ++slot) {
        expectedMultiLane[slot] = expectedCombined[slot];
    }
    expectedMultiLane[kFinalNotifyDataSlot] = kNotifyData;
    expectedMultiLane[kFinalNotifySignalSlot] = kFinalNotifySignal;
    if (mode == kModeNotify || mode == kModeNotifyImmediateRepeat) {
        return CheckWords(recvBuf, expectedNotify, "receiver target");
    }
    if (mode == kModeFaa) {
        return CheckWords(recvBuf, expectedFaa, "receiver target");
    }
    if (mode == kModeCas) {
        return CheckWords(recvBuf, expectedCas, "receiver target");
    }
    if (mode == kModeMultiLane) {
        return CheckWords(recvBuf, expectedMultiLane, "receiver target");
    }
    return CheckWords(recvBuf, expectedCombined, "receiver target");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <rank> <nranks> <root_info_file> [mode]" << std::endl;
        std::cout << "  mode: notify | faa | cas | single (default) | batch_last | multi_lane | notify_immediate_repeat"
                  << std::endl;
        return 1;
    }

    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    if (nranks < 2) {
        std::cout << "nranks must be at least 2." << std::endl;
        return 1;
    }

    uint32_t mode = kModeSingle;
    if (argc > 4 && !ParseMode(argv[4], mode)) {
        std::cout << "Unknown mode: " << argv[4] << std::endl;
        std::cout << "  mode: notify | faa | cas | single (default) | batch_last | multi_lane | notify_immediate_repeat"
                  << std::endl;
        return 1;
    }

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    HcclComm hcclComm = nullptr;
    HCCLCHECK(InitCommByRootInfo(static_cast<uint32_t>(rank), static_cast<uint32_t>(nranks), rootInfoFile, &hcclComm));

    void* sendBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&recvBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, mode));

    // Both ranks register both buffers under the same tags, so the sender can look up the
    // receiver's recv buffer by tag and the buffer tables stay symmetric.
    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, kBufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, kBufferBytes};
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

    // Only kSenderRank drives traffic. Every other rank just holds its registered buffers
    // open so the sender's WQEs have a valid target.
    if (rank == kSenderRank) {
        LaunchSimtNotifyAtomic(stream, channel, kLocalSendIdx, remoteRecvIdx, mode);
        ACLCHECK(aclrtSynchronizeStream(stream));
    }

    // Separates the sender's kernel from the receiver's read. kSenderRank only arrives here
    // after aclrtSynchronizeStream, so kReceiverRank cannot sample its recv buffer while the
    // WQEs are still in flight. Without this the receiver races ahead straight from the
    // "ready" barrier and reads the memset-zeroed buffer, which looks like nothing landed.
    HCCLCHECK(HostFileBarrier(rank, nranks, "sent"));

    bool pass = true;
    if (rank == kSenderRank) {
        pass = CheckSender(sendBuf, mode);
    } else if (rank == kReceiverRank) {
        pass = CheckReceiver(recvBuf, mode);
    }
    if (!pass && (rank == kSenderRank || rank == kReceiverRank)) {
        std::cout << "FAIL | Rank=" << rank << " | Mode=" << ModeName(mode) << std::endl;
    }

    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(hcclComm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
