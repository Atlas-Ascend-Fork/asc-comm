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

constexpr uint32_t kCyclesIndex = 0U;
constexpr uint32_t kDrainStatusIndex = 1U;
constexpr uint32_t kCompletedIndex = 2U;
constexpr uint32_t kNotifyAlignment = sizeof(uint64_t);

// This is the validated notify_immediate_repeat functional flow with timing added
// around only the measured WQE-fill + doorbell loop.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ uint64_t *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t *local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t *remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    uint32_t notifyOffset =
        (payloadBytes + kNotifyAlignment - 1U) & ~(kNotifyAlignment - 1U);
    AscendC::simt::Hcomm<> hcomm;

    for (uint32_t i = 0U; i < warmup; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    int32_t status = warmup == 0U ? 0 : hcomm.Drain(channel);
    if (status != 0) {
        timing[kCyclesIndex] = 0U;
        timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
        timing[kCompletedIndex] = 0U;
        return;
    }

    uint64_t begin = clock();
    for (uint32_t i = 0U; i < iterations; ++i) {
        (void)hcomm.WriteWithNotifyNbi(
            channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    }
    uint64_t end = clock();

    status = hcomm.Drain(channel);
    timing[kCyclesIndex] = end - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
    timing[kCompletedIndex] = status == 0 ? iterations : 0U;
}
} // namespace

extern "C" __global__ __vector__ void SimtNotifyPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup)
{
    asc_vf_call<SimtNotifyPerfVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), payloadBytes,
        iterations, warmup);
}

void LaunchSimtNotifyPerf(
    void *stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    void *timing, uint32_t payloadBytes, uint32_t iterations, uint32_t warmup)
{
    SimtNotifyPerf<<<1, 0, stream>>>(
        channel, sendBufIdx, recvBufIdx, timing, payloadBytes, iterations, warmup);
}
