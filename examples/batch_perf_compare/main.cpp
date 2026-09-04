/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <algorithm>
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

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"

#include "batch_perf_compare_common.h"

extern void LaunchSimdBatchPerf(
    void* stream, uint64_t channel, void* local, void* remote, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize);
extern void LaunchSimtMultiJettyPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount);

namespace {
using namespace batch_perf_compare;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr CommProtocol kProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendTag = "batchPerfCompareSend";
constexpr const char* kRecvTag = "batchPerfCompareRecv";

#define CHECK_ACL(call)                                                                            \
    do {                                                                                           \
        const aclError checkRet = (call);                                                          \
        if (checkRet != ACL_SUCCESS) {                                                             \
            std::cerr << #call << " failed, ret=" << checkRet << std::endl;                       \
            return checkRet;                                                                       \
        }                                                                                          \
    } while (0)
#define CHECK_HCCL(call)                                                                           \
    do {                                                                                           \
        const HcclResult checkRet = (call);                                                        \
        if (checkRet != HCCL_SUCCESS) {                                                            \
            std::cerr << #call << " failed, ret=" << checkRet << std::endl;                       \
            return checkRet;                                                                       \
        }                                                                                          \
    } while (0)

bool FileReady(const std::string& path, size_t bytes = 1U)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(bytes);
}

