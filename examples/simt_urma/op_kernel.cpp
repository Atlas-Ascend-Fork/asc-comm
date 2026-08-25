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
 * \brief Hcomm SIMT URMA功能样例Kernel侧实现，覆盖Write/WriteValue/Notify/FAA/CAS
 *
 * 每个kernel都由单个lane发起：一个channel必须由一个lane驱动，延迟WQE要等后续
 * commit=true的提交发布覆盖整批的PI，而该PI无法区分是哪个lane写了哪个basic block。
 */

#include <cstdio>

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "hcomm/hcomm_simt.h"

#include "simt_urma_common.h"

namespace {
using namespace simt_urma;

// Every kernel below runs on lane 0 only. Returning from the other lanes here keeps the
// single-lane-per-channel contract in one place instead of repeating the guard per kernel.
__simt_callee__ inline bool IsDrivingLane() { return threadIdx.x == 0U && threadIdx.y == 0U && threadIdx.z == 0U; }

// ---------------------------------------------------------------------------------------------
// Write family

// kWriteSlotCount independent WriteNbi<commit=true> calls. Each post rings its own doorbell,
// so nothing else is needed before Drain.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteSingleVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kWriteSlotCount; ++i) {
        (void)hcomm.WriteNbi<true>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
    }
    (void)hcomm.Drain(channel);
}

// One lane issues kWriteSlotCount - 1 deferred posts serially, then a WriteNbi<commit=true>.
// The last post rings the doorbell with a PI that covers the whole batch, so the deferred WQEs
// are swept up together with it. No barrier is needed because a single lane cannot observe its
// own WQEs half-written.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteBatchLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kWriteSlotCount - 1U; ++i) {
        (void)hcomm.WriteNbi<false>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
    }
    (void)hcomm.WriteNbi(
        channel, remoteAddr + (kWriteSlotCount - 1U) * kSlotBytes, localAddr + (kWriteSlotCount - 1U) * kSlotBytes,
        kSlotBytes);
    (void)hcomm.Drain(channel);
}

// The WriteValueNbi counterpart of write_single. The value is carried inline in the WQE, so no
// local address is passed and the sender's send buffer is never read. The values written must
// still match SlotValue(i) so the receiver's check is identical for both write families.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteValueSingleVf(uint64_t channel, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
        return;
    }
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kWriteSlotCount; ++i) {
        (void)hcomm.WriteValueNbi<uint64_t, true>(channel, remoteAddr + i * kSlotBytes, SlotValue(i));
    }
    (void)hcomm.Drain(channel);
}

// The WriteValueNbi counterpart of write_batch_last. Deferred inline posts stay in the send
// queue until the final commit=true post publishes a PI covering the whole batch.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteValueBatchLastVf(uint64_t channel, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
        return;
    }
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(nullptr, 0) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kWriteSlotCount - 1U; ++i) {
        (void)hcomm.WriteValueNbi<uint64_t, false>(channel, remoteAddr + i * kSlotBytes, SlotValue(i));
    }
    (void)hcomm.WriteValueNbi<uint64_t, true>(
        channel, remoteAddr + (kWriteSlotCount - 1U) * kSlotBytes, SlotValue(kWriteSlotCount - 1U));
    (void)hcomm.Drain(channel);
}

// ---------------------------------------------------------------------------------------------
// Notify/atomic family

// Isolated Notify case: exactly one immediate DWQE, followed by Drain.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyVf(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
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
    if (!IsDrivingLane()) {
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
    if (!IsDrivingLane()) {
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
    if (!IsDrivingLane()) {
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
    if (!IsDrivingLane()) {
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

// One lane defers Notify and FAA, then CAS with commit=true publishes the batch.
__simt_vf__ __launch_bounds__(1024) inline void SimtNotifyAtomicBatchLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    if (!IsDrivingLane()) {
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

extern "C" __global__ __vector__ void SimtWriteSingle(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtWriteSingleVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtWriteBatchLast(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtWriteBatchLastVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtWriteValueSingle(uint64_t channel, uint32_t recvBufIdx)
{
    asc_vf_call<SimtWriteValueSingleVf>(dim3(1), channel, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtWriteValueBatchLast(uint64_t channel, uint32_t recvBufIdx)
{
    asc_vf_call<SimtWriteValueBatchLastVf>(dim3(1), channel, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtNotify(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtFaa(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtFaaVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtCas(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtCasVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
}

extern "C" __global__ __vector__ void SimtNotifyImmediateRepeat(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    asc_vf_call<SimtNotifyImmediateRepeatVf>(dim3(1), channel, sendBufIdx, recvBufIdx);
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

void LaunchSimtUrma(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode)
{
    using namespace simt_urma;
    switch (mode) {
        case kModeWriteSingle:
            SimtWriteSingle<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeWriteBatchLast:
            SimtWriteBatchLast<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeWriteValueSingle:
            SimtWriteValueSingle<<<1, 0, stream>>>(channel, recvBufIdx);
            break;
        case kModeWriteValueBatchLast:
            SimtWriteValueBatchLast<<<1, 0, stream>>>(channel, recvBufIdx);
            break;
        case kModeNotify:
            SimtNotify<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeFaa:
            SimtFaa<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeCas:
            SimtCas<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeNotifyImmediateRepeat:
            SimtNotifyImmediateRepeat<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeNotifyAtomicBatchLast:
            SimtNotifyAtomicBatchLast<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        case kModeNotifyAtomicSingle:
            SimtNotifyAtomicSingle<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
            break;
        // 每个 mode 都有显式 case，default 只用于兜住未知取值。这里不静默回落到某个模式：
        // main.cpp 的 ParseMode 已经拒绝了无法识别的命令行取值，所以走到这里说明新增了 mode
        // 却漏了分发，直接打印出来比按 single 跑掉更容易排查。
        default:
            std::printf("[simt_urma] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
