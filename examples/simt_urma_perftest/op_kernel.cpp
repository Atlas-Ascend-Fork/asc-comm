/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file op_kernel.cpp
 * \brief Device side of the SIMT URMA performance example.
 *
 * Five interfaces are measured: WriteNbi, WriteValueNbi, WriteWithNotifyNbi, AtomicFAA and
 * AtomicCAS. Two publishing strategies apply to each:
 *   Immediate  every WQE rings its own doorbell (commit=true).
 *   Last       every WQE but the last is deferred (commit=false); the final committed WQE
 *              publishes the whole batch with one head update.
 *
 * The two differ in SQ footprint as well as in doorbell count: a deferred WriteNbi fits in one
 * 64-byte basic block, while a committed one splits its payload across two SGEs to fill the
 * 128-byte DWQE window. So Immediate consumes twice the SQ space per WriteNbi. Notify and the
 * atomics are fixed 2-BB WQEs either way.
 *
 * Every kernel posts from a single lane: one channel is driven by one lane.
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "hcomm/hcomm_simt.h"

#include "simt_urma_perftest_common.h"

namespace {
using namespace simt_urma_perftest;

enum class PerfApi : uint32_t { WRITE, WRITE_VALUE, NOTIFY, FAA, CAS };

// One operation. The address advances per iteration so consecutive WQEs do not all target the
// same cache line, which would measure the remote memory system rather than the issue path.
// The atomics are the exception: they must hit one word for the accumulation to mean anything.
template <PerfApi api, bool commit>
__simt_callee__ inline int32_t SubmitOne(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t slot)
{
    uint64_t offset = static_cast<uint64_t>(slot) * slotBytes;
    if constexpr (api == PerfApi::WRITE) {
        return hcomm.WriteNbi<commit>(channel, remoteBase + offset, localBase + offset, payloadBytes);
    } else if constexpr (api == PerfApi::WRITE_VALUE) {
        return hcomm.WriteValueNbi<uint64_t, commit>(channel, remoteBase + offset, kInlineValue);
    } else if constexpr (api == PerfApi::NOTIFY) {
        // The signal word sits immediately after the payload, rounded up to 8 bytes so the
        // notify address stays aligned.
        uint32_t notifyOffset = (payloadBytes + sizeof(uint64_t) - 1U) & ~(sizeof(uint64_t) - 1U);
        __gm__ uint8_t* remote = remoteBase + offset;
        return hcomm.WriteWithNotifyNbi<commit>(
            channel, remote, localBase + offset, payloadBytes, remote + notifyOffset, kNotifySignal);
    } else {
        __gm__ uint64_t* remote = reinterpret_cast<__gm__ uint64_t*>(remoteBase);
        __gm__ uint64_t* local = reinterpret_cast<__gm__ uint64_t*>(localBase);
        if constexpr (api == PerfApi::FAA) {
            return hcomm.AtomicFAA<uint64_t, commit>(channel, &remote[0], &local[0], kFaaAdd);
        } else {
            return hcomm.AtomicCAS<uint64_t, commit>(channel, &remote[0], &local[0], kCasInitial, kCasSwap);
        }
    }
}

// The atomics reuse one remote word, so rotating slots would change what they measure.
template <PerfApi api>
__simt_callee__ inline constexpr bool RotatesSlots()
{
    return api != PerfApi::FAA && api != PerfApi::CAS;
}

// Issues count operations. commitEach selects the publishing strategy: true rings a doorbell per
// WQE, false defers everything but the last. Returns on the first failure so a broken run reports
// a status instead of a meaningless cycle count.
template <PerfApi api, bool commitEach>
__simt_callee__ inline int32_t SubmitSeries(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __gm__ uint8_t* remoteBase, __gm__ uint8_t* localBase,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t slotCount, uint32_t count)
{
    if (count == 0U) {
        return 0;
    }
    if constexpr (commitEach) {
        for (uint32_t i = 0U; i < count; ++i) {
            uint32_t slot = RotatesSlots<api>() ? i % slotCount : 0U;
            int32_t status = SubmitOne<api, true>(hcomm, channel, remoteBase, localBase, payloadBytes, slotBytes, slot);
            if (status != 0) {
                return status;
            }
        }
        return 0;
    } else {
        // Every WQE but the last is deferred, so each reserves its own SQ space; the final
        // committed WQE publishes the whole batch with one head update.
        for (uint32_t i = 1U; i < count; ++i) {
            uint32_t slot = RotatesSlots<api>() ? i % slotCount : 0U;
            int32_t status =
                SubmitOne<api, false>(hcomm, channel, remoteBase, localBase, payloadBytes, slotBytes, slot);
            if (status != 0) {
                return status;
            }
        }
        uint32_t lastSlot = RotatesSlots<api>() ? (count - 1U) % slotCount : 0U;
        return SubmitOne<api, true>(hcomm, channel, remoteBase, localBase, payloadBytes, slotBytes, lastSlot);
    }
}

__simt_callee__ inline void ReportFailure(__gm__ uint64_t* timing, int32_t status)
{
    timing[kIssueCyclesIndex] = 0U;
    timing[kCompletionCyclesIndex] = 0U;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(status);
    timing[kCompletedIndex] = 0U;
}

// Warmup, then a timed run. IssueCycles covers only the posting loop; CompletionCycles also
// covers the Drain that waits for every CQE, so the two separate issue cost from end-to-end
// latency. A failure in either phase reports through DrainStatus with zeroed cycle counts.
template <PerfApi api, bool commitEach>
__simt_vf__ __launch_bounds__(32) inline void SimtUrmaPerfVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ uint64_t* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        ReportFailure(timing, -1);
        return;
    }

    int32_t status =
        SubmitSeries<api, commitEach>(hcomm, channel, remote, local, payloadBytes, slotBytes, slotCount, warmup);
    int32_t drainStatus = status != 0 ? status : (warmup == 0U ? 0 : hcomm.Drain(channel));
    if (drainStatus != 0) {
        ReportFailure(timing, drainStatus);
        return;
    }

    uint64_t begin = clock();
    status =
        SubmitSeries<api, commitEach>(hcomm, channel, remote, local, payloadBytes, slotBytes, slotCount, iterations);
    uint64_t issueEnd = clock();
    drainStatus = status != 0 ? status : hcomm.Drain(channel);
    uint64_t completionEnd = clock();

    timing[kIssueCyclesIndex] = issueEnd - begin;
    timing[kCompletionCyclesIndex] = completionEnd - begin;
    timing[kDrainStatusIndex] = static_cast<uint64_t>(drainStatus);
    timing[kCompletedIndex] = drainStatus == 0 ? iterations : 0U;
}

} // namespace

