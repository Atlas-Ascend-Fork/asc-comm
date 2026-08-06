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
    POST_STATUS = 1U,
    DRAIN_STATUS = 2U,
    COMPLETED_WQES = 3U,
    SQ_DEPTH = 4U,
    CQ_DEPTH = 5U,
};

template <bool commit>
__aicore__ inline int32_t SubmitOne(AscendC::Hcomm<> &hcomm, uint32_t mode,
    uint64_t channel, __gm__ uint8_t *local, __gm__ uint8_t *remote,
    uint32_t payloadBytes)
{
    if (mode == PERF_MODE_AIV_WRITE) {
        return hcomm.WriteNbi<commit>(channel, remote, local, payloadBytes);
    } else if (mode == kModeNotify) {
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
} // namespace

extern "C" [[bisheng::core_ratio(0, 1)]] __global__ __aicore__ void AivSubmitPerf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __gm__ void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t batch)
{
    __gm__ AscendC::ChannelEntity *entity =
        reinterpret_cast<__gm__ AscendC::ChannelEntity *>(channel);
    __gm__ uint8_t *local = reinterpret_cast<__gm__ uint8_t *>(
        entity->localBufferAddr[sendBufIdx].bufferInfo.rma.addr);
    __gm__ uint8_t *remote = reinterpret_cast<__gm__ uint8_t *>(
        entity->remoteBufferAddr[recvBufIdx].bufferInfo.rma.addr);
    __gm__ uint64_t *timingData = reinterpret_cast<__gm__ uint64_t *>(timing);
    timingData[SQ_DEPTH] = entity->sqContextAddr[0].contextInfo.ubJfs.sqDepth;
    timingData[CQ_DEPTH] = entity->cqContextAddr[0].contextInfo.ubJfc.cqDepth;

    AscendC::LocalTensor<uint8_t> workspace = AscendC::LocalTensor<uint8_t>(
        AscendC::TPosition::VECOUT, 0, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::Hcomm<> hcomm;
    int32_t status = hcomm.Init(workspace, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    if (status != AscendC::HCOMM_SUCCESS) {
        timingData[POST_CYCLES] = 0U;
        timingData[POST_STATUS] = static_cast<uint64_t>(status);
        timingData[DRAIN_STATUS] = 0U;
        timingData[COMPLETED_WQES] = 0U;
        return;
    }

    for (uint32_t completed = 0U; completed < warmup;) {
        uint32_t remaining = warmup - completed;
        uint32_t batchSize = remaining < batch ? remaining : batch;
        for (uint32_t i = 0U; i < batchSize; ++i) {
            status = SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
            if (status != AscendC::HCOMM_SUCCESS) {
                break;
            }
        }
        if (status != AscendC::HCOMM_SUCCESS) {
            timingData[POST_CYCLES] = 0U;
            timingData[POST_STATUS] = static_cast<uint64_t>(status);
            timingData[DRAIN_STATUS] = 0U;
            timingData[COMPLETED_WQES] = 0U;
            return;
        }
        status = hcomm.Drain(channel);
        if (status != AscendC::HCOMM_SUCCESS) {
            timingData[POST_CYCLES] = 0U;
            timingData[POST_STATUS] = 0U;
            timingData[DRAIN_STATUS] = static_cast<uint64_t>(status);
            timingData[COMPLETED_WQES] = 0U;
            return;
        }
        completed += batchSize;
    }

    uint64_t postCycles = 0U;
    uint32_t completed = 0U;
    int32_t postStatus = AscendC::HCOMM_SUCCESS;
    int32_t drainStatus = AscendC::HCOMM_SUCCESS;
    while (completed < iterations) {
        uint32_t remaining = iterations - completed;
        uint32_t batchSize = remaining < batch ? remaining : batch;
        AscendC::PipeBarrier<PIPE_ALL>();
        uint64_t begin = AscendC::GetSystemCycle();
        for (uint32_t i = 0U; i < batchSize; ++i) {
            postStatus = SubmitOne<true>(hcomm, mode, channel, local, remote, payloadBytes);
            if (postStatus != AscendC::HCOMM_SUCCESS) {
                break;
            }
        }
        uint64_t end = AscendC::GetSystemCycle();
        postCycles += end - begin;
        if (postStatus != AscendC::HCOMM_SUCCESS) {
            break;
        }
        // Drain/PollCq is deliberately outside the timed WQE-fill + doorbell window.
        drainStatus = hcomm.Drain(channel);
        if (drainStatus != AscendC::HCOMM_SUCCESS) {
            break;
        }
        completed += batchSize;
    }
    timingData[POST_CYCLES] = postCycles;
    timingData[POST_STATUS] = static_cast<uint64_t>(postStatus);
    timingData[DRAIN_STATUS] = static_cast<uint64_t>(drainStatus);
    timingData[COMPLETED_WQES] = completed;
}

void LaunchAivSubmitPerf(void *stream, uint64_t channel, uint32_t sendBufIdx,
    uint32_t recvBufIdx, void *timing, uint32_t mode, uint32_t payloadBytes,
    uint32_t iterations, uint32_t warmup, uint32_t batch)
{
    AivSubmitPerf<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx,
        timing, mode, payloadBytes, iterations, warmup, batch);
}
