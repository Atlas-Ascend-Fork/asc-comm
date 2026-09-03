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

#include "batch_perf_compare_common.h"

namespace {
using namespace batch_perf_compare;

__aicore__ inline int32_t AppendSimd(
    AscendC::Hcomm<>& hcomm, AscendC::UbcCtpBatchHandle& batch, uint32_t api, GM_ADDR remote, GM_ADDR local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation)
{
    const uint64_t offset = static_cast<uint64_t>(operation % kSlotCount) * slotBytes;
    if (api == kApiWrite) {
        return hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes);
    }
    if (api == kApiRead) {
        return hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes);
    }
    return hcomm.WriteWithNotifyNbi(
        batch, remote + offset, local + offset, payloadBytes,
        remote + offset + slotBytes - sizeof(uint64_t), kNotifyValue);
}

__aicore__ inline int32_t SubmitSimd(
    AscendC::Hcomm<>& hcomm, AscendC::UbcCtpBatchHandle& batch, uint32_t api, GM_ADDR remote, GM_ADDR local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t count, uint32_t batchSize)
{
    for (uint32_t begin = 0U; begin < count; begin += batchSize) {
        for (uint32_t item = 0U; item < batchSize; ++item) {
            const int32_t status = AppendSimd(
                hcomm, batch, api, remote, local, payloadBytes, slotBytes, begin + item);
            if (status != 0) {
                return status;
            }
        }
        const int32_t status = hcomm.BatchCommit(batch);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}
} // namespace

extern "C" __global__ __aicore__ void SimdBatchPerfKernel(
    uint64_t channel, GM_ADDR local, GM_ADDR remote, GM_ADDR timingAddr, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize)
{
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer;
    pipe.InitBuffer(batchBuffer, kSimdBatchBufferBytes);
    AscendC::LocalTensor<uint8_t> batchTensor = batchBuffer.Get<uint8_t>();
    auto* timing = reinterpret_cast<__gm__ uint64_t*>(timingAddr);

    AscendC::Hcomm<> hcomm;
    auto batch = hcomm.MakeBatchHandle(
        static_cast<AscendC::ChannelHandle>(channel), batchTensor, kSimdBatchBufferBytes, remote);
    int32_t status = SubmitSimd(
        hcomm, batch, api, remote, local, payloadBytes, slotBytes, warmup, batchSize);
    if (status == 0 && warmup != 0U) {
        status = hcomm.Drain(batch);
    }
    if (status != 0) {
        timing[kStatusIndex] = static_cast<uint64_t>(status);
        return;
    }

    const uint64_t begin = static_cast<uint64_t>(AscendC::GetSystemCycle());
    status = SubmitSimd(
        hcomm, batch, api, remote, local, payloadBytes, slotBytes, iterations, batchSize);
    const uint64_t issueEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
    if (status == 0) {
        status = hcomm.Drain(batch);
    }
    const uint64_t completionEnd = static_cast<uint64_t>(AscendC::GetSystemCycle());
    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kStatusIndex] = static_cast<uint64_t>(status);
    timing[kCompletedIndex] = status == 0 ? iterations : 0U;
    timing[kPublishCountIndex] = iterations / batchSize;
}

void LaunchSimdBatchPerf(
    void* stream, uint64_t channel, void* local, void* remote, void* timing, uint32_t api, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t iterations, uint32_t warmup, uint32_t batchSize)
{
    SimdBatchPerfKernel<<<1U, nullptr, stream>>>(
        channel, reinterpret_cast<GM_ADDR>(local), reinterpret_cast<GM_ADDR>(remote),
        reinterpret_cast<GM_ADDR>(timing), api, payloadBytes, slotBytes, iterations, warmup, batchSize);
}
