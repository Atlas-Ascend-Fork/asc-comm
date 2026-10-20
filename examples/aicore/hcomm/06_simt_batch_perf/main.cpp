/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "utils/arg_parser.h"
#include "utils/check.h"
#include "utils/hccl_comm_init.h"
#include "utils/process_manager.h"
#include "utils/rank_sync.h"

#include "simt_batch_perf_common.h"

extern void LaunchSimtBatchPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t api, bool explicitBatch, bool groupBatch,
    uint32_t groupLanes);

namespace {
using namespace simt_batch_perf;

constexpr uint32_t kLocalSendIdx = 1U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtBatchPerfSendBuf";
constexpr const char* kRecvBufTag = "simtBatchPerfRecvBuf";

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
    return api == "write" || api == "read" || api == "notify" || api == "write_value" || api == "faa" || api == "cas";
}

const char* ModeLabel(const std::string& mode)
{
    return mode == "group" ? "GroupBatch" : mode == "batch" ? "ExplicitBatch" : "Immediate";
}

uint32_t AlignUp(uint32_t value, uint32_t alignment) { return (value + alignment - 1U) / alignment * alignment; }

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

// 发送侧填pattern、接收侧填初值。原子接口预置远端初值，让首轮迭代就是有定义的原子操作。
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
    EXAMPLES_HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = kTargetCommProtocol;
    desc.notifyNum = 0;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;
    uint32_t* layers = nullptr;
    uint32_t layerNum = 0U;
    EXAMPLES_HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));
    if (layers == nullptr || layerNum == 0U) {
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

// 计时与预热轮转kSlotCount个slot：一次写入留下的字节与第一次相同，按slot而非按操作校验。
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
                    return false;
                }
            }
            if (api == kApiNotify) {
                uint64_t notify = 0U;
                std::memcpy(&notify, value + slotBytes - sizeof(uint64_t), sizeof(notify));
                if (notify != kNotifyValue) {
                    return false;
                }
            }
        } else {
            uint64_t result = 0U;
            std::memcpy(&result, value, sizeof(result));
            const uint64_t expected =
                api == kApiWriteValue ?
                    kInlineValue :
                    (api == kApiFaa ? kFaaInitial + static_cast<uint64_t>(count) * kFaaAdd : kCasSwap);
            if (result != expected) {
                return false;
            }
        }
    }
    return true;
}

// 发送侧只回写fetch返回值：FAA返回加之前的旧值，CAS只在首轮发生真实交换。
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
        const uint64_t expected = api == kApiFaa ? kFaaInitial + static_cast<uint64_t>(count - 1U) * kFaaAdd :
                                                   (count == 1U ? kCasInitial : kCasSwap);
        if (result != expected) {
            return false;
        }
    }
    return true;
}

// 解析逗号分隔的GroupBatch lane数列表，每个值须 >= 2。
bool ParseLaneList(const std::string& text, std::vector<uint32_t>& lanes)
{
    lanes.clear();
    size_t begin = 0U;
    while (true) {
        const size_t end = text.find(',', begin);
        const std::string token = text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
        uint32_t value = 0U;
        if (!examples::ParseUint32(token.c_str(), value, 2U)) {
            return false;
        }
        lanes.push_back(value);
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1U;
    }
    return !lanes.empty();
}
} // namespace

// 本样例三个host侧同步点的tag。
constexpr uint32_t kTagReady = examples::kTagUserBase;
constexpr uint32_t kTagCompleted = kTagReady + 1U;
constexpr uint32_t kTagStatus = kTagCompleted + 1U;

