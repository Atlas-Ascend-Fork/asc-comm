/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "kernel_operator.h"
#include "utils/debug/asc_time.h"

#include "hcomm/hcomm.h"

#include "simt_notify_atomic_common.h"

namespace {
using namespace simt_notify_atomic;

constexpr uint32_t kCyclesIndex = 0U;
constexpr uint32_t kDrainStatusIndex = 1U;
constexpr uint32_t kCompletedIndex = 2U;
constexpr uint32_t kCompletionCyclesIndex = 3U;
constexpr uint32_t kNotifyAlignment = sizeof(uint64_t);
} // namespace

extern "C" [[bisheng::core_ratio(0, 1)]] __global__ __aicore__ void AivNotifyPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    __gm__ AscendC::ChannelEntity *entity =
        reinterpret_cast<__gm__ AscendC::ChannelEntity *>(channel);
    GM_ADDR local = reinterpret_cast<GM_ADDR>(
        entity->localBufferAddr[sendBufIdx].bufferInfo.rma.addr);
    GM_ADDR remote = reinterpret_cast<GM_ADDR>(
        entity->remoteBufferAddr[recvBufIdx].bufferInfo.rma.addr);
    __gm__ uint64_t *timingData = reinterpret_cast<__gm__ uint64_t *>(timing);
    uint32_t notifyOffset =
        (payloadBytes + kNotifyAlignment - 1U) & ~(kNotifyAlignment - 1U);

    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> workspaceBuffer;
    pipe.InitBuffer(workspaceBuffer, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> workspace = workspaceBuffer.Get<uint8_t>();
    AscendC::Hcomm<> hcomm;
    int32_t status = hcomm.Init(workspace, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    if (status != AscendC::HCOMM_SUCCESS) {
        timingData[kCyclesIndex] = 0U;
        timingData[kDrainStatusIndex] = static_cast<uint64_t>(status);
        timingData[kCompletedIndex] = 0U;
        timingData[kCompletionCyclesIndex] = 0U;
        return;
    }

    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    if (warmup != 0U) {
        status = hcomm.Drain(channel);
        if (status != AscendC::HCOMM_SUCCESS) {
            timingData[kCyclesIndex] = 0U;
            timingData[kDrainStatusIndex] = static_cast<uint64_t>(status);
            timingData[kCompletedIndex] = 0U;
            timingData[kCompletionCyclesIndex] = 0U;
            return;
        }
    }

    AscendC::PipeBarrier<PIPE_ALL>();
    uint64_t begin = AscendC::GetSystemCycle();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    uint64_t end = AscendC::GetSystemCycle();

    status = hcomm.Drain(channel);
    uint64_t completionEnd = AscendC::GetSystemCycle();
    timingData[kCyclesIndex] = end - begin;
    timingData[kDrainStatusIndex] = static_cast<uint64_t>(status);
    timingData[kCompletedIndex] = status == AscendC::HCOMM_SUCCESS ? iterations : 0U;
    timingData[kCompletionCyclesIndex] = completionEnd - begin;
}

void LaunchAivNotifyPerf(
    void *stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    void *timing, uint32_t payloadBytes, uint32_t iterations, uint32_t warmup)
{
    AivNotifyPerf<<<1, 0, stream>>>(
        channel, sendBufIdx, recvBufIdx, timing,
        payloadBytes, iterations, warmup);
}