#define DEFINE_URMA_PERF_KERNEL(kernelName, api, commitEach)                                                    \
    extern "C" __global__ __vector__ void kernelName(                                                           \
        uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ void* timing, uint32_t payloadBytes, \
        uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup)                           \
    {                                                                                                           \
        asc_vf_call<SimtUrmaPerfVf<api, commitEach>>(                                                           \
            dim3(1), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__gm__ uint64_t*>(timing), payloadBytes, \
            slotBytes, slotCount, iterations, warmup);                                                          \
    }

DEFINE_URMA_PERF_KERNEL(SimtWritePerfImmediate, PerfApi::WRITE, true)
DEFINE_URMA_PERF_KERNEL(SimtWritePerfLast, PerfApi::WRITE, false)
DEFINE_URMA_PERF_KERNEL(SimtWriteValuePerfImmediate, PerfApi::WRITE_VALUE, true)
DEFINE_URMA_PERF_KERNEL(SimtWriteValuePerfLast, PerfApi::WRITE_VALUE, false)
DEFINE_URMA_PERF_KERNEL(SimtNotifyPerfImmediate, PerfApi::NOTIFY, true)
DEFINE_URMA_PERF_KERNEL(SimtNotifyPerfLast, PerfApi::NOTIFY, false)
DEFINE_URMA_PERF_KERNEL(SimtFaaPerfImmediate, PerfApi::FAA, true)
DEFINE_URMA_PERF_KERNEL(SimtFaaPerfLast, PerfApi::FAA, false)
DEFINE_URMA_PERF_KERNEL(SimtCasPerfImmediate, PerfApi::CAS, true)
DEFINE_URMA_PERF_KERNEL(SimtCasPerfLast, PerfApi::CAS, false)

#undef DEFINE_URMA_PERF_KERNEL

void LaunchSimtUrmaPerf(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* timing, uint32_t payloadBytes,
    uint32_t slotBytes, uint32_t slotCount, uint32_t iterations, uint32_t warmup, uint32_t api, bool commitEach)
{
    using namespace simt_urma_perftest;

#define LAUNCH(kernelName)        \
    kernelName<<<1, 0, stream>>>( \
        channel, sendBufIdx, recvBufIdx, timing, payloadBytes, slotBytes, slotCount, iterations, warmup)

    if (api == kApiWrite) {
        commitEach ? LAUNCH(SimtWritePerfImmediate) : LAUNCH(SimtWritePerfLast);
    } else if (api == kApiWriteValue) {
        commitEach ? LAUNCH(SimtWriteValuePerfImmediate) : LAUNCH(SimtWriteValuePerfLast);
    } else if (api == kApiNotify) {
        commitEach ? LAUNCH(SimtNotifyPerfImmediate) : LAUNCH(SimtNotifyPerfLast);
    } else if (api == kApiFaa) {
        commitEach ? LAUNCH(SimtFaaPerfImmediate) : LAUNCH(SimtFaaPerfLast);
    } else {
        commitEach ? LAUNCH(SimtCasPerfImmediate) : LAUNCH(SimtCasPerfLast);
    }

#undef LAUNCH
}
