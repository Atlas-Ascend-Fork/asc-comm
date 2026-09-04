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
    uint32_t operationsPerBatch, uint32_t batchCount, bool groupMode);
extern void LaunchSimtBatchBasic(
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
    if (rank == kRootRank) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        ret = WriteRootInfoFile(rootInfoFile, rootInfo);
    } else {
        ret = ReadRootInfoFile(rootInfoFile, &rootInfo);
    }
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    HCCLCHECK(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
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

bool CheckStatus(void* statusBuf, bool groupMode = false)
{
    std::array<int32_t, kStatusCount> status {};
    if (aclrtMemcpy(status.data(), sizeof(status), statusBuf, sizeof(status), ACL_MEMCPY_DEVICE_TO_HOST) !=
        ACL_SUCCESS) {
        return false;
    }
    bool pass = status[kStatusInit] == 0;
    const uint32_t checkedCaseCount = groupMode ? kApiCas + 1U : kCaseCount;
    for (uint32_t testCase = 0U; testCase < checkedCaseCount; ++testCase) {
        pass = pass && status[kStatusCaseBase + testCase] == 0;
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
    return status[0] == 0;
}

uint64_t LoadWord(const std::vector<uint8_t>& buffer, size_t offset)
{
    uint64_t value = 0U;
    std::memcpy(&value, buffer.data() + offset, sizeof(value));
    return value;
}

bool CheckPayloads(
    const std::vector<uint8_t>& actual, size_t offset, uint8_t valueBase,
    uint32_t operationCount = kOperationCount)
{
    bool pass = true;
    for (uint32_t operation = 0U; operation < operationCount; ++operation) {
        const uint8_t expected = valueBase + operation;
        const size_t operationOffset = offset + static_cast<size_t>(operation) * kPayloadBytes;
        bool operationPass = true;
        for (uint32_t byte = 0U; byte < kPayloadBytes; ++byte) {
            if (actual[operationOffset + byte] != expected) {
                operationPass = false;
                break;
            }
        }
        pass = pass && operationPass;
    }
    return pass;
}

bool CheckWord(const std::vector<uint8_t>& actual, size_t offset, uint64_t expected)
{
    return LoadWord(actual, offset) == expected;
}

bool CheckSender(void* sendBuf, uint32_t operationCount = kOperationCount, bool includeMixed = true)
{
    std::vector<uint8_t> actual(kBufferBytes);
    if (aclrtMemcpy(actual.data(), kBufferBytes, sendBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const bool readPass = CheckPayloads(actual, kReadOffset, kReadByteBase, operationCount);
    bool faaPass = true;
    bool casPass = true;
    for (uint32_t operation = 0U; operation < operationCount; ++operation) {
        const uint64_t expectedFaa = operation < kTargetCount ? kFaaInitial : kFaaInitial + kFaaAdd;
        const uint64_t expectedCas = operation < kTargetCount ? kCasInitial : kCasSwap;
        faaPass = CheckWord(actual, kFaaFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            expectedFaa) && faaPass;
        casPass = CheckWord(actual, kCasFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            expectedCas) && casPass;
    }
    bool mixedPass = true;
    if (includeMixed) {
        mixedPass = CheckWord(actual, kMixedReadOffset, kMixedReadValue);
        mixedPass = CheckWord(actual, kMixedFaaFetchOffset, kMixedFaaInitial) && mixedPass;
        mixedPass = CheckWord(actual, kMixedCasFetchOffset, kMixedCasInitial) && mixedPass;
    }
    return readPass && faaPass && casPass && mixedPass;
}

bool CheckReceiver(
    void* recvBuf, uint32_t operationCount = kOperationCount, bool includeExtended = true)
{
    std::vector<uint8_t> actual(kBufferBytes);
    if (aclrtMemcpy(actual.data(), kBufferBytes, recvBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    const bool writePass = CheckPayloads(actual, kWriteOffset, kWriteByteBase, operationCount);
    const bool readPass = CheckPayloads(actual, kReadOffset, kReadByteBase, operationCount);
    const bool notifyDataPass = CheckPayloads(actual, kNotifyDataOffset, kNotifyByteBase, operationCount);
    bool notifySignalPass = true;
    bool writeValuePass = true;
    for (uint32_t operation = 0U; operation < operationCount; ++operation) {
        notifySignalPass = CheckWord(
            actual, kNotifySignalOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            kNotifyValueBase + operation) && notifySignalPass;
        writeValuePass = CheckWord(actual, kWriteValueOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            kWriteValueBase + operation) && writeValuePass;
    }
    bool faaPass = true;
    bool casPass = true;
    for (uint32_t target = 0U; target < kTargetCount; ++target) {
        const uint32_t targetUpdates = operationCount > target ?
            (operationCount - 1U - target) / kTargetCount + 1U : 0U;
        faaPass = CheckWord(actual, kFaaTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            kFaaInitial + targetUpdates * kFaaAdd) && faaPass;
        casPass = CheckWord(actual, kCasTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            targetUpdates == 0U ? kCasInitial : kCasSwap) && casPass;
    }
    bool mixedPass = true;
    bool singlePass = true;
    if (includeExtended) {
        mixedPass = CheckWord(actual, kMixedWriteOffset, kMixedWriteValue);
        mixedPass = CheckWord(actual, kMixedReadOffset, kMixedReadValue) && mixedPass;
        mixedPass = CheckWord(actual, kMixedNotifyDataOffset, kMixedNotifyData) && mixedPass;
        mixedPass = CheckWord(actual, kMixedNotifySignalOffset, kMixedNotifyValue) && mixedPass;
        mixedPass = CheckWord(actual, kMixedWriteValueOffset, kMixedInlineValue) && mixedPass;
        mixedPass = CheckWord(actual, kMixedFaaTargetOffset, kMixedFaaInitial + kMixedFaaAdd) && mixedPass;
        mixedPass = CheckWord(actual, kMixedCasTargetOffset, kMixedCasSwap) && mixedPass;
        singlePass = CheckWord(actual, kSingleWriteValueOffset, kSingleWriteValue);
    }
    return writePass && readPass && notifyDataPass && notifySignalPass && writeValuePass && faaPass &&
        casPass && mixedPass && singlePass;
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
                pass = false;
                break;
            }
        }
    }
    return pass;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 4 && argc != 5) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> [basic|full|matrix-1x1|matrix-{2,8}x{1,2}]"
                  << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string modeName = argc == 5 ? argv[4] : "full";
    const bool matrixMode = modeName == "matrix-1x1" || modeName == "matrix-2x1" || modeName == "matrix-8x1" ||
        modeName == "matrix-2x2" || modeName == "matrix-8x2";
    if (modeName != "basic" && modeName != "full" && !matrixMode) {
        std::cout << "invalid mode." << std::endl;
        return 1;
    }
    const uint32_t operationsPerBatch = modeName == "matrix-1x1" ? 1U :
        (modeName.find("matrix-2x") == 0U ? 2U : kOperationsPerBatch);
    const uint32_t batchCount = matrixMode && modeName.back() == '1' ? 1U : kBatchCount;
    const RunMode mode = modeName == "basic" ? RunMode::BASIC : (matrixMode ? RunMode::MATRIX : RunMode::FULL);
    if ((nranks != 2 && nranks != 8) || rank < 0 || rank >= nranks) {
        std::cout << "simt_batch requires 2 or 8 valid ranks." << std::endl;
        return 1;
    }
    const bool isSender = (static_cast<uint32_t>(rank) & 1U) == 0U;

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    HCCLCHECK(InitCommByRootInfo(
        static_cast<uint32_t>(rank), static_cast<uint32_t>(nranks), rootInfoFile, &comm));

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

    const uint32_t peer = static_cast<uint32_t>(rank) ^ 1U;
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));

    void* statusBuf = nullptr;
    bool pass = true;
    if (isSender) {
        const size_t statusCount = mode == RunMode::BASIC ? kBasicStatusCount : kStatusCount;
        const size_t statusBytes = statusCount * sizeof(int32_t);
        ACLCHECK(aclrtMalloc(&statusBuf, statusBytes, ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(statusBuf, statusBytes, 0xFF, statusBytes));
        if (mode == RunMode::BASIC) {
            LaunchSimtBatchBasic(stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf);
        } else {
            LaunchSimtBatch(
                stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf, operationsPerBatch, batchCount,
                mode == RunMode::MATRIX);
        }
        ACLCHECK(aclrtSynchronizeStream(stream));
        pass = mode == RunMode::BASIC ? CheckBasicStatus(statusBuf) :
            CheckStatus(statusBuf, mode == RunMode::MATRIX) &&
                (mode == RunMode::MATRIX ?
                    CheckSender(sendBuf, operationsPerBatch * batchCount, false) : CheckSender(sendBuf));
    }

    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    if (!isSender) {
        pass = mode == RunMode::FULL ? CheckReceiver(recvBuf) :
            (mode == RunMode::MATRIX ? CheckReceiver(recvBuf, operationsPerBatch * batchCount, false) :
                CheckBasicReceiver(recvBuf));
    }
    const std::string suiteName = mode == RunMode::BASIC ? "simt_batch_basic" :
        (mode == RunMode::MATRIX ? "simt_batch_" + modeName : "simt_batch");
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
