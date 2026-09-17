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

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "hccl/hccl.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"
#include "hccl/hccl_rank_graph.h"

#include "utils/check.h"
#include "utils/hccl_comm_init.h"
#include "utils/process_manager.h"
#include "utils/rank_sync.h"
#include "utils/arg_parser.h"

#include "simt_urma_common.h"

extern void LaunchSimtUrma(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode);

namespace {

using namespace simt_urma;

// 相邻节点相关资源（用于环形拓扑）
struct PeerRankCtx {
    uint32_t rankId{0};
    ChannelHandle channel{0};
    uint32_t remoteRecvIdx{0};
};

// SIMT接口只在UB_CTP/URMA协议上实现，其余链路协议无对应实现。
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UB_CTP;

// 两块内存注册到通信域时使用的tag。两个rank使用相同tag，远端内存才能按tag查回。
constexpr const char* kSendBufTag = "simtUrmaSendBuf";
constexpr const char* kRecvBufTag = "simtUrmaRecvBuf";

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

// 显式传入却不匹配任何 mode 是笔误而非取默认值，因此直接拒绝。
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

// write 系列需要递增序列的发送 payload 和清零的接收 buffer；notify/atomic 系列需要一个
// 数据字和接收侧预置的 FAA/CAS 初值（这两个操作定义在已知初值之上）。
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

// 与对端建链，并按 kRecvBufTag 查出对端接收 buffer 在远端内存表中的索引。
// kernel 通过 AscendC::simt::Local/RemoteBufferAddr 按索引解析 buffer 基地址；本地表
// index 0 保留给 CCL buffer，显式注册的内存从 1 开始。
HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, PeerRankCtx& peer, HcclMemHandle* memHandles, uint32_t memHandleNum)
{
    HcclChannelDesc channelDesc;
    EXAMPLES_HCCLCHECK(HcclChannelDescInit(&channelDesc, 1));
    channelDesc.remoteRank = peer.rankId;
    channelDesc.channelProtocol = kTargetCommProtocol;
    channelDesc.notifyNum = 0;
    channelDesc.memHandles = memHandles;
    channelDesc.memHandleNum = memHandleNum;

    uint32_t* netLayers = nullptr;
    uint32_t netLayerNum = 0;
    EXAMPLES_HCCLCHECK(HcclRankGraphGetLayers(comm, &netLayers, &netLayerNum));
    if (netLayerNum == 0 || netLayers == nullptr) {
        std::cout << "[rank " << rank << "] no net layer reported" << std::endl;
        return HCCL_E_INTERNAL;
    }

    CommLink* links = nullptr;
    uint32_t linkNum = 0;
    EXAMPLES_HCCLCHECK(HcclRankGraphGetLinks(comm, netLayers[0], rank, peer.rankId, &links, &linkNum));
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
        std::cout << "[rank " << rank << "] no UB_CTP link to peer " << peer.rankId << std::endl;
        return HCCL_E_NOT_SUPPORT;
    }

    EXAMPLES_HCCLCHECK(HcclChannelAcquire(comm, COMM_ENGINE_AIV, &channelDesc, 1, &peer.channel));
    if (peer.channel == 0) {
        std::cout << "[rank " << rank << "] channel acquire returned an invalid handle" << std::endl;
        return HCCL_E_INTERNAL;
    }

    uint32_t memNum = 0;
    CommMem* remoteMems = nullptr;
    char** memTags = nullptr;
    EXAMPLES_HCCLCHECK(HcclChannelGetRemoteMems(comm, peer.channel, &memNum, &remoteMems, &memTags));
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

