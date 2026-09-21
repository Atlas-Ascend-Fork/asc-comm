/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/hcomm.h"
#include "kernel_operator.h"

#include "multi_jetty_batch_perf_common.h"

namespace {
using namespace multi_jetty_batch_perf;

__aicore__ inline int32_t SubmitBatches(
    AscendC::Hcomm<>& hcomm, AscendC::UbcCtpBatchHandle& batch, uint32_t api, GM_ADDR remote, GM_ADDR local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t operationBase, uint32_t count, uint32_t batchSize,
    uint32_t jetty)
{
    for (uint32_t begin = 0U; begin < count; begin += batchSize) {
        for (uint32_t item = 0U; item < batchSize; ++item) {
            const uint32_t operation = operationBase + begin + item;
            const uint64_t offset = static_cast<uint64_t>(operation) * slotBytes;
            int32_t status = 0;
            if (api == kApiWrite) {
                status = hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes);
            } else if (api == kApiRead) {
                status = hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes);
            } else {
                const uint64_t notifyValue = kNotifyValueBase | (static_cast<uint64_t>(jetty) << 32U) | operation;
                status = hcomm.WriteWithNotifyNbi(
                    batch, remote + offset, local + offset, payloadBytes,
                    remote + offset + slotBytes - sizeof(uint64_t), notifyValue);
            }
            if (status != AscendC::HCOMM_SUCCESS) {
                return status;
            }
        }
        const int32_t status = hcomm.BatchCommit(batch);
        if (status != AscendC::HCOMM_SUCCESS) {
            return status;
        }
    }
    return AscendC::HCOMM_SUCCESS;
}

__aicore__ inline int32_t SubmitAllJettys(
    AscendC::Hcomm<>& hcomm, AscendC::UbcCtpBatchHandle* batches, __gm__ uint64_t* remoteAddrs,
    GM_ADDR localBase, uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes,
    uint32_t operationBase, uint32_t count, uint32_t batchSize, uint32_t jettyCount)
{
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        auto remote = reinterpret_cast<GM_ADDR>(remoteAddrs[jetty]) +
            static_cast<uint64_t>(jetty) * regionBytes;
        GM_ADDR local = localBase + static_cast<uint64_t>(jetty) * regionBytes;
        const int32_t status = SubmitBatches(
            hcomm, batches[jetty], api, remote, local, payloadBytes, slotBytes, operationBase, count, batchSize,
            jetty);
        if (status != AscendC::HCOMM_SUCCESS) {
            return status;
        }
    }
    return AscendC::HCOMM_SUCCESS;
}

__aicore__ inline int32_t DrainAllJettys(
    AscendC::Hcomm<>& hcomm, AscendC::UbcCtpBatchHandle* batches, uint32_t jettyCount)
{
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        const int32_t status = hcomm.Drain(batches[jetty]);
        if (status != AscendC::HCOMM_SUCCESS) {
            return status;
        }
    }
    return AscendC::HCOMM_SUCCESS;
}

template <uint32_t jettyCount>
__aicore__ inline void RunMultiJettys(
    AscendC::Hcomm<>& hcomm, AscendC::LocalTensor<uint8_t> batchTensor, __gm__ uint64_t* channels,
    __gm__ uint64_t* remoteAddrs, GM_ADDR local, __gm__ uint64_t* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize)
{
    AscendC::UbcCtpBatchHandle batches[jettyCount];
    for (uint32_t jetty = 0U; jetty < jettyCount; ++jetty) {
        batches[jetty] = hcomm.MakeBatchHandle(
            static_cast<AscendC::ChannelHandle>(channels[jetty]), batchTensor, kSimdBatchWorkspaceBytes,
            reinterpret_cast<GM_ADDR>(remoteAddrs[jetty]) + static_cast<uint64_t>(jetty) * regionBytes);
    }

    int32_t status = SubmitAllJettys(
        hcomm, batches, remoteAddrs, local, api, payloadBytes, slotBytes, regionBytes, 0U, warmup, batchSize,
        jettyCount);
    if (status == AscendC::HCOMM_SUCCESS && warmup != 0U) {
        status = DrainAllJettys(hcomm, batches, jettyCount);
    }
    if (status != AscendC::HCOMM_SUCCESS) {
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
        return;
    }

    const uint64_t begin = static_cast<uint64_t>(AscendC::GetSystemCycle());
    status = SubmitAllJettys(
        hcomm, batches, remoteAddrs, local, api, payloadBytes, slotBytes, regionBytes, warmup, iterations,
        batchSize, jettyCount);
    const uint64_t issueEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
    if (status == AscendC::HCOMM_SUCCESS) {
        status = DrainAllJettys(hcomm, batches, jettyCount);
    }
    const uint64_t completionEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
    timing[kCompletedIndex] = status == AscendC::HCOMM_SUCCESS ? iterations * jettyCount : 0U;
    timing[kDoorbellCountIndex] = iterations / batchSize * jettyCount;
}

} // namespace

