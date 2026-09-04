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

#include "simt_batch_perf_common.h"

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

extern void LaunchSimtBatchPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize, uint32_t api, bool explicitBatch,
    bool groupBatch, uint32_t groupLanes);

namespace {
using namespace simt_batch_perf;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr uint32_t kLocalSendIdx = 1U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtBatchPerfSendBuf";
constexpr const char* kRecvBufTag = "simtBatchPerfRecvBuf";

bool FileReady(const std::string& path, size_t minBytes = 1U)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(minBytes);
}

HcclResult WriteRootInfo(const std::string& path, const HcclRootInfo& rootInfo)
{
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
    out.close();
    if (!out || std::rename(tmp.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfo(const std::string& path, HcclRootInfo* rootInfo)
{
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
        if (FileReady(path, sizeof(*rootInfo))) {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char*>(rootInfo), sizeof(*rootInfo));
            if (in.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
                return HCCL_SUCCESS;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitComm(uint32_t rank, uint32_t nranks, const std::string& path, HcclComm* comm)
{
    HcclRootInfo rootInfo {};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == kRootRank) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        ret = WriteRootInfo(path, rootInfo);
    } else {
        ret = ReadRootInfo(path, &rootInfo);
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    HCCLCHECK(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
    return HCCL_SUCCESS;
}

std::string SyncDir()
{
    const char* value = std::getenv("SIMT_BATCH_PERF_SYNC_DIR");
    return value == nullptr ? "/tmp/simt_batch_perf" : value;
}

HcclResult HostBarrier(int rank, int nranks, const char* name)
{
    const std::string dir = SyncDir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = dir + "/" + name + ".";
    std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
    marker << '1';
    marker.close();
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
        bool ready = true;
        for (int i = 0; i < nranks; ++i) {
            ready = ready && FileReady(prefix + std::to_string(i));
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    return HCCL_E_TIMEOUT;
}

uint32_t ApiIndex(const std::string& api)
{
    if (api == "write") {
        return kApiWrite;
    }
    if (api == "read") {
        return kApiRead;
    }
    if (api == "notify") {
        return kApiNotify;
    }
    if (api == "write_value") {
        return kApiWriteValue;
    }
    if (api == "faa") {
        return kApiFaa;
    }
    return kApiCas;
}

bool ValidApi(const std::string& api)
{
    return api == "write" || api == "read" || api == "notify" || api == "write_value" || api == "faa" ||
        api == "cas";
}

const char* FailurePhase(uint64_t phase)
{
    constexpr const char* names[] = {"None", "Init", "WarmupSubmit", "WarmupDrain", "TimedSubmit", "TimedDrain"};
    return phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "Unknown";
}

const char* FailureAction(uint64_t action)
{
    constexpr const char* names[] = {"None", "Immediate", "Append", "BatchCommit", "Drain"};
    return action < sizeof(names) / sizeof(names[0]) ? names[action] : "Unknown";
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

uint32_t SlotBytes(uint32_t api, uint32_t payloadBytes)
{
    if (api == kApiNotify) {
        return AlignUp(payloadBytes, sizeof(uint64_t)) + sizeof(uint64_t);
    }
    if (api == kApiWrite || api == kApiRead) {
        return AlignUp(payloadBytes, sizeof(uint64_t));
    }
    return sizeof(uint64_t);
}

aclError InitBuffers(void* sendBuf, void* recvBuf, size_t bytes, uint32_t api, uint32_t slotBytes)
{
    std::vector<uint8_t> send(bytes, kPayloadByte);
    std::vector<uint8_t> recv(bytes, kRemoteInitialByte);
    if (api == kApiFaa || api == kApiCas) {
        const uint64_t initial = api == kApiFaa ? kFaaInitial : kCasInitial;
        for (uint32_t slot = 0U; slot < kSlotCount; ++slot) {
            std::memcpy(recv.data() + static_cast<size_t>(slot) * slotBytes, &initial, sizeof(initial));
        }
    }
    aclError ret = aclrtMemcpy(sendBuf, bytes, send.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ? aclrtMemcpy(recvBuf, bytes, recv.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE) : ret;
}

HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle* handles, ChannelHandle& channel,
    uint32_t& remoteRecvIdx)
{
    HcclChannelDesc desc;
    HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = kTargetCommProtocol;
    desc.notifyNum = 0;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;
    uint32_t* layers = nullptr;
    uint32_t layerNum = 0U;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));
    if (layers == nullptr || layerNum == 0U) {
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

uint32_t OperationCount(uint32_t total, uint32_t slot)
{
    return total <= slot ? 0U : (total - 1U - slot) / kSlotCount + 1U;
}

bool CheckBuffer(
    void* buffer, size_t bytes, uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t iterations,
    uint32_t warmup)
{
    std::vector<uint8_t> actual(bytes);
    if (aclrtMemcpy(actual.data(), bytes, buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    for (uint32_t slot = 0U; slot < kSlotCount; ++slot) {
        const uint32_t count = OperationCount(iterations, slot) + OperationCount(warmup, slot);
        if (count == 0U) {
            continue;
        }
        uint8_t* value = actual.data() + static_cast<size_t>(slot) * slotBytes;
        if (api == kApiWrite || api == kApiRead || api == kApiNotify) {
            const uint8_t expected = api == kApiRead ? kRemoteInitialByte : kPayloadByte;
            for (uint32_t i = 0U; i < payloadBytes; ++i) {
                if (value[i] != expected) {
                    std::cout << "CHECK_DETAIL | Side=" << (api == kApiRead ? "Sender" : "Receiver")
                              << " | Slot=" << slot << " | Byte=" << i
                              << " | Actual=" << static_cast<uint32_t>(value[i])
                              << " | Expected=" << static_cast<uint32_t>(expected) << " | FAIL" << std::endl;
                    return false;
                }
            }
            if (api == kApiNotify) {
                uint64_t notify = 0U;
                std::memcpy(&notify, value + slotBytes - sizeof(uint64_t), sizeof(notify));
                if (notify != kNotifyValue) {
                    std::cout << "CHECK_DETAIL | Side=Receiver | Slot=" << slot << " | Field=Notify"
                              << " | Actual=" << notify << " | Expected=" << kNotifyValue << " | FAIL"
                              << std::endl;
                    return false;
                }
            }
        } else {
            uint64_t result = 0U;
            std::memcpy(&result, value, sizeof(result));
            const uint64_t expected = api == kApiWriteValue ? kInlineValue :
                (api == kApiFaa ? kFaaInitial + static_cast<uint64_t>(count) * kFaaAdd : kCasSwap);
            if (result != expected) {
                std::cout << "CHECK_DETAIL | Side=Receiver | Slot=" << slot << " | Actual=" << result
                          << " | Expected=" << expected << " | Operations=" << count << " | FAIL" << std::endl;
                return false;
            }
        }
    }
    return true;
}

bool CheckAtomicFetchBuffer(
    void* buffer, size_t bytes, uint32_t api, uint32_t slotBytes, uint32_t iterations, uint32_t warmup)
{
    std::vector<uint8_t> actual(bytes);
    if (aclrtMemcpy(actual.data(), bytes, buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    for (uint32_t slot = 0U; slot < kSlotCount; ++slot) {
        const uint32_t count = OperationCount(iterations, slot) + OperationCount(warmup, slot);
        if (count == 0U) {
            continue;
        }
        uint64_t result = 0U;
        std::memcpy(&result, actual.data() + static_cast<size_t>(slot) * slotBytes, sizeof(result));
        const uint64_t expected = api == kApiFaa ?
            kFaaInitial + static_cast<uint64_t>(count - 1U) * kFaaAdd :
            (count == 1U ? kCasInitial : kCasSwap);
        if (result != expected) {
            std::cout << "CHECK_DETAIL | Side=Sender | Field=AtomicFetch | Slot=" << slot
                      << " | Actual=" << result << " | Expected=" << expected << " | Operations=" << count
                      << " | FAIL" << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 11) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> <api> <iterations> <warmup> <payload> <batch_size>"
                     " <immediate|batch|group> <group_lanes>" << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string apiName = argv[4];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t batchSize = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const std::string mode = argv[9];
    const uint32_t groupLanes = static_cast<uint32_t>(std::strtoul(argv[10], nullptr, 10));
    if ((nranks != 2 && nranks != 8) || rank < 0 || rank >= nranks || !ValidApi(apiName) || iterations == 0U ||
        payloadBytes < 2U || batchSize == 0U || batchSize > kMaxBatchSize ||
        (mode != "immediate" && mode != "batch" && mode != "group") ||
        (mode == "batch" && batchSize != 1U) ||
        (mode == "group" && (groupLanes <= 1U || groupLanes > kMaxGroupLanes || batchSize != groupLanes ||
         iterations % groupLanes != 0U || warmup % groupLanes != 0U)) ||
        (mode != "group" && groupLanes != 1U)) {
        return 1;
    }
    const bool isSender = (static_cast<uint32_t>(rank) & 1U) == 0U;
    const uint32_t api = ApiIndex(apiName);
    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const size_t bufferBytes = static_cast<size_t>(slotBytes) * kSlotCount;

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    HCCLCHECK(InitComm(rank, nranks, rootInfoFile, &comm));
    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, api, slotBytes));
    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    const uint32_t peer = static_cast<uint32_t>(rank) ^ 1U;
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));

    void* timingBuf = nullptr;
    bool pass = true;
    if (isSender) {
        uint64_t timing[kTimingWords] = {};
        ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        LaunchSimtBatchPerf(stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, slotBytes,
            iterations, warmup, batchSize, api, mode != "immediate", mode == "group", groupLanes);
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
        const aclError syncRet = aclrtSynchronizeStreamWithTimeout(stream, 5000);
        if (syncRet != ACL_SUCCESS) {
            std::cout << "DIAG_PROGRESS | ComputeStreamSync=" << syncRet << std::endl;
            return syncRet;
        }
#else
        ACLCHECK(aclrtSynchronizeStream(stream));
#endif
        ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));
        const bool devicePass = static_cast<int64_t>(timing[kStatusIndex]) == 0 &&
            timing[kCompletedIndex] == iterations;
        bool localCheckPass = true;
        if (devicePass && api == kApiRead) {
            localCheckPass = CheckBuffer(sendBuf, bufferBytes, api, payloadBytes, slotBytes, iterations, warmup);
        } else if (devicePass && (api == kApiFaa || api == kApiCas)) {
            localCheckPass = CheckAtomicFetchBuffer(
                sendBuf, bufferBytes, api, slotBytes, iterations, warmup);
        }
        pass = devicePass && localCheckPass;
        const uint32_t dataBytes = api == kApiWrite || api == kApiRead || api == kApiNotify ? payloadBytes : 8U;
        const double issueNs = static_cast<double>(timing[kIssueCyclesIndex]) / iterations;
        const double bandwidth = timing[kCompletionCyclesIndex] == 0U ? 0.0 :
            static_cast<double>(dataBytes) * iterations / timing[kCompletionCyclesIndex];
        const uint32_t lanes = mode == "group" ? groupLanes : 1U;
        const uint32_t batchItems = mode == "immediate" ? 1U : batchSize;
        std::cout << std::fixed << std::setprecision(6) << "PERF_DATA | API=" << apiName
                  << " | DataSize/B=" << dataBytes << " | BatchSize=" << batchItems << " | Lanes=" << lanes
                  << " | DBCount=" << timing[kPublishCountIndex]
                  << " | Mode=" << (mode == "group" ? "GroupBatch" : mode == "batch" ? "ExplicitBatch" : "Immediate")
                  << " | Bandwidth/GB/s=" << bandwidth << " | IssueTime/ns=" << issueNs;
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
        if (mode != "immediate") {
            const double commitNs = static_cast<double>(timing[kCommitCyclesIndex]) / iterations;
            const double appendNs = issueNs > commitNs ? issueNs - commitNs : 0.0;
            const double reserveNs = static_cast<double>(timing[kReserveCyclesIndex]) / iterations;
            const double emitNs = static_cast<double>(timing[kEmitCyclesIndex]) / iterations;
            const double publishNs = static_cast<double>(timing[kDwqePublishCyclesIndex]) / iterations;
            const double readySyncNs = static_cast<double>(timing[kReadySyncCyclesIndex]) / iterations;
            const double returnSyncNs = static_cast<double>(timing[kReturnSyncCyclesIndex]) / iterations;
            const bool groupBatch = mode == "group";
            const double appendOtherNs = appendNs > emitNs ? appendNs - emitNs : 0.0;
            // In GroupBatch, Publish/Reserve execute on the tail lane while ReturnSync measures
            // how long lane 0 waits for that work. They overlap and must not be added together.
            const double measuredCommitNs = groupBatch ? readySyncNs + returnSyncNs : publishNs + reserveNs;
            const double commitOtherNs = commitNs > measuredCommitNs ? commitNs - measuredCommitNs : 0.0;
            std::cout << " | AppendTime/ns=" << appendNs << " | CommitTime/ns=" << commitNs
                       << " | EmitTime/ns=" << emitNs << " | AppendOtherTime/ns=" << appendOtherNs
                       << " | ReadySyncTime/ns=" << readySyncNs << " | PublishTime/ns=" << publishNs
                       << " | ReserveTime/ns=" << reserveNs << " | ReturnSyncTime/ns=" << returnSyncNs
                       << " | CommitOtherTime/ns=" << commitOtherNs;
        }
#endif
        std::cout << std::endl;
        if (!pass) {
            std::cout << "FAIL_DETAIL | Rank=" << rank << " | Peer=" << peer
                      << " | StatusCode=" << static_cast<int64_t>(timing[kStatusIndex])
                      << " | FailurePhase=" << FailurePhase(timing[kFailurePhaseIndex])
                      << " | FailureAction=" << FailureAction(timing[kFailureActionIndex])
                      << " | FailureOperation=" << timing[kFailureOperationIndex]
                      << " | LocalCheck=" << (!devicePass ? "SKIP" : (localCheckPass ? "PASS" : "FAIL"))
                      << std::endl;
        }
    }

    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    if (!isSender && api != kApiRead) {
        pass = CheckBuffer(recvBuf, bufferBytes, api, payloadBytes, slotBytes, iterations, warmup);
        if (!pass) {
            std::cout << "FAIL_DETAIL | Rank=" << rank << " | Peer=" << peer
                      << " | API=" << apiName << " | Side=Receiver" << std::endl;
        }
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