HcclResult InitComm(uint32_t rank, uint32_t nranks, const std::string& rootFile, HcclComm* comm)
{
    HcclRootInfo rootInfo {};
    if (rank == kRootRank) {
        CHECK_HCCL(HcclGetRootInfo(&rootInfo));
        std::ofstream output(rootFile + ".tmp", std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
        output.close();
        if (!output || std::rename((rootFile + ".tmp").c_str(), rootFile.c_str()) != 0) {
            return HCCL_E_SYSCALL;
        }
    } else {
        const uint32_t polls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
        for (uint32_t poll = 0U; poll < polls && !FileReady(rootFile, sizeof(rootInfo)); ++poll) {
            std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
        }
        std::ifstream input(rootFile, std::ios::binary);
        input.read(reinterpret_cast<char*>(&rootInfo), sizeof(rootInfo));
        if (input.gcount() != static_cast<std::streamsize>(sizeof(rootInfo))) {
            return HCCL_E_TIMEOUT;
        }
    }
    CHECK_HCCL(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
    return HCCL_SUCCESS;
}

HcclResult HostBarrier(uint32_t rank, uint32_t nranks, const char* name)
{
    const char* syncValue = std::getenv("BATCH_PERF_COMPARE_SYNC_DIR");
    const std::string syncDir = syncValue == nullptr ? "/tmp/batch_perf_compare" : syncValue;
    if (mkdir(syncDir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = syncDir + "/" + name + ".";
    std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
    marker << '1';
    marker.close();
    const uint32_t polls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < polls; ++poll) {
        bool ready = true;
        for (uint32_t peer = 0U; peer < nranks; ++peer) {
            ready = ready && FileReady(prefix + std::to_string(peer));
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
    return api == "write" ? kApiWrite : api == "read" ? kApiRead : kApiNotify;
}

const char* DiagPhase(uint64_t phase)
{
    constexpr const char* names[] = {
        "None", "MakeHandle", "WarmupAppend", "WarmupCommit", "WarmupDrain",
        "TimedAppend", "TimedCommit", "TimedDrain"};
    return phase < sizeof(names) / sizeof(names[0]) ? names[phase] : "Unknown";
}

void PrintDiagnostics(const uint64_t* timing, uint32_t jettyCount)
{
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        const uint64_t* d = timing + kDiagBaseIndex + jetty * kDiagWordsPerJetty;
        if (d[0] == kDiagPhaseNone) {
            continue;
        }
        std::cout << "DIAG | Jetty=" << jetty << " | Phase=" << DiagPhase(d[0])
                  << " | Status=" << static_cast<int64_t>(d[1]) << " | Operation=" << d[2]
                  << " | Rank=" << d[3] << " | Head=0x" << std::hex << d[4]
                  << " | ReservedHead=0x" << d[5] << std::dec << " | SqTail=" << d[6]
                  << " | CachedSqTail=" << d[7] << " | SqDepth=" << d[8]
                  << " | ItemBB=" << d[9] << " | GroupSize=" << d[10]
                  << " | ReservationStatus=" << static_cast<int64_t>(d[11])
                  << " | CqTail=" << d[12] << " | CqDepth=" << d[13]
                  << " | Cqe0=0x" << std::hex << d[14] << " | Cqe1=0x" << d[15]
                  << " | PublisherHead=" << std::dec << d[16] << " | SqDiffMask=0x" << std::hex << d[17]
                  << " | DwqeDiffMask=0x" << d[18] << " | SqFirstDiff=" << std::dec << d[19]
                  << " | SqExpected=0x" << std::hex << d[20] << " | SqActual=0x" << d[21]
                  << " | DwqeFirstDiff=" << std::dec << d[22] << " | DwqeExpected=0x" << std::hex << d[23]
                  << " | DwqeActual=0x" << d[24] << " | UbWord0=0x" << d[25]
                  << " | SqWord0=0x" << d[26] << " | DwqeWord0=0x" << d[27]
                  << " | HeadAddr=0x" << d[28] << " | DwqeAddr=0x" << d[29]
                  << " | SqBaseAddr=0x" << d[30] << " | Context=0x" << d[31]
                  << " | Channel=0x" << d[32] << std::dec << std::endl;
    }
}

uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

uint32_t SlotBytes(uint32_t api, uint32_t payloadBytes)
{
    return AlignUp(payloadBytes, sizeof(uint64_t)) + (api == kApiNotify ? sizeof(uint64_t) : 0U);
}

aclError InitBuffers(void* send, void* recv, size_t bytes)
{
    std::vector<uint8_t> sendData(bytes, kPayloadByte);
    std::vector<uint8_t> recvData(bytes, kRemoteInitialByte);
    aclError ret = aclrtMemcpy(send, bytes, sendData.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ? aclrtMemcpy(recv, bytes, recvData.data(), bytes, ACL_MEMCPY_HOST_TO_DEVICE) : ret;
}

HcclResult BuildChannelDesc(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, HcclChannelDesc& desc)
{
    CHECK_HCCL(HcclChannelDescInit(&desc, 1U));
    desc.remoteRank = peer;
    desc.channelProtocol = kProtocol;
    desc.notifyNum = 0U;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;
    uint32_t* layers = nullptr;
    uint32_t layerCount = 0U;
    CHECK_HCCL(HcclRankGraphGetLayers(comm, &layers, &layerCount));
    for (uint32_t layer = 0U; layer < layerCount; ++layer) {
        CommLink* links = nullptr;
        uint32_t linkCount = 0U;
        CHECK_HCCL(HcclRankGraphGetLinks(comm, layers[layer], rank, peer, &links, &linkCount));
        for (uint32_t link = 0U; link < linkCount; ++link) {
            if (links[link].linkAttr.linkProtocol == kProtocol) {
                desc.localEndpoint = links[link].srcEndpointDesc;
                desc.remoteEndpoint = links[link].dstEndpointDesc;
                return HCCL_SUCCESS;
            }
        }
    }
    return HCCL_E_NOT_SUPPORT;
}

HcclResult AcquireChannel(
    HcclComm comm, uint32_t rank, uint32_t peer, HcclMemHandle* handles, ChannelHandle& channel,
    uint64_t& remoteRecvAddr, uint32_t& remoteRecvIndex)
{
    HcclChannelDesc desc {};
    CHECK_HCCL(BuildChannelDesc(comm, rank, peer, handles, desc));
    CHECK_HCCL(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &desc, 1U, &channel));
    uint32_t memCount = 0U;
    CommMem* remoteMems = nullptr;
    char** tags = nullptr;
    CHECK_HCCL(HcclChannelGetRemoteMems(comm, channel, &memCount, &remoteMems, &tags));
    for (uint32_t mem = 0U; mem < memCount; ++mem) {
        if (tags[mem] != nullptr && std::strcmp(tags[mem], kRecvTag) == 0) {
            remoteRecvAddr = reinterpret_cast<uint64_t>(remoteMems[mem].addr);
            remoteRecvIndex = mem;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

bool CheckData(
    void* buffer, size_t bytes, uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionCount)
{
    std::vector<uint8_t> actual(bytes);
    if (aclrtMemcpy(actual.data(), bytes, buffer, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const uint8_t expected = api == kApiRead ? kRemoteInitialByte : kPayloadByte;
    const size_t regionBytes = static_cast<size_t>(slotBytes) * kSlotCount;
    bool allPass = true;
    for (uint32_t region = 0U; region < regionCount; ++region) {
        uint32_t passedSlots = 0U;
        uint32_t firstFailedSlot = kSlotCount;
        uint32_t firstFailedByte = 0U;
        uint64_t firstActual = 0U;
        uint64_t firstExpected = 0U;
        for (uint32_t slot = 0U; slot < kSlotCount; ++slot) {
            const uint8_t* value = actual.data() + region * regionBytes + static_cast<size_t>(slot) * slotBytes;
            bool slotPass = true;
            for (uint32_t byte = 0U; byte < payloadBytes; ++byte) {
                if (value[byte] != expected) {
                    slotPass = false;
                    if (firstFailedSlot == kSlotCount) {
                        firstFailedSlot = slot;
                        firstFailedByte = byte;
                        firstActual = value[byte];
                        firstExpected = expected;
                    }
                    break;
                }
            }
            if (slotPass && api == kApiNotify) {
                uint64_t notify = 0U;
                std::memcpy(&notify, value + slotBytes - sizeof(notify), sizeof(notify));
                if (notify != kNotifyValue) {
                    slotPass = false;
                    if (firstFailedSlot == kSlotCount) {
                        firstFailedSlot = slot;
                        firstFailedByte = slotBytes - sizeof(notify);
                        firstActual = notify;
                        firstExpected = kNotifyValue;
                    }
                }
            }
            passedSlots += slotPass ? 1U : 0U;
        }
        if (passedSlots != kSlotCount) {
            allPass = false;
            std::cout << "DATA_DIAG | Region=" << region << " | PassedSlots=" << passedSlots
                      << " | TotalSlots=" << kSlotCount << " | FirstFailedSlot=" << firstFailedSlot
                      << " | FirstFailedByte=" << firstFailedByte << " | Actual=0x" << std::hex << firstActual
                      << " | Expected=0x" << firstExpected << std::dec << std::endl;
        }
    }
    return allPass;
}
} // namespace

int main(int argc, char* argv[])
{
    if (argc != 11) {
        std::cerr << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_file> <simd|multi-jetty> <write|read|notify>"
                     " <iterations> <warmup> <payload> <batch_size> <jettys>" << std::endl;
        return 1;
    }
    const uint32_t rank = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const uint32_t nranks = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const std::string rootFile = argv[3];
    const std::string path = argv[4];
    const std::string apiName = argv[5];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const uint32_t batchSize = static_cast<uint32_t>(std::strtoul(argv[9], nullptr, 10));
    const uint32_t jettyCount = static_cast<uint32_t>(std::strtoul(argv[10], nullptr, 10));
    const bool simd = path == "simd";
    if ((nranks != 2U && nranks != 8U) || rank >= nranks || (!simd && path != "multi-jetty") ||
        (apiName != "write" && apiName != "read" && apiName != "notify") || payloadBytes < 2U ||
        iterations == 0U || batchSize == 0U || iterations % batchSize != 0U || warmup % batchSize != 0U ||
        (simd && (batchSize > kMaxBatchSize || jettyCount != 1U)) ||
        (!simd && ((batchSize != 1U && batchSize != 8U && batchSize != 32U && batchSize != 64U &&
        batchSize != 128U && batchSize != 256U) || (jettyCount != 1U && jettyCount != 2U &&
         jettyCount != 4U && jettyCount != 8U)))) {
        return 1;
    }

    const bool sender = (rank & 1U) == 0U;
    const uint32_t peer = rank ^ 1U;
    const uint32_t api = ApiIndex(apiName);
    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const uint32_t regionBytes = slotBytes * kSlotCount;
    const size_t bufferBytes = static_cast<size_t>(regionBytes) * jettyCount;
    CHECK_ACL(aclInit(nullptr));
    CHECK_ACL(aclrtSetDevice(static_cast<int32_t>(rank)));
    aclrtStream stream = nullptr;
    CHECK_ACL(aclrtCreateStream(&stream));
    const uint32_t commCount = simd ? 1U : jettyCount;
    std::vector<HcclComm> comms(commCount, nullptr);
    for (uint32_t jetty = 0U; jetty < commCount; ++jetty) {
        CHECK_HCCL(InitComm(rank, nranks, rootFile + ".jetty_" + std::to_string(jetty), &comms[jetty]));
    }

    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    CHECK_ACL(aclrtMalloc(&sendBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&recvBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(InitBuffers(sendBuffer, recvBuffer, bufferBytes));
    CommMem sendMem {COMM_MEM_TYPE_DEVICE, sendBuffer, bufferBytes};
    CommMem recvMem {COMM_MEM_TYPE_DEVICE, recvBuffer, bufferBytes};
    std::vector<std::array<HcclMemHandle, 2U>> handles(commCount, {nullptr, nullptr});
    std::vector<ChannelHandle> channels(commCount);
    std::vector<uint64_t> remoteRecvAddrs(commCount);
    std::vector<uint32_t> remoteRecvIndices(commCount);
    for (uint32_t jetty = 0U; jetty < commCount; ++jetty) {
        CHECK_HCCL(HcclCommMemReg(comms[jetty], kSendTag, &sendMem, &handles[jetty][0]));
        CHECK_HCCL(HcclCommMemReg(comms[jetty], kRecvTag, &recvMem, &handles[jetty][1]));
        CHECK_HCCL(AcquireChannel(
            comms[jetty], rank, peer, handles[jetty].data(), channels[jetty], remoteRecvAddrs[jetty],
            remoteRecvIndices[jetty]));
    }
    CHECK_HCCL(HostBarrier(rank, nranks, "ready"));

    void* timingDevice = nullptr;
    void* channelsDevice = nullptr;
    void* remoteIndicesDevice = nullptr;
    uint64_t timing[kTimingWords] = {};
    bool pass = true;
    if (sender) {
        CHECK_ACL(aclrtMalloc(&timingDevice, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(timingDevice, sizeof(timing), 0, sizeof(timing)));
        if (simd) {
            LaunchSimdBatchPerf(
                stream, channels[0], sendBuffer, reinterpret_cast<void*>(remoteRecvAddrs[0]), timingDevice, api,
                payloadBytes, slotBytes, iterations, warmup, batchSize);
        } else {
            std::vector<uint64_t> channelValues(channels.begin(), channels.end());
            CHECK_ACL(aclrtMalloc(&channelsDevice, channelValues.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMalloc(
                &remoteIndicesDevice, remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEM_MALLOC_HUGE_FIRST));
            CHECK_ACL(aclrtMemcpy(
                channelsDevice, channelValues.size() * sizeof(uint64_t), channelValues.data(),
                channelValues.size() * sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
            CHECK_ACL(aclrtMemcpy(
                remoteIndicesDevice, remoteRecvIndices.size() * sizeof(uint32_t), remoteRecvIndices.data(),
                remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEMCPY_HOST_TO_DEVICE));
            LaunchSimtMultiJettyPerf(
                stream, channelsDevice, remoteIndicesDevice, timingDevice, api, payloadBytes, slotBytes,
                regionBytes, iterations, warmup, batchSize, jettyCount);
        }
        CHECK_ACL(aclrtSynchronizeStream(stream));
        CHECK_ACL(aclrtMemcpy(timing, sizeof(timing), timingDevice, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));
        PrintDiagnostics(timing, simd ? 0U : jettyCount);
        const uint32_t totalOperations = iterations * (simd ? 1U : jettyCount);
        pass = static_cast<int64_t>(timing[kStatusIndex]) == 0 && timing[kCompletedIndex] == totalOperations;
        if (pass && api == kApiRead) {
            pass = CheckData(sendBuffer, bufferBytes, api, payloadBytes, slotBytes, jettyCount);
        }
        const uint32_t lanes = simd ? 1U : std::min(batchSize * jettyCount, 1024U);
        const uint32_t waves = simd ? 1U : (batchSize * jettyCount + 1023U) / 1024U;
        const double issueNs = static_cast<double>(timing[kIssueCyclesIndex]) / totalOperations;
        const double bandwidth = timing[kCompletionCyclesIndex] == 0U ? 0.0 :
            static_cast<double>(payloadBytes) * totalOperations / timing[kCompletionCyclesIndex];
        std::cout << std::fixed << std::setprecision(6)
                  << "PERF_DATA | Path=" << (simd ? "SIMD" : "SIMT-MultiJetty")
                  << " | API=" << apiName << " | DataSize/B=" << payloadBytes
                  << " | BatchSize=" << batchSize << " | Lanes=" << lanes << " | Cores=1 | Waves=" << waves
                  << " | Jettys=" << jettyCount << " | DBCount=" << timing[kPublishCountIndex]
                  << " | Bandwidth/GB/s=" << bandwidth << " | IssueTime/ns=" << issueNs << std::endl;
    }

    CHECK_HCCL(HostBarrier(rank, nranks, "completed"));
    if (!sender && api != kApiRead) {
        pass = CheckData(recvBuffer, bufferBytes, api, payloadBytes, slotBytes, jettyCount);
    }
    if (timingDevice != nullptr) {
        CHECK_ACL(aclrtFree(timingDevice));
    }
    if (channelsDevice != nullptr) {
        CHECK_ACL(aclrtFree(channelsDevice));
    }
    if (remoteIndicesDevice != nullptr) {
        CHECK_ACL(aclrtFree(remoteIndicesDevice));
    }
    CHECK_ACL(aclrtFree(sendBuffer));
    CHECK_ACL(aclrtFree(recvBuffer));
    for (HcclComm comm : comms) {
        CHECK_HCCL(HcclCommDestroy(comm));
    }
    CHECK_ACL(aclrtDestroyStream(stream));
    CHECK_ACL(aclFinalize());
    return pass ? 0 : 2;
}