// 单个rank的执行流程：建链、注册内存、执行kernel并校验，pair两端状态经控制通道汇聚后
// 由发送方输出RESULT行。由ProcessGroup在每个子进程中调用。
int32_t RunSimtBatchPerfRank(
    uint32_t rank, uint32_t nranks, const std::string& ctrlEndpoint, const std::string& apiName, uint32_t iterations,
    uint32_t warmup, uint32_t payloadBytes, uint32_t batchSize, const std::string& mode, uint32_t groupLanes)
{
    if ((nranks != 2U && nranks != 8U) || rank >= nranks || !ValidApi(apiName) || iterations == 0U ||
        payloadBytes < 2U || (mode != "immediate" && mode != "batch" && mode != "group") ||
        (mode == "group" && (groupLanes != batchSize || groupLanes < 2U)) ||
        (mode != "group" && (groupLanes != 1U || batchSize != 1U))) {
        std::cerr << "[rank " << rank << "] invalid rank arguments" << std::endl;
        return 1;
    }
    const bool isSender = (rank & 1U) == 0U;
    const uint32_t api = ApiIndex(apiName);
    const uint32_t peer = rank ^ 1U;
    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const size_t bufferBytes = static_cast<size_t>(slotBytes) * kSlotCount;

    examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(ctrlEndpoint, nranks) :
                                                  examples::RankSyncContext::Connect(rank, nranks, ctrlEndpoint);
    if (!sync.Ok()) {
        std::cerr << "[rank " << rank << "] control channel setup failed" << std::endl;
        return 1;
    }

    EXAMPLES_ACLCHECK(aclInit(nullptr));
    EXAMPLES_ACLCHECK(aclrtSetDevice(static_cast<int32_t>(rank)));
    aclrtStream stream = nullptr;
    EXAMPLES_ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    EXAMPLES_HCCLCHECK(examples::InitCommByRootInfo(sync, &comm));

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    EXAMPLES_ACLCHECK(aclrtMalloc(&sendBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(aclrtMalloc(&recvBuf, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(InitBuffers(sendBuf, recvBuf, bufferBytes, api, slotBytes));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, bufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, bufferBytes};
    HcclMemHandle handles[2] = {nullptr, nullptr};
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &handles[0]));
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &handles[1]));

    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    EXAMPLES_HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    if (!sync.Barrier(kTagReady)) {
        std::cerr << "[rank " << rank << "] ready barrier failed" << std::endl;
        return 1;
    }

    bool pass = true;
    double issueTicks = 0.0;
    void* timingBuf = nullptr;
    if (isSender) {
        uint64_t timing[kTimingWords] = {};
        EXAMPLES_ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
        LaunchSimtBatchPerf(
            stream, channel, kLocalSendIdx, remoteRecvIdx, timingBuf, payloadBytes, slotBytes, iterations, warmup, api,
            mode != "immediate", mode == "group", groupLanes);
        EXAMPLES_ACLCHECK(aclrtSynchronizeStream(stream));
        EXAMPLES_ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));
        const bool devicePass =
            static_cast<int64_t>(timing[kStatusIndex]) == 0 && timing[kCompletedIndex] == iterations;
        bool localCheckPass = true;
        if (devicePass && api == kApiRead) {
            localCheckPass = CheckBuffer(sendBuf, bufferBytes, api, payloadBytes, slotBytes, iterations, warmup);
        } else if (devicePass && (api == kApiFaa || api == kApiCas)) {
            localCheckPass = CheckAtomicFetchBuffer(sendBuf, bufferBytes, api, slotBytes, iterations, warmup);
        }
        pass = devicePass && localCheckPass;
        issueTicks = static_cast<double>(timing[kIssueCyclesIndex]) / iterations;
    }

    if (!sync.Barrier(kTagCompleted)) {
        std::cerr << "[rank " << rank << "] completed barrier failed" << std::endl;
        return 1;
    }
    if (!isSender && api != kApiRead) {
        pass = CheckBuffer(recvBuf, bufferBytes, api, payloadBytes, slotBytes, iterations, warmup);
    }

    // pair两端校验结果经控制通道汇聚，由发送方输出RESULT行。
    const uint32_t flags = pass ? 1U : 0U;
    std::vector<uint32_t> allFlags(nranks, 0U);
    if (!sync.Allgather(kTagStatus, &flags, sizeof(flags), allFlags.data())) {
        std::cerr << "[rank " << rank << "] status allgather failed" << std::endl;
        return 1;
    }
    const bool pairPass = allFlags[rank] == 1U && allFlags[peer] == 1U;
    if (isSender) {
        const uint32_t dataBytes = api == kApiWrite || api == kApiRead || api == kApiNotify ? payloadBytes : 8U;
        const uint32_t batchItems = mode == "group" ? groupLanes : 1U;
        std::cout << std::fixed << std::setprecision(6) << "RESULT | API=" << apiName << " | Mode=" << ModeLabel(mode)
                  << " | DataSize/B=" << dataBytes << " | BatchSize=" << batchItems << " | Jettys=1"
                  << " | ConcurrentJettys=1 | IssueTicksPerRequest=" << issueTicks << " | Pair=" << rank << "-"
                  << peer << " | Status=" << (pairPass ? "PASS" : "FAIL") << std::endl;
    }

    if (timingBuf != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(timingBuf));
    }
    EXAMPLES_ACLCHECK(aclrtFree(sendBuf));
    EXAMPLES_ACLCHECK(aclrtFree(recvBuf));
    EXAMPLES_HCCLCHECK(HcclCommDestroy(comm));
    EXAMPLES_ACLCHECK(aclrtDestroyStream(stream));
    EXAMPLES_ACLCHECK(aclFinalize());
    return pairPass ? 0 : 2;
}

namespace {

// 一个用例的完整参数：api + 提交方式 + batchSize + lane数。
// Immediate/ExplicitBatch的batch size固定为1，GroupBatch的batch size等于lane数。
struct PerfCase {
    std::string api;
    std::string mode;
    uint32_t batchSize;
    uint32_t lanes;
};

} // namespace

