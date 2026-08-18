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
 * \brief Hcomm SIMT WriteNbi样例Kernel侧实现
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
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __gm__ int32_t* status)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    status[kStatusInit] = hcomm.Init(workspace, workspaceBytes);
    if (status[kStatusInit] != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount; ++i) {
        status[kStatusPost] =
            hcomm.WriteNbi<true>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
        if (status[kStatusPost] != 0) {
            return;
        }
    }
    status[kStatusDrain] = hcomm.Drain(channel);
}

// Scenario 2: one lane issues kSlotCount - 1 deferred posts serially, then a
// WriteNbi<commit=true>. The last post rings the doorbell with a PI that covers the whole
// batch, so the deferred WQEs are swept up together with it. No barrier is needed because a
// single lane cannot observe its own WQEs half-written.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteBatchLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __gm__ int32_t* status)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    AscendC::simt::Hcomm<> hcomm;
    status[kStatusInit] = hcomm.Init(workspace, workspaceBytes);
    if (status[kStatusInit] != 0) {
        return;
    }
    for (uint32_t i = 0; i < kSlotCount - 1U; ++i) {
        status[kStatusPost] =
            hcomm.WriteNbi<false>(channel, remoteAddr + i * kSlotBytes, localAddr + i * kSlotBytes, kSlotBytes);
        if (status[kStatusPost] != 0) {
            return;
        }
    }
    status[kStatusPost] = hcomm.WriteNbi(
        channel, remoteAddr + (kSlotCount - 1U) * kSlotBytes, localAddr + (kSlotCount - 1U) * kSlotBytes, kSlotBytes);
    if (status[kStatusPost] != 0) {
        return;
    }
    status[kStatusDrain] = hcomm.Drain(channel);
}

// Lanes concurrently fill deferred SQ entries. The barrier ensures every WQE
// is complete before lane 0 performs the only commit=true publication.
__simt_vf__ __launch_bounds__(1024) inline void SimtWriteMultiLaneVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* workspace, uint32_t workspaceBytes,
    __ubuf__ uint32_t* postStatus, __gm__ int32_t* status)
{
    if (threadIdx.x >= kMultiLaneCount || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    __gm__ uint8_t* localAddr = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remoteAddr = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);

    uint32_t lane = threadIdx.x;
    AscendC::simt::Hcomm<> hcomm;
    if (lane == 0U) {
        postStatus[0] = 0U;
        status[kStatusInit] = 0;
        status[kStatusPost] = 0;
        status[kStatusDrain] = 0;
    }
    asc_syncthreads();
    int32_t initStatus = hcomm.Init(workspace, workspaceBytes);
    if (initStatus != 0) {
        postStatus[0] = 1U;
        status[kStatusInit] = initStatus;
    }
    asc_syncthreads();
    if (postStatus[0] != 0U) {
        return;
    }
    int32_t writeStatus =
        hcomm.WriteNbi<false>(channel, remoteAddr + lane * kSlotBytes, localAddr + lane * kSlotBytes, kSlotBytes);
    if (writeStatus != 0) {
        postStatus[0] = 1U;
        status[kStatusPost] = writeStatus;
    }

    asc_syncthreads();
    if (lane == 0U && postStatus[0] == 0U) {
        status[kStatusPost] = hcomm.WriteNbi<true>(
            channel, remoteAddr + kMultiLaneCount * kSlotBytes, localAddr + kMultiLaneCount * kSlotBytes, kSlotBytes);
        if (status[kStatusPost] != 0) {
            return;
        }
        status[kStatusDrain] = hcomm.Drain(channel);
    }
}

} // namespace

extern "C" __global__ __vector__ void SimtWriteSingle(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteSingleVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace), status);
}

extern "C" __global__ __vector__ void SimtWriteBatchLast(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kSingleLaneWorkspaceWords];
    asc_vf_call<SimtWriteBatchLastVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, (__ubuf__ uint8_t*)workspace, sizeof(workspace), status);
}

extern "C" __global__ __vector__ void SimtWriteMultiLane(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    alignas(kWorkspaceAlign) __ubuf__ uint64_t workspace[kMultiLaneWorkspaceWords];
    __ubuf__ uint32_t postStatus[1];
    asc_vf_call<SimtWriteMultiLaneVf>(
        dim3(kMultiLaneCount), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__ubuf__ uint8_t*>(workspace),
        sizeof(workspace), postStatus, status);
}

void LaunchSimtWrite(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, uint32_t mode, int32_t* status)
{
    if (mode == simt_write::kModeSingle) {
        SimtWriteSingle<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, status);
    } else if (mode == simt_write::kModeMultiLane) {
        SimtWriteMultiLane<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, status);
    } else {
        SimtWriteBatchLast<<<1, 0, stream>>>(channel, sendBufIdx, recvBufIdx, status);
    }
}
