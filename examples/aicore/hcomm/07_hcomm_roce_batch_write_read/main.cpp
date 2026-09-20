/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <mpi.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "acl/acl.h"
#include "hccl/hccl.h"
#include "hccl/hccl_comm.h"
#include "hccl/hccl_rank_graph.h"
#include "hccl/hccl_res.h"
#include "hccl/hccl_types.h"

#include "roce_batch_write_read_def.h"

extern void LaunchHcommRoceBatchWriteRead(void* stream, uint64_t context);

namespace {

using namespace HcommRoceBatchWriteReadExample;

constexpr CommProtocol kCommProtocol = COMM_PROTOCOL_ROCE;

// Keep all MPI ranks in the same control flow after every setup stage. This avoids one rank
// returning while another rank is still inside an HCCL collective.
bool AllRanksOk(bool localOk)
{
    int local = localOk ? 1 : 0;
    int global = 0;
    if (MPI_Allreduce(&local, &global, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD) != MPI_SUCCESS) {
        return false;
    }
    return global != 0;
}

struct RankResources {
    ~RankResources()
    {
        if (comm != nullptr) {
            (void)HcclCommDestroy(comm);
        }
        if (deviceContext != nullptr) {
            (void)aclrtFree(deviceContext);
        }
        if (deviceBuffer != nullptr) {
            (void)aclrtFree(deviceBuffer);
        }
        if (stream != nullptr) {
            (void)aclrtDestroyStream(stream);
        }
        if (deviceSet) {
            (void)aclrtResetDevice(rank);
        }
        if (aclInitialized) {
            (void)aclFinalize();
        }
    }

    RankResources() = default;

    RankResources(const RankResources&) = delete;
    RankResources& operator=(const RankResources&) = delete;

    int rank{0};
    int worldSize{0};
    bool aclInitialized{false};
    bool deviceSet{false};
    aclrtStream stream{nullptr};
    HcclComm comm{nullptr};
    void* deviceBuffer{nullptr};
    KernelContext* deviceContext{nullptr};
    HcclMemHandle memHandle{nullptr};
    ChannelHandle channel{0U};
    uint64_t remoteBuffer{0U};
};

bool CheckAcl(int rank, aclError actual, aclError expected, const char* operation)
{
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "[ERROR] rank %d: %s failed, ret=%d\n", rank, operation, static_cast<int>(actual));
    return false;
}

bool CheckHccl(int rank, HcclResult actual, HcclResult expected, const char* operation)
{
    if (actual == expected) {
        return true;
    }
    std::fprintf(stderr, "[ERROR] rank %d: %s failed, ret=%d\n", rank, operation, static_cast<int>(actual));
    return false;
}

bool InitAcl(RankResources& resources)
{
    if (!CheckAcl(resources.rank, aclInit(nullptr), ACL_SUCCESS, "aclInit")) {
        return false;
    }
    resources.aclInitialized = true;

    uint32_t deviceCount = 0U;
    if (!CheckAcl(resources.rank, aclrtGetDeviceCount(&deviceCount), ACL_SUCCESS, "aclrtGetDeviceCount")) {
        return false;
    }
    if (resources.rank >= static_cast<int>(deviceCount)) {
        std::fprintf(
            stderr, "[ERROR] rank %d: MPI rank maps to device %d, but only %u device(s) are available\n",
            resources.rank, resources.rank, deviceCount);
        return false;
    }
    if (!CheckAcl(resources.rank, aclrtSetDevice(resources.rank), ACL_SUCCESS, "aclrtSetDevice")) {
        return false;
    }
    resources.deviceSet = true;
    return CheckAcl(resources.rank, aclrtCreateStream(&resources.stream), ACL_SUCCESS, "aclrtCreateStream");
}