// 父进程流程：解析命令行，构造用例列表，逐个拉起全部rank进程并等待结果。
int main(int32_t argc, char* argv[])
{
    if (argc < 4) {
        std::cout << "Usage: " << argv[0]
                  << " <tcp://ip:port> <nranks> <write|read|notify|write_value|faa|cas|all> [options]" << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --mode MODE         compare, immediate, batch, or group; default: compare" << std::endl;
        std::cout << "  --iterations N      Timed request count, default: 1024" << std::endl;
        std::cout << "  --warmup N          Warmup request count, default: 96" << std::endl;
        std::cout << "  --payload-bytes N   Write/Read/Notify payload, default: 64; minimum 2" << std::endl;
        std::cout << "  --group-lanes LIST  GroupBatch lane counts, default: 2,4,8,16,32,128" << std::endl;
        return 1;
    }
    const std::string endpoint = argv[1];
    uint32_t nranks = 0;
    if (!examples::ParseUint32(argv[2], nranks, 2) || (nranks != 2U && nranks != 8U)) {
        std::cout << "invalid nranks: " << argv[2] << ", expected 2 (one pair) or 8 (four pairs)" << std::endl;
        return 1;
    }
    const std::string apiArg = argv[3];
    std::vector<std::string> apis;
    if (apiArg == "all") {
        apis = {"write", "read", "notify", "write_value", "faa", "cas"};
    } else if (ValidApi(apiArg)) {
        apis = {apiArg};
    } else {
        std::cout << "invalid api: " << apiArg << std::endl;
        return 1;
    }

    std::string mode = "compare";
    uint32_t iterations = 1024U;
    uint32_t warmup = 96U;
    uint32_t payloadBytes = 64U;
    std::vector<uint32_t> laneCounts = {2U, 4U, 8U, 16U, 32U, 128U};
    for (int i = 4; i < argc; ++i) {
        const std::string option = argv[i];
        const bool hasValue = i + 1 < argc;
        if (option == "--mode" && hasValue) {
            mode = argv[++i];
        } else if (option == "--iterations" && hasValue) {
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
        } else if (option == "--group-lanes" && hasValue) {
            if (!ParseLaneList(argv[++i], laneCounts)) {
                std::cout << "invalid --group-lanes value, expected comma-separated integers >= 2" << std::endl;
                return 1;
            }
        } else {
            std::cout << "Unknown or incomplete option: " << option << std::endl;
            return 1;
        }
    }
    if (mode != "compare" && mode != "immediate" && mode != "batch" && mode != "group") {
        std::cout << "invalid mode: " << mode << ", expected compare, immediate, batch or group" << std::endl;
        return 1;
    }

    // compare模式对照Immediate与GroupBatch；ExplicitBatch仅--mode batch时执行。
    std::vector<PerfCase> cases;
    for (const std::string& api : apis) {
        if (mode == "immediate" || mode == "batch") {
            cases.push_back({api, mode, 1U, 1U});
        } else {
            if (mode == "compare") {
                cases.push_back({api, "immediate", 1U, 1U});
            }
            for (uint32_t lanes : laneCounts) {
                cases.push_back({api, "group", lanes, lanes});
            }
        }
    }
    for (const PerfCase& perfCase : cases) {
        if (perfCase.lanes > kMaxGroupLanes || iterations % perfCase.lanes != 0U) {
            std::cout << "GroupBatch lanes must be in 2.." << kMaxGroupLanes << " and divide iterations "
                      << iterations << std::endl;
            return 1;
        }
#if !SIMT_BATCH_ENABLE_LARGE_GROUP
        if (perfCase.lanes > 128U) {
            std::cout << "group lanes > 128 require building with -DSIMT_BATCH_ENABLE_LARGE_GROUP=ON" << std::endl;
            return 1;
        }
#endif
    }

    bool allPassed = true;
    bool interrupted = false;
    for (const PerfCase& perfCase : cases) {
        // GroupBatch按整batch预热；其它提交方式无此约束。
        const uint32_t caseWarmup = perfCase.mode == "group" ? AlignUp(warmup, perfCase.lanes) : warmup;
        std::vector<examples::ArgsOf<decltype(RunSimtBatchPerfRank)>> perRankArgs;
        perRankArgs.reserve(nranks);
        for (uint32_t rank = 0; rank < nranks; ++rank) {
            perRankArgs.emplace_back(
                rank, nranks, endpoint, perfCase.api, iterations, caseWarmup, payloadBytes, perfCase.batchSize,
                perfCase.mode, perfCase.lanes);
        }
        examples::ProcessGroup group;
        if (!group.Launch(nranks, RunSimtBatchPerfRank, std::move(perRankArgs))) {
            return 1;
        }
        const bool casePassed = group.WaitAll();
        if (group.Interrupted()) {
            interrupted = true;
            break;
        }
        if (!casePassed) {
            std::cout << "CASE_FAILED | API=" << perfCase.api << " | Mode=" << ModeLabel(perfCase.mode)
                      << " | BatchSize=" << perfCase.batchSize << " | ExitCodes=";
            for (int code : group.ExitCodes()) {
                std::cout << code << ",";
            }
            std::cout << std::endl;
        }
        allPassed = casePassed && allPassed;
    }

    if (interrupted) {
        std::cout << "RESULT | Example=simt_batch_perf | Status=INTERRUPTED" << std::endl;
        return 1;
    }
    std::cout << "RESULT | Example=simt_batch_perf | Status=" << (allPassed ? "PASS" : "FAIL") << std::endl;
    return allPassed ? 0 : 1;
}
