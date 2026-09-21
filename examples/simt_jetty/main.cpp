/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>

#include <sys/stat.h>
#include <sys/types.h>

#include "hccl/hccl.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "simt_jetty_common.h"

#define ACLCHECK(ret)                                                                            \
    do {                                                                                         \
        if ((ret) != ACL_SUCCESS) {                                                              \
            printf("acl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                        \
        }                                                                                        \
    } while (0)

#define HCCLCHECK(ret)                                                                            \
    do {                                                                                          \
        if ((ret) != HCCL_SUCCESS) {                                                              \
            printf("hccl interface return err %s:%d, retcode: %d \n", __FILE__, __LINE__, (ret)); \
            return (ret);                                                                         \
        }                                                                                         \
    } while (0)

extern void LaunchSimtJetty(
    void* stream, uint64_t channel, void* jettyPtrTable, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode,
    void* timing, uint32_t iterations, uint32_t warmup);

namespace {

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr uint32_t kIterationPollMs = 1U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtJettySendBuf";
constexpr const char* kRecvBufTag = "simtJettyRecvBuf";

const char* ModeName(uint32_t mode)
{
    constexpr const char* kModeNames[] = {"thread", "warp-pad", "group", "dispatch", "warp", "mixed"};
    return mode < sizeof(kModeNames) / sizeof(kModeNames[0]) ? kModeNames[mode] : "unknown";
}

bool IsMarkerFileReady(const std::string& path)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= 1;
}

HcclResult WriteMarkerFile(const std::string& path)
{
    const std::string tmpPath = path + ".tmp";
    std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return HCCL_E_OPEN_FILE_FAILURE;
    }
    ofs << '1';
    ofs.close();
    if (!ofs) {
        return HCCL_E_SYSCALL;
    }
    if (std::rename(tmpPath.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult WaitMarkerFile(const std::string& path, uint32_t pollMs = kBarrierPollMs)
{
    const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000U / pollMs;
    for (uint32_t i = 0; i < maxPollTimes; ++i) {
        if (IsMarkerFileReady(path)) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult HostBarrier(int rank, int nranks, const std::string& syncDir, const std::string& tag)
{
    if (mkdir(syncDir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string marker = syncDir + "/" + tag + "." + std::to_string(rank);
    HcclResult ret = WriteMarkerFile(marker);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    const std::string prefix = syncDir + "/" + tag + ".";
    const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0; poll < maxPollTimes; ++poll) {
        bool ready = true;
        for (int i = 0; i < nranks; ++i) {
            if (!IsMarkerFileReady(prefix + std::to_string(i))) {
                ready = false;
                break;
            }
        }
        if (ready) {
            return HCCL_SUCCESS;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitCommByRootInfo(uint32_t rank, uint32_t nranks, const std::string& rootInfoFile, HcclComm* comm)
{
    HcclRootInfo rootInfo{};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == 0U) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        const std::string tmpPath = rootInfoFile + ".tmp";
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            return HCCL_E_OPEN_FILE_FAILURE;
        }
        ofs.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
        ofs.close();
        if (!ofs) {
            return HCCL_E_SYSCALL;
        }
        if (std::rename(tmpPath.c_str(), rootInfoFile.c_str()) != 0) {
            return HCCL_E_SYSCALL;
        }
    } else {
        const uint32_t maxPollTimes = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
        for (uint32_t i = 0; i < maxPollTimes; ++i) {
            struct stat st {};
            if (stat(rootInfoFile.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(sizeof(HcclRootInfo))) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
        }
        std::ifstream ifs(rootInfoFile, std::ios::binary);
        if (!ifs.is_open()) {
            return HCCL_E_OPEN_FILE_FAILURE;
        }
        ifs.read(reinterpret_cast<char*>(&rootInfo), sizeof(rootInfo));
        if (ifs.gcount() != static_cast<std::streamsize>(sizeof(rootInfo))) {
            return HCCL_E_SYSCALL;
        }
    }
    HCCLCHECK(HcclCommInitRootInfo(nranks, &rootInfo, rank, comm));
    return HCCL_SUCCESS;
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

bool CheckWords(void* deviceBuf, const uint64_t expected[simt_jetty::kSlotCount])
{
    uint64_t actual[simt_jetty::kSlotCount] = {};
    if (aclrtMemcpy(actual, simt_jetty::kBufferBytes, deviceBuf, simt_jetty::kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) !=
        ACL_SUCCESS) {
        return false;
    }
    for (uint32_t i = 0; i < simt_jetty::kSlotCount; ++i) {
        if (actual[i] != expected[i]) {
            std::cout << "slot " << i << " actual=" << actual[i] << " expected=" << expected[i] << std::endl;
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 6 && argc != 9) {
        std::cout << "Usage: " << argv[0]
                  << " <rank> <nranks> <root_info_file> <sync_dir> <mode> [iterations warmup timing]" << std::endl;
        return 1;
    }

    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    const std::string syncDir = argv[4];
    const uint32_t mode = static_cast<uint32_t>(std::strtoul(argv[5], nullptr, 10));
    const uint32_t iterations = argc == 9 ? static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10)) : 1U;
    const uint32_t warmup = argc == 9 ? static_cast<uint32_t>(std::strtoul(argv[7], nullptr, 10)) : 0U;
    const bool deviceTiming = argc == 9 && std::strtoul(argv[8], nullptr, 10) != 0U;

    if (nranks < 2 || rank < 0 || rank >= nranks || syncDir.empty() || mode > 5U || iterations == 0U) {
        std::cout << "invalid rank/nranks/sync_dir/mode/iterations" << std::endl;
        return 1;
    }
    if (rank >= 2) {
        std::cout << "SKIP | Rank=" << rank << " | Only rank 0 and rank 1 run simt_jetty." << std::endl;
        return 0;
    }

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));

    HcclComm comm = nullptr;
    HCCLCHECK(InitCommByRootInfo(rank, nranks, rootInfoFile, &comm));

    void* sendBuf = nullptr;
    void* recvBuf = nullptr;
    ACLCHECK(aclrtMalloc(&sendBuf, simt_jetty::kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    ACLCHECK(aclrtMalloc(&recvBuf, simt_jetty::kBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST));
    uint64_t sendInit[simt_jetty::kSlotCount] = {};
    for (uint32_t slot = 0; slot < simt_jetty::kSlotCount; ++slot) {
        sendInit[slot] = simt_jetty::SlotValue(slot);
    }
    uint64_t recvInit[simt_jetty::kSlotCount] = {};
    ACLCHECK(
        aclrtMemcpy(sendBuf, simt_jetty::kBufferBytes, sendInit, simt_jetty::kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE));
    ACLCHECK(
        aclrtMemcpy(recvBuf, simt_jetty::kBufferBytes, recvInit, simt_jetty::kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE));

    CommMem sendMem{COMM_MEM_TYPE_DEVICE, sendBuf, simt_jetty::kBufferBytes};
    CommMem recvMem{COMM_MEM_TYPE_DEVICE, recvBuf, simt_jetty::kBufferBytes};
    HcclMemHandle memHandles[2] = {nullptr, nullptr};
    HCCLCHECK(HcclCommMemReg(comm, kSendBufTag, &sendMem, &memHandles[0]));
    HCCLCHECK(HcclCommMemReg(comm, kRecvBufTag, &recvMem, &memHandles[1]));

    const uint32_t peerRank = static_cast<uint32_t>(rank == 0 ? 1 : 0);
    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    HCCLCHECK(AcquirePeerChannel(comm, static_cast<uint32_t>(rank), peerRank, memHandles, channel, remoteRecvIdx));

    void* jettyPtrTable = nullptr;
    ACLCHECK(aclrtMalloc(&jettyPtrTable, sizeof(uint64_t), ACL_MEM_MALLOC_HUGE_FIRST));
    const uint64_t channelValue = static_cast<uint64_t>(channel);
    ACLCHECK(aclrtMemcpy(
        jettyPtrTable, sizeof(channelValue), &channelValue, sizeof(channelValue), ACL_MEMCPY_HOST_TO_DEVICE));

    HCCLCHECK(HostBarrier(rank, nranks, syncDir, "ready"));

    constexpr uint32_t kLocalSendIdx = 1U;
    bool pass = true;
    void* timingBuf = nullptr;
    uint64_t timing[4] = {};
    if (rank == 0) {
        if (deviceTiming) {
            ACLCHECK(aclrtMalloc(&timingBuf, sizeof(timing), ACL_MEM_MALLOC_HUGE_FIRST));
            ACLCHECK(aclrtMemset(timingBuf, sizeof(timing), 0, sizeof(timing)));
            LaunchSimtJetty(
                stream, static_cast<uint64_t>(channel), jettyPtrTable, kLocalSendIdx, remoteRecvIdx, mode, timingBuf,
                iterations, warmup);
            ACLCHECK(aclrtSynchronizeStream(stream));
            ACLCHECK(aclrtMemcpy(timing, sizeof(timing), timingBuf, sizeof(timing), ACL_MEMCPY_DEVICE_TO_HOST));
            pass = timing[3] == iterations;
            const double averageWriteCycles = static_cast<double>(timing[0]) / iterations;
            const double averageCommitCycles = static_cast<double>(timing[1]) / iterations;
            const double averageCompletionCycles = static_cast<double>(timing[2]) / iterations;
            std::cout << std::fixed << std::setprecision(3) << "PERF | mode=" << ModeName(mode)
                      << " | iterations=" << iterations << " | warmup=" << warmup << " | write_cycles=" << timing[0]
                      << " | average_write_cycles=" << averageWriteCycles
                      << " | average_write_us=" << averageWriteCycles / 1000.0 << " | commit_cycles=" << timing[1]
                      << " | average_commit_cycles=" << averageCommitCycles
                      << " | average_commit_us=" << averageCommitCycles / 1000.0 << " | completion_cycles=" << timing[2]
                      << " | average_completion_cycles=" << averageCompletionCycles
                      << " | average_completion_us=" << averageCompletionCycles / 1000.0 << " | completed=" << timing[3]
                      << std::endl;
        } else {
            for (uint32_t iteration = 0U; iteration < iterations; ++iteration) {
                LaunchSimtJetty(
                    stream, static_cast<uint64_t>(channel), jettyPtrTable, kLocalSendIdx, remoteRecvIdx, mode, nullptr,
                    1U, 0U);
                ACLCHECK(aclrtSynchronizeStream(stream));
                HCCLCHECK(WriteMarkerFile(syncDir + "/done." + std::to_string(iteration)));
                HCCLCHECK(WaitMarkerFile(syncDir + "/checked." + std::to_string(iteration), kIterationPollMs));
            }
        }
        if (deviceTiming) {
            HCCLCHECK(WriteMarkerFile(syncDir + "/done.0"));
            HCCLCHECK(WaitMarkerFile(syncDir + "/checked.0", kIterationPollMs));
        }
    } else {
        uint64_t expected[simt_jetty::kSlotCount] = {};
        if (mode == 0U || mode == 3U) {
            for (uint32_t slot = 0; slot < simt_jetty::kSlotCount; ++slot) {
                expected[slot] = simt_jetty::SlotValue(slot);
            }
        } else if (mode == 1U) {
            expected[0] = simt_jetty::SlotValue(0);
        } else if (mode == 2U || mode == 4U || mode == 5U) {
            // group/warp/mixed posts carry kConfiguredSgeNum SGEs; warp-pad keeps slot 0 only.
            for (uint32_t slot = 0; slot < simt_jetty::kConfiguredSgeNum; ++slot) {
                expected[slot] = simt_jetty::SlotValue(slot);
            }
        } else {
            expected[0] = simt_jetty::SlotValue(0);
            expected[1] = simt_jetty::SlotValue(1);
        }
        const uint32_t checks = deviceTiming ? 1U : iterations;
        for (uint32_t iteration = 0U; iteration < checks; ++iteration) {
            HCCLCHECK(
                WaitMarkerFile(syncDir + "/done." + std::to_string(deviceTiming ? 0U : iteration), kIterationPollMs));
            const bool iterationPass = CheckWords(recvBuf, expected);
            pass = pass && iterationPass;
            if (!iterationPass) {
                std::cout << "[rank 1] simt_jetty mode " << mode << " iteration " << iteration << " FAIL"
                          << " checks="
                          << (mode == 0U || mode == 3U               ? simt_jetty::kSlotCount :
                              mode == 1U                             ? 1U :
                              mode == 2U || mode == 4U || mode == 5U ? simt_jetty::kConfiguredSgeNum :
                                                                       2U)
                          << std::endl;
            }
            if (!deviceTiming && iteration + 1U < checks) {
                ACLCHECK(aclrtMemset(recvBuf, simt_jetty::kBufferBytes, 0, simt_jetty::kBufferBytes));
            }
            HCCLCHECK(WriteMarkerFile(syncDir + "/checked." + std::to_string(deviceTiming ? 0U : iteration)));
        }
        std::cout << "[rank 1] simt_jetty mode " << mode << " iterations " << checks << (pass ? " PASS" : " FAIL")
                  << std::endl;
    }

    if (timingBuf != nullptr) {
        ACLCHECK(aclrtFree(timingBuf));
    }
    ACLCHECK(aclrtFree(jettyPtrTable));
    ACLCHECK(aclrtFree(sendBuf));
    ACLCHECK(aclrtFree(recvBuf));
    HCCLCHECK(HcclCommDestroy(comm));
    ACLCHECK(aclrtDestroyStream(stream));
    ACLCHECK(aclFinalize());
    return pass ? 0 : 2;
}
