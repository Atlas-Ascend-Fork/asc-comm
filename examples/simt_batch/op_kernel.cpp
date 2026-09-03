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
#include "simt_api/cooperative_groups.h"

#include "hcomm/hcomm_simt.h"
#include "simt_batch_common.h"

namespace {
using namespace simt_batch;

// This is a Kernel-only sizing detail. Keep the shared example header independent of
// device-side Hcomm headers because main.cpp includes it as ordinary Host C++.
constexpr uint32_t kBatchBufferBytes = AscendC::simt::HcommSimtBatchBufferBytes(kOperationsPerBatch);
constexpr uint32_t kGroupStateWords = 3U;

template <uint32_t api, typename HcommT>
__simt_callee__ inline int32_t AppendOperation(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, __gm__ uint8_t* local,
    __gm__ uint8_t* remote, uint32_t operation)
{
    if constexpr (api == kApiWrite) {
        const size_t offset = kWriteOffset + static_cast<size_t>(operation) * kPayloadBytes;
        return hcomm.WriteNbi(batch, remote + offset, local + offset, kPayloadBytes);
    } else if constexpr (api == kApiRead) {
        const size_t offset = kReadOffset + static_cast<size_t>(operation) * kPayloadBytes;
        return hcomm.ReadNbi(batch, local + offset, remote + offset, kPayloadBytes);
    } else if constexpr (api == kApiNotify) {
        const size_t dataOffset = kNotifyDataOffset + static_cast<size_t>(operation) * kPayloadBytes;
        const size_t signalOffset = kNotifySignalOffset + static_cast<size_t>(operation) * sizeof(uint64_t);
        return hcomm.WriteWithNotifyNbi(
            batch, remote + dataOffset, local + dataOffset, kPayloadBytes, remote + signalOffset,
            kNotifyValueBase + operation);
    } else if constexpr (api == kApiWriteValue) {
        return hcomm.template WriteValueNbi<uint64_t>(
            batch, remote + kWriteValueOffset + static_cast<size_t>(operation) * sizeof(uint64_t),
            kWriteValueBase + operation);
    } else if constexpr (api == kApiFaa) {
        const uint32_t target = operation % kTargetCount;
        return hcomm.template AtomicFAA<uint64_t>(
            batch, remote + kFaaTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            local + kFaaFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t), kFaaAdd);
    } else {
        const uint32_t target = operation % kTargetCount;
        return hcomm.template AtomicCAS<uint64_t>(
            batch, remote + kCasTargetOffset + static_cast<size_t>(target) * sizeof(uint64_t),
            local + kCasFetchOffset + static_cast<size_t>(operation) * sizeof(uint64_t), kCasInitial, kCasSwap);
    }
}

template <uint32_t api, typename HcommT, typename Group>
__simt_callee__ inline int32_t RunGroupCase(
    HcommT& hcomm, const Group& group, uint64_t channel, __ubuf__ uint8_t* batchBuffer,
    __gm__ uint8_t* local, __gm__ uint8_t* remote, __gm__ int32_t* status,
    uint32_t operationsPerBatch, uint32_t batchCount)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t base = kStatusCaseBase + api * kStatusFieldsPerCase;
    constexpr uint32_t itemBb = api == kApiNotify || api == kApiFaa || api == kApiCas ? 2U : 1U;
    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, itemBb);
    if (rank == 0U) {
        status[base + kStatusAppend] = 0;
        status[base + kStatusCommitCount] = 0;
    }
    for (uint32_t batchIndex = 0U; batchIndex < batchCount; ++batchIndex) {
        const uint32_t operation = batchIndex * operationsPerBatch + rank;
        const int32_t appendRet = AppendOperation<api>(hcomm, batch, local, remote, operation);
        const int32_t commitRet = appendRet == 0 ? hcomm.BatchCommit(batch) : appendRet;
        if (rank == 0U) {
            status[base + kStatusAppend] = appendRet;
            status[base + kStatusCommit] = commitRet;
            status[base + kStatusCommitCount] += commitRet == 0 ? 1 : 0;
        }
        if (commitRet != 0) {
            return commitRet;
        }
    }
    if (rank == 0U) {
        status[base + kStatusDrain] = hcomm.Drain(batch);
    }
    group.sync();
    return status[base + kStatusDrain];
}

