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
 * \brief Hcomm SIMT WriteNbi/WriteValueNbi样例Kernel侧实现，覆盖三种提交方式
 */

#include "kernel_operator.h"
#include "simt_api/asc_simt.h"

#include "hcomm/hcomm_simt.h"

#include "simt_write_common.h"

namespace {
using namespace simt_write;

constexpr uint32_t kSingleLaneWorkspaceWords = HcommWorkspaceBytes(1U) / sizeof(uint64_t);
constexpr uint32_t kMultiLaneWorkspaceWords = HcommWorkspaceBytes(kMultiLaneCount) / sizeof(uint64_t);

// Scenario 1: kSlotCount independent WriteNbi<commit=true> calls. Each post rings its own
// doorbell, so nothing else is needed before Drain.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteSingleVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        (void)hcomm.WriteNbi<true>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
    }
    (void)hcomm.Drain(channel);
}

// Scenario 2: one lane issues kSlotCount - 1 deferred posts serially, then a
// WriteNbi<commit=true>. The last post rings the doorbell with a PI that covers the whole
// batch, so the deferred WQEs are swept up together with it. No barrier is needed because a
// single lane cannot observe its own WQEs half-written.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteBatchLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount - 1U; ++i) {
        (void)hcomm.WriteNbi<false>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
    }
    (void)hcomm.WriteNbi(
        channel, remoteAddr + (kSlotCount - 1U) * kSlotBytes, localAddr + (kSlotCount - 1U) * kSlotBytes, kSlotBytes);
    (void)hcomm.Drain(channel);
}

// Scenario 3: kMultiLaneCount lanes each post one deferred write concurrently, then a barrier,
// then lane 0 issues the final WriteNbi<commit=true>. Same publishing mechanism as scenario 2,
// but the deferred WQEs come from different lanes racing for SQ space through the atomic
// reserve rather than from one lane looping.
//
// The barrier is a correctness requirement, not a tuning knob: the final post's doorbell
// publishes a PI covering the whole batch, so every lane must have finished writing its WQE
// first, or the hardware consumes SQ slots that are still empty. Every lane must reach the
// barrier, which is why the lane filter below returns only for lanes outside the batch.
//
// Every lane in the block must call Init as well: with the full cached workspace layout the
// block-shared post context is synchronized there.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteMultiLaneVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx,
    __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x >= kMultiLaneCount || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    uint32_t lane = threadIdx.x;
    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    (void)hcomm.WriteNbi<false>(channel, remoteAddr + lane * kSlotBytes, localAddr + lane * kSlotBytes, kSlotBytes);

    asc_syncthreads();

    if (lane == 0U) {
        (void)hcomm.WriteNbi(
            channel, remoteAddr + kMultiLaneCount * kSlotBytes, localAddr + kMultiLaneCount * kSlotBytes, kSlotBytes);
        (void)hcomm.Drain(channel);
    }
}

// Scenario 4: the WriteValueNbi counterpart of scenario 1. The value is carried inline in the
// WQE, so no local address is passed and the sender's send buffer is never read. The values
// written must still match SlotValue(i) so the receiver's check is identical for both families.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteValueSingleVf(
    uint64_t channel, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        (void)hcomm.WriteValueNbi<uint64_t, true>(channel, remoteAddr + i * kSlotBytes, SlotValue(i));
    }
    (void)hcomm.Drain(channel);
}

// Scenario 5: the WriteValueNbi counterpart of scenario 2. Deferred inline posts stay in the
// send queue until the final commit=true post publishes a PI covering the whole batch.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteValueBatchLastVf(
    uint64_t channel, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount - 1U; ++i) {
        (void)hcomm.WriteValueNbi<uint64_t, false>(channel, remoteAddr + i * kSlotBytes, SlotValue(i));
    }
    (void)hcomm.WriteValueNbi<uint64_t, true>(
        channel, remoteAddr + (kSlotCount - 1U) * kSlotBytes, SlotValue(kSlotCount - 1U));
    (void)hcomm.Drain(channel);
}

// Scenario 6: the WriteValueNbi counterpart of scenario 3. The barrier requirement is the same:
// lane 0's doorbell publishes the whole batch, so every lane must have written its WQE first.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteValueMultiLaneVf(
    uint64_t channel, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes)
{
    if (threadIdx.x >= kMultiLaneCount || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    uint32_t lane = threadIdx.x;
    AscendC::simt::Hcomm<> hcomm;
    if (hcomm.Init(workspace, workspaceBytes) != 0) {
        return;
    }
    (void)hcomm.WriteValueNbi<uint64_t, false>(channel, remoteAddr + lane * kSlotBytes, SlotValue(lane));

    asc_syncthreads();

    if (lane == 0U) {
        (void)hcomm.WriteValueNbi<uint64_t, true>(
            channel, remoteAddr + kMultiLaneCount * kSlotBytes, SlotValue(kMultiLaneCount));
        (void)hcomm.Drain(channel);
    }
}
} // namespace

extern "C" __global__ __vector__ void SimtWriteSingle(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteSingleVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

extern "C" __global__ __vector__ void SimtWriteBatchLast(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteBatchLastVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

extern "C" __global__ __vector__ void SimtWriteMultiLane(uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kMultiLaneWorkspaceWords];
    asc_vf_call<SimtWriteMultiLaneVf>(
        dim3(simt_write::kMultiLaneCount), channel, sendBufIdx, recvBufIdx,
        (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

extern "C" __global__ __vector__ void SimtWriteValueSingle(uint64_t channel, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteValueSingleVf>(
        dim3(1), channel, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

extern "C" __global__ __vector__ void SimtWriteValueBatchLast(uint64_t channel, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteValueBatchLastVf>(
        dim3(1), channel, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

extern "C" __global__ __vector__ void SimtWriteValueMultiLane(uint64_t channel, uint32_t recvBufIdx)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kMultiLaneWorkspaceWords];
    asc_vf_call<SimtWriteValueMultiLaneVf>(
        dim3(simt_write::kMultiLaneCount), channel, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace));
}

void LaunchSimtWrite(void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode)
{
    if (mode == simt_write::kModeSingle) {
        SimtWriteSingle<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_write::kModeBatchLast) {
        SimtWriteBatchLast<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_write::kModeMultiLane) {
        SimtWriteMultiLane<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx);
    } else if (mode == simt_write::kModeValueSingle) {
        SimtWriteValueSingle<<<1, 0, stream>>>(channel, recvBufIdx);
    } else if (mode == simt_write::kModeValueBatchLast) {
        SimtWriteValueBatchLast<<<1, 0, stream>>>(channel, recvBufIdx);
    } else {
        SimtWriteValueMultiLane<<<1, 0, stream>>>(channel, recvBufIdx);
    }
}
