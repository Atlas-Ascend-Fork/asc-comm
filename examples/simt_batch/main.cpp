/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
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

#include "simt_batch_common.h"

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

extern void LaunchSimtBatch(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status,
    uint32_t operationsPerBatch, uint32_t batchCount, bool writeOnly);
extern void LaunchSimtBatchBasic(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status);
extern void LaunchSimtSingleLast(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status);

namespace {
using namespace simt_batch;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr uint32_t kLocalSendIdx = 1U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtBatchSendBuf";
constexpr const char* kRecvBufTag = "simtBatchRecvBuf";

enum class RunMode : uint32_t {
    BASIC,
    FULL,
    SINGLE_LAST,
    MATRIX,
};

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
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
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
    HcclRootInfo rootInfo {};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == kSenderRank) {
        std::cout << "PROGRESS | Rank=" << rank << " | Stage=GetRootInfo.begin" << std::endl;
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        std::cout << "PROGRESS | Rank=" << rank << " | Stage=GetRootInfo.end" << std::endl;
        ret = WriteRootInfoFile(rootInfoFile, rootInfo);
    } else {
        std::cout << "PROGRESS | Rank=" << rank << " | Stage=ReadRootInfo.begin" << std::endl;
        ret = ReadRootInfoFile(rootInfoFile, &rootInfo);
        std::cout << "PROGRESS | Rank=" << rank << " | Stage=ReadRootInfo.end | Code=" << ret << std::endl;
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    std::cout << "PROGRESS | Rank=" << rank << " | Stage=HcclCommInitRootInfo.begin" << std::endl;
    HCCLCHECK(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
    std::cout << "PROGRESS | Rank=" << rank << " | Stage=HcclCommInitRootInfo.end" << std::endl;
    return HCCL_SUCCESS;
}

std::string SyncDir()
{
    const char* value = std::getenv("SIMT_BATCH_SYNC_DIR");
    return value == nullptr ? "/tmp/simt_batch" : value;
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

aclError InitBuffers(void* sendBuf, void* recvBuf)
{
    std::vector<uint8_t> send(kBufferBytes, 0U);
    std::vector<uint8_t> recv(kBufferBytes, 0U);
    for (uint32_t operation = 0U; operation < kOperationCount; ++operation) {
        std::memset(send.data() + kWriteOffset + static_cast<size_t>(operation) * kPayloadBytes,
            kWriteByteBase + operation, kPayloadBytes);
        std::memset(recv.data() + kReadOffset + static_cast<size_t>(operation) * kPayloadBytes,
            kReadByteBase + operation, kPayloadBytes);
        std::memset(send.data() + kNotifyDataOffset + static_cast<size_t>(operation) * kPayloadBytes,
            kNotifyByteBase + operation, kPayloadBytes);
    }
    for (uint32_t target = 0U; target < kTargetCount; ++target) {
        std::memcpy(recv.data() + kFaaTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            &kFaaInitial, sizeof(kFaaInitial));
        std::memcpy(recv.data() + kCasTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            &kCasInitial, sizeof(kCasInitial));
    }
    std::memcpy(send.data() + kMixedWriteOffset, &kMixedWriteValue, sizeof(kMixedWriteValue));
    std::memcpy(recv.data() + kMixedReadOffset, &kMixedReadValue, sizeof(kMixedReadValue));
    std::memcpy(send.data() + kMixedNotifyDataOffset, &kMixedNotifyData, sizeof(kMixedNotifyData));
    std::memcpy(recv.data() + kMixedFaaTargetOffset, &kMixedFaaInitial, sizeof(kMixedFaaInitial));
    std::memcpy(recv.data() + kMixedCasTargetOffset, &kMixedCasInitial, sizeof(kMixedCasInitial));
    aclError ret = aclrtMemcpy(sendBuf, kBufferBytes, send.data(), kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ?
        aclrtMemcpy(recvBuf, kBufferBytes, recv.data(), kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE) : ret;
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

bool CheckStatus(void* statusBuf, uint32_t expectedBatchCount = kBatchCount, bool writeOnly = false)
{
    std::array<int32_t, kStatusCount> status {};
    constexpr std::array<const char*, kCaseCount> cases = {
        "WriteNbi.homogeneous", "ReadNbi.homogeneous", "WriteWithNotifyNbi.homogeneous",
        "WriteValueNbi.homogeneous", "AtomicFAA.homogeneous", "AtomicCAS.homogeneous", "Mixed.partial",
        "WriteValueNbi.single"};
    constexpr std::array<const char*, 3U> steps = {"Append", "BatchCommit", "Drain"};
    if (aclrtMemcpy(status.data(), sizeof(status), statusBuf, sizeof(status), ACL_MEMCPY_DEVICE_TO_HOST) !=
        ACL_SUCCESS) {
        return false;
    }
    bool pass = status[kStatusInit] == 0;
    std::cout << "STATUS | Case=Init | Step=Init | Code=" << status[kStatusInit] << " | "
              << (pass ? "PASS" : "FAIL") << std::endl;
    const bool handlePass = status[kStatusHandle] == 0;
    std::cout << "STATUS | Case=Handle | Step=MakeBatchHandle | Code=" << status[kStatusHandle]
              << " | Capacity=" << status[kStatusHandleCapacity] << " | SqDepth=" << status[kStatusSqDepth]
              << " | " << (handlePass ? "PASS" : "FAIL") << std::endl;
    pass = pass && handlePass;
    const auto loadDiagU64 = [&status](uint32_t lowIndex) {
        return static_cast<uint64_t>(static_cast<uint32_t>(status[lowIndex])) |
               (static_cast<uint64_t>(static_cast<uint32_t>(status[lowIndex + 1U])) << 32U);
    };
    const uint32_t guardMask = static_cast<uint32_t>(status[kDiagGuardMask]);
    std::cout << "DIAG | Point=FirstWriteAppend"
              << " | Ret=" << status[kDiagAppendRet]
              << " | GuardMask=0x" << std::hex << guardMask
              << " | Valid=" << ((guardMask & kDiagInvalidHandle) == 0U ? 1 : 0)
              << " | Prepared=" << std::dec << status[kDiagPreparedBefore] << "->"
              << status[kDiagPreparedAfter]
              << " | Capacity=" << status[kDiagCapacity]
              << " | Target=0x" << std::hex << loadDiagU64(kDiagTargetAddrLow)
              << " | Len=" << std::dec << loadDiagU64(kDiagTargetLenLow)
              << " | RemoteBase=0x" << std::hex << loadDiagU64(kDiagRemoteBaseLow)
              << " | RemoteSize=" << std::dec << loadDiagU64(kDiagRemoteSizeLow)
              << " | Offset=" << loadDiagU64(kDiagOffsetLow)
              << std::endl;
    if (status[kDiagAppendRet] == kStatusNotRun) {
        std::cout << "DIAG | Point=FirstWriteAppend | FailedGuards=NOT_RUN" << std::endl;
    } else if (guardMask != 0U) {
        std::cout << "DIAG | Point=FirstWriteAppend | FailedGuards=";
        const char* separator = "";
        const auto printGuard = [&separator, guardMask](uint32_t bit, const char* name) {
            if ((guardMask & bit) != 0U) {
                std::cout << separator << name;
                separator = ",";
            }
        };
        printGuard(kDiagInvalidHandle, "InvalidHandle");
        printGuard(kDiagCapacityExhausted, "CapacityExhausted");
        printGuard(kDiagTargetLenTooLarge, "TargetLenTooLarge");
        printGuard(kDiagNullTarget, "NullTarget");
        printGuard(kDiagTargetBelowBase, "TargetBelowRemoteBase");
        printGuard(kDiagOffsetPastEnd, "OffsetPastRemoteEnd");
        printGuard(kDiagLenPastEnd, "LengthPastRemoteEnd");
        std::cout << std::endl;
    } else {
        std::cout << "DIAG | Point=FirstWriteAppend | FailedGuards=NoneObserved" << std::endl;
    }
    for (uint32_t commit = 0U; commit < expectedBatchCount; ++commit) {
        const uint32_t base = kCommitDiagBase + commit * kCommitDiagWordCount;
        const uint64_t tailUbWord0 = loadDiagU64(base + kCommitDiagTailUbWord0);
        const uint64_t tailSqWord0 = loadDiagU64(base + kCommitDiagTailSqWord0);
        const uint64_t headBefore = loadDiagU64(base + kCommitDiagHeadBefore);
        const uint64_t headAfter = loadDiagU64(base + kCommitDiagHeadAfter);
        std::cout << "DIAG | Point=BatchCommit"
                  << " | Commit=" << commit
                  << " | Prepared=" << status[base + kCommitDiagPrepared]
                  << " | PreparedCqe=" << status[base + kCommitDiagPreparedCqe]
                  << " | ChunkItems=" << status[base + kCommitDiagChunkItems]
                  << " | SqTailBefore=" << status[base + kCommitDiagSqTailBefore]
                  << " | HeadSnapshot=0x" << std::hex << loadDiagU64(base + kCommitDiagHeadSnapshot)
                  << " | HeadBefore=0x" << headBefore
                  << " | HeadVal=0x" << loadDiagU64(base + kCommitDiagHeadVal)
                  << " | HeadAfter=0x" << headAfter
                  << " | HeadBb=" << std::dec << static_cast<uint32_t>(headBefore) << "->"
                  << static_cast<uint32_t>(headAfter)
                  << " | HeadCqe=" << static_cast<uint32_t>(headBefore >> 32U) << "->"
                  << static_cast<uint32_t>(headAfter >> 32U)
                  << " | TailItemHead=" << std::dec << status[base + kCommitDiagTailItemHead]
                  << " | TailUbWord0=0x" << std::hex << tailUbWord0
                  << " | TailSqWord0=0x" << tailSqWord0
                  << " | TailNextHead=" << std::dec << (tailUbWord0 & 0xFFFFU)
                  << " | TailOwner=" << ((tailUbWord0 >> 31U) & 1U)
                  << " | TailOpcode=0x" << std::hex << ((tailUbWord0 >> 40U) & 0xFFU)
                  << " | RingReached=" << std::dec << status[base + kCommitDiagRingReached]
                  << " | PublishCount=" << status[base + kCommitDiagPublishCount]
                  << " | TailSqMismatchMask=0x" << std::hex
                  << static_cast<uint32_t>(status[base + kCommitDiagTailSqMismatchMask])
                  << std::endl;
    }
    const uint32_t cqeWord0 = static_cast<uint32_t>(status[kDrainDiagCqeWord0]);
    const uint32_t cqeWord1 = static_cast<uint32_t>(status[kDrainDiagCqeWord1]);
    std::cout << "DIAG | Point=Drain"
              << " | ExpectedCqTail=" << status[kDrainDiagExpectedCqTail]
              << " | InitialCqTail=" << status[kDrainDiagInitialCqTail]
              << " | FinalCqTail=" << status[kDrainDiagFinalCqTail]
              << " | InitialSqTail=" << status[kDrainDiagInitialSqTail]
              << " | FinalSqTail=" << status[kDrainDiagFinalSqTail]
              << " | PollRet=0x" << std::hex << static_cast<uint32_t>(status[kDrainDiagPollRet])
              << " | FailedCqIndex=" << std::dec << status[kDrainDiagFailedCqIndex]
              << " | RetryCount=" << status[kDrainDiagRetryCount]
              << " | CqeWord0=0x" << std::hex << cqeWord0
              << " | CqeWord1=0x" << cqeWord1
              << " | CqeOwner=" << std::dec << ((cqeWord0 >> 2U) & 1U)
              << " | CqeStatus=" << ((cqeWord0 >> 24U) & 0xFFU)
              << " | CqeSubstatus=" << ((cqeWord0 >> 16U) & 0xFFU)
              << " | CqeEntryIdx=" << (cqeWord1 & 0xFFFFU)
              << std::endl;
    const uint32_t checkedCaseCount = writeOnly ? 1U : kCaseCount;
    for (uint32_t testCase = 0U; testCase < checkedCaseCount; ++testCase) {
        for (uint32_t field = 0U; field < steps.size(); ++field) {
            const uint32_t index = kStatusCaseBase + testCase * kStatusFieldsPerCase + field;
            const int32_t value = status[index];
            const bool itemPass = value == 0;
            std::cout << "STATUS | Case=" << cases[testCase] << " | Step=" << steps[field]
                      << " | Code=" << value << " | "
                      << (itemPass ? "PASS" : (value == kStatusNotRun ? "NOT_RUN" : "FAIL")) << std::endl;
            pass = pass && itemPass;
        }
        const int32_t expectedCommits = testCase < kCaseMixed ? static_cast<int32_t>(expectedBatchCount) : 1;
        const uint32_t commitIndex = kStatusCaseBase + testCase * kStatusFieldsPerCase + kStatusCommitCount;
        const int32_t actualCommits = status[commitIndex];
        const bool commitPass = actualCommits == expectedCommits;
        std::cout << "STATUS | Case=" << cases[testCase] << " | Step=BatchCommitCount | Actual="
                  << actualCommits << " | Expected=" << expectedCommits << " | "
                  << (commitPass ? "PASS" : (actualCommits == kStatusNotRun ? "NOT_RUN" : "FAIL"))
                  << std::endl;
        pass = pass && commitPass;
    }
    return pass;
}

bool CheckBasicStatus(void* statusBuf)
{
    std::array<int32_t, kBasicStatusCount> status {};
    if (aclrtMemcpy(status.data(), sizeof(status), statusBuf, sizeof(status), ACL_MEMCPY_DEVICE_TO_HOST) !=
        ACL_SUCCESS) {
        return false;
    }
    constexpr std::array<const char*, 6U> steps = {
        "Init", "MakeBatchHandle", "WriteNbi#1", "WriteNbi#2", "BatchCommit", "Drain"};
    constexpr std::array<uint32_t, 6U> indices = {
        kBasicStatusInit, kBasicStatusHandle, kBasicStatusAppendFirst, kBasicStatusAppendSecond,
        kBasicStatusCommit, kBasicStatusDrain};
    bool pass = true;
    for (uint32_t item = 0U; item < steps.size(); ++item) {
        const int32_t code = status[indices[item]];
        bool itemPass = code == 0;
        std::cout << "STATUS | API=" << steps[item] << " | Code=" << code;
        if (indices[item] == kBasicStatusHandle) {
            const bool capacityPass = status[kBasicStatusCapacity] == static_cast<int32_t>(kBasicOperationCount);
            itemPass = itemPass && capacityPass;
            std::cout << " | Capacity=" << status[kBasicStatusCapacity]
                      << " | ExpectedCapacity=" << kBasicOperationCount;
        }
        std::cout << " | " << (itemPass ? "PASS" : (code == kBasicStatusNotRun ? "NOT_RUN" : "FAIL"))
                  << std::endl;
        pass = pass && itemPass;
    }
    const auto loadDiagU64 = [&status](uint32_t lowIndex) {
        return static_cast<uint64_t>(static_cast<uint32_t>(status[lowIndex])) |
               (static_cast<uint64_t>(static_cast<uint32_t>(status[lowIndex + 1U])) << 32U);
    };
    const uint32_t base = kBasicCommitDiagBase;
    const uint64_t tailUbWord0 = loadDiagU64(base + kCommitDiagTailUbWord0);
    const uint64_t tailSqWord0 = loadDiagU64(base + kCommitDiagTailSqWord0);
    const uint64_t headBefore = loadDiagU64(base + kCommitDiagHeadBefore);
    const uint64_t headAfter = loadDiagU64(base + kCommitDiagHeadAfter);
    std::cout << "DIAG | Point=BasicBatchCommit"
              << " | Prepared=" << status[base + kCommitDiagPrepared]
              << " | PreparedCqe=" << status[base + kCommitDiagPreparedCqe]
              << " | ChunkItems=" << status[base + kCommitDiagChunkItems]
              << " | SqTailBefore=" << status[base + kCommitDiagSqTailBefore]
              << " | HeadSnapshot=0x" << std::hex << loadDiagU64(base + kCommitDiagHeadSnapshot)
              << " | HeadBefore=0x" << headBefore
              << " | HeadVal=0x" << loadDiagU64(base + kCommitDiagHeadVal)
              << " | HeadAfter=0x" << headAfter
              << " | HeadBb=" << std::dec << static_cast<uint32_t>(headBefore) << "->"
              << static_cast<uint32_t>(headAfter)
              << " | HeadCqe=" << static_cast<uint32_t>(headBefore >> 32U) << "->"
              << static_cast<uint32_t>(headAfter >> 32U)
              << " | TailItemHead=" << std::dec << status[base + kCommitDiagTailItemHead]
              << " | TailUbWord0=0x" << std::hex << tailUbWord0
              << " | TailSqWord0=0x" << tailSqWord0
              << " | TailNextHead=" << std::dec << (tailUbWord0 & 0xFFFFU)
              << " | RingReached=" << status[base + kCommitDiagRingReached]
              << " | PublishCount=" << status[base + kCommitDiagPublishCount]
              << " | TailSqMismatchMask=0x" << std::hex
              << static_cast<uint32_t>(status[base + kCommitDiagTailSqMismatchMask])
              << std::endl;
    const uint32_t drainBase = kBasicDrainDiagBase;
    const uint32_t cqeWord0 = static_cast<uint32_t>(status[drainBase + 8U]);
    const uint32_t cqeWord1 = static_cast<uint32_t>(status[drainBase + 9U]);
    std::cout << "DIAG | Point=BasicDrain"
              << " | ExpectedCqTail=" << status[drainBase]
              << " | InitialCqTail=" << status[drainBase + 1U]
              << " | FinalCqTail=" << status[drainBase + 2U]
              << " | InitialSqTail=" << status[drainBase + 3U]
              << " | FinalSqTail=" << status[drainBase + 4U]
              << " | PollRet=0x" << std::hex << static_cast<uint32_t>(status[drainBase + 5U])
              << " | FailedCqIndex=" << std::dec << status[drainBase + 6U]
              << " | RetryCount=" << status[drainBase + 7U]
              << " | CqeWord0=0x" << std::hex << cqeWord0
              << " | CqeWord1=0x" << cqeWord1
              << " | CqeOwner=" << std::dec << ((cqeWord0 >> 2U) & 1U)
              << " | CqeStatus=" << ((cqeWord0 >> 24U) & 0xFFU)
              << " | CqeSubstatus=" << ((cqeWord0 >> 16U) & 0xFFU)
              << " | CqeEntryIdx=" << (cqeWord1 & 0xFFFFU)
              << std::endl;
    return pass;
}

bool CheckSingleLastStatus(void* statusBuf)
{
    std::array<int32_t, kSingleLastStatusCount> status {};
    if (aclrtMemcpy(status.data(), sizeof(status), statusBuf, sizeof(status), ACL_MEMCPY_DEVICE_TO_HOST) !=
        ACL_SUCCESS) {
        return false;
    }
    constexpr std::array<const char*, kSingleLastStatusCount> steps = {
        "Init", "WriteNbi<false>", "WriteNbi<true>", "Drain"};
    bool pass = true;
    for (uint32_t index = 0U; index < status.size(); ++index) {
        const int32_t code = status[index];
        const bool itemPass = code == 0;
        std::cout << "STATUS | Control=SingleLast | Step=" << steps[index] << " | Code=" << code << " | "
                  << (itemPass ? "PASS" : (code == kBasicStatusNotRun ? "NOT_RUN" : "FAIL")) << std::endl;
        pass = pass && itemPass;
    }
    return pass;
}

uint64_t LoadWord(const std::vector<uint8_t>& buffer, size_t offset)
{
    uint64_t value = 0U;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

bool CheckPayloads(
    const std::vector<uint8_t>& actual, size_t offset, uint8_t valueBase, const char* api)
{
    uint32_t executionBitmap = 0U;
    bool pass = true;
    for (uint32_t operation = 0U; operation < kOperationCount; ++operation) {
        const uint8_t expected = valueBase + operation;
        const size_t operationOffset = offset + static_cast<size_t>(operation) * kPayloadBytes;
        bool operationPass = true;
        for (uint32_t byte = 0U; byte < kPayloadBytes; ++byte) {
            if (actual[operationOffset + byte] != expected) {
                std::cout << "CHECK | API=" << api << " | Operation=" << operation << " | Byte=" << byte
                          << " | Actual=" << static_cast<uint32_t>(actual[operationOffset + byte])
                          << " | Expected=" << static_cast<uint32_t>(expected) << " | FAIL" << std::endl;
                operationPass = false;
                break;
            }
        }
        if (operationPass) {
            executionBitmap |= 1U << operation;
        }
        pass = pass && operationPass;
    }
    std::cout << "DIAG | API=" << api << " | ExecutionBitmap=0x" << std::hex << executionBitmap
              << " | FirstBytes=";
    for (uint32_t operation = 0U; operation < kOperationCount; ++operation) {
        std::cout << (operation == 0U ? "" : ",") << std::dec
                  << static_cast<uint32_t>(actual[offset + static_cast<size_t>(operation) * kPayloadBytes]);
    }
    std::cout << std::endl;
    std::cout << "CHECK | API=" << api << " | Operations=" << kOperationCount << " | PayloadBytes="
              << kPayloadBytes << " | " << (pass ? "PASS" : "FAIL") << std::endl;
    return pass;
}

bool CheckWord(const std::vector<uint8_t>& actual, size_t offset, uint64_t expected, const char* api, uint32_t item)
{
    const uint64_t value = LoadWord(actual, offset);
    if (value != expected) {
        std::cout << "CHECK | API=" << api << " | Item=" << item << " | Actual=" << value
                  << " | Expected=" << expected << " | FAIL" << std::endl;
        return false;
    }
    return true;
}

bool CheckSender(void* sendBuf)
{
    std::vector<uint8_t> actual(kBufferBytes);
    if (aclrtMemcpy(actual.data(), kBufferBytes, sendBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const bool readPass = CheckPayloads(actual, kReadOffset, kReadByteBase, "ReadNbi.fetch");
    bool faaPass = true;
    bool casPass = true;
    for (uint32_t operation = 0U; operation < kOperationCount; ++operation) {
        const uint64_t expectedFaa = operation < kTargetCount ? kFaaInitial : kFaaInitial + kFaaAdd;
        const uint64_t expectedCas = operation < kTargetCount ? kCasInitial : kCasSwap;
        faaPass = CheckWord(actual, kFaaFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            expectedFaa, "AtomicFAA.fetch", operation) && faaPass;
        casPass = CheckWord(actual, kCasFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            expectedCas, "AtomicCAS.fetch", operation) && casPass;
    }
    std::cout << "CHECK | API=AtomicFAA.fetch | Items=" << kOperationCount << " | "
              << (faaPass ? "PASS" : "FAIL") << std::endl;
    std::cout << "CHECK | API=AtomicCAS.fetch | Items=" << kOperationCount << " | "
              << (casPass ? "PASS" : "FAIL") << std::endl;
    bool mixedPass = CheckWord(actual, kMixedReadOffset, kMixedReadValue, "Mixed.ReadNbi.fetch", 0U);
    mixedPass = CheckWord(actual, kMixedFaaFetchOffset, kMixedFaaInitial, "Mixed.AtomicFAA.fetch", 0U) && mixedPass;
    mixedPass = CheckWord(actual, kMixedCasFetchOffset, kMixedCasInitial, "Mixed.AtomicCAS.fetch", 0U) && mixedPass;
    std::cout << "CHECK | API=Mixed.fetch | Items=3 | " << (mixedPass ? "PASS" : "FAIL") << std::endl;
    const bool pass = readPass && faaPass && casPass && mixedPass;
    std::cout << "CHECK | Side=Sender | Status=" << (pass ? "PASS" : "FAIL") << std::endl;
    return pass;
}

bool CheckReceiver(void* recvBuf)
{
    std::vector<uint8_t> actual(kBufferBytes);
    if (aclrtMemcpy(actual.data(), kBufferBytes, recvBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const bool writePass = CheckPayloads(actual, kWriteOffset, kWriteByteBase, "WriteNbi.target");
    const bool readPass = CheckPayloads(actual, kReadOffset, kReadByteBase, "ReadNbi.source");
    const bool notifyDataPass = CheckPayloads(
        actual, kNotifyDataOffset, kNotifyByteBase, "WriteWithNotifyNbi.data");
    bool notifySignalPass = true;
    bool writeValuePass = true;
    for (uint32_t operation = 0U; operation < kOperationCount; ++operation) {
        notifySignalPass = CheckWord(
            actual, kNotifySignalOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            kNotifyValueBase + operation, "WriteWithNotifyNbi.notify", operation) && notifySignalPass;
        writeValuePass = CheckWord(actual, kWriteValueOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            kWriteValueBase + operation, "WriteValueNbi.target", operation) && writeValuePass;
    }
    std::cout << "CHECK | API=WriteWithNotifyNbi.notify | Items=" << kOperationCount << " | "
              << (notifySignalPass ? "PASS" : "FAIL") << std::endl;
    std::cout << "CHECK | API=WriteValueNbi.target | Items=" << kOperationCount << " | "
              << (writeValuePass ? "PASS" : "FAIL") << std::endl;
    bool faaPass = true;
    bool casPass = true;
    for (uint32_t target = 0U; target < kTargetCount; ++target) {
        faaPass = CheckWord(actual, kFaaTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            kFaaInitial + kBatchCount * kFaaAdd, "AtomicFAA.target", target) && faaPass;
        casPass = CheckWord(actual, kCasTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            kCasSwap, "AtomicCAS.target", target) && casPass;
    }
    std::cout << "CHECK | API=AtomicFAA.target | Items=" << kTargetCount << " | "
              << (faaPass ? "PASS" : "FAIL") << std::endl;
    std::cout << "CHECK | API=AtomicCAS.target | Items=" << kTargetCount << " | "
              << (casPass ? "PASS" : "FAIL") << std::endl;
    bool mixedPass = CheckWord(actual, kMixedWriteOffset, kMixedWriteValue, "Mixed.WriteNbi.target", 0U);
    mixedPass = CheckWord(actual, kMixedReadOffset, kMixedReadValue, "Mixed.ReadNbi.source", 0U) && mixedPass;
    mixedPass = CheckWord(actual, kMixedNotifyDataOffset, kMixedNotifyData, "Mixed.Notify.data", 0U) && mixedPass;
    mixedPass = CheckWord(actual, kMixedNotifySignalOffset, kMixedNotifyValue, "Mixed.Notify.signal", 0U) && mixedPass;
    mixedPass = CheckWord(
        actual, kMixedWriteValueOffset, kMixedInlineValue, "Mixed.WriteValue.target", 0U) && mixedPass;
    mixedPass = CheckWord(actual, kMixedFaaTargetOffset, kMixedFaaInitial + kMixedFaaAdd,
        "Mixed.AtomicFAA.target", 0U) && mixedPass;
    mixedPass = CheckWord(actual, kMixedCasTargetOffset, kMixedCasSwap, "Mixed.AtomicCAS.target", 0U) && mixedPass;
    const bool singlePass = CheckWord(
        actual, kSingleWriteValueOffset, kSingleWriteValue, "WriteValueNbi.single", 0U);
    std::cout << "CHECK | API=Mixed.target | Items=7 | " << (mixedPass ? "PASS" : "FAIL") << std::endl;
    std::cout << "CHECK | API=WriteValueNbi.single | Items=1 | " << (singlePass ? "PASS" : "FAIL") << std::endl;
    const bool pass = writePass && readPass && notifyDataPass && notifySignalPass && writeValuePass && faaPass &&
        casPass && mixedPass && singlePass;
    std::cout << "CHECK | Side=Receiver | Status=" << (pass ? "PASS" : "FAIL") << std::endl;
    return pass;
}

bool CheckBasicReceiver(void* recvBuf, uint32_t operationCount = kBasicOperationCount)
{
    std::array<uint8_t, kOperationCount * kPayloadBytes> actual {};
    const size_t bytes = static_cast<size_t>(operationCount) * kPayloadBytes;
    if (aclrtMemcpy(actual.data(), bytes, recvBuf, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    bool pass = true;
    for (uint32_t operation = 0U; operation < operationCount; ++operation) {
        const uint8_t expected = kWriteByteBase + operation;
        for (uint32_t byte = 0U; byte < kPayloadBytes; ++byte) {
            const size_t offset = static_cast<size_t>(operation) * kPayloadBytes + byte;
            if (actual[offset] != expected) {
                std::cout << "CHECK | API=WriteNbi | Item=" << operation << " | Byte=" << byte
                          << " | Actual=" << static_cast<uint32_t>(actual[offset])
                          << " | Expected=" << static_cast<uint32_t>(expected) << " | FAIL" << std::endl;
                pass = false;
                break;
            }
        }
    }
    std::cout << "CHECK | API=WriteNbi | Items=" << operationCount << " | PayloadBytes="
              << kPayloadBytes << " | " << (pass ? "PASS" : "FAIL") << std::endl;
    std::cout << "CHECK | Side=Receiver | Status=" << (pass ? "PASS" : "FAIL") << std::endl;
    return pass;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 4 && argc != 5) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> [basic|full|single-last|matrix-1x1|matrix-{2,8}x{1,2}]"
                  << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string modeName = argc == 5 ? argv[4] : "full";
    const bool matrixMode = modeName == "matrix-1x1" || modeName == "matrix-2x1" || modeName == "matrix-8x1" ||
        modeName == "matrix-2x2" || modeName == "matrix-8x2";
    if (modeName != "basic" && modeName != "full" && modeName != "single-last" && !matrixMode) {
        std::cout << "invalid mode." << std::endl;
        return 1;
    }
    const uint32_t operationsPerBatch = modeName == "matrix-1x1" ? 1U :
        (modeName.find("matrix-2x") == 0U ? 2U : kOperationsPerBatch);
    const uint32_t batchCount = matrixMode && modeName.back() == '1' ? 1U : kBatchCount;
    const RunMode mode = modeName == "basic" ? RunMode::BASIC :
        (modeName == "single-last" ? RunMode::SINGLE_LAST : (matrixMode ? RunMode::MATRIX : RunMode::FULL));
    if (nranks != 2 || rank < 0 || rank >= nranks) {
        std::cout << "simt_batch requires exactly two valid ranks." << std::endl;
        return 1;
    }

    auto logStage = [rank](const char* stage) {
        std::cout << "PROGRESS | Rank=" << rank << " | Stage=" << stage << std::endl;
    };
    logStage("AclInit.begin");
    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    logStage("InitComm.begin");
    HCCLCHECK(InitCommByRootInfo(
        static_cast<uint32_t>(rank), static_cast<uint32_t>(nranks), rootInfoFile, &comm));
    logStage("InitComm.end");

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, kBufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, kBufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));

    const uint32_t peer = rank == static_cast<int>(kSenderRank) ? kReceiverRank : kSenderRank;
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    logStage("AcquirePeerChannel.begin");
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    logStage("AcquirePeerChannel.end");
    logStage("ReadyBarrier.begin");
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));
    logStage("ReadyBarrier.end");

    void* statusBuf = nullptr;
    bool pass = true;
    if (rank == static_cast<int>(kSenderRank)) {
        const size_t statusCount = mode == RunMode::BASIC ? kBasicStatusCount :
            (mode == RunMode::SINGLE_LAST ? kSingleLastStatusCount : kStatusCount);
        const size_t statusBytes = statusCount * sizeof(int32_t);
        ACLCHECK(aclrtMalloc(&statusBuf, statusBytes, ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(statusBuf, statusBytes, 0xFF, statusBytes));
        if (mode == RunMode::BASIC) {
            LaunchSimtBatchBasic(stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf);
        } else if (mode == RunMode::SINGLE_LAST) {
            LaunchSimtSingleLast(stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf);
        } else {
            LaunchSimtBatch(
                stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf, operationsPerBatch, batchCount,
                mode == RunMode::MATRIX);
        }
        logStage("StreamSynchronize.begin");
        ACLCHECK(aclrtSynchronizeStream(stream));
        logStage("StreamSynchronize.end");
        pass = mode == RunMode::BASIC ? CheckBasicStatus(statusBuf) :
            (mode == RunMode::SINGLE_LAST ? CheckSingleLastStatus(statusBuf) :
                CheckStatus(statusBuf, batchCount, mode == RunMode::MATRIX) &&
                    (mode == RunMode::MATRIX || CheckSender(sendBuf)));
    }

    logStage("CompletedBarrier.begin");
    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    logStage("CompletedBarrier.end");
    if (rank == static_cast<int>(kReceiverRank)) {
        pass = mode == RunMode::FULL ? CheckReceiver(recvBuf) :
            CheckBasicReceiver(recvBuf, mode == RunMode::MATRIX ? operationsPerBatch * batchCount :
                kBasicOperationCount);
    }
    const std::string suiteName = mode == RunMode::BASIC ? "simt_batch_basic" :
        (mode == RunMode::SINGLE_LAST ? "simt_single_last_control" :
            (mode == RunMode::MATRIX ? "simt_batch_" + modeName : "simt_batch"));
    std::cout << "[rank " << rank << "] " << suiteName << " " << (pass ? "PASS" : "FAIL") << std::endl;

    if (statusBuf != nullptr) {
        ACLCHECK(aclrtFree(statusBuf));
    }
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
