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
#include "simt_batch_common.h"

namespace {
using namespace simt_batch;

constexpr uint32_t kBasicBatchBufferBytes =
    AscendC::simt::HcommSimtBatchBufferBytes(kBasicOperationCount);

__simt_callee__ inline void StoreBasicDiagU64(__gm__ int32_t* status, uint32_t lowIndex, uint64_t value)
{
    status[lowIndex] = static_cast<int32_t>(value & 0xFFFFFFFFULL);
    status[lowIndex + 1U] = static_cast<int32_t>(value >> 32U);
}

#if defined(HCOMM_SIMT_BATCH_DEBUG)
__simt_callee__ inline void RecordBasicCommitDebug(
    const AscendC::simt::UbcCtpBatchHandle& batch, __gm__ int32_t* status)
{
    const uint32_t base = kBasicCommitDiagBase;
    StoreBasicDiagU64(status, base + kCommitDiagHeadSnapshot, batch.debug.commitHeadSnapshot);
    StoreBasicDiagU64(status, base + kCommitDiagHeadBefore, batch.debug.commitHeadBefore);
    StoreBasicDiagU64(status, base + kCommitDiagHeadVal, batch.debug.commitHeadVal);
    StoreBasicDiagU64(status, base + kCommitDiagHeadAfter, batch.debug.commitHeadAfter);
    status[base + kCommitDiagPrepared] = static_cast<int32_t>(batch.debug.commitPrepared);
    status[base + kCommitDiagPreparedCqe] = static_cast<int32_t>(batch.debug.commitPreparedCqe);
    status[base + kCommitDiagSqTailBefore] = static_cast<int32_t>(batch.debug.commitSqTailBefore);
    status[base + kCommitDiagChunkItems] = static_cast<int32_t>(batch.debug.commitChunkItems);
    status[base + kCommitDiagTailItemHead] = static_cast<int32_t>(batch.debug.tailItemHead);
    StoreBasicDiagU64(status, base + kCommitDiagTailUbWord0, batch.debug.tailUbWord0);
    StoreBasicDiagU64(status, base + kCommitDiagTailSqWord0, batch.debug.tailSqWord0);
    status[base + kCommitDiagRingReached] = static_cast<int32_t>(batch.debug.ringReached);
    status[base + kCommitDiagPublishCount] = static_cast<int32_t>(batch.debug.commitPublishCount);
    status[base + kCommitDiagTailSqMismatchMask] = static_cast<int32_t>(batch.debug.tailSqMismatchMask);
}

__simt_callee__ inline void RecordBasicDrainDebug(
    const AscendC::simt::UbcCtpBatchHandle& batch, __gm__ int32_t* status)
{
    const uint32_t base = kBasicDrainDiagBase;
    status[base] = static_cast<int32_t>(batch.debug.pollExpectedCqTail);
    status[base + 1U] = static_cast<int32_t>(batch.debug.pollInitialCqTail);
    status[base + 2U] = static_cast<int32_t>(batch.debug.pollFinalCqTail);
    status[base + 3U] = static_cast<int32_t>(batch.debug.pollInitialSqTail);
    status[base + 4U] = static_cast<int32_t>(batch.debug.pollFinalSqTail);
    status[base + 5U] = static_cast<int32_t>(batch.debug.pollRet);
    status[base + 6U] = static_cast<int32_t>(batch.debug.pollFailedCqIndex);
    status[base + 7U] = static_cast<int32_t>(batch.debug.pollRetryCount);
    status[base + 8U] = static_cast<int32_t>(batch.debug.pollCqeWord0);
    status[base + 9U] = static_cast<int32_t>(batch.debug.pollCqeWord1);
}
#endif

__simt_vf__ __launch_bounds__(32) inline void SimtBatchBasicVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* batchBuffer,
    __gm__ int32_t* status)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    for (uint32_t index = 0U; index < kBasicStatusCount; ++index) {
        status[index] = kBasicStatusNotRun;
    }

    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::Hcomm<> hcomm;
    status[kBasicStatusInit] = hcomm.Init(nullptr, 0);
    if (status[kBasicStatusInit] != 0) {
        return;
    }

    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBasicBatchBufferBytes, remote);
    status[kBasicStatusHandle] = batch.valid ? 0 : -1;
    status[kBasicStatusCapacity] = static_cast<int32_t>(batch.capacity);
    if (status[kBasicStatusHandle] != 0) {
        return;
    }

    status[kBasicStatusAppendFirst] = hcomm.WriteNbi(
        batch, remote + kWriteOffset, local + kWriteOffset, kPayloadBytes);
    if (status[kBasicStatusAppendFirst] != 0) {
        return;
    }
    status[kBasicStatusAppendSecond] = hcomm.WriteNbi(
        batch, remote + kWriteOffset + kPayloadBytes, local + kWriteOffset + kPayloadBytes, kPayloadBytes);
    if (status[kBasicStatusAppendSecond] != 0) {
        return;
    }

    status[kBasicStatusCommit] = hcomm.BatchCommit(batch);
#if defined(HCOMM_SIMT_BATCH_DEBUG)
    RecordBasicCommitDebug(batch, status);
#endif
    if (status[kBasicStatusCommit] != 0) {
        return;
    }
    status[kBasicStatusDrain] = hcomm.Drain(batch);
#if defined(HCOMM_SIMT_BATCH_DEBUG)
    RecordBasicDrainDebug(batch, status);
#endif
}

} // namespace

extern "C" __global__ __vector__ void SimtBatchBasic(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    alignas(128) __ubuf__ uint64_t batchBuffer[kBasicBatchBufferBytes / sizeof(uint64_t)];
    asc_vf_call<SimtBatchBasicVf>(
        dim3(1), channel, sendBufIdx, recvBufIdx, reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), status);
}

void LaunchSimtBatchBasic(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status)
{
    SimtBatchBasic<<<1, nullptr, stream>>>(
        channel, sendBufIdx, recvBufIdx, reinterpret_cast<int32_t*>(status));
}

namespace {

__simt_vf__ __launch_bounds__(32) inline void SimtSingleLastVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    for (uint32_t index = 0U; index < kSingleLastStatusCount; ++index) {
        status[index] = kBasicStatusNotRun;
    }

    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    AscendC::simt::Hcomm<> hcomm;
    status[kSingleLastStatusInit] = hcomm.Init(nullptr, 0);
    if (status[kSingleLastStatusInit] != 0) {
        return;
    }
    status[kSingleLastStatusDeferred] = hcomm.WriteNbi<false>(
        channel, remote + kWriteOffset, local + kWriteOffset, kPayloadBytes);
    if (status[kSingleLastStatusDeferred] != 0) {
        return;
    }
    status[kSingleLastStatusImmediate] = hcomm.WriteNbi<true>(
        channel, remote + kWriteOffset + kPayloadBytes, local + kWriteOffset + kPayloadBytes, kPayloadBytes);
    if (status[kSingleLastStatusImmediate] != 0) {
        return;
    }
    status[kSingleLastStatusDrain] = hcomm.Drain(channel);
}

} // namespace

extern "C" __global__ __vector__ void SimtSingleLast(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status)
{
    asc_vf_call<SimtSingleLastVf>(dim3(1), channel, sendBufIdx, recvBufIdx, status);
}

void LaunchSimtSingleLast(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status)
{
    SimtSingleLast<<<1, nullptr, stream>>>(
        channel, sendBufIdx, recvBufIdx, reinterpret_cast<int32_t*>(status));
}
