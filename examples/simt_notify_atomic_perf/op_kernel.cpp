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
    POST_STATUS = 1U,
    DRAIN_STATUS = 2U,
    COMPLETED_WQES = 3U,
    SQ_DEPTH = 4U,
    CQ_DEPTH = 5U,
};

template <bool commit>
__simt_callee__ inline int32_t SubmitOne(AscendC::simt::Hcomm<> &hcomm,
    uint32_t mode, uint64_t channel, __gm__ uint8_t *local,
    __gm__ uint8_t *remote, uint32_t payloadBytes)
{
    if (mode == kModeNotify) {
        uint32_t notifyOffset =
            (payloadBytes + NOTIFY_ALIGNMENT - 1U) & ~(NOTIFY_ALIGNMENT - 1U);
        return hcomm.WriteWithNotifyNbi<commit>(channel, remote, local, payloadBytes,
            remote + notifyOffset, kNotifySignal);
    } else if (mode == kModeFaa) {
        return hcomm.AtomicFAA<uint64_t, commit>(channel, remote, local, kFaaAdd);
    } else {
        return hcomm.AtomicCAS<uint64_t, commit>(
            channel, remote, local, kCasInitial, kCasSwap);
    }
}

__simt_vf__ __launch_bounds__(1024) inline void SimtSubmitPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ uint64_t *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t batch)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }

    __gm__ uint8_t *local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t *remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    __gm__ AscendC::simt::HcommSimtChannelEntity *entity =
        reinterpret_cast<__gm__ AscendC::simt::HcommSimtChannelEntity *>(channel);
    __gm__ AscendC::SqContext *sqContexts =
        reinterpret_cast<__gm__ AscendC::SqContext *>(entity->sqContextAddr);
    __gm__ AscendC::CqContext *cqContexts =
        reinterpret_cast<__gm__ AscendC::CqContext *>(entity->cqContextAddr);
    timing[SQ_DEPTH] = sqContexts[0].contextInfo.ubJfs.sqDepth;
    timing[CQ_DEPTH] = cqContexts[0].contextInfo.ubJfc.cqDepth;
    int32_t postStatus = 0;
    int32_t drainStatus = 0;
    for (uint32_t completed = 0U; completed < warmup;) {
        uint32_t remaining = warmup - completed;
        uint32_t batchSize = remaining < batch ? remaining : batch;
        for (uint32_t i = 0U; i < batchSize; ++i) {
            postStatus = SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
            if (postStatus != 0) {
                break;
            }
        }
        if (postStatus != 0) {
            timing[POST_CYCLES] = 0U;
            timing[POST_STATUS] = static_cast<uint64_t>(postStatus);
            timing[DRAIN_STATUS] = 0U;
            timing[COMPLETED_WQES] = 0U;
            return;
        }
        drainStatus = hcomm.Drain(channel);
        if (drainStatus != 0) {
            timing[POST_CYCLES] = 0U;
            timing[POST_STATUS] = 0U;
            timing[DRAIN_STATUS] = static_cast<uint64_t>(drainStatus);
            timing[COMPLETED_WQES] = 0U;
            return;
        }
        completed += batchSize;
    }

    uint64_t postCycles = 0U;
    uint32_t completed = 0U;
    while (completed < iterations) {
        uint32_t remaining = iterations - completed;
        uint32_t batchSize = remaining < batch ? remaining : batch;
        uint64_t begin = clock();
        for (uint32_t i = 0U; i < batchSize; ++i) {
            postStatus = SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
            if (postStatus != 0) {
                break;
            }
        }
        uint64_t end = clock();
        postCycles += end - begin;
        if (postStatus != 0) {
            break;
        }
        // Drain/PollCq is deliberately outside the timed WQE-fill + doorbell window.
        drainStatus = hcomm.Drain(channel);
        if (drainStatus != 0) {
            break;
        }
        completed += batchSize;
    }
    timing[POST_CYCLES] = postCycles;
    timing[POST_STATUS] = static_cast<uint64_t>(postStatus);
    timing[DRAIN_STATUS] = static_cast<uint64_t>(drainStatus);
    timing[COMPLETED_WQES] = completed;
}
} // namespace

extern "C" __global__ __vector__ void SimtSubmitPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t batch)
{
    asc_vf_call<SimtSubmitPerfVf>(dim3(1), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__gm__ uint64_t *>(timing), mode, payloadBytes,
        iterations, warmup, batch);
}

void LaunchSimtSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t batch)
{
    SimtSubmitPerf<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx,
        timing, mode, payloadBytes, iterations, warmup, batch);
}
