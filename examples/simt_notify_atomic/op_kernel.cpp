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

#include "hcomm/hcomm_simt.h"

#include "simt_notify_atomic_common.h"

namespace {
using namespace simt_notify_atomic;

// Isolated Notify case: exactly one immediate DWQE, followed by Drain.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyVf(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    (void)hcomm.WriteWithNotifyNbi(
        channel, remote + kNotifyDataSlot * kSlotBytes, local, kSlotBytes, remote + kNotifySignalSlot * kSlotBytes,
        kNotifySignal);
    reinterpret_cast<__gm__ uint64_t*>(local)[kNotifyDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

// Reproduces the immediate-post burst used by the performance benchmark: one lane rings
// the DWQE doorbell for every 2-BB Notify WQE, then calls Drain only after the whole burst.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyImmediateRepeatVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    for (uint32_t i = 0; i < kNotifyImmediateRepeatCount; ++i) {
        (void)hcomm.WriteWithNotifyNbi<true>(
            channel, remote + kNotifyDataSlot * kSlotBytes, local, kSlotBytes, remote + kNotifySignalSlot * kSlotBytes,
            kNotifySignal);
    }
    reinterpret_cast<__gm__ uint64_t*>(local)[kNotifyDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

// Isolated FAA case: verifies both the remote sum and the fetched old value.
__simt_vf__ __launch_bounds__(1024) inline void SimtFaaVf(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    (void)hcomm.AtomicFAA<uint64_t>(
        channel, remote + kFaaTargetSlot * kSlotBytes, local + kFaaFetchSlot * kSlotBytes, kFaaAdd);
    reinterpret_cast<__gm__ uint64_t*>(local)[kFaaDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

// Isolated CAS case: verifies both the remote swap and the fetched old value.
__simt_vf__ __launch_bounds__(1024) inline void SimtCasVf(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    (void)hcomm.AtomicCAS<uint64_t>(
        channel, remote + kCasTargetSlot * kSlotBytes, local + kCasFetchSlot * kSlotBytes, kCasInitial, kCasSwap);
    reinterpret_cast<__gm__ uint64_t*>(local)[kCasDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

// Strictly serialized single-lane case: wait for each CQE before reusing the DWQE window.
// Drain return values are stored in otherwise-unused sender slots for diagnosis.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyAtomicSingleVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    (void)hcomm.WriteWithNotifyNbi(
        channel, remote + kNotifyDataSlot * kSlotBytes, local, kSlotBytes, remote + kNotifySignalSlot * kSlotBytes,
        kNotifySignal);
    reinterpret_cast<__gm__ uint64_t*>(local)[kNotifyDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
    (void)hcomm.AtomicFAA<uint64_t>(
        channel, remote + kFaaTargetSlot * kSlotBytes, local + kFaaFetchSlot * kSlotBytes, kFaaAdd);
    reinterpret_cast<__gm__ uint64_t*>(local)[kFaaDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
    (void)hcomm.AtomicCAS<uint64_t>(
        channel, remote + kCasTargetSlot * kSlotBytes, local + kCasFetchSlot * kSlotBytes, kCasInitial, kCasSwap);
    reinterpret_cast<__gm__ uint64_t*>(local)[kCasDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

// Scenario 2: one lane defers Notify and FAA, then CAS with commit=true publishes the batch.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyAtomicBatchLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    (void)hcomm.Init(nullptr, 0);
    (void)hcomm.WriteWithNotifyNbi<false>(
        channel, remote + kNotifyDataSlot * kSlotBytes, local, kSlotBytes, remote + kNotifySignalSlot * kSlotBytes,
        kNotifySignal);
    (void)hcomm.AtomicFAA<uint64_t, false>(
        channel, remote + kFaaTargetSlot * kSlotBytes, local + kFaaFetchSlot * kSlotBytes, kFaaAdd);
    (void)hcomm.AtomicCAS<uint64_t>(
        channel, remote + kCasTargetSlot * kSlotBytes, local + kCasFetchSlot * kSlotBytes, kCasInitial, kCasSwap);
    reinterpret_cast<__gm__ uint64_t*>(local)[kNotifyDrainSlot] = static_cast<uint64_t>(hcomm.Drain(channel));
}

} // namespace

extern "C" __global__ __vector__ void SimtNotify(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtFaa(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtFaaVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtNotifyImmediateRepeat(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyImmediateRepeatVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtCas(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtCasVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtNotifyAtomicSingle(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyAtomicSingleVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtNotifyAtomicBatchLast(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyAtomicBatchLastVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

void LaunchSimtNotifyAtomic(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode)
{
    if (mode == simt_notify_atomic::kModeNotify) {
        SimtNotify<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_notify_atomic::kModeNotifyImmediateRepeat) {
        SimtNotifyImmediateRepeat<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_notify_atomic::kModeFaa) {
        SimtFaa<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_notify_atomic::kModeCas) {
        SimtCas<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_notify_atomic::kModeBatchLast) {
        SimtNotifyAtomicBatchLast<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else {
        SimtNotifyAtomicSingle<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    }
}