template <uint32_t api>
__simt_callee__ inline int32_t RunHomogeneousCase(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __ubuf__ uint8_t* batchBuffer,
    __gm__ uint8_t* local, __gm__ uint8_t* remote, __gm__ int32_t* status,
    uint32_t operationsPerBatch = kOperationsPerBatch, uint32_t batchCount = kBatchCount)
{
    const uint32_t base = kStatusCaseBase + api * kStatusFieldsPerCase;
    constexpr uint32_t itemBb = api == kApiNotify || api == kApiFaa || api == kApiCas ? 2U : 1U;
    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, itemBb);
    status[base + kStatusAppend] = 0;
    status[base + kStatusCommitCount] = 0;
    for (uint32_t batchIndex = 0U; batchIndex < batchCount; ++batchIndex) {
        const uint32_t begin = batchIndex * operationsPerBatch;
        for (uint32_t item = 0U; item < operationsPerBatch; ++item) {
            const int32_t ret = AppendOperation<api>(hcomm, batch, local, remote, begin + item);
            if (ret != 0) {
                status[base + kStatusAppend] = ret;
                return ret;
            }
            const int32_t commitRet = hcomm.BatchCommit(batch);
            status[base + kStatusCommit] = commitRet;
            if (commitRet != 0) {
                return commitRet;
            }
            ++status[base + kStatusCommitCount];
        }
    }
    const int32_t ret = hcomm.Drain(batch);
    status[base + kStatusDrain] = ret;
    return ret;
}

__simt_callee__ inline int32_t RunMixedCase(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __ubuf__ uint8_t* batchBuffer,
    __gm__ uint8_t* local, __gm__ uint8_t* remote, __gm__ int32_t* status)
{
    const uint32_t base = kStatusCaseBase + kCaseMixed * kStatusFieldsPerCase;
    status[base + kStatusAppend] = 0;
    status[base + kStatusCommitCount] = 0;
    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 1U);
    int32_t ret = hcomm.WriteNbi(batch, remote + kMixedWriteOffset, local + kMixedWriteOffset, sizeof(uint64_t));
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    ret = ret == 0 ? hcomm.ReadNbi(batch, local + kMixedReadOffset, remote + kMixedReadOffset, sizeof(uint64_t)) : ret;
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 2U);
    ret = ret == 0 ? hcomm.WriteWithNotifyNbi(
        batch, remote + kMixedNotifyDataOffset, local + kMixedNotifyDataOffset, sizeof(uint64_t),
        remote + kMixedNotifySignalOffset, kMixedNotifyValue) : ret;
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 1U);
    ret = ret == 0 ? hcomm.WriteValueNbi<uint64_t>(batch, remote + kMixedWriteValueOffset, kMixedInlineValue) : ret;
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 2U);
    ret = ret == 0 ? hcomm.AtomicFAA<uint64_t>(
        batch, remote + kMixedFaaTargetOffset, local + kMixedFaaFetchOffset, kMixedFaaAdd) : ret;
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    ret = ret == 0 ? hcomm.AtomicCAS<uint64_t>(
        batch, remote + kMixedCasTargetOffset, local + kMixedCasFetchOffset, kMixedCasInitial,
        kMixedCasSwap) : ret;
    ret = ret == 0 ? hcomm.BatchCommit(batch) : ret;
    status[base + kStatusCommitCount] += ret == 0 ? 1 : 0;
    status[base + kStatusAppend] = ret;
    if (ret != 0) {
        return ret;
    }
    status[base + kStatusCommit] = ret;
    ret = hcomm.Drain(batch);
    status[base + kStatusDrain] = ret;
    return ret;
}

__simt_callee__ inline int32_t RunSingleCase(
    AscendC::simt::Hcomm<>& hcomm, uint64_t channel, __ubuf__ uint8_t* batchBuffer,
    __gm__ uint8_t* remote, __gm__ int32_t* status)
{
    const uint32_t base = kStatusCaseBase + kCaseSingle * kStatusFieldsPerCase;
    auto batch = hcomm.MakeBatchHandle(channel, batchBuffer, kBatchBufferBytes, remote, 1U);
    status[base + kStatusCommitCount] = 0;
    int32_t ret = hcomm.WriteValueNbi<uint64_t>(batch, remote + kSingleWriteValueOffset, kSingleWriteValue);
    status[base + kStatusAppend] = ret;
    if (ret != 0) {
        return ret;
    }
    ret = hcomm.BatchCommit(batch);
    status[base + kStatusCommit] = ret;
    if (ret != 0) {
        return ret;
    }
    status[base + kStatusCommitCount] = 1;
    ret = hcomm.Drain(batch);
    status[base + kStatusDrain] = ret;
    return ret;
}

