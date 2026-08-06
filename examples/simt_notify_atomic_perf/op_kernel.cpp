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
#include "simt_api/asc_simt.h"
#include "utils/debug/asc_time.h"

#include "hcomm/hcomm_simt.h"

#include "simt_notify_atomic_common.h"

namespace {
using namespace simt_notify_atomic;

constexpr uint32_t NOTIFY_ALIGNMENT = sizeof(uint64_t);

enum TimingItem : uint32_t {
    POST_CYCLES = 0U,
    DRAIN_STATUS = 1U,
};

template <bool commit>
__simt_callee__ inline void SubmitOne(AscendC::simt::Hcomm<> &hcomm,
    uint32_t mode, uint64_t channel, __gm__ uint8_t *local,
    __gm__ uint8_t *remote, uint32_t payloadBytes)
{
    if (mode == kModeNotify) {
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

__simt_vf__ __launch_bounds__(1024) inline void SimtSubmitPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ uint64_t *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t *local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t *remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    int32_t status = 0;
    for (uint32_t i = 0U; i < warmup; ++i) {
        SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
    }
    if (warmup != 0U) {
        status = hcomm.Drain(channel);
        if (status != 0) {
            timing[POST_CYCLES] = 0U;
            timing[DRAIN_STATUS] = static_cast<uint64_t>(status);
            return;
        }
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
    }
    uint64_t end = clock();

    // Drain is required to retire the queue, but is deliberately outside the timed window.
    status = hcomm.Drain(channel);
    timing[POST_CYCLES] = end - begin;
    timing[DRAIN_STATUS] = static_cast<uint64_t>(status);
}
} // namespace

extern "C" __global__ __vector__ void SimtSubmitPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtSubmitPerfVf>(dim3(1), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), mode, payloadBytes,
        iterations, warmup);
}

void LaunchSimtSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    SimtSubmitPerf<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx,
        timing, mode, payloadBytes, iterations, warmup);
}
