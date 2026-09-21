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
 * \brief Hcomm SIMT URMA功能样例Host侧流程：通信域创建、内存注册、P2P通道创建与结果校验
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

#include "simt_urma_common.h"

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

extern void LaunchSimtUrma(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode);

namespace {

using namespace simt_urma;

// 相邻节点相关资源（用于环形拓扑）
struct PeerRankCtx {
    uint32_t rankId{0};
    ChannelHandle channel{0};
    uint32_t remoteRecvIdx{0};
};

constexpr uint32_t kBarrierWaitSeconds = 60;
constexpr uint32_t kBarrierPollMs = 100;

// SIMT接口只在UBC_CTP/URMA协议上实现，其余链路协议无对应实现。
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;

// 两块内存注册到通信域时使用的tag。两个rank使用相同tag，远端内存才能按tag查回。
constexpr const char* kSendBufTag = "simtUrmaSendBuf";
constexpr const char* kRecvBufTag = "simtUrmaRecvBuf";

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

// 一张表同时驱动解析和打印，避免某个 mode 能被解析、却打印成另一个名字。
struct ModeEntry {
    const char* name;
    uint32_t mode;
};

constexpr ModeEntry kModes[] = {
    {"write_single", kModeWriteSingle},
    {"write_batch_last", kModeWriteBatchLast},
    {"write_value_single", kModeWriteValueSingle},
    {"write_value_batch_last", kModeWriteValueBatchLast},
    {"notify", kModeNotify},
    {"faa", kModeFaa},
    {"cas", kModeCas},
    {"single", kModeNotifyAtomicSingle},
    {"batch_last", kModeNotifyAtomicBatchLast},
    {"notify_immediate_repeat", kModeNotifyImmediateRepeat},
};

constexpr uint32_t kDefaultMode = kModeNotifyAtomicSingle;

// Unlike a defaulted environment variable, an explicit command-line argument that does not
// match any mode is a typo rather than a request for the default, so it is rejected.
bool ParseMode(const char* arg, uint32_t& mode)
{
    const std::string name(arg);
    for (const ModeEntry& entry : kModes) {
        if (name == entry.name) {
            mode = entry.mode;
            return true;
        }
    }
    return false;
}

const char* ModeName(uint32_t mode)
{
    for (const ModeEntry& entry : kModes) {
        if (mode == entry.mode) {
            return entry.name;
        }
    }
    return "unknown";
}

void PrintModes()
{
    std::cout << "  mode: write_single | write_batch_last | write_value_single | write_value_batch_last" << std::endl;
    std::cout << "        notify | faa | cas | single (default) | batch_last | notify_immediate_repeat" << std::endl;
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
// deliberately avoids issuing collectives so only the SIMT posts touch the QP.
// syncDir holds the barrier marker files and is passed in on the command line rather than through
// the environment: an exported variable is easy to lose across a shell wrapper or a scheduler, and
// losing it silently sent every rank to the same default directory, where markers from a previous
// run could let a barrier pass immediately.
HcclResult HostFileBarrier(
    int rank, int nranks, const std::string& syncDir, const std::string& tag, const std::string& payload = "1")
{
    if (mkdir(syncDir.c_str(), 0700) != 0 && errno != EEXIST) {
        std::cout << "Create sync dir failed: " << syncDir << std::endl;
        return HCCL_E_SYSCALL;
    }
    const std::string markerPrefix = syncDir + "/" + tag + ".";
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
//
// The two families seed different things. The write modes need an ascending payload in the send
// buffer and a zeroed receive buffer. The notify/atomic modes need one data word to send, and
// the FAA/CAS targets pre-seeded on the receiving side, because those operations are defined
// against a known starting value.
aclError InitBuffers(void* sendBuf, void* recvBuf, uint32_t mode)
{
    uint64_t send[kSlotCount] = {};
    uint64_t recv[kSlotCount] = {};
    if (IsWriteMode(mode)) {
        for (uint32_t i = 0; i < kWriteSlotCount; ++i) {
            send[i] = SlotValue(i);
        }
    } else {
        send[kNotifyDataSlot] = kNotifyData;
        recv[kFaaTargetSlot] = kFaaInitial;
        recv[kCasTargetSlot] = kCasInitial;
    }
    aclError ret = aclrtMemcpy(sendBuf, kBufferBytes, send, kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    if (ret != ACL_SUCCESS) {
        return ret;
    }
    return aclrtMemcpy(recvBuf, kBufferBytes, recv, kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
}

// Builds the P2P channel to peer.rankId over kTargetCommProtocol and fills in peer.channel plus
// the index of the remote buffer tagged kRecvBufTag in that channel's remote buffer table.
//
// The kernel resolves buffer base addresses through AscendC::simt::LocalBufferAddr and
// RemoteBufferAddr, both of which take an index into the channel's buffer table. That table is
// built from the memHandles handed to HcclChannelAcquire below. Local table index 0 is reserved
// for the CCL buffer, so explicitly registered buffers start at index 1. The remote index is
// looked up by tag because the remote table may hold entries this example did not register.
HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, PeerRankCtx& peer, HcclMemHandle* memHandles, uint32_t memHandleNum)
{
    HcclChannelDesc channelDesc;
    HCCLCHECK(HcclChannelDescInit(&channelDesc, 1));
    channelDesc.remoteRank = peer.rankId;
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
    HCCLCHECK(HcclRankGraphGetLinks(comm, netLayers[0], rank, peer.rankId, &links, &linkNum));
    if (linkNum == 0 || links == nullptr) {
        std::cout << "[rank " << rank << "] no link to peer " << peer.rankId << std::endl;
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
        std::cout << "[rank " << rank << "] no UBC_CTP link to peer " << peer.rankId
                  << ", SIMT URMA interfaces require the URMA path" << std::endl;
        return HCCL_E_NOT_SUPPORT;
    }

    HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &peer.channel));
    if (peer.channel == 0) {
        std::cout << "[rank " << rank << "] channel acquire returned an invalid handle" << std::endl;
        return HCCL_E_INTERNAL;
    }

    uint32_t memNum = 0;
    CommMem* remoteMems = nullptr;
    char** memTags = nullptr;
    HCCLCHECK(HcclChannelGetRemoteMems(comm, peer.channel, &memNum, &remoteMems, &memTags));
    if (memNum == 0 || remoteMems == nullptr || memTags == nullptr) {
        std::cout << "[rank " << rank << "] no remote memory reported on the channel" << std::endl;
        return HCCL_E_INTERNAL;
    }
    for (uint32_t i = 0; i < memNum; ++i) {
        if (memTags[i] != nullptr && std::strcmp(memTags[i], kRecvBufTag) == 0) {
            peer.remoteRecvIdx = i;
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

// 环上每个 rank 都是发送方，因此每个 rank 都要校验自己取回的 fetch 值。
//
// 远端写入只落在 recvBuf：peer 的 remoteRecvIdx 是按 kRecvBufTag 查出来的，sendBuf 虽然也注册
// 在通道上，但从不作为远端目标。环上每个 recvBuf 恰好只有一个写者（它的 prev），所以 FAA/CAS
// 取回的旧值仍然精确等于 host 播种的初值，这项校验在环形拓扑下依然成立。
bool CheckSender(void* sendBuf, uint32_t mode)
{
    // write 系列不读回 sendBuf：它只是数据源，kernel 不往里写任何东西。
    if (IsWriteMode(mode)) {
        return true;
    }
    uint64_t expected[kSlotCount] = {};
    expected[kNotifyDataSlot] = kNotifyData;
    if (mode == kModeFaa) {
        expected[kFaaFetchSlot] = kFaaInitial;
    } else if (mode == kModeCas) {
        expected[kCasFetchSlot] = kCasInitial;
    } else if (mode == kModeNotifyAtomicSingle || mode == kModeNotifyAtomicBatchLast) {
        expected[kFaaFetchSlot] = kFaaInitial;
        expected[kCasFetchSlot] = kCasInitial;
    }
    return CheckWords(sendBuf, expected, "sender fetch/drain");
}

bool CheckReceiver(void* recvBuf, uint32_t mode)
{
    uint64_t expected[kSlotCount] = {};

    // All four write modes land the same ascending sequence, whether the payload came from an
    // SGE or inline in the WQE, so one expected table covers them.
    if (IsWriteMode(mode)) {
        for (uint32_t i = 0; i < kWriteSlotCount; ++i) {
            expected[i] = SlotValue(i);
        }
        return CheckWords(recvBuf, expected, "receiver target");
    }

    // notify_immediate_repeat posts the same Notify kNotifyImmediateRepeatCount times, so the
    // landed state is indistinguishable from a single Notify.
    if (mode == kModeNotify || mode == kModeNotifyImmediateRepeat) {
        expected[kNotifyDataSlot] = kNotifyData;
        expected[kNotifySignalSlot] = kNotifySignal;
        expected[kFaaTargetSlot] = kFaaInitial;
        expected[kCasTargetSlot] = kCasInitial;
    } else if (mode == kModeFaa) {
        expected[kFaaTargetSlot] = kFaaInitial + kFaaAdd;
        expected[kCasTargetSlot] = kCasInitial;
    } else if (mode == kModeCas) {
        expected[kFaaTargetSlot] = kFaaInitial;
        expected[kCasTargetSlot] = kCasSwap;
    } else {
        // single and batch_last both run all three operations, so both end in the same state.
        expected[kNotifyDataSlot] = kNotifyData;
        expected[kNotifySignalSlot] = kNotifySignal;
        expected[kFaaTargetSlot] = kFaaInitial + kFaaAdd;
        expected[kCasTargetSlot] = kCasSwap;
    }
    return CheckWords(recvBuf, expected, "receiver target");
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc < 5) {
        std::cout << "Usage: " << argv[0] << " <rank> <nranks> <root_info_file> <sync_dir> [mode]" << std::endl;
        std::cout << "  sync_dir: directory for host barrier marker files, shared by all ranks" << std::endl;
        PrintModes();
        return 1;
    }

    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string syncDir = argv[4];
    if (nranks < 2) {
        std::cout << "nranks must be at least 2." << std::endl;
        return 1;
    }
    if (rank < 0 || rank >= nranks) {
        std::cout << "rank must be in [0, " << nranks << ")." << std::endl;
        return 1;
    }
    if (syncDir.empty()) {
        std::cout << "sync_dir must not be empty." << std::endl;
        return 1;
    }

    uint32_t mode = kDefaultMode;
    if (argc > 5 && !ParseMode(argv[5], mode)) {
        std::cout << "Unknown mode: " << argv[5] << std::endl;
        PrintModes();
        return 1;
    }

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    HcclComm hcclComm = nullptr;
    HCCLCHECK(InitCommByRootInfo(rank, nranks, rootInfoFile, &hcclComm));

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
    // Position in the memHandles array handed to HcclChannelAcquire.
    constexpr uint32_t kSendMemHandleIdx = 0U;
    constexpr uint32_t kRecvMemHandleIdx = 1U;
    // Index into the channel's local buffer table, which the kernel passes to
    // AscendC::simt::LocalBufferAddr. Table index 0 is the CCL buffer, so this is the
    // memHandles position plus one. Passing the memHandles position directly resolves to the
    // CCL buffer instead: the post then reads the wrong source, yet the WQE, the remote address
    // and the token are all valid, so the NIC completes it and returns a clean CQE while the
    // receiver observes the CCL buffer's contents rather than the payload.
    constexpr uint32_t kLocalSendIdx = kSendMemHandleIdx + 1U;
    HCCLCHECK(HcclCommMemReg(hcclComm, kSendBufTag, &sendMem, &memHandles[kSendMemHandleIdx]));
    HCCLCHECK(HcclCommMemReg(hcclComm, kRecvBufTag, &recvMem, &memHandles[kRecvMemHandleIdx]));

    // 环形拓扑：每个 rank 向 next 发送，并校验从 prev 收到的数据，因此所有 rank 都参与收发。
    PeerRankCtx prev;
    PeerRankCtx next;
    const uint32_t uRank = static_cast<uint32_t>(rank);
    const uint32_t uNranks = static_cast<uint32_t>(nranks);
    prev.rankId = (uRank + uNranks - 1U) % uNranks;
    next.rankId = (uRank + 1U) % uNranks;

    // 建链顺序按 rank 奇偶错开。两个 rank 若都先等对方作为 prev 的那条链，会形成环上的循环
    // 等待；错开之后每一步都有一侧在主动应答。
    if (rank % 2 == 0) {
        HCCLCHECK(AcquirePeerChannel(hcclComm, uRank, prev, memHandles, 2U));
        HCCLCHECK(AcquirePeerChannel(hcclComm, uRank, next, memHandles, 2U));
    } else {
        HCCLCHECK(AcquirePeerChannel(hcclComm, uRank, next, memHandles, 2U));
        HCCLCHECK(AcquirePeerChannel(hcclComm, uRank, prev, memHandles, 2U));
    }

    HCCLCHECK(HostFileBarrier(rank, nranks, syncDir, "ready"));

    // 每个 rank 都通过 next 通道发送。发送内容与 rank 无关，因此 prev 写进来的数据和本 rank
    // 写出去的数据期望值相同，接收侧校验表不需要按来源 rank 区分。
    // The write_value_* modes carry their payload inline in the WQE and never read the send
    // buffer, so kLocalSendIdx is unused there. It is still registered on every rank to keep the
    // buffer tables symmetric and the channel setup identical across modes.
    std::cout << "[rank " << rank << "] simt_urma mode=" << ModeName(mode) << " sending to rank " << next.rankId
              << std::endl;
    LaunchSimtUrma(stream, next.channel, kLocalSendIdx, next.remoteRecvIdx, mode);
    ACLCHECK(aclrtSynchronizeStream(stream));

    // Separates each rank's own kernel from its read of what prev wrote. Every rank only arrives
    // here after its own aclrtSynchronizeStream, so no rank samples its recv buffer while the
    // WQEs targeting it are still in flight. Without this a rank races ahead straight from the
    // "ready" barrier and reads the zeroed buffer, which looks like nothing landed.
    HCCLCHECK(HostFileBarrier(rank, nranks, syncDir, "sent"));

    // 环上每个 rank 同时是发送方和接收方，两侧都要校验：sendBuf 里是本 rank 取回的 fetch 值，
    // recvBuf 里是 prev 写进来的数据。
    const bool senderPass = CheckSender(sendBuf, mode);
    const bool receiverPass = CheckReceiver(recvBuf, mode);
    const bool pass = senderPass && receiverPass;
    if (!senderPass) {
        std::cout << "[rank " << rank << "] simt_urma " << ModeName(mode) << " FAIL (sender fetch check)" << std::endl;
    }
    if (!receiverPass) {
        std::cout << "[rank " << rank << "] simt_urma " << ModeName(mode)
                  << " FAIL (receiver check) | received from rank " << prev.rankId << std::endl;
    }
    if (pass) {
        std::cout << "[rank " << rank << "] simt_urma " << ModeName(mode) << " PASS | sent to rank " << next.rankId
                  << ", received from rank " << prev.rankId << std::endl;
    }

    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(hcclComm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