extern "C" __vector__ __global__ __aicore__ void SimdMultiJettyBatchPerfKernel(
    __gm__ void* channelsAddr, __gm__ void* remoteAddrsAddr, __gm__ void* localAddr, __gm__ void* timingAddr,
    uint32_t api, uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations,
    uint32_t warmup, uint32_t batchSize, uint32_t jettyCount)
{
    using namespace multi_jetty_batch_perf;
    auto* channels = reinterpret_cast<__gm__ uint64_t*>(channelsAddr);
    auto* remoteAddrs = reinterpret_cast<__gm__ uint64_t*>(remoteAddrsAddr);
    auto local = reinterpret_cast<GM_ADDR>(localAddr);
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);

    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer;
    // Jettys are submitted serially, so their handles can reuse the same workspace after every BatchCommit.
    // Keeping eight 2-BB x 256-item workspaces resident would exceed the hybrid Kernel runtime UB limit.
    pipe.InitBuffer(batchBuffer, kSimdBatchWorkspaceBytes);
    AscendC::LocalTensor<uint8_t> batchTensor = batchBuffer.Get<uint8_t>();
    AscendC::Hcomm<> hcomm;
    if (jettyCount == 1U) {
        auto batch = hcomm.MakeBatchHandle(
            static_cast<AscendC::ChannelHandle>(channels[0]), batchTensor, kSimdBatchWorkspaceBytes,
            reinterpret_cast<GM_ADDR>(remoteAddrs[0]));
        int32_t status = SubmitBatches(
            hcomm, batch, api, reinterpret_cast<GM_ADDR>(remoteAddrs[0]), local, payloadBytes, slotBytes,
            0U, warmup, batchSize, 0U);
        if (status == AscendC::HCOMM_SUCCESS && warmup != 0U) {
            status = hcomm.Drain(batch);
        }
        if (status != AscendC::HCOMM_SUCCESS) {
            timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
            return;
        }

        const uint64_t begin = static_cast<uint64_t>(AscendC::GetSystemCycle());
        status = SubmitBatches(
            hcomm, batch, api, reinterpret_cast<GM_ADDR>(remoteAddrs[0]), local, payloadBytes, slotBytes,
            warmup, iterations, batchSize, 0U);
        const uint64_t issueEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
        if (status == AscendC::HCOMM_SUCCESS) {
            status = hcomm.Drain(batch);
        }
        const uint64_t completionEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
        timing[kIssueCyclesIndex] = issueEnd - begin;
        timing[kCompletionCyclesIndex] = completionEnd - begin;
        timing[kStatusIndex] = static_cast<uint64_t>(static_cast<int64_t>(status));
        timing[kCompletedIndex] = status == AscendC::HCOMM_SUCCESS ? iterations : 0U;
        timing[kDoorbellCountIndex] = iterations / batchSize;
        return;
    }

    if (jettyCount == 2U) {
        RunMultiJettys<2U>(hcomm, batchTensor, channels, remoteAddrs, local, timing, api, payloadBytes,
            slotBytes, regionBytes, iterations, warmup, batchSize);
    } else if (jettyCount == 4U) {
        RunMultiJettys<4U>(hcomm, batchTensor, channels, remoteAddrs, local, timing, api, payloadBytes,
            slotBytes, regionBytes, iterations, warmup, batchSize);
    } else {
        RunMultiJettys<8U>(hcomm, batchTensor, channels, remoteAddrs, local, timing, api, payloadBytes,
            slotBytes, regionBytes, iterations, warmup, batchSize);
    }
}

void LaunchSimdMultiJettyBatchPerf(
    void* stream, void* channels, void* remoteAddrs, void* local, void* timing, uint32_t api,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t regionBytes, uint32_t iterations, uint32_t warmup,
    uint32_t batchSize, uint32_t jettyCount)
{
    SimdMultiJettyBatchPerfKernel<<<1U, nullptr, stream>>>(
        channels, remoteAddrs, local, timing, api, payloadBytes, slotBytes, regionBytes, iterations, warmup,
        batchSize, jettyCount);
}
