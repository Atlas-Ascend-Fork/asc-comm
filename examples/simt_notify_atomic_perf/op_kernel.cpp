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
constexpr uint32_t WQE_BB_COUNT = 2U;
constexpr uint32_t QUEUE_GUARD = 10U;

enum TimingItem : uint32_t {
    POST_CYCLES = 0U,
    DRAIN_STATUS = 1U,
    BURST_WQES = 2U,
    SQ_DEPTH = 3U,
    CQ_DEPTH = 4U,
};

__simt_callee__ inline uint32_t GetBurstWqes(
    uint64_t channel, uint32_t &sqDepth, uint32_t &cqDepth)
{
    __gm__ AscendC::simt::HcommSimtChannelEntity *entity =
        reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity *>(channel);
    __gm__ AscendC::SqContext *sqContexts =
        reinterpret_cast<__gm__ AscendC::SqContext *>(entity->sqContextAddr);
    __gm__ AscendC::CqContext *cqContexts =
        reinterpret_cast<__gm__ AscendC::CqContext *>(entity->cqContextAddr);
    sqDepth = sqContexts[0].contextInfo.ubJfs.sqDepth;
    cqDepth = cqContexts[0].contextInfo.ubJfc.cqDepth;
    if (sqDepth <= QUEUE_GUARD || cqDepth <= QUEUE_GUARD) {
        return 0U;
    }
    uint32_t sqCapacity = (sqDepth - QUEUE_GUARD) / WQE_BB_COUNT;
    uint32_t cqCapacity = cqDepth - QUEUE_GUARD;
    return sqCapacity < cqCapacity ? sqCapacity : cqCapacity;
}

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
    uint32_t sqDepth = 0U;
    uint32_t cqDepth = 0U;
    uint32_t burstWqes = GetBurstWqes(channel, sqDepth, cqDepth);
    timing[SQ_DEPTH] = sqDepth;
    timing[CQ_DEPTH] = cqDepth;
    timing[BURST_WQES] = burstWqes;
    if (burstWqes == 0U) {
        timing[POST_CYCLES] = 0U;
        timing[DRAIN_STATUS] = static_cast<uint64_t>(-1);
        return;
    }
    for (uint32_t completed = 0U; completed < warmup;) {
        uint32_t remaining = warmup - completed;
        uint32_t batchSize = remaining < burstWqes ? remaining : burstWqes;
        for (uint32_t i = 0U; i < batchSize; ++i) {
            SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
        }
        status = hcomm.Drain(channel);
        if (status != 0) {
            timing[POST_CYCLES] = 0U;
            timing[DRAIN_STATUS] = static_cast<uint64_t>(status);
            return;
        }
        completed += batchSize;
    }

    uint64_t postCycles = 0U;
    for (uint32_t completed = 0U; completed < iterations;) {
        uint32_t remaining = iterations - completed;
        uint32_t batchSize = remaining < burstWqes ? remaining : burstWqes;
        uint64_t begin = clock();
        for (uint32_t i = 0U; i < batchSize; ++i) {
            SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
        }
        uint64_t end = clock();
        postCycles += end - begin;
        // Retire each SQ-capacity-bounded burst outside the timed interval.
        status = hcomm.Drain(channel);
        if (status != 0) {
            break;
        }
        completed += batchSize;
    }
    timing[POST_CYCLES] = postCycles;
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
