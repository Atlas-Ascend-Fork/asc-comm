/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once

namespace multi_jetty_batch_perf {

template <typename HcommT, typename Group>
__simt_callee__ inline int32_t AppendOne(
    HcommT& hcomm, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api, __gm__ uint8_t* remote, __gm__ uint8_t* local,
    uint32_t payloadBytes, uint32_t slotBytes, uint32_t operation, uint32_t jetty, const Group& group)
{
    const uint64_t offset = static_cast<uint64_t>(operation) * slotBytes;
    if (api == kApiWrite) {
        return hcomm.WriteNbi(batch, remote + offset, local + offset, payloadBytes, group);
    }
    if (api == kApiRead) {
        return hcomm.ReadNbi(batch, local + offset, remote + offset, payloadBytes, group);
    }
    const uint64_t notifyValue = kNotifyValueBase | (static_cast<uint64_t>(jetty) << 32U) | operation;
    return hcomm.WriteWithNotifyNbi(
        batch, remote + offset, local + offset, payloadBytes, remote + offset + slotBytes - sizeof(uint64_t),
        notifyValue, group);
}

template <typename Group>
__simt_callee__ inline void PrepareOrdinarySq(AscendC::simt::UbcCtpBatchHandle& batch, const Group& group)
{
    using namespace AscendC::simt;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    if (context->reservationStatus != AscendC::HCOMM_SUCCESS) {
        return;
    }
    group.sync();
    if (!publisher) {
        HcommSimtFlushBatchLaneSq(context, rank, size);
    }
    group.sync();
    if (publisher) {
        // SQ maintenance is owned by the producing lanes; do not scan it a second time.
        // Retain the existing publication ordering boundary before the block rendezvous.
        asc_threadfence();
        asc_threadfence_block();
    }
}

__simt_callee__ inline void PublishPreparedPublisherSq(AscendC::simt::UbcCtpBatchHandle& batch, uint32_t size)
{
    using namespace AscendC::simt;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    HcommSimtFlushBatchLaneSq(context, size - 1U, size);
    HcommSimtResolvedPost post;
    HcommSimtLoadBatchStaticPost(context, post);
    const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
    // SQ maintenance is owned by the producing lanes; do not scan it a second time.
    asc_threadfence();
    asc_threadfence_block();
    const uint64_t publishedHead = context->reservedHead + ((static_cast<uint64_t>(size) << 32U) | totalBb);
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    *post.headAddr = publishedHead;
    asc_threadfence();
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    HcommSimtRingBatchDwqe(post.dwqeAddr, HcommSimtBatchPublisherWords(context));
}

template <typename HcommT, typename Group, typename Block>
__simt_callee__ inline int32_t SubmitPublisherCoreBatches(
    HcommT& hcomm, const Group& group, const Block& block, AscendC::simt::UbcCtpBatchHandle& batch, uint32_t api,
    __gm__ uint8_t* remote, __gm__ uint8_t* local, uint32_t payloadBytes, uint32_t slotBytes, uint32_t operationBase,
    uint32_t count, uint32_t jetty)
{
    using namespace AscendC::simt;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    auto* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batch.context);
    int32_t status = context == nullptr ? AscendC::HCOMM_FAILED : AscendC::HCOMM_SUCCESS;
    for (uint32_t begin = 0U; begin < count; begin += size) {
        // Freeze the group-uniform status before any next-batch Plan can change it.
        // Current noncollective Append fails only for this shared reservationStatus.
        if (status == AscendC::HCOMM_SUCCESS && context->reservationStatus != AscendC::HCOMM_SUCCESS) {
            status = AscendC::HCOMM_FAILED;
        }
        if (status == AscendC::HCOMM_SUCCESS) {
            if (!publisher) {
                (void)AppendOne(
                    hcomm, batch, api, remote, local, payloadBytes, slotBytes, operationBase + begin + rank, jetty,
                    group);
            }
            PrepareOrdinarySq(batch, group);
        }
        // All groups arrive, including failed groups. Each Jetty publishes independently.
        block.sync();
        if (publisher && status == AscendC::HCOMM_SUCCESS) {
            (void)AppendOne(
                hcomm, batch, api, remote, local, payloadBytes, slotBytes, operationBase + begin + rank, jetty, group);
            PublishPreparedPublisherSq(batch, size);
        }
        block.sync();
        if (status == AscendC::HCOMM_SUCCESS) {
            if (publisher) {
                const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
                const uint64_t publishedHead = context->reservedHead + ((static_cast<uint64_t>(size) << 32U) | totalBb);
                // Other Jettys may finish their private reservation while the last one publishes.
                // A failed next reservation is reported by the next Append/Commit.
                HcommSimtPlanBatchFromHead(context, publishedHead);
            }
            group.sync();
            asc_threadfence_block();
            asc_threadfence();
            group.sync();
        }
        // All publication, reservation and group cleanup finishes before the next batch.
        block.sync();
    }
    return status;
}

template <uint32_t batchSize, typename Block>
__simt_callee__ inline auto MakeParallelGroup(const Block& block)
{
    if constexpr (batchSize < 32U) {
        return cooperative_groups::tiled_partition(cooperative_groups::coalesced_threads(), batchSize);
    } else if constexpr (batchSize == 32U) {
        (void)block;
        return cooperative_groups::coalesced_threads();
    } else {
        return cooperative_groups::tiled_partition<batchSize>(block);
    }
}

} // namespace multi_jetty_batch_perf
