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

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "utils/check.h"
#include "utils/hccl_comm_init.h"
#include "utils/process_manager.h"
#include "utils/rank_sync.h"
#include "utils/arg_parser.h"

#include "simt_urma_perftest_common.h"

extern void LaunchSimtUrmaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup, uint32_t api, bool commitEach);

namespace {
using namespace simt_urma_perftest;

constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UB_CTP;
constexpr const char* kSendBufTag = "simtUrmaPerfSendBuf";
constexpr const char* kRecvBufTag = "simtUrmaPerfRecvBuf";

// Consecutive WQEs rotate over this many slots so they do not all land on one cache line.
// Capped so a large payload does not blow the buffer up: the product is the allocation size.
constexpr uint32_t kMaxSlotCount = 64U;
constexpr size_t kMaxBufferBytes = 64UL * 1024UL * 1024UL;
constexpr size_t kSlotAlign = 128U;

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

// 发送侧填 payload、接收侧清零。WriteValue/atomic 系列不读 sendBuf，填不同字节以区分；
// CAS 只在目标已是初值时交换，预置初值让首轮迭代执行真实交换。
aclError InitBuffers(void* sendBuf, void* recvBuf, size_t bytes, uint32_t api)
{
    std::vector<uint8_t> send(bytes, kPayloadByte);
    if (!ApiReadsSendBuffer(api)) {
        std::fill(send.begin(), send.end(), static_cast<uint8_t>(0U));
    }
    std::vector<uint8_t> recv(bytes, 0U);
    if (api == kApiCas) {
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
    EXAMPLES_HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = kTargetCommProtocol;
    desc.notifyNum = 0;
    desc.memHandles = memHandles;
    desc.memHandleNum = 2U;

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0U;
    EXAMPLES_HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));
    if (layerNum == 0U || layers == nullptr) {
        return HCCL_E_INTERNAL;
    }
    CommLink* links = nullptr;
    uint32_t linkNum = 0U;
    EXAMPLES_HCCLCHECK(HcclRankGraphGetLinks(comm, layers[0], rank, peerRank, &links, &linkNum));
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
    EXAMPLES_HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &desc, 1U, &channel));

    uint32_t memNum = 0U;
    CommMem* remoteMems = nullptr;
    char** tags = nullptr;
    EXAMPLES_HCCLCHECK(HcclChannelGetRemoteMems(comm, channel, &memNum, &remoteMems, &tags));
    for (uint32_t i = 0U; i < memNum; ++i) {
        if (tags[i] != nullptr && std::strcmp(tags[i], kRecvBufTag) == 0) {
            remoteRecvIdx = i;
            return HCCL_SUCCESS;
        }
    }
    return HCCL_E_INTERNAL;
}

// 预热与正式计时各自从 slot 0 重新开始，被写过的前缀是两者的较大值；相加会把没写过的
// slot 纳入校验，还可能溢出回绕成 0 而整体跳过校验。
uint32_t TouchedSlotCount(uint32_t warmup, uint32_t iterations, uint32_t slotCount)
{
    const uint32_t reach = warmup > iterations ? warmup : iterations;
    return reach < slotCount ? reach : slotCount;
}

// 按 slot 而不是按操作校验：最后一次写入留下的字节与第一次相同。
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

// 一张表同时驱动解析和打印，保证 RESULT 行回报的名字与解析一致。
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

// 已提交的 WriteNbi 占满 128B DWQE（两个 BB），延迟提交只占一个 64B BB。
uint32_t SqBlocksPerWqe(uint32_t api, bool commitEach) { return (api == kApiWrite && !commitEach) ? 1U : 2U; }

} // namespace

// 单个rank的基准流程。由ProcessGroup在每个子进程中调用：只有rank 0（发送方）和
// rank 1（接收方）建链并计时。本样例两个host侧同步点的tag。
constexpr uint32_t kTagReady = examples::kTagUserBase;
constexpr uint32_t kTagCompleted = examples::kTagUserBase + 1U;

