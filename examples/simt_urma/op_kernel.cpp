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

// 最后一次立即提交的门铃发布覆盖整批的 PI，把前面的延迟 WQE 一并带出。单 lane 不会观察到自己
// 写了一半的 WQE，因此不需要 barrier。
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

// 值内联在 WQE 中，因此不传本地地址、完全不读发送缓冲区。写入值仍取 SlotValue(i)，
// 使接收侧对两个 write 系列使用同一套校验。
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

// 每个 2BB Notify WQE 各敲一次 DWQE 门铃，整批发完才 Drain。
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

// 逐个操作等 CQE 返回后再复用 DWQE 窗口。Drain 返回值写进发送侧空闲 slot，便于定位失败环节。
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
        default:
            std::printf("[simt_urma] unhandled mode %u, no kernel launched\n", mode);
            break;
    }
}
