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

constexpr uint32_t NOTIFY_ALIGNMENT = sizeof(uint64_t);
constexpr uint32_t PERF_MODE_AIV_WRITE = 7U;

enum TimingItem : uint32_t {
    POST_CYCLES = 0U,
    DRAIN_STATUS = 1U,
};

template <bool commit>
__aicore__ inline void SubmitOne(AscendC::Hcomm<> &hcomm, uint32_t mode,
    uint64_t channel, __gm__ uint8_t *local, __gm__ uint8_t *remote,
    uint32_t payloadBytes)
{
    if (mode == PERF_MODE_AIV_WRITE) {
        (void)hcomm.WriteNbi<commit>(channel, remote, local, payloadBytes);
    } else if (mode == kModeNotify) {
        uint32_t notifyOffset =
            (payloadBytes + NOTIFY_ALIGNMENT - 1U) & ~(NOTIFY_ALIGNMENT - 1U);
        (void)hcomm.WriteWithNotifyNbi<commit>(channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    } else if (mode == kModeFaa) {
        (void)hcomm.AtomicFAA<uint64_t, commit>(channel, remote, local, kFaaAdd);
    } else {
        (void)hcomm.AtomicCAS<uint64_t, commit>(
            channel, remote, local, kCasInitial, kCasSwap);
    }
}
} // namespace

extern "C" [[bisheng::core_ratio(0, 1)]] __global__ __aicore__ void AivSubmitPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    __gm__ AscendC::ChannelEntity *entity =
        reinterpret_cast<__gm__ AscendC::ChannelEntity *>(channel);
    __gm__ uint8_t *local = reinterpret_cast<__gm__ uint8_t *>(
        entity->localBufferAddr[sendBufIdx].bufferInfo.rma.addr);
    __gm__ uint8_t *remote = reinterpret_cast<__gm__ uint8_t *>(
        entity->remoteBufferAddr[recvBufIdx].bufferInfo.rma.addr);
    __gm__ uint64_t *timingData = reinterpret_cast<__gm__ uint64_t *>(timing);

    AscendC::LocalTensor<uint8_t> workspace = AscendC::LocalTensor<uint8_t>(
        AscendC::TPosition::VECOUT, 0, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::Hcomm<> hcomm;
    int32_t status = hcomm.Init(workspace, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    if (status != AscendC::HCOMM_SUCCESS) {
        timingData[POST_CYCLES] = 0U;
        timingData[DRAIN_STATUS] = static_cast<uint64_t>(status);
        return;
    }

    for (uint32_t i = 0U; i < warmup; ++i) {
        SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
    }
    if (warmup != 0U) {
        status = hcomm.Drain(channel);
        if (status != AscendC::HCOMM_SUCCESS) {
            timingData[POST_CYCLES] = 0U;
            timingData[DRAIN_STATUS] = static_cast<uint64_t>(status);
            return;
        }
    }

    AscendC::PipeBarrier<PIPE_ALL>();
    uint64_t begin = AscendC::GetSystemCycle();
    for (uint32_t i = 0U; i < iterations; ++i) {
        SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
    }
    uint64_t end = AscendC::GetSystemCycle();

    // Keep completion and CQ polling outside the submit-time measurement.
    status = hcomm.Drain(channel);
    timingData[POST_CYCLES] = end - begin;
    timingData[DRAIN_STATUS] = static_cast<uint64_t>(status);
}

void LaunchAivSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    AivSubmitPerf<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx,
        timing, mode, payloadBytes, iterations, warmup);
}
