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

#include "host_setup.h"
#include "validation.h"

void LaunchSimtMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize,
    uint32_t jettyCount, uint32_t firstJetty, uint32_t kernelJettyCount, void* completionBuffer);

namespace {
using namespace multi_jetty_batch_perf;

constexpr uint32_t kIterations = 1024U;
constexpr uint32_t kWarmupBase = 96U;
constexpr uint32_t kPayloadBytes = 64U;

// 发送侧按jetty/operation填identity pattern，接收侧填远端镜像pattern。
aclError InitializeBuffers(
    void* sendBuffer, void* recvBuffer, size_t bufferBytes, uint32_t jettyCount, uint32_t operationCount,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes)
{
    std::vector<uint8_t> send(bufferBytes, 0U);
    std::vector<uint8_t> recv(bufferBytes, 0U);
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        for (uint32_t operation = 0U; operation < operationCount; ++operation) {
            const size_t offset = static_cast<size_t>(jetty) * regionBytes + static_cast<size_t>(operation) * slotBytes;
            for (uint32_t byte = 0U; byte < payloadBytes; ++byte) {
                send[offset + byte] = IdentityByte(false, jetty, operation, byte);
                recv[offset + byte] = IdentityByte(true, jetty, operation, byte);
            }
        }
    }
    aclError result = aclrtMemcpy(sendBuffer, bufferBytes, send.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return result == ACL_SUCCESS ?
               aclrtMemcpy(recvBuffer, bufferBytes, recv.data(), bufferBytes, ACL_MEMCPY_HOST_TO_DEVICE) :
               result;
}

bool CheckBuffer(
    void* buffer, size_t bufferBytes, bool expectRemotePattern, uint32_t api, uint32_t jettyCount,
    uint32_t operationCount, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes)
{
    std::vector<uint8_t> actual(bufferBytes);
    if (aclrtMemcpy(actual.data(), bufferBytes, buffer, bufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    return CheckIdentityBuffer(
        actual, expectRemotePattern, jettyCount, operationCount, payloadBytes, slotBytes, regionBytes,
        MULTI_JETTY_MATRIX_BATCH, api);
}

bool ValidJettyCount(uint32_t jettyCount)
{
    return jettyCount == 1U || jettyCount == 2U || jettyCount == 4U || jettyCount == 8U;
}
} // namespace

// 本样例三个host侧同步点的tag。
constexpr uint32_t kTagReady = examples::kTagUserBase;
constexpr uint32_t kTagCompleted = kTagReady + 1U;
constexpr uint32_t kTagStatus = kTagCompleted + 1U;

// 单个rank的执行流程：建链、创建Jetty通道、执行kernel并校验，pair两端状态经控制通道汇聚后
// 由发送方输出RESULT行。由ProcessGroup在每个子进程中调用。
int32_t RunMultiJettyRank(
    uint32_t rank, uint32_t nranks, const std::string& ctrlEndpoint, const std::string& apiName, uint32_t jettyCount,
    uint32_t warmup)
{
    using namespace multi_jetty_batch_perf;
    if (nranks != 2U || rank >= nranks || (apiName != "write" && apiName != "read" && apiName != "notify") ||
        !ValidJettyCount(jettyCount) || warmup != AlignUp(kWarmupBase, MULTI_JETTY_MATRIX_BATCH)) {
        std::cerr << "[rank " << rank << "] invalid rank arguments" << std::endl;
        return 1;
    }
    const uint32_t batchSize = MULTI_JETTY_MATRIX_BATCH;
    const uint32_t api = apiName == "write" ? kApiWrite : apiName == "read" ? kApiRead : kApiNotify;
    const bool sender = (rank & 1U) == 0U;
    const uint32_t peer = rank ^ 1U;
    const uint32_t operationCount = warmup + kIterations;
    const uint32_t slotBytes = SlotBytes(api, kPayloadBytes);
    const uint32_t regionBytes = slotBytes * operationCount;
    const size_t bufferBytes = static_cast<size_t>(regionBytes) * jettyCount;
    // 单kernel内可并行的Jetty数由编译期batch size决定；超出时拆成多个kernel接续执行。
    const uint32_t jettysPerKernel = std::max(1U, kMaxActiveLanes / batchSize);
    const uint32_t kernelLaunchCount = (jettyCount + jettysPerKernel - 1U) / jettysPerKernel;

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
    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    EXAMPLES_ACLCHECK(aclrtMalloc(&sendBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(aclrtMalloc(&recvBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    EXAMPLES_ACLCHECK(InitializeBuffers(
        sendBuffer, recvBuffer, bufferBytes, jettyCount, operationCount, kPayloadBytes, slotBytes, regionBytes));
    CommMem sendMemory{COMM_MEM_TYPE_DEVICE, sendBuffer, bufferBytes};
    CommMem recvMemory{COMM_MEM_TYPE_DEVICE, recvBuffer, bufferBytes};
    std::array<HcclMemHandle, 2U> handles{nullptr, nullptr};
    std::vector<ChannelHandle> channels(jettyCount, 0U);
    std::vector<uint64_t> remoteRecvAddresses(jettyCount, 0U);
    std::vector<uint32_t> remoteRecvIndices(jettyCount, 0U);
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kSendTag, &sendMemory, &handles[0]));
    EXAMPLES_HCCLCHECK(HcclCommMemReg(comm, kRecvTag, &recvMemory, &handles[1]));
    EXAMPLES_HCCLCHECK(
        AcquireChannels(comm, rank, peer, handles.data(), channels, remoteRecvAddresses, remoteRecvIndices));
    for (uint32_t lhs = 0U; lhs < jettyCount; ++lhs) {
        if (channels[lhs] == 0U) {
            std::cerr << "[rank " << rank << "] Jetty channel is invalid: " << lhs << std::endl;
            return 1;
        }
        for (uint32_t rhs = lhs + 1U; rhs < jettyCount; ++rhs) {
            if (channels[lhs] == channels[rhs]) {
                std::cerr << "[rank " << rank << "] Jetty channels are not independent: " << lhs << " and " << rhs
                          << std::endl;
                return 1;
            }
        }
    }
    if (!sync.Barrier(kTagReady)) {
        std::cerr << "[rank " << rank << "] ready barrier failed" << std::endl;
        return 1;
    }

    bool pass = true;
    double issueTicks = 0.0;
    void* completionDevice = nullptr;
    void* timingDevice = nullptr;
    void* channelsDevice = nullptr;
    void* remoteDevice = nullptr;
    if (sender) {
        std::vector<CompletionSummary> publishSummaries(jettyCount);
        EXAMPLES_ACLCHECK(aclrtMalloc(&completionDevice, kCompletionAllocationBytes, ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(aclrtMemset(completionDevice, kCompletionAllocationBytes, 0, kCompletionAllocationBytes));
        std::vector<uint64_t> timing(static_cast<size_t>(kernelLaunchCount) * kTimingWords, 0U);
        std::vector<uint64_t> channelValues(channels.begin(), channels.end());
        EXAMPLES_ACLCHECK(aclrtMalloc(&timingDevice, timing.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(
            aclrtMemset(timingDevice, timing.size() * sizeof(uint64_t), 0, timing.size() * sizeof(uint64_t)));
        EXAMPLES_ACLCHECK(
            aclrtMalloc(&channelsDevice, channelValues.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(aclrtMemcpy(
            channelsDevice, channelValues.size() * sizeof(uint64_t), channelValues.data(),
            channelValues.size() * sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
        EXAMPLES_ACLCHECK(
            aclrtMalloc(&remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEM_MALLOC_HUGE_FIRST));
        EXAMPLES_ACLCHECK(aclrtMemcpy(
            remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), remoteRecvIndices.data(),
            remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEMCPY_HOST_TO_DEVICE));
        for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
            const uint32_t firstJetty = launch * jettysPerKernel;
            const uint32_t kernelJettyCount = std::min(jettyCount - firstJetty, jettysPerKernel);
            void* launchTiming =
                static_cast<uint8_t*>(timingDevice) + static_cast<size_t>(launch) * kTimingWords * sizeof(uint64_t);
            LaunchSimtMultiJettyBatchPerf(
                stream, channelsDevice, remoteDevice, launchTiming, api, kPayloadBytes, slotBytes, regionBytes,
                kIterations, warmup, batchSize, jettyCount, firstJetty, kernelJettyCount, completionDevice);
        }
        EXAMPLES_ACLCHECK(aclrtSynchronizeStream(stream));
        EXAMPLES_ACLCHECK(aclrtMemcpy(
            timing.data(), timing.size() * sizeof(uint64_t), timingDevice, timing.size() * sizeof(uint64_t),
            ACL_MEMCPY_DEVICE_TO_HOST));
        EXAMPLES_ACLCHECK(aclrtMemcpy(
            publishSummaries.data(), publishSummaries.size() * sizeof(CompletionSummary), completionDevice,
            publishSummaries.size() * sizeof(CompletionSummary), ACL_MEMCPY_DEVICE_TO_HOST));
        PrintCompletionState(publishSummaries);
        pass = CheckCompletion(publishSummaries, warmup, operationCount, batchSize, api, jettysPerKernel);
        uint64_t issueCycles = 0U;
        uint64_t completed = 0U;
        for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
            const uint64_t* result = timing.data() + static_cast<size_t>(launch) * kTimingWords;
            pass = pass && static_cast<int64_t>(result[kStatusIndex]) == 0;
            issueCycles += result[kIssueCyclesIndex];
            completed += result[kCompletedIndex];
        }
        const uint64_t totalOperations = static_cast<uint64_t>(kIterations) * jettyCount;
        pass = pass && completed == totalOperations;
        if (pass && api == kApiRead) {
            pass = CheckBuffer(
                sendBuffer, bufferBytes, true, api, jettyCount, operationCount, kPayloadBytes, slotBytes, regionBytes);
        }
        issueTicks = totalOperations == 0U ?
                         0.0 :
                         static_cast<double>(issueCycles) / static_cast<double>(totalOperations);
    }
    if (!sync.Barrier(kTagCompleted)) {
        std::cerr << "[rank " << rank << "] completed barrier failed" << std::endl;
        return 1;
    }
    if (!sender && api != kApiRead) {
        pass = CheckBuffer(
            recvBuffer, bufferBytes, false, api, jettyCount, operationCount, kPayloadBytes, slotBytes, regionBytes);
    }

    // pair两端校验结果经控制通道汇聚，由发送方输出RESULT行。
    const uint32_t flags = pass ? 1U : 0U;
    std::vector<uint32_t> allFlags(nranks, 0U);
    if (!sync.Allgather(kTagStatus, &flags, sizeof(flags), allFlags.data())) {
        std::cerr << "[rank " << rank << "] status allgather failed" << std::endl;
        return 1;
    }
    const bool pairPass = allFlags[rank] == 1U && allFlags[peer] == 1U;
    if (sender) {
        const uint32_t concurrentJettys = std::min(jettyCount, jettysPerKernel);
        std::cout << std::fixed << std::setprecision(6) << "RESULT | API=" << apiName << " | Mode=MultiJetty"
                  << " | DataSize/B=" << kPayloadBytes << " | BatchSize=" << batchSize << " | Jettys=" << jettyCount
                  << " | ConcurrentJettys=" << concurrentJettys << " | IssueTicksPerRequest=" << issueTicks
                  << " | Pair=" << rank << "-" << peer << " | Status=" << (pairPass ? "PASS" : "FAIL") << std::endl;
    }

    if (timingDevice != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(timingDevice));
    }
    if (channelsDevice != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(channelsDevice));
    }
    if (remoteDevice != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(remoteDevice));
    }
    if (completionDevice != nullptr) {
        EXAMPLES_ACLCHECK(aclrtFree(completionDevice));
    }
    EXAMPLES_ACLCHECK(aclrtFree(sendBuffer));
    EXAMPLES_ACLCHECK(aclrtFree(recvBuffer));
    EXAMPLES_HCCLCHECK(HcclCommDestroy(comm));
    EXAMPLES_ACLCHECK(aclrtDestroyStream(stream));
    EXAMPLES_ACLCHECK(aclFinalize());
    return pairPass ? 0 : 2;
}

namespace {

// 一个用例的完整参数：api + Jetty数。
struct PerfCase {
    std::string api;
    uint32_t jettyCount;
};

} // namespace

// 父进程流程：解析命令行，构造用例列表，逐个拉起全部rank进程并等待结果。
int main(int32_t argc, char* argv[])
{
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <tcp://ip:port> <nranks> [write|read|notify|all] [options]"
                  << std::endl;
        std::cout << "Options:" << std::endl;
        std::cout << "  --jetty-counts LIST  Comma-separated Jetty counts, default: 1,2,4,8" << std::endl;
        return 1;
    }
    const std::string endpoint = argv[1];
    uint32_t nranks = 0;
    if (!examples::ParseUint32(argv[2], nranks, 2) || nranks != 2U) {
        std::cout << "invalid nranks: " << argv[2] << ", this benchmark runs a fixed pair of 2 ranks" << std::endl;
        return 1;
    }
    std::string apiArg = "all";
    std::vector<uint32_t> jettyCounts = {1U, 2U, 4U, 8U};
    for (int i = 3; i < argc; ++i) {
        const std::string option = argv[i];
        const bool hasValue = i + 1 < argc;
        if (option == "--jetty-counts" && hasValue) {
            jettyCounts.clear();
            const std::string text = argv[++i];
            size_t begin = 0U;
            bool valid = true;
            while (true) {
                const size_t end = text.find(',', begin);
                const std::string token =
                    text.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
                uint32_t value = 0U;
                if (!examples::ParseUint32(token.c_str(), value, 1U)) {
                    valid = false;
                    break;
                }
                jettyCounts.push_back(value);
                if (end == std::string::npos) {
                    break;
                }
                begin = end + 1U;
            }
            if (!valid || jettyCounts.empty()) {
                std::cout << "invalid --jetty-counts value, expected comma-separated integers in 1..8" << std::endl;
                return 1;
            }
        } else if (option == "write" || option == "read" || option == "notify" || option == "all") {
            apiArg = option;
        } else {
            std::cout << "Unknown or incomplete option: " << option << std::endl;
            return 1;
        }
    }
    for (uint32_t jettyCount : jettyCounts) {
        if (!ValidJettyCount(jettyCount)) {
            std::cout << "invalid Jetty count: " << jettyCount << ", expected one of 1, 2, 4, 8" << std::endl;
            return 1;
        }
    }

    std::vector<std::string> apis;
    if (apiArg == "all") {
        apis = {"write", "read", "notify"};
    } else {
        apis = {apiArg};
    }
    // 固定64B payload、每Jetty 1024计时请求；warmup按batch size向上对齐。
    const uint32_t warmup = AlignUp(kWarmupBase, MULTI_JETTY_MATRIX_BATCH);

    std::vector<PerfCase> cases;
    for (const std::string& api : apis) {
        for (uint32_t jettyCount : jettyCounts) {
            cases.push_back({api, jettyCount});
        }
    }

    bool allPassed = true;
    bool interrupted = false;
    for (const PerfCase& perfCase : cases) {
        std::vector<examples::ArgsOf<decltype(RunMultiJettyRank)>> perRankArgs;
        perRankArgs.reserve(nranks);
        for (uint32_t rank = 0; rank < nranks; ++rank) {
            perRankArgs.emplace_back(rank, nranks, endpoint, perfCase.api, perfCase.jettyCount, warmup);
        }
        examples::ProcessGroup group;
        if (!group.Launch(nranks, RunMultiJettyRank, std::move(perRankArgs))) {
            return 1;
        }
        const bool casePassed = group.WaitAll();
        if (group.Interrupted()) {
            interrupted = true;
            break;
        }
        if (!casePassed) {
            std::cout << "CASE_FAILED | API=" << perfCase.api << " | Jettys=" << perfCase.jettyCount
                      << " | ExitCodes=";
            for (int code : group.ExitCodes()) {
                std::cout << code << ",";
            }
            std::cout << std::endl;
        }
        allPassed = casePassed && allPassed;
    }

    if (interrupted) {
        std::cout << "RESULT | Example=multi_jetty_batch_perf | Status=INTERRUPTED" << std::endl;
        return 1;
    }
    std::cout << "RESULT | Example=multi_jetty_batch_perf | Status=" << (allPassed ? "PASS" : "FAIL") << std::endl;
    return allPassed ? 0 : 1;
}