__simt_vf__ __launch_bounds__(32) inline void SimtBatchVf(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __ubuf__ uint8_t* batchBuffer,
    __ubuf__ uint32_t* groupState, __gm__ int32_t* status, uint32_t operationsPerBatch,
    uint32_t batchCount, bool groupMode)
{
    __gm__ uint8_t* local = AscendC::simt::LocalBufferAddr(channel, sendBufIdx);
    __gm__ uint8_t* remote = AscendC::simt::RemoteBufferAddr(channel, recvBufIdx);
    if (groupMode) {
        auto group = cooperative_groups::this_thread_block();
        const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
        if (rank == 0U) {
            for (uint32_t index = 0U; index < kStatusCount; ++index) {
                status[index] = kStatusNotRun;
            }
        }
        group.sync();

        AscendC::simt::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP, decltype(group)> hcomm(group);
        const int32_t initRet = hcomm.Init(
            reinterpret_cast<__ubuf__ uint8_t*>(groupState), kGroupStateWords * sizeof(uint32_t));
        if (rank == 0U) {
            status[kStatusInit] = initRet;
            status[kStatusHandle] = initRet;
        }
        if (RunGroupCase<kApiWrite>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 ||
            RunGroupCase<kApiRead>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 ||
            RunGroupCase<kApiNotify>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 ||
            RunGroupCase<kApiWriteValue>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 ||
            RunGroupCase<kApiFaa>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 ||
            RunGroupCase<kApiCas>(
                hcomm, group, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0) {
            return;
        }
        return;
    }

    if (threadIdx.x != 0U || threadIdx.y != 0U || threadIdx.z != 0U) {
        return;
    }
    for (uint32_t index = 0U; index < kStatusCount; ++index) {
        status[index] = kStatusNotRun;
    }
    AscendC::simt::Hcomm<> hcomm;
    status[kStatusInit] = hcomm.Init(nullptr, 0);
    if (status[kStatusInit] != 0) {
        return;
    }

    status[kStatusHandle] = 0;
    if (operationsPerBatch == 0U || operationsPerBatch > kOperationsPerBatch ||
        batchCount == 0U || batchCount > kBatchCount) {
        return;
    }
    if (RunHomogeneousCase<kApiWrite>(
            hcomm, channel, batchBuffer, local, remote, status, operationsPerBatch, batchCount) != 0 || groupMode) {
        return;
    }
    if (
        RunHomogeneousCase<kApiRead>(hcomm, channel, batchBuffer, local, remote, status) != 0 ||
        RunHomogeneousCase<kApiNotify>(hcomm, channel, batchBuffer, local, remote, status) != 0 ||
        RunHomogeneousCase<kApiWriteValue>(hcomm, channel, batchBuffer, local, remote, status) != 0 ||
        RunHomogeneousCase<kApiFaa>(hcomm, channel, batchBuffer, local, remote, status) != 0 ||
        RunHomogeneousCase<kApiCas>(hcomm, channel, batchBuffer, local, remote, status) != 0 ||
        RunMixedCase(hcomm, channel, batchBuffer, local, remote, status) != 0) {
        return;
    }
    (void)RunSingleCase(hcomm, channel, batchBuffer, remote, status);
}

} // namespace

extern "C" __global__ __vector__ void SimtBatch(
    uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, __gm__ int32_t* status,
    uint32_t operationsPerBatch, uint32_t batchCount, bool groupMode)
{
    alignas(128) __ubuf__ uint64_t batchBuffer[kBatchBufferBytes / sizeof(uint64_t)];
    alignas(16) __ubuf__ uint32_t groupState[kGroupStateWords];
    asc_vf_call<SimtBatchVf>(
        dim3(groupMode ? operationsPerBatch : 1U), channel, sendBufIdx, recvBufIdx,
        reinterpret_cast<__ubuf__ uint8_t*>(batchBuffer), groupState, status, operationsPerBatch, batchCount,
        groupMode);
}

void LaunchSimtBatch(
    void* stream, uint64_t channel, uint32_t sendBufIdx, uint32_t recvBufIdx, void* status,
    uint32_t operationsPerBatch, uint32_t batchCount, bool groupMode)
{
    SimtBatch<<<1, nullptr, stream>>>(
        channel, sendBufIdx, recvBufIdx, reinterpret_cast<int32_t*>(status), operationsPerBatch, batchCount,
        groupMode);
}
