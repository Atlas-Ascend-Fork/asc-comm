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
 * \brief Host side of the SIMT WriteNbi/WriteValueNbi performance example.
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

#include "simt_write_perf_common.h"

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

extern void LaunchSimtWritePerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup, uint32_t api, bool commitEach);

namespace {
using namespace simt_write_perf;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtWritePerfSendBuf";
constexpr const char* kRecvBufTag = "simtWritePerfRecvBuf";

// Consecutive WQEs rotate over this many slots so they do not all land on one cache line.
// Capped so a large payload does not blow the buffer up: the product is the allocation size.
constexpr uint32_t kMaxSlotCount = 64U;
constexpr size_t kMaxBufferBytes = 64UL * 1024UL * 1024UL;
constexpr size_t kSlotAlign = 128U;

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

std::string SyncDir()
{
    const char* value = std::getenv("SIMT_WRITE_PERF_SYNC_DIR");
    return value == nullptr ? "/tmp/simt_write_perf" : value;
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

// WriteValueNbi always carries 8 bytes inline, whatever --payload-bytes says.
uint32_t DataBytes(uint32_t api, uint32_t payloadBytes)
{
    return api == kApiWrite ? payloadBytes : static_cast<uint32_t>(sizeof(uint64_t));
}

// Slots are padded so each starts on its own 128-byte boundary.
uint32_t SlotBytes(uint32_t api, uint32_t payloadBytes)
{
    return static_cast<uint32_t>(AlignUp(DataBytes(api, payloadBytes), kSlotAlign));
}

// Rotating over fewer slots is fine; rotating over more than the payload budget allows is not.
uint32_t SlotCount(uint32_t slotBytes)
{
    uint32_t affordable = static_cast<uint32_t>(kMaxBufferBytes / slotBytes);
    if (affordable == 0U) {
        return 0U;
    }
    return affordable < kMaxSlotCount ? affordable : kMaxSlotCount;
}

// Fills the sender's payload and clears the receiver so an operation that never lands reads
// back as 0. Both ranks call this: the sender needs the payload, the receiver a clean slate.
aclError InitBuffers(void* sendBuf, void* recvBuf, size_t bytes, uint32_t api)
{
    std::vector<uint8_t> send(bytes, kPayloadByte);
    if (api == kApiWriteValue) {
        // The inline value comes from the WQE, so the send buffer is never read. Fill it with a
        // different byte so a receive buffer that accidentally mirrors it is not mistaken for a
        // correct inline write.
        std::fill(send.begin(), send.end(), static_cast<uint8_t>(0U));
    }
    const std::vector<uint8_t> recv(bytes, 0U);
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
        std::cout << "no UBC_CTP link to peer " << peerRank << ", SIMT requires the URMA path" << std::endl;
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

// Every slot the run touched must hold the expected payload. Slots are revisited across
// iterations, so the check is per slot rather than per operation: the last write to a slot
// leaves the same bytes as the first.
bool CheckReceiver(
    void* recvBuf, size_t bytes, uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t slotCount,
    uint32_t totalOperations)
{
    std::vector<uint8_t> actual(bytes, 0U);
    if (aclrtMemcpy(actual.data(), bytes, recvBuf, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const uint32_t touched = totalOperations < slotCount ? totalOperations : slotCount;
    for (uint32_t slot = 0U; slot < touched; ++slot) {
        const size_t base = static_cast<size_t>(slot) * slotBytes;
        if (api == kApiWrite) {
            for (uint32_t i = 0U; i < payloadBytes; ++i) {
                if (actual[base + i] != kPayloadByte) {
                    std::cout << "receiver slot " << slot << " byte " << i << " actual=0x" << std::hex
                              << static_cast<uint32_t>(actual[base + i]) << " expected=0x"
                              << static_cast<uint32_t>(kPayloadByte) << std::dec << std::endl;
                    return false;
                }
            }
        } else {
            uint64_t value = 0U;
            std::memcpy(&value, actual.data() + base, sizeof(value));
            if (value != kInlineValue) {
                std::cout << "receiver slot " << slot << " actual=0x" << std::hex << value << " expected=0x"
                          << kInlineValue << std::dec << std::endl;
                return false;
            }
        }
    }
    return true;
}

bool ParseApi(const std::string& name, uint32_t& api)
{
    if (name == "write") {
        api = kApiWrite;
        return true;
    }
    if (name == "write_value") {
        api = kApiWriteValue;
        return true;
    }
    return false;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 9) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> <write|write_value>"
                     " <iterations> <warmup> <payload_bytes> <immediate|last>"
                  << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string apiName = argv[4];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const std::string commitMode = argv[8];

    uint32_t api = kApiWrite;
    if (nranks < 2) {
        std::cout << "nranks must be at least 2." << std::endl;
        return 1;
    }
    if (!ParseApi(apiName, api)) {
        std::cout << "Unknown api: " << apiName << ". Expected write or write_value." << std::endl;
        return 1;
    }
    if (iterations == 0U) {
        std::cout << "iterations must be greater than 0." << std::endl;
        return 1;
    }
    if (payloadBytes < kMinPayloadBytes) {
        std::cout << "payload_bytes must be at least " << kMinPayloadBytes
                  << ": a committed WriteNbi splits its payload across two SGEs and an SGE length may not be 0."
                  << std::endl;
        return 1;
    }
    if (commitMode != "immediate" && commitMode != "last") {
        std::cout << "Unknown commit mode: " << commitMode << ". Expected immediate or last." << std::endl;
        return 1;
    }
    const bool commitEach = commitMode == "immediate";

    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const uint32_t slotCount = SlotCount(slotBytes);
    if (slotCount == 0U) {
        std::cout << "payload_bytes too large: one slot exceeds the " << kMaxBufferBytes << "-byte buffer budget."
                  << std::endl;
        return 1;
    }
    const size_t bufferBytes = static_cast<size_t>(slotBytes) * slotCount;

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    HCCLCHECK(InitCommByRootInfo(rank, nranks, rootInfoFile, &comm));

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, api));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));