int RunPerftestRank(
    uint32_t rank, uint32_t nranks, const std::string& ctrlEndpoint, const std::string& apiName, uint32_t iterations,
    uint32_t warmup, uint32_t payloadBytes, const std::string& commitMode)
{
    if (nranks < kBenchRanks) {
        std::cout << "nranks must be at least " << kBenchRanks
                  << ": this benchmark measures point-to-point latency and bandwidth between rank 0 (sender) and "
                     "rank 1 (receiver)."
                  << std::endl;
        return 1;
    }
    if (rank >= kBenchRanks) {
        std::cout << "SKIP | Rank=" << rank << " | Only rank 0 and rank 1 run the point-to-point benchmark."
                  << std::endl;
        return 0;
    }
    examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(ctrlEndpoint, nranks) :
                                                  examples::RankSyncContext::Connect(rank, nranks, ctrlEndpoint);
    if (!sync.Ok()) {
        std::cout << "[rank " << rank << "] control channel setup failed" << std::endl;
        return 1;
    }

    uint32_t api = kApiWrite;
    if (!ParseApi(apiName, api)) {
        std::cout << "Unknown api: " << apiName << ". Expected write, write_value, notify, faa or cas." << std::endl;
        return 1;
    }
    if (iterations == 0U) {
        std::cout << "iterations must be greater than 0." << std::endl;
        return 1;
    }
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

    EXAMPLES_ACLCHECK(aclInit(nullptr));
    EXAMPLES_ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    EXAMPLES_ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    // 通信域只包含参与测试的前两个 rank，其余 rank 已提前退出，不加入通信域。
    EXAMPLES_HCCLCHECK(examples::InitCommByRootInfo(sync, &comm));

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    EXAMPLES_ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, api));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));

    const uint32_t peer = rank == kSenderRank ? kReceiverRank : kSenderRank;
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    EXAMPLES_HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    if (!sync.Barrier(kTagReady)) {
        std::cout << "[rank " << rank << "] ready barrier failed" << std::endl;
        return 1;
    }

    void* timingBuf = nullptr;
    uint64_t timing[kTimingWords] = {};
    bool pass = true;
    // 本地 buffer 表 index 0 是 CCL buffer，注册内存从 1 开始。
    constexpr uint32_t kLocalSendIdx = 1U;
    if (rank == kSenderRank) {
        EXAMPLES_ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        LaunchSimtUrmaPerf(
            stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, slotBytes, slotCount, iterations,
            warmup, api, commitEach);
        EXAMPLES_ACLCHECK(aclrtSynchronizeStream(stream));
        EXAMPLES_ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));

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

    if (!sync.Barrier(kTagCompleted)) {
        std::cout << "[rank " << rank << "] completed barrier failed" << std::endl;
        return 1;
    }
    if (rank == kReceiverRank) {
        const uint32_t touchedSlots = TouchedSlotCount(warmup, iterations, slotCount);
        pass = CheckReceiver(recvBuf, bufferBytes, api, payloadBytes, slotBytes, touchedSlots);
        std::cout << "[rank " << rank << "] simt_urma_perftest " << apiName << " | received from rank " << peer << " | "
                  << (pass ? "PASS" : "FAIL") << std::endl;
    }

    if (timingBuf != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(timingBuf));
    }
    EXAMPLES_ACLCHECK(aclrtFree(sendBuf));
    EXAMPLES_ACLCHECK(aclrtFree(recvBuf));
    EXAMPLES_HCCLCHECK(HcclCommDestroy(comm));
    EXAMPLES_ACLCHECK(aclrtDestroyStream(stream));
    EXAMPLES_ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}

// 父进程流程：解析命令行，拉起全部rank进程并等待结果。
int main(int argc, char* argv[])
{
    if (argc < 4) {
        std::cout << "Usage: " << argv[0] << " <tcp://ip:port> <nranks> <write|write_value|notify|faa|cas> [options]"
                  << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --iterations N      Timed WQE count, default: 1024" << std::endl;
        std::cout << "  --warmup N          Warmup WQE count, default: 100" << std::endl;
        std::cout << "  --payload-bytes N   Payload bytes for write and notify, default: 4096; minimum 2." << std::endl;
        std::cout << "  --commit-mode MODE  immediate (one doorbell per WQE) or last (defer all but the last); "
                     "default: immediate"
                  << std::endl;
        return 1;
    }
    uint32_t nranks = 0;
    if (!examples::ParseUint32(argv[2], nranks, 2)) {
        std::cout << "invalid nranks: " << argv[2] << ", expected an integer >= 2" << std::endl;
        return 1;
    }
    const std::string endpoint = argv[1];
    const std::string apiName = argv[3];

    uint32_t iterations = 1024U;
    uint32_t warmup = 100U;
    uint32_t payloadBytes = 4096U;
    std::string commitMode = "immediate";
    for (int i = 4; i < argc; ++i) {
        const std::string option = argv[i];
        const bool hasValue = i + 1 < argc;
        if (option == "--iterations" && hasValue) {
            if (!examples::ParseUint32(argv[++i], iterations, 1)) {
                std::cout << "invalid --iterations value" << std::endl;
                return 1;
            }
        } else if (option == "--warmup" && hasValue) {
            if (!examples::ParseUint32(argv[++i], warmup)) {
                std::cout << "invalid --warmup value" << std::endl;
                return 1;
            }
        } else if (option == "--payload-bytes" && hasValue) {
            if (!examples::ParseUint32(argv[++i], payloadBytes, 2)) {
                std::cout << "invalid --payload-bytes value (minimum 2)" << std::endl;
                return 1;
            }
        } else if (option == "--commit-mode" && hasValue) {
            commitMode = argv[++i];
        } else {
            std::cout << "Unknown or incomplete option: " << option << std::endl;
            return 1;
        }
    }

    std::vector<examples::ArgsOf<decltype(RunPerftestRank)>> perRankArgs;
    perRankArgs.reserve(nranks);
    for (uint32_t rank = 0; rank < nranks; ++rank) {
        perRankArgs.emplace_back(rank, nranks, endpoint, apiName, iterations, warmup, payloadBytes, commitMode);
    }

    examples::ProcessGroup group;
    if (!group.Launch(nranks, RunPerftestRank, std::move(perRankArgs))) {
        return 1;
    }
    const bool pass = group.WaitAll();

    if (group.Interrupted()) {
        std::cout << "RESULT | Example=simt_urma_perftest API=" << apiName << " | Status=INTERRUPTED" << std::endl;
        return 1;
    }
    std::cout << "RESULT | Example=simt_urma_perftest API=" << apiName << " | Status=" << (pass ? "PASS" : "FAIL")
              << std::endl;
    return pass ? 0 : 1;
}
