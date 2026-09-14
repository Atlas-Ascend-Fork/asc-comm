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
#include <iostream>
#include <string>
#include <thread>

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

extern void LaunchSimtBatch(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status);

namespace {
using namespace simt_batch;

constexpr uint32_t kBarrierWaitSeconds = 60U;
constexpr uint32_t kBarrierPollMs = 100U;
constexpr uint32_t kLocalSendIdx = 1U;
constexpr CommProtocol kTargetCommProtocol = COMM_PROTOCOL_UBC_CTP;
constexpr const char* kSendBufTag = "simtBatchSendBuf";
constexpr const char* kRecvBufTag = "simtBatchRecvBuf";

bool FileReady(const std::string& path, size_t minBytes = 1U)
{
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && st.st_size >= static_cast<off_t>(minBytes);
}

HcclResult WriteRootInfo(const std::string& path, const HcclRootInfo& rootInfo)
{
    const std::string tmp = path + ".tmp";
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(&rootInfo), sizeof(rootInfo));
    out.close();
    if (!out || std::rename(tmp.c_str(), path.c_str()) != 0) {
        return HCCL_E_SYSCALL;
    }
    return HCCL_SUCCESS;
}

HcclResult ReadRootInfo(const std::string& path, HcclRootInfo* rootInfo)
{
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
        if (FileReady(path, sizeof(*rootInfo))) {
            std::ifstream in(path, std::ios::binary);
            in.read(reinterpret_cast<char*>(rootInfo), sizeof(*rootInfo));
            if (in.gcount() == static_cast<std::streamsize>(sizeof(*rootInfo))) {
                return HCCL_SUCCESS;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kBarrierPollMs));
    }
    return HCCL_E_TIMEOUT;
}

HcclResult InitComm(uint32_t rank, uint32_t nranks, const std::string& path, HcclComm* comm)
{
    HcclRootInfo rootInfo{};
    HcclResult ret = HCCL_SUCCESS;
    if (rank == kRootRank) {
        HCCLCHECK(HcclGetRootInfo(&rootInfo));
        ret = WriteRootInfo(path, rootInfo);
    } else {
        ret = ReadRootInfo(path, &rootInfo);
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

HcclResult HostBarrier(int rank, int nranks, const char* name)
{
    const std::string dir = SyncDir();
    if (mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) {
        return HCCL_E_SYSCALL;
    }
    const std::string prefix = dir + "/" + name + ".";
    std::ofstream marker(prefix + std::to_string(rank), std::ios::trunc);
    marker << '1';
    marker.close();
    const uint32_t maxPolls = kBarrierWaitSeconds * 1000U / kBarrierPollMs;
    for (uint32_t poll = 0U; poll < maxPolls; ++poll) {
        bool ready = true;
        for (int peer = 0; peer < nranks; ++peer) {
            ready = ready && FileReady(prefix + std::to_string(peer));
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
    std::array<uint8_t, kBufferBytes> send{};
    std::array<uint8_t, kBufferBytes> recv{};
    std::memset(send.data(), kPayloadBase, kPayloadBytes);
    aclError ret = aclrtMemcpy(sendBuf, kBufferBytes, send.data(), kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE);
    return ret == ACL_SUCCESS ?
               aclrtMemcpy(recvBuf, kBufferBytes, recv.data(), kBufferBytes, ACL_MEMCPY_HOST_TO_DEVICE) :
               ret;
}

HcclResult AcquirePeerChannel(
    HcclComm comm, uint32_t rank, uint32_t peerRank, HcclMemHandle* handles, ChannelHandle& channel,
    uint32_t& remoteRecvIdx)
{
    HcclChannelDesc desc;
    HCCLCHECK(HcclChannelDescInit(&desc, 1));
    desc.remoteRank = peerRank;
    desc.channelProtocol = kTargetCommProtocol;
    desc.notifyNum = 0;
    desc.memHandles = handles;
    desc.memHandleNum = 2U;

    uint32_t* layers = nullptr;
    uint32_t layerNum = 0U;
    HCCLCHECK(HcclRankGraphGetLayers(comm, &layers, &layerNum));
    if (layers == nullptr || layerNum == 0U) {
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

bool CheckReceiver(void* recvBuf)
{
    std::array<uint8_t, kBufferBytes> actual{};
    if (aclrtMemcpy(actual.data(), kBufferBytes, recvBuf, kBufferBytes, ACL_MEMCPY_DEVICE_TO_HOST) != ACL_SUCCESS) {
        return false;
    }
    for (uint32_t byte = 0U; byte < kPayloadBytes; ++byte) {
        if (actual[byte] != kPayloadBase) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <rank> <nranks> <root_info_file>" << std::endl;
        return 1;
    }
    const int rank = std::atoi(argv[1]);
    const int nranks = std::atoi(argv[2]);
    const std::string rootInfoFile = argv[3];
    if ((nranks != 2 && nranks != 8) || rank < 0 || rank >= nranks) {
        return 1;
    }
    const bool isSender = (static_cast<uint32_t>(rank) & 1U) == 0U;

    ACLCHECK(aclInit(nullptr));
    ACLCHECK(aclrtSetDevice(rank));
    aclrtStream stream = nullptr;
    ACLCHECK(aclrtCreateStream(&stream));
    HcclComm comm = nullptr;
    HCCLCHECK(InitComm(static_cast<uint32_t>(rank), static_cast<uint32_t>(nranks), rootInfoFile, &comm));

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

    ChannelHandle channel = 0U;
    uint32_t remoteRecvIdx = 0U;
    const uint32_t peer = static_cast<uint32_t>(rank) ^ 1U;
    HCCLCHECK(AcquirePeerChannel(comm, rank, peer, handles, channel, remoteRecvIdx));
    HCCLCHECK(HostBarrier(rank, nranks, "ready"));

    void* statusBuf = nullptr;
    bool pass = true;
    if (isSender) {
        int32_t status = -1;
        ACLCHECK(aclrtMalloc(&statusBuf, sizeof(status), ACL_MEM_MALLOC_HUGE_FIRST));
        ACLCHECK(aclrtMemset(statusBuf, sizeof(status), 0xFF, sizeof(status)));
        LaunchSimtBatch(stream, channel, kLocalSendIdx, remoteRecvIdx, statusBuf);
        ACLCHECK(aclrtSynchronizeStream(stream));
        ACLCHECK(aclrtMemcpy(&status, sizeof(status), statusBuf, sizeof(status), ACL_MEMCPY_DEVICE_TO_HOST));
        pass = status == 0;
    }

    HCCLCHECK(HostBarrier(rank, nranks, "completed"));
    if (!isSender) {
        pass = CheckReceiver(recvBuf);
    }
    std::cout << "[rank " << rank << "] simt_batch " << (pass ? "PASS" : "FAIL") << std::endl;

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