bool InitCommunicator(RankResources& resources)
{
    HcclRootInfo rootInfo{};
    int rootStatus = HCCL_SUCCESS;
    if (resources.rank == 0) {
        rootStatus = static_cast<int>(HcclGetRootInfo(&rootInfo));
    }
    if (MPI_Bcast(&rootStatus, 1, MPI_INT, 0, MPI_COMM_WORLD) != MPI_SUCCESS || rootStatus != HCCL_SUCCESS) {
        std::fprintf(stderr, "[ERROR] rank %d: HcclGetRootInfo/MPI_Bcast failed, ret=%d\n", resources.rank, rootStatus);
        return false;
    }
    if (MPI_Bcast(&rootInfo, static_cast<int>(sizeof(rootInfo)), MPI_BYTE, 0, MPI_COMM_WORLD) != MPI_SUCCESS) {
        std::fprintf(stderr, "[ERROR] rank %d: root info broadcast failed\n", resources.rank);
        return false;
    }
    return CheckHccl(
        resources.rank,
        HcclCommInitRootInfo(
            static_cast<uint32_t>(resources.worldSize), &rootInfo, static_cast<uint32_t>(resources.rank),
            &resources.comm),
        HCCL_SUCCESS, "HcclCommInitRootInfo");
}

bool PrepareMemory(RankResources& resources)
{
    if (!CheckAcl(
            resources.rank, aclrtMalloc(&resources.deviceBuffer, kCommBufferBytes, ACL_MEM_MALLOC_HUGE_FIRST),
            ACL_SUCCESS, "aclrtMalloc communication buffer")) {
        return false;
    }

    std::vector<uint8_t> hostBuffer(static_cast<size_t>(kCommBufferBytes), 0U);
    for (uint32_t slot = 0U; slot < kOperationCount; ++slot) {
        for (uint32_t byte = 0U; byte < kTransferBytes; ++byte) {
            hostBuffer[kSendOffset + static_cast<uint64_t>(slot) * kTransferBytes + byte] =
                DataValue(static_cast<uint32_t>(resources.rank), slot, byte);
        }
    }
    if (!CheckAcl(
            resources.rank,
            aclrtMemcpy(
                resources.deviceBuffer, kCommBufferBytes, hostBuffer.data(), kCommBufferBytes,
                ACL_MEMCPY_HOST_TO_DEVICE),
            ACL_SUCCESS, "aclrtMemcpy source buffer")) {
        return false;
    }

    CommMem memory{};
    memory.type = COMM_MEM_TYPE_DEVICE;
    memory.addr = resources.deviceBuffer;
    memory.size = kCommBufferBytes;
    return CheckHccl(
        resources.rank, HcclCommMemReg(resources.comm, kMemoryTag, &memory, &resources.memHandle), HCCL_SUCCESS,
        "HcclCommMemReg");
}

bool FindRoceChannelDesc(RankResources& resources, uint32_t peerRank, HcclChannelDesc& channelDesc)
{
    uint32_t* layers = nullptr;
    uint32_t layerCount = 0U;
    if (!CheckHccl(
            resources.rank, HcclRankGraphGetLayers(resources.comm, &layers, &layerCount), HCCL_SUCCESS,
            "HcclRankGraphGetLayers")) {
        return false;
    }

    for (uint32_t layer = 0U; layer < layerCount; ++layer) {
        CommLink* links = nullptr;
        uint32_t linkCount = 0U;
        if (!CheckHccl(
                resources.rank,
                HcclRankGraphGetLinks(
                    resources.comm, layers[layer], static_cast<uint32_t>(resources.rank), peerRank, &links, &linkCount),
                HCCL_SUCCESS, "HcclRankGraphGetLinks")) {
            return false;
        }
        for (uint32_t index = 0U; index < linkCount; ++index) {
            if (links[index].linkAttr.linkProtocol != kCommProtocol) {
                continue;
            }
            if (!CheckHccl(
                    resources.rank, HcclChannelDescInit(&channelDesc, 1U), HCCL_SUCCESS, "HcclChannelDescInit")) {
                return false;
            }
            channelDesc.remoteRank = peerRank;
            channelDesc.channelProtocol = kCommProtocol;
            channelDesc.localEndpoint = links[index].srcEndpointDesc;
            channelDesc.remoteEndpoint = links[index].dstEndpointDesc;
            channelDesc.memHandles = &resources.memHandle;
            channelDesc.memHandleNum = 1U;
            return true;
        }
    }

    std::fprintf(stderr, "[ERROR] rank %d: no COMM_PROTOCOL_ROCE link to rank %u\n", resources.rank, peerRank);
    return false;
}

