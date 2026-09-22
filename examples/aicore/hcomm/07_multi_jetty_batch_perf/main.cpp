/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <iomanip>
#include "host_setup.h"
#include "utils/hccl_comm_init.h"
#include "validation.h"

void LaunchSimtMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteIndices, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize,
    uint32_t jettyCount, uint32_t firstJetty, uint32_t kernelJettyCount, void* completionBuffer);

namespace {
using namespace multi_jetty_batch_perf;
constexpr uint32_t kTagReady = examples::kTagUserBase;
constexpr uint32_t kTagCompleted = kTagReady + 1U;

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
    uint32_t operationCount, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t batchSize = 256U)
{
    std::vector<uint8_t> actual(bufferBytes);
    if (aclrtMemcpy(actual.data(), bufferBytes, buffer, bufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    return CheckIdentityBuffer(
        actual, expectRemotePattern, jettyCount, operationCount, payloadBytes, slotBytes, regionBytes, batchSize, api);
}

} // namespace

int main(int argc, char* argv[])
{
    using namespace multi_jetty_batch_perf;
    if (argc == 2 && std::strcmp(argv[1], "--build-info") == 0) {
        std::cout << "BUILD_CONFIG | Backend=simt | Layout=matrix-v4 | CompletionVersion=1 | MatrixBatch="
                  << MULTI_JETTY_MATRIX_BATCH << std::endl;
        return 0;
    }
    if (argc != 10) {
        std::cerr << "Usage: " << argv[0]
                  << " <rank> <nranks> <control_endpoint>"
                     " <write|read|notify> <iterations> <warmup> <payload> <batch_size> <jettys>"
                  << std::endl;
        return 1;
    }
    const uint32_t rank = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
    const uint32_t rankCount = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
    const std::string controlEndpoint = argv[3];
    const std::string apiName = argv[4];
    const uint32_t iterations = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
    const uint32_t warmup = static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10));
    const uint32_t payloadBytes = static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10));
    const uint32_t batchSize = static_cast<uint32_t>(std::strtoul(argv[8], nullptr, 10));
    const uint32_t jettyCount = static_cast<uint32_t>(std::strtoul(argv[9], nullptr, 10));
    if (rankCount != 2U || rank >= rankCount || (apiName != "write" && apiName != "read" && apiName != "notify") ||
        batchSize != MULTI_JETTY_MATRIX_BATCH ||
        (jettyCount != 1U && jettyCount != 2U && jettyCount != 4U && jettyCount != 8U) || iterations != 1024U ||
        warmup != AlignUp(96U, batchSize) || payloadBytes != 64U) {
        std::cerr << "Expected selected batch, 64B, 1024 requests, warmup96 aligned, J1/2/4/8, two ranks" << std::endl;
        return 1;
    }
    const uint32_t api = apiName == "write" ? kApiWrite : apiName == "read" ? kApiRead : kApiNotify;
    const bool sender = (rank & 1U) == 0U;
    const uint32_t peer = rank ^ 1U;
    const uint32_t operationCount = warmup + iterations;
    const uint32_t slotBytes = SlotBytes(api, payloadBytes);
    const uint32_t regionBytes = slotBytes * operationCount;
    const size_t bufferBytes = static_cast<size_t>(regionBytes) * jettyCount;
    const uint32_t jettysPerKernel = std::max(1U, kMaxActiveLanes / batchSize);
    const uint32_t kernelLaunchCount = (jettyCount + jettysPerKernel - 1U) / jettysPerKernel;
    const uint32_t activeJettyCount = jettyCount;
    examples::RankSyncContext sync = rank == 0U ? examples::RankSyncContext::Listen(controlEndpoint, rankCount) :
                                                  examples::RankSyncContext::Connect(rank, rankCount, controlEndpoint);
    if (!sync.Ok()) {
        return 1;
    }
    CHECK_ACL(aclInit(nullptr));
    CHECK_ACL(aclrtSetDevice(static_cast<int32_t>(rank)));
    aclrtStream stream = nullptr;
    CHECK_ACL(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    CHECK_HCCL(examples::InitCommByRootInfo(sync, &comm));
    void* sendBuffer = nullptr;
    void* recvBuffer = nullptr;
    CHECK_ACL(aclrtMalloc(&sendBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(aclrtMalloc(&recvBuffer, bufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    CHECK_ACL(InitializeBuffers(
        sendBuffer, recvBuffer, bufferBytes, jettyCount, operationCount, payloadBytes, slotBytes, regionBytes));
    CommMem sendMemory{COMM_MEM_TYPE_DEVICE, sendBuffer, bufferBytes};
    CommMem recvMemory{COMM_MEM_TYPE_DEVICE, recvBuffer, bufferBytes};
    std::array<HcclMemHandle, 2U> handles{nullptr, nullptr};
    std::vector<ChannelHandle> channels(jettyCount, 0U);
    std::vector<uint64_t> remoteRecvAddresses(jettyCount, 0U);
    std::vector<uint32_t> remoteRecvIndices(jettyCount, 0U);
    CHECK_HCCL(HcclCommMemReg(comm, kSendTag, &sendMemory, &handles[0]));
    CHECK_HCCL(HcclCommMemReg(comm, kRecvTag, &recvMemory, &handles[1]));
    CHECK_HCCL(AcquireChannels(comm, rank, peer, handles.data(), channels, remoteRecvAddresses, remoteRecvIndices));
    for (uint32_t lhs = 0U; lhs < jettyCount; ++lhs) {
        if (channels[lhs] == 0U) {
            std::cerr << "Jetty channel is invalid: " << lhs << std::endl;
            return HCCL_E_INTERNAL;
        }
        for (uint32_t rhs = lhs + 1U; rhs < jettyCount; ++rhs) {
            if (channels[lhs] == channels[rhs]) {
                std::cerr << "Jetty channels are not independent: " << lhs << " and " << rhs << std::endl;
                return HCCL_E_INTERNAL;
            }
        }
    }
    if (!sync.Barrier(kTagReady)) {
        return 1;
    }
    bool pass = true;
    void* timingDevice = nullptr;
    void* channelsDevice = nullptr;
    void* remoteDevice = nullptr;
    void* completionDevice = nullptr;
    if (sender) {
        std::vector<CompletionSummary> publishSummaries(jettyCount);
        CHECK_ACL(aclrtMalloc(&completionDevice, kCompletionAllocationBytes, ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(completionDevice, kCompletionAllocationBytes, 0, kCompletionAllocationBytes));
        std::vector<uint64_t> timing(static_cast<size_t>(kernelLaunchCount) * kTimingWords, 0U);
        std::vector<uint64_t> channelValues(channels.begin(), channels.end());
        CHECK_ACL(aclrtMalloc(&timingDevice, timing.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemset(timingDevice, timing.size() * sizeof(uint64_t), 0, timing.size() * sizeof(uint64_t)));
        CHECK_ACL(aclrtMalloc(&channelsDevice, channelValues.size() * sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemcpy(
            channelsDevice, channelValues.size() * sizeof(uint64_t), channelValues.data(),
            channelValues.size() * sizeof(uint64_t), ACL_MEMCPY_HOST_TO_DEVICE));
        CHECK_ACL(aclrtMalloc(&remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEM_MALLOC_HUGE_FIRST));
        CHECK_ACL(aclrtMemcpy(
            remoteDevice, remoteRecvIndices.size() * sizeof(uint32_t), remoteRecvIndices.data(),
            remoteRecvIndices.size() * sizeof(uint32_t), ACL_MEMCPY_HOST_TO_DEVICE));
        for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
            const uint32_t firstJetty = launch * jettysPerKernel;
            const uint32_t kernelJettyCount = std::min(jettyCount - firstJetty, jettysPerKernel);
            void* launchTiming =
                static_cast<uint8_t*>(timingDevice) + static_cast<size_t>(launch) * kTimingWords * sizeof(uint64_t);
            LaunchSimtMultiJettyBatchPerf(
                stream, channelsDevice, remoteDevice, launchTiming, api, payloadBytes, slotBytes, regionBytes,
                iterations, warmup, batchSize, jettyCount, firstJetty, kernelJettyCount, completionDevice);
        }
        CHECK_ACL(aclrtSynchronizeStream(stream));
        CHECK_ACL(aclrtMemcpy(
            timing.data(), timing.size() * sizeof(uint64_t), timingDevice, timing.size() * sizeof(uint64_t),
            ACL_MEMCPY_DEVICE_TO_HOST));
        CHECK_ACL(aclrtMemcpy(
            publishSummaries.data(), publishSummaries.size() * sizeof(CompletionSummary), completionDevice,
            publishSummaries.size() * sizeof(CompletionSummary), ACL_MEMCPY_DEVICE_TO_HOST));
        PrintCompletionState(publishSummaries);
        pass = CheckCompletion(publishSummaries, warmup, operationCount, batchSize, api, jettysPerKernel) && pass;
        uint64_t issueCycles = 0U;
        uint64_t completed = 0U;
        for (uint32_t launch = 0U; launch < kernelLaunchCount; ++launch) {
            const uint64_t* result = timing.data() + static_cast<size_t>(launch) * kTimingWords;
            pass = pass && static_cast<int64_t>(result[kStatusIndex]) == 0;
            issueCycles += result[kIssueCyclesIndex];
            completed += result[kCompletedIndex];
        }
        const uint64_t totalOperations = static_cast<uint64_t>(iterations) * activeJettyCount;
        pass = pass && completed == totalOperations;
        if (pass && api == kApiRead) {
            pass = CheckBuffer(
                sendBuffer, bufferBytes, true, api, activeJettyCount, operationCount, payloadBytes, slotBytes,
                regionBytes, batchSize);
        }
        const double issueTime =
            totalOperations == 0U ? 0.0 : static_cast<double>(issueCycles) / static_cast<double>(totalOperations);
        std::cout << std::fixed << std::setprecision(6) << "PERF_DATA | API=" << apiName << " | Mode=MultiJetty"
                  << " | DataSize/B=" << payloadBytes << " | BatchSize=" << batchSize << " | Jettys=" << jettyCount
                  << " | ConcurrentJettys=" << std::min(jettyCount, jettysPerKernel)
                  << " | IssueTicksPerRequest=" << issueTime << std::endl;
    }
    if (!sync.Barrier(kTagCompleted)) {
        return 1;
    }
    if (!sender && api != kApiRead) {
        pass = CheckBuffer(
            recvBuffer, bufferBytes, false, api, activeJettyCount, operationCount, payloadBytes, slotBytes, regionBytes,
            batchSize);
    }
    if (timingDevice != nullptr) {
        CHECK_ACL(aclrtFree(timingDevice));
    }
    if (channelsDevice != nullptr) {
        CHECK_ACL(aclrtFree(channelsDevice));
    }
    if (remoteDevice != nullptr) {
        CHECK_ACL(aclrtFree(remoteDevice));
    }
    if (completionDevice != nullptr) {
        CHECK_ACL(aclrtFree(completionDevice));
    }
    CHECK_ACL(aclrtFree(sendBuffer));
    CHECK_ACL(aclrtFree(recvBuffer));
    CHECK_HCCL(HcclCommDestroy(comm));
    CHECK_ACL(aclrtDestroyStream(stream));
    CHECK_ACL(aclFinalize());
    return pass ? 0 : 2;
}