// 环上每个 recvBuf 只有一个写者（它的 prev），FAA/CAS 取回的旧值仍精确等于 host 播种的
// 初值，因此环形拓扑下 fetch 值校验依然成立。write 系列只把 sendBuf 当数据源，不校验。
bool CheckSender(void* sendBuf, uint32_t mode)
{
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

    // 四种 write 模式落地相同的递增序列（payload 无论走 SGE 还是 WQE 内联），一张期望表通用。
    if (IsWriteMode(mode)) {
        for (uint32_t i = 0; i < kWriteSlotCount; ++i) {
            expected[i] = SlotValue(i);
        }
        return CheckWords(recvBuf, expected, "receiver target");
    }

    // notify_immediate_repeat 重复投递同一个 Notify，落地状态与单次 Notify 相同。
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
        // single 与 batch_last 都跑全部三种操作，终态相同。
        expected[kNotifyDataSlot] = kNotifyData;
        expected[kNotifySignalSlot] = kNotifySignal;
        expected[kFaaTargetSlot] = kFaaInitial + kFaaAdd;
        expected[kCasTargetSlot] = kCasSwap;
    }
    return CheckWords(recvBuf, expected, "receiver target");
}

} // namespace

// 本样例两个host侧同步点的tag。
constexpr uint32_t kTagReady = examples::kTagUserBase;
constexpr uint32_t kTagSent = examples::kTagUserBase + 1U;