bool AcquireChannel(RankResources& resources)
{
    const uint32_t peerRank = static_cast<uint32_t>((resources.rank + 1) % resources.worldSize);
    HcclChannelDesc channelDesc{};
    if (!FindRoceChannelDesc(resources, peerRank, channelDesc)) {
        return false;
    }
    if (!CheckHccl(
            resources.rank, HcclChannelAcquire(resources.comm, COMM_ENGINE_AIV, &channelDesc, 1U, &resources.channel),
            HCCL_SUCCESS, "HcclChannelAcquire")) {
        return false;
    }
    if (resources.channel == 0U) {
        std::fprintf(stderr, "[ERROR] rank %d: HcclChannelAcquire returned a null channel\n", resources.rank);
        return false;
    }
    return true;
}

bool GetRemoteBuffer(RankResources& resources)
{
    uint32_t memoryCount = 0U;
    CommMem* memories = nullptr;
    char** tags = nullptr;
    if (!CheckHccl(
            resources.rank, HcclChannelGetRemoteMems(resources.comm, resources.channel, &memoryCount, &memories, &tags),
            HCCL_SUCCESS, "HcclChannelGetRemoteMems")) {
        return false;
    }
    for (uint32_t index = 0U; index < memoryCount; ++index) {
        if (tags != nullptr && tags[index] != nullptr && std::strcmp(tags[index], kMemoryTag) == 0) {
            resources.remoteBuffer = reinterpret_cast<uint64_t>(memories[index].addr);
            break;
        }
    }
    if (resources.remoteBuffer == 0U && memoryCount == 1U) {
        resources.remoteBuffer = reinterpret_cast<uint64_t>(memories[0].addr);
    }
    if (resources.remoteBuffer == 0U) {
        std::fprintf(stderr, "[ERROR] rank %d: remote communication buffer not found\n", resources.rank);
        return false;
    }
    return true;
}

bool PrepareKernelContext(RankResources& resources)
{
    if (!CheckAcl(
            resources.rank,
            aclrtMalloc(
                reinterpret_cast<void**>(&resources.deviceContext), sizeof(KernelContext), ACL_MEM_MALLOC_HUGE_FIRST),
            ACL_SUCCESS, "aclrtMalloc kernel context")) {
        return false;
    }
    KernelContext context{};
    context.channelHandle = static_cast<uint64_t>(resources.channel);
    context.localBuffer = reinterpret_cast<uint64_t>(resources.deviceBuffer);
    context.remoteBuffer = resources.remoteBuffer;
    context.result = KERNEL_SUCCESS;
    context.hcommResult = 0;
    return CheckAcl(
        resources.rank,
        aclrtMemcpy(resources.deviceContext, sizeof(context), &context, sizeof(context), ACL_MEMCPY_HOST_TO_DEVICE),
        ACL_SUCCESS, "aclrtMemcpy kernel context");
}

const char* KernelError(KernelResult result)
{
    switch (result) {
        case KERNEL_MAKE_HANDLE_FAILED:
            return "MakeBatchHandle failed";
        case KERNEL_WRITE_FAILED:
            return "WriteNbi failed";
        case KERNEL_READ_FAILED:
            return "ReadNbi failed";
        case KERNEL_COMMIT_FAILED:
            return "BatchCommit failed";
        case KERNEL_DRAIN_FAILED:
            return "Drain failed";
        default:
            return "unknown kernel error";
    }
}

bool LaunchAndCheck(RankResources& resources)
{
    LaunchHcommRoceBatchWriteRead(
        reinterpret_cast<void*>(resources.stream),
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resources.deviceContext)));
    if (!CheckAcl(resources.rank, aclrtSynchronizeStream(resources.stream), ACL_SUCCESS, "aclrtSynchronizeStream")) {
        return false;
    }

    KernelContext context{};
    if (!CheckAcl(
            resources.rank,
            aclrtMemcpy(&context, sizeof(context), resources.deviceContext, sizeof(context), ACL_MEMCPY_DEVICE_TO_HOST),
            ACL_SUCCESS, "aclrtMemcpy kernel result")) {
        return false;
    }
    if (context.result != KERNEL_SUCCESS) {
        std::fprintf(
            stderr, "[ERROR] rank %d: %s, hcomm ret=%d\n", resources.rank, KernelError(context.result),
            context.hcommResult);
        return false;
    }
    return true;
}

