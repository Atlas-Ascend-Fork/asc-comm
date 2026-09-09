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
 * \brief Host side of the SIMT URMA performance example.
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

#include "simt_urma_perftest_common.h"

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

extern void LaunchSimtUrmaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup, uint32_t api, bool commitEach);

namespace {
using namespace simt_urma_perftest;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UB_CTP;
constexpr const char* kSendBufTag = "simtUrmaPerfSendBuf";
constexpr const char* kRecvBufTag = "simtUrmaPerfRecvBuf";

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

bool MarkerReady(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size != 0;
}

// syncDir holds the barrier marker files and is passed in on the command line rather than through
// the environment: an exported variable is easy to lose across a shell wrapper or a scheduler, and
// losing it silently sent every rank to the same default directory, where markers from a previous
// run could let a barrier pass immediately.
HcclResult HostBarrier(int rank, int nranks, const std::string& syncDir, const char* name)
{
    if (mkdir(syncDir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = syncDir + "/" + name + ".";
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

// Only WriteNbi and WriteWithNotifyNbi move a configurable payload; WriteValueNbi always
// carries 8 bytes inline and the atomics always operate on one 8-byte word.
uint32_t DataBytes(uint32_t api, uint32_t payloadBytes)
{
    return ApiUsesPayload(api) ? payloadBytes : static_cast<uint32_t>(sizeof(uint64_t));
}

// Bytes a slot must hold. Notify writes its signal word immediately after the payload, rounded
// up to 8 bytes for alignment, so its slot needs room for both; every other interface only needs
// the payload itself. This is deliberately not DataBytes: that reports what the interface moves,
// for the bandwidth figure, whereas this sizes the buffer.
uint32_t SlotFootprint(uint32_t api, uint32_t payloadBytes)
{
    const uint32_t dataBytes = DataBytes(api, payloadBytes);
    if (api != kApiNotify) {
        return dataBytes;
    }
    const uint32_t notifyOffset = static_cast<uint32_t>(AlignUp(dataBytes, sizeof(uint64_t)));
    return notifyOffset + static_cast<uint32_t>(sizeof(uint64_t));
}

// Slots are padded so each starts on its own 128-byte boundary.
uint32_t SlotBytes(uint32_t api, uint32_t payloadBytes)
{
    return static_cast<uint32_t>(AlignUp(SlotFootprint(api, payloadBytes), kSlotAlign));
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
    if (!ApiReadsSendBuffer(api)) {
        // WriteValueNbi takes its payload from the WQE and the atomics only write a fetched value
        // back, so the send buffer is never read as a source. Fill it with a different byte so a
        // receive buffer that accidentally mirrors it is not mistaken for a correct write.
        std::fill(send.begin(), send.end(), static_cast<uint8_t>(0U));
    }
    std::vector<uint8_t> recv(bytes, 0U);
    if (api == kApiCas) {
        // CAS only swaps when the target already holds kCasInitial. Seeding it means the first
        // iteration performs a real swap rather than a no-op compare.
        const uint64_t initial = kCasInitial;
        std::memcpy(recv.data(), &initial, sizeof(initial));
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
        std::cout << "no UB_CTP link to peer " << peerRank << ", SIMT requires the URMA path" << std::endl;
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

// 预热和正式计时是两轮独立的 SubmitSeries，slot 下标各自从 0 重新开始，因此被写过的连续前缀
// 是两者的较大值，不是两者之和。用和会把没写过的 slot 也纳入校验（(1, 1) 只写了 slot 0 却会去
// 检查 slot 1，把正确的运行判成 FAIL），相加还可能溢出 uint32_t 回绕成 0 而整体跳过校验。
// 回归用例见 test_touched_slots.cpp。
uint32_t TouchedSlotCount(uint32_t warmup, uint32_t iterations, uint32_t slotCount)
{
    const uint32_t reach = warmup > iterations ? warmup : iterations;
    return reach < slotCount ? reach : slotCount;
}

// 按 slot 而不是按操作校验：slot 会被多轮迭代重复写入，而最后一次写入留下的字节与第一次相同。
// 只有 ApiHasStableReceiverImage 为真的接口会走到实际比对。
bool CheckReceiver(
    void* recvBuf, size_t bytes, uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t touchedSlots)
{
    if (!ApiHasStableReceiverImage(api)) {
        return true;
    }
    std::vector<uint8_t> actual(bytes, 0U);
    if (aclrtMemcpy(actual.data(), bytes, recvBuf, bytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    for (uint32_t slot = 0U; slot < touchedSlots; ++slot) {
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

// One table drives both parsing and printing so an api can never be accepted under a name the
// RESULT line does not report back.
struct ApiEntry {
    const char* name;
    uint32_t api;
};

constexpr ApiEntry kApis[] = {
    {"write", kApiWrite}, {"write_value", kApiWriteValue}, {"notify", kApiNotify}, {"faa", kApiFaa}, {"cas", kApiCas},
};

bool ParseApi(const std::string& name, uint32_t& api)
{
    for (const ApiEntry& entry : kApis) {
        if (name == entry.name) {
            api = entry.api;
            return true;
        }
    }
    return false;
}

// A committed WriteNbi fills the 128-byte DWQE window (two basic blocks); a deferred one fits in
// a single 64-byte block. Every other interface is a fixed 2-BB WQE either way.
uint32_t SqBlocksPerWqe(uint32_t api, bool commitEach) { return (api == kApiWrite && !commitEach) ? 1U : 2U; }

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 10) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> <sync_dir> <write|write_value|notify|faa|cas>"
                     " <iterations> <warmup> <payload_bytes> <immediate|last>"
                  << std::endl;
        std::cout << "  sync_dir: directory for host barrier marker files, shared by all ranks" << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string syncDir = argv[4];
    const std::string apiName = argv[5];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const std::string commitMode = argv[9];

    uint32_t api = kApiWrite;
    // 这是点对点基准测试：允许拉起 nranks(>=2) 个进程，但只有 rank 0（发送方）和 rank 1
    // （接收方）建链并计时，其余 rank 直接退出，不加入通信域。只有一个发送方计时，多方并发
    // 发送会互相争抢链路和 SQ 资源，测出来的数字没有可比性。
    if (nranks < kBenchRanks) {
        std::cout << "nranks must be at least " << kBenchRanks
                  << ": this benchmark measures point-to-point latency and bandwidth between rank 0 (sender) and "
                     "rank 1 (receiver)."
                  << std::endl;
        return 1;
    }
    if (rank < 0 || rank >= nranks) {
        std::cout << "rank must be in [0, " << nranks << ")." << std::endl;
        return 1;
    }
    if (rank >= kBenchRanks) {
        std::cout << "SKIP | Rank=" << rank << " | Only rank 0 and rank 1 run the point-to-point benchmark."
                  << std::endl;
        return 0;
    }
    if (syncDir.empty()) {
        std::cout << "sync_dir must not be empty." << std::endl;
        return 1;
    }
    if (!ParseApi(apiName, api)) {
        std::cout << "Unknown api: " << apiName << ". Expected write, write_value, notify, faa or cas." << std::endl;
        return 1;
    }
    if (iterations == 0U) {
        std::cout << "iterations must be greater than 0." << std::endl;
        return 1;
    }
    // Only the payload-carrying interfaces are constrained here; the others ignore the value.
    if (ApiUsesPayload(api) && payloadBytes < kMinPayloadBytes) {
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
    // 通信域只包含参与测试的前两个 rank，其余 rank 已提前退出，不加入通信域。
    HCCLCHECK(InitCommByRootInfo(rank, kBenchRanks, rootInfoFile, &comm));

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
    HCCLCHECK(HostBarrier(rank, kBenchRanks, syncDir, "ready"));

    void* timingBuf = nullptr;
    uint64_t timing[kTimingWords] = {};
    bool pass = true;
    // Local table index 0 is the CCL buffer, so the explicitly registered send buffer is at 1.
    constexpr uint32_t kLocalSendIdx = 1U;
    if (rank == kSenderRank) {
        ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        LaunchSimtUrmaPerf(
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
        std::cout << std::fixed << std::setprecision(6) << "RESULT | Path=SIMT-t1"
                  << " | API=" << apiName << " | DataSize/B=" << dataBytes << " | WqeCount=" << iterations
                  << " | Warmup=" << warmup << " | CommitMode=" << (commitEach ? "Immediate" : "Last")
                  << " | SqBlocksPerWqe=" << SqBlocksPerWqe(api, commitEach) << " | Slots=" << slotCount
                  << " | IssueTime/us=" << issueUs << " | AverageIssue/ns=" << averageIssueNs
                  << " | CompletionTime/us=" << completionUs << " | CompletionBandwidth/GB/s=" << completionBandwidth
                  << " | DrainStatus=" << static_cast<int64_t>(timing[kDrainStatusIndex])
                  << " | Completed=" << timing[kCompletedIndex] << std::endl;
    }

    HCCLCHECK(HostBarrier(rank, kBenchRanks, syncDir, "completed"));
    if (rank == kReceiverRank) {
        const uint32_t touchedSlots = TouchedSlotCount(warmup, iterations, slotCount);
        pass = CheckReceiver(recvBuf, bufferBytes, api, payloadBytes, slotBytes, touchedSlots);
        std::cout << "[rank " << rank << "] simt_urma_perftest " << apiName << " " << (pass ? "PASS" : "FAIL")
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