// 单个rank的完整流程。由ProcessGroup在每个子进程中调用。
int RunSimtUrmaRank(uint32_t rank, uint32_t nranks, const std::string& ctrlEndpoint, const std::string& modeName)
{
    uint32_t mode = kDefaultMode;
    if (!modeName.empty() && !ParseMode(modeName.c_str(), mode)) {
        std::cout << "Unknown mode: " << modeName << std::endl;
        PrintModes();
        return 1;
    }

    examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(ctrlEndpoint, nranks) :
                                                  examples::RankSyncContext::Connect(rank, nranks, ctrlEndpoint);
    if (!sync.Ok()) {
        std::cout << "[rank " << rank << "] control channel setup failed" << std::endl;
        return 1;
    }

    EXAMPLES_ACLCHECK(aclInit(nullptr));
    EXAMPLES_ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    EXAMPLES_ACLCHECK(aclrtCreateStream(&stream));

    HcclComm hcclComm = nullptr;
    EXAMPLES_HCCLCHECK(examples::InitCommByRootInfo(sync, &hcclComm));

    void* sendBuf = nullptr;
    EXAMPLES_ACLCHECK(aclrtMalloc(&sendBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    void* recvBuf = nullptr;
    EXAMPLES_ACLCHECK(aclrtMalloc(&recvBuf, kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(InitBuffers(sendBuf, recvBuf, mode));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, kBufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, kBufferBytes};
    HcclMemHandle memHandles[2] = {nullptr, nullptr};
    constexpr uint32_t kSendMemHandleIdx = 0U;
    constexpr uint32_t kRecvMemHandleIdx = 1U;
    // 本地 buffer 表 index 0 是 CCL buffer，注册内存从 1 开始，见 AcquirePeerChannel 的注释。
    constexpr uint32_t kLocalSendIdx = kSendMemHandleIdx + 1U;
    EXAMPLES_HCCLCHECK(HcclCommMemReg(hcclComm, kSendBufTag, &sendMem, &memHandles[kSendMemHandleIdx]));
    EXAMPLES_HCCLCHECK(HcclCommMemReg(hcclComm, kRecvBufTag, &recvMem, &memHandles[kRecvMemHandleIdx]));

    // 环形拓扑：每个 rank 向 next 发送，并校验从 prev 收到的数据。
    PeerRankCtx prev;
    PeerRankCtx next;
    prev.rankId = (rank + nranks - 1U) % nranks;
    next.rankId = (rank + 1U) % nranks;

    // nranks==2 时 prev 与 next 是同一个对端：环形退化为点对点，两个方向复用同一条通道。
    // 对同一对端用相同的desc重复建链，通道握手无法区分两条通道的配对关系。
    if (prev.rankId == next.rankId) {
        std::cout << "[rank " << rank << "] ring degenerates to point-to-point, reusing one channel" << std::endl;
        EXAMPLES_HCCLCHECK(AcquirePeerChannel(hcclComm, rank, next, memHandles, 2U));
        prev.channel = next.channel;
        prev.remoteRecvIdx = next.remoteRecvIdx;
    } else if (rank % 2 == 0) {
        // 建链顺序按 rank 奇偶错开，避免环上循环等待。
        EXAMPLES_HCCLCHECK(AcquirePeerChannel(hcclComm, rank, prev, memHandles, 2U));
        EXAMPLES_HCCLCHECK(AcquirePeerChannel(hcclComm, rank, next, memHandles, 2U));
    } else {
        EXAMPLES_HCCLCHECK(AcquirePeerChannel(hcclComm, rank, next, memHandles, 2U));
        EXAMPLES_HCCLCHECK(AcquirePeerChannel(hcclComm, rank, prev, memHandles, 2U));
    }

    if (!sync.Barrier(kTagReady)) {
        std::cout << "[rank " << rank << "] ready barrier failed" << std::endl;
        return HCCL_E_INTERNAL;
    }

    LaunchSimtUrma(stream, next.channel, kLocalSendIdx, next.remoteRecvIdx, mode);
    EXAMPLES_ACLCHECK(aclrtSynchronizeStream(stream));

    // 本 rank 的 kernel 已同步完成，此 barrier 保证读到 prev 写入时所有 WQE 都已落地。
    if (!sync.Barrier(kTagSent)) {
        std::cout << "[rank " << rank << "] sent barrier failed" << std::endl;
        return HCCL_E_INTERNAL;
    }

    // sendBuf 是本 rank 取回的 fetch 值，recvBuf 是 prev 写进来的数据，两侧都要校验。
    const bool senderPass = CheckSender(sendBuf, mode);
    const bool receiverPass = CheckReceiver(recvBuf, mode);
    const bool pass = senderPass && receiverPass;
    if (!senderPass) {
        std::cout << "[rank " << rank << "] " << ModeName(mode) << " FAIL (sender fetch check)" << std::endl;
    }
    if (!receiverPass) {
        std::cout << "[rank " << rank << "] " << ModeName(mode) << " FAIL (receiver check) | received from rank "
                  << prev.rankId << std::endl;
    }
    if (pass) {
        std::cout << "[rank " << rank << "] simt_urma " << ModeName(mode) << " | sent to rank " << next.rankId
                  << ", received from rank " << prev.rankId << " | PASS" << std::endl;
    }

    EXAMPLES_ACLCHECK(aclrtFree(sendBuf));
    EXAMPLES_ACLCHECK(aclrtFree(recvBuf));
    EXAMPLES_HCCLCHECK(HcclCommDestroy(hcclComm));
    EXAMPLES_ACLCHECK(aclrtDestroyStream(stream));
    EXAMPLES_ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}

// 父进程流程：解析命令行（<tcp://ip:port> <nranks> [mode]），拉起全部rank进程并等待结果。
int main(int argc, char* argv[])
{
    if (argc > 4) {
        std::cout << "Usage: " << argv[0] << " <tcp://ip:port> <nranks> [mode]" << std::endl;
        PrintModes();
        return 1;
    }
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <tcp://ip:port> <nranks> [mode]" << std::endl;
        PrintModes();
        return 1;
    }
    uint32_t nranks = 0;
    if (!examples::ParseUint32(argv[2], nranks, 2)) {
        std::cout << "invalid nranks: " << argv[2] << ", expected an integer >= 2" << std::endl;
        return 1;
    }
    const std::string endpoint = argv[1];
    const std::string modeName = argc > 3 ? argv[3] : "";

    std::vector<examples::ArgsOf<decltype(RunSimtUrmaRank)>> perRankArgs;
    perRankArgs.reserve(nranks);
    for (uint32_t rank = 0; rank < nranks; ++rank) {
        perRankArgs.emplace_back(rank, nranks, endpoint, modeName);
    }

    examples::ProcessGroup group;
    if (!group.Launch(nranks, RunSimtUrmaRank, std::move(perRankArgs))) {
        return 1;
    }
    const bool pass = group.WaitAll();

    if (group.Interrupted()) {
        std::cout << "RESULT | Example=simt_urma Mode=" << (modeName.empty() ? "single" : modeName)
                  << " | Status=INTERRUPTED" << std::endl;
        return 1;
    }
    std::cout << "RESULT | Example=simt_urma Mode=" << (modeName.empty() ? "single" : modeName)
              << " | Status=" << (pass ? "PASS" : "FAIL") << std::endl;
    return pass ? 0 : 1;
}