bool ValidateResults(RankResources& resources)
{
    std::vector<uint8_t> hostBuffer(static_cast<size_t>(kCommBufferBytes), 0U);
    if (!CheckAcl(
            resources.rank,
            aclrtMemcpy(
                hostBuffer.data(), kCommBufferBytes, resources.deviceBuffer, kCommBufferBytes,
                ACL_MEMCPY_DEVICE_TO_HOST),
            ACL_SUCCESS, "aclrtMemcpy result buffer")) {
        return false;
    }

    const uint32_t previousRank =
        static_cast<uint32_t>((resources.rank - 1 + resources.worldSize) % resources.worldSize);
    const uint32_t nextRank = static_cast<uint32_t>((resources.rank + 1) % resources.worldSize);
    for (uint32_t slot = 0U; slot < kOperationCount; ++slot) {
        for (uint32_t byte = 0U; byte < kTransferBytes; ++byte) {
            const uint64_t offset = static_cast<uint64_t>(slot) * kTransferBytes + byte;
            const uint8_t writeExpected = DataValue(previousRank, slot, byte);
            if (hostBuffer[kWriteResultOffset + offset] != writeExpected) {
                std::fprintf(
                    stderr,
                    "[ERROR] rank %d: WriteNbi result mismatch at slot=%u byte=%u, actual=0x%02x expected=0x%02x\n",
                    resources.rank, slot, byte, hostBuffer[kWriteResultOffset + offset], writeExpected);
                return false;
            }
            const uint8_t readExpected = DataValue(nextRank, slot, byte);
            if (hostBuffer[kReadResultOffset + offset] != readExpected) {
                std::fprintf(
                    stderr,
                    "[ERROR] rank %d: ReadNbi result mismatch at slot=%u byte=%u, actual=0x%02x expected=0x%02x\n",
                    resources.rank, slot, byte, hostBuffer[kReadResultOffset + offset], readExpected);
                return false;
            }
        }
    }

    std::printf("[rank %d] hcomm_roce_batch_write_read | WriteNbi=1000 ReadNbi=1000 | PASS\n", resources.rank);
    return true;
}

bool RunRank(int rank, int worldSize)
{
    RankResources resources;
    resources.rank = rank;
    resources.worldSize = worldSize;

    bool localOk = InitAcl(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    localOk = InitCommunicator(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    localOk = PrepareMemory(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    // Establish channels in two phases to avoid a circular wait when every rank connects to its
    // next rank at the same time.
    localOk = resources.rank % 2 == 0 ? AcquireChannel(resources) : true;
    if (!AllRanksOk(localOk) || MPI_Barrier(MPI_COMM_WORLD) != MPI_SUCCESS) {
        return false;
    }
    localOk = resources.rank % 2 == 0 ? true : AcquireChannel(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    localOk = GetRemoteBuffer(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    localOk = PrepareKernelContext(resources);
    if (!AllRanksOk(localOk)) {
        return false;
    }

    if (MPI_Barrier(MPI_COMM_WORLD) != MPI_SUCCESS) {
        return false;
    }
    localOk = LaunchAndCheck(resources);
    // Drain makes this rank's outgoing writes and reads complete. The barrier lets every rank
    // finish before any rank validates the write target that its previous rank populated.
    if (MPI_Barrier(MPI_COMM_WORLD) != MPI_SUCCESS) {
        return false;
    }
    if (!AllRanksOk(localOk)) {
        return false;
    }

    localOk = ValidateResults(resources);
    return AllRanksOk(localOk);
}

} // namespace

int main(int argc, char** argv)
{
    int provided = 0;
    if (MPI_Init_thread(&argc, &argv, MPI_THREAD_FUNNELED, &provided) != MPI_SUCCESS) {
        return 1;
    }

    int rank = 0;
    int worldSize = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &worldSize);

    if (worldSize < 2) {
        if (rank == 0) {
            std::fprintf(stderr, "Usage: mpirun -np <2..N> %s\n", argv[0]);
        }
        MPI_Finalize();
        return 1;
    }

    const bool passed = RunRank(rank, worldSize);
    if (rank == 0) {
        std::printf("RESULT | Example=hcomm_roce_batch_write_read | Status=%s\n", passed ? "PASS" : "FAIL");
    }
    MPI_Finalize();
    return passed ? 0 : 1;
}