    const uint32_t peer = rank == kSenderRank ? kReceiverRank : kSenderRank;
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    HCCLCHECK(AcquirePeerChannel(comm, static_cast<uint32_t>(rank), peer, handles, channel, remoteRecvIdx));
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));

    void* timingBuf = nullptr;
    uint64_t timing[kTimingWords] = {};
    bool pass = true;
    // Local table index 0 is the CCL buffer, so the explicitly registered send buffer is at 1.
    constexpr uint32_t kLocalSendIdx = 1U;
    if (rank == kSenderRank) {
        ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        LaunchSimtWritePerf(
            stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, slotBytes, slotCount, iterations,
            warmup, api, commitEach);
        ACLCHECK(aclrtSynchronizeStream(stream));
        ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));

        pass = static_cast<int64_t>(timing[kDrainStatusIndex]) == 0 && timing[kCompletedIndex] == iterations;
        const uint32_t dataBytes = DataBytes(api, payloadBytes);
        const double issueUs = static_cast<double>(timing[kIssueCyclesIndex]) / 1000.0;
        const double averageIssueNs = static_cast<double>(timing[kIssueCyclesIndex]) / iterations;
        const double completionUs = static_cast<double>(timing[kCompletionCyclesIndex]) / 1000.0;
        const double completionBandwidth =
            timing[kCompletionCyclesIndex] == 0U ?
                0.0 :
                static_cast<double>(dataBytes) * iterations / timing[kCompletionCyclesIndex];
        // A committed WriteNbi fills the 128-byte DWQE window (two basic blocks); a deferred one
        // fits in a single 64-byte block. WriteValueNbi is always two blocks.
        const uint32_t sqBlocksPerWqe = (api == kApiWrite && !commitEach) ? 1U : 2U;
        std::cout << std::fixed << std::setprecision(6) << "RESULT | Path=SIMT-t1"
                  << " | API=" << apiName << " | DataSize/B=" << dataBytes << " | WqeCount=" << iterations
                  << " | Warmup=" << warmup << " | CommitMode=" << (commitEach ? "Immediate" : "Last")
                  << " | SqBlocksPerWqe=" << sqBlocksPerWqe << " | Slots=" << slotCount << " | IssueTime/us=" << issueUs
                  << " | AverageIssue/ns=" << averageIssueNs << " | CompletionTime/us=" << completionUs
                  << " | CompletionBandwidth/GB/s=" << completionBandwidth
                  << " | DrainStatus=" << static_cast<int64_t>(timing[kDrainStatusIndex])
                  << " | Completed=" << timing[kCompletedIndex] << std::endl;
    }

    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    if (rank == kReceiverRank) {
        const uint32_t totalOperations = iterations + warmup;
        pass = CheckReceiver(recvBuf, bufferBytes, api, payloadBytes, slotBytes, slotCount, totalOperations);
    }
    if (rank == kSenderRank || rank == kReceiverRank) {
        std::cout << "[rank " << rank << "] " << apiName << " " << commitMode << " " << (pass ? "PASS" : "FAIL")
                  << std::endl;
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
