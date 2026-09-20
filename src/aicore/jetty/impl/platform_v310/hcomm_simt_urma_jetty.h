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
 * \file hcomm_simt_urma_jetty.h
 * \brief SIMT Jetty implementation.
 *
 * A WRITE occupies the WQEBBs required by its 48-byte header and active SGEs,
 * up to four WQEBBs for twelve SGEs. SIMT fills the SQ; publication can be
 * performed by SIMD through PublishSq().
 */
#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_JETTY_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_JETTY_H

#include <cstddef>
#include <cstdint>
#include <kernel_operator.h>
#include <simt_api/device_atomic_functions.h>
#include <simt_api/device_warp_functions.h>
#include <simt_api/cooperative_groups.h>

#include "hcomm_simt_urma_jetty_def.h"
#include "../../../hcomm/common/hcomm_inner_def.h"
#include "../../../hcomm/common/hcomm_log.h"
#include "../../../hcomm/impl/platform_v310/hcomm_simt_urma_def.h"
#include "../../../utils/simt_utils.h"

namespace AscendC::simt {
namespace jetty {
constexpr uint32_t HCOMM_SIMT_SQE_HEADER_WORDS = 6U;
constexpr uint64_t HCOMM_SIMT_WRITE_HEADER = 0x0000030030010000ULL;
constexpr uint64_t HCOMM_SIMT_NOP_HEADER = 0x0000110000000000ULL;
constexpr uint64_t HCOMM_SIMT_FINAL_FLAGS = (1ULL << 18U) | (1ULL << 19U) | (1ULL << 21U);
constexpr uint64_t HCOMM_SIMT_NOP_CONTROL_FLAGS = (1ULL << 28U) | (1ULL << 29U);
constexpr uint64_t HCOMM_SIMT_NOP_FINAL_FLAGS = (1ULL << 16U) | HCOMM_SIMT_FINAL_FLAGS | HCOMM_SIMT_NOP_CONTROL_FLAGS;
constexpr uint64_t HCOMM_SIMT_NOP_PUBLISH_FLAGS = HCOMM_SIMT_NOP_FINAL_FLAGS & ~(1ULL << 21U);

template <uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes>
__simt_callee__ constexpr uint32_t PostSendPayloadWords()
{
    if constexpr (kOpcode == HcommPostOpcode::kWrite) {
        return HCOMM_SIMT_SQE_HEADER_WORDS + kSgeNum * 2U;
    } else if constexpr (kOpcode == HcommPostOpcode::kWriteWithNotify) {
        return 2U * HCOMM_SIMT_WQEBB_LANES;
    } else {
        return HCOMM_SIMT_SQE_HEADER_WORDS + (kInlineBytes + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    }
}

template <uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes>
__simt_callee__ constexpr uint32_t PostSendPayloadWqebbs()
{
    return (PostSendPayloadWords<kSgeNum, kOpcode, kInlineBytes>() + HCOMM_SIMT_WQEBB_LANES - 1U) /
           HCOMM_SIMT_WQEBB_LANES;
}

// SIMD and SIMT describe the same hardware queues and exchange these descriptors through UB,
// so both use the single definition in jetty/common. HcommPeerInfo keeps the SIMT-side spelling
// of the peer descriptor; the layout and the size/offset asserts live with the definition.
using AscendC::HcommJettyInfo;
using HcommPeerInfo = AscendC::HcommJettyPeerInfo;

namespace detail {

template <typename T>
__simt_callee__ __forceinline__ T Load(const uint64_t addr, const uint32_t offset)
{
    return *reinterpret_cast<__gm__ T*>(addr + offset);
}
__simt_callee__ __forceinline__ uint64_t
PeerWord(__ubuf__ const HcommPeerInfo* p, const uint32_t word, const uint32_t sgeNum, __gm__ void* dst)
{
    auto* words = reinterpret_cast<__ubuf__ const uint64_t*>(p);
    const uint64_t cached = words[word - 1U];
    const uint64_t word1 = (cached & 0x000FFFFF00FFFFFFULL) | (static_cast<uint64_t>(sgeNum) << 24U);
    const uint64_t word1Mask = 0ULL - static_cast<uint64_t>(word == 1U);
    const uint64_t word5Mask = 0ULL - static_cast<uint64_t>(word == 5U);
    return (cached & ~(word1Mask | word5Mask)) | (word1 & word1Mask) | (reinterpret_cast<uint64_t>(dst) & word5Mask);
}
} // namespace detail

class HcommPeer {
public:
    __simt_callee__ __forceinline__ HcommPeer() = default;
    __simt_callee__ __forceinline__ HcommPeer(__ubuf__ HcommPeerInfo* info, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        Init(info, jettyTable, jettyIdx);
    }
    __simt_callee__ __forceinline__ HcommPeer(__ubuf__ HcommPeerInfo* info, uint64_t channelAddr) { Attach(info); }
    __simt_callee__ __forceinline__ __ubuf__ HcommPeerInfo* Info() const
    {
        return reinterpret_cast<__ubuf__ HcommPeerInfo*>(infoAddr);
    }

    // Keep the two-argument attach form used by existing SIMT kernels. The
    // channel is already represented by the cached peer metadata.
    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommPeerInfo* info, uint64_t) { Attach(info); }

private:
    __simt_callee__ __forceinline__ void Attach(__ubuf__ HcommPeerInfo* info)
    {
        infoAddr = reinterpret_cast<uint64_t>(info);
    }
    __simt_callee__ __forceinline__ void Init(__ubuf__ HcommPeerInfo* info, __gm__ void* jettyTable, uint32_t jettyIdx)
    {
        const uint64_t table = reinterpret_cast<uint64_t>(jettyTable);
        HCOMM_DEBUG_TRAP_IF(table == 0U || info == nullptr, "invalid SIMT HcommPeer initialization\n");
        const uint64_t channelAddr = *reinterpret_cast<__gm__ uint64_t*>(table + jettyIdx * sizeof(uint64_t));
        Attach(info);
        HCOMM_DEBUG_TRAP_IF(channelAddr == 0U, "invalid SIMT HcommPeer channel\n");
        const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
        const uint64_t rbAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, remoteBufferAddr));
        const uint32_t rbNum = detail::Load<uint32_t>(channelAddr, offsetof(ChannelEntity, remoteBufferNum));
        HCOMM_DEBUG_TRAP_IF(sqCtxAddr == 0U || rbAddr == 0U || rbNum == 0U, "invalid SIMT HcommPeer channel context\n");
        auto* sq = reinterpret_cast<__gm__ SqContext*>(sqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
        auto* rb = reinterpret_cast<__gm__ RegedBufferEntity*>(rbAddr) + rbNum - 1U;
        info->tpIdNumSgesRemoteTokenId =
            static_cast<uint64_t>(sq->contextInfo.ubJfs.tpID) |
            (static_cast<uint64_t>(rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
        uint64_t remoteEidL = 0U;
        uint64_t remoteEidH = 0U;
        for (uint32_t i = 0U; i < 8U; ++i) {
            remoteEidL |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i]) << (i * 8U);
            remoteEidH |= static_cast<uint64_t>(sq->contextInfo.ubJfs.remoteEID[i + 8U]) << (i * 8U);
        }
        info->remoteEid[0] = remoteEidL;
        info->remoteEid[1] = remoteEidH;
        info->remoteTokenValueUdf = rb->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        info->remoteBaseAddr = rb->bufferInfo.rma.addr;
        info->remoteBufferSize = rb->bufferInfo.rma.size;
    }

private:
    uint64_t infoAddr = 0U;
};

__simt_callee__ __forceinline__
JettyImpl::JettyImpl(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
{
    Init(info, stage, table, index);
}

__simt_callee__ __forceinline__
JettyImpl::JettyImpl(__ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
{
    Attach(info, stage, channelAddr);
}

__simt_callee__ __forceinline__ __ubuf__ HcommJettyInfo* JettyImpl::Info() const
{
    return reinterpret_cast<__ubuf__ HcommJettyInfo*>(infoAddr_);
}

template <uint32_t kSgeNum, bool kDoCommit, typename Coop>
__simt_callee__ __forceinline__ int32_t
JettyImpl::Write(const HcommPeer& peer, __gm__ void* dst, const JettySge* sges, const Coop& coop) const
{
    if constexpr (HcommCoopTraits<Coop>::kWarpCapable) {
        const JettySge laneSge = sges[0];
        return PostSend<kSgeNum, HcommPostOpcode::kWrite, kDoCommit>(coop, peer, detail::PostSendArgs<>{dst, &laneSge});
    } else {
        return PostSend<kSgeNum, HcommPostOpcode::kWrite, kDoCommit>(coop, peer, detail::PostSendArgs<>{dst, sges});
    }
}

template <int64_t, typename T, bool kDoCommit>
__simt_callee__ __forceinline__ int32_t JettyImpl::WriteValue(const HcommPeer& peer, __gm__ void* dst, T value)
{
    static_assert(
        sizeof(T) <= HCOMM_URMA_WQE_BB_SIZE - HCOMM_SIMT_SQE_HEADER_WORDS * sizeof(uint64_t),
        "inline value does not fit in a URMA WQEBB");
    HcommCoopThread thread;
    return PostSend<0U, HcommPostOpcode::kWriteValue, kDoCommit, sizeof(T)>(
        thread, peer, detail::InlinePostSendArgs{dst, reinterpret_cast<const uint8_t*>(&value)});
}

template <int64_t, bool kDoCommit>
__simt_callee__ __forceinline__ int32_t JettyImpl::WriteWithNotify(
    const HcommPeer& peer, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyValue)
{
    HcommCoopThread thread;
    const JettySge sges[] = {{reinterpret_cast<uint64_t>(src), static_cast<int>(len)}};
    return PostSend<1U, HcommPostOpcode::kWriteWithNotify, kDoCommit>(
        thread, peer, detail::PostSendArgs<true>{dst, sges, notifyAddr, notifyValue});
}

__simt_callee__ __forceinline__ void JettyImpl::AdvanceSq() const
{
    if (implicitActive_ && implicitWqebbs_ != 0U) {
        AdvanceImplicitSq();
    } else if (stageAddr_ != 0U && Batch()->wqebbCount != 0U) {
        AdvanceBatch();
    }
}

template <typename Coop>
__simt_callee__ __forceinline__ void JettyImpl::AdvanceSq(const Coop& coop) const
{
    coop.sync();
    if (coop.thread_rank() == 0U)
        AdvanceSq();
    coop.sync();
}

template <typename Coop>
__simt_callee__ __forceinline__ void JettyImpl::AdvanceSq(const Coop& coop, uint32_t, uint32_t) const
{
    AdvanceSq(coop);
}

__simt_callee__ __forceinline__ void JettyImpl::AdvanceImplicitSq() const
{
    auto* info = Info();
    const uint32_t end = static_cast<uint32_t>(*reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead));
    const uint32_t begin = implicitBeginHead_;
    const uint32_t submittedWqebbs = implicitWqebbs_;
    HCOMM_DEBUG_TRAP_IF(
        end <= begin || submittedWqebbs > HCOMM_SIMT_SLOT_WQEBBS || submittedWqebbs > end - begin,
        "invalid SIMT HcommJetty advance range\n");
    const uint32_t last = end - submittedWqebbs;
    const uint32_t payloadWqebbs = implicitPayloadWqebbs_ == 0U ? submittedWqebbs : implicitPayloadWqebbs_;
    HCOMM_DEBUG_TRAP_IF(payloadWqebbs > submittedWqebbs, "invalid SIMT HcommJetty payload range\n");
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
    const uint32_t idx = last & (info->sqDepth - 1U);
    uint64_t header = HCOMM_SIMT_WRITE_HEADER | idx;
    if (payloadWqebbs == submittedWqebbs) {
        header = HCOMM_SIMT_WRITE_HEADER | (end & 0xFFFFU) | HCOMM_SIMT_FINAL_FLAGS;
    } else {
        const uint32_t nopHead = begin + payloadWqebbs;
        const uint32_t nopIdx = nopHead & (info->sqDepth - 1U);
        const uint64_t nop = HCOMM_SIMT_NOP_HEADER | (end & 0xFFFFU) | HCOMM_SIMT_NOP_FINAL_FLAGS;
        asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(nopIdx) * HCOMM_SIMT_WQEBB_LANES), nop);
    }
    asc_threadfence();
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * HCOMM_SIMT_WQEBB_LANES), header);
    asc_atomic_add(reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead) + 1, 1U);
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr), info->packedHead);
    implicitActive_ = false;
    implicitWqebbs_ = 0U;
    implicitPayloadWqebbs_ = 0U;
}

__simt_callee__ __forceinline__ void JettyImpl::PublishSq() const
{
    auto* info = Info();
    HCOMM_DEBUG_TRAP_IF(
        info == nullptr || info->sqHeadAddr == 0U || info->sqDoorbellAddr == 0U || info->sqDepth == 0U,
        "invalid SIMT HcommJetty publish state\n");
    const uint64_t packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
    const uint32_t head = static_cast<uint32_t>(packedHead);
    // A publication DWQE consumes two BBs. Its NOP headers therefore encode
    // the post-DWQE PI and the following BB index, respectively.
    const uint32_t nextHead = head + HCOMM_URMA_DWQE_BB_CNT;
    const uint64_t nop0 = HCOMM_SIMT_NOP_HEADER | (nextHead & 0xFFFFU);
    const uint64_t nop1 = HCOMM_SIMT_NOP_HEADER | ((nextHead + 1U) & 0xFFFFU) | HCOMM_SIMT_NOP_PUBLISH_FLAGS;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
    StoreSqePair(sq, info->sqDepth, head, 0U, nop0, 0U);
    StoreSqePair(sq, info->sqDepth, head, HCOMM_SIMT_WQEBB_LANES, nop1, 0U);
    info->packedHead = (packedHead & 0xFFFFFFFF00000000ULL) | nextHead;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr), info->packedHead);

    // Cache-locked Jetty doorbells consume a complete 128B DWQE. Both NOPs
    // follow the SQ payload instead of aliasing it. The second NOP publishes
    // their final PI but intentionally omits bit 21 (CQE).
    auto* dwqe = reinterpret_cast<__gm__ ulonglong2*>(info->sqDoorbellAddr - HCOMM_URMA_DWQE_DB_OFFSET);
    asc_threadfence();
    asc_stwt(&dwqe[0], make_ulonglong2(nop0, 0U));
    asc_stwt(&dwqe[1], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[2], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[3], make_ulonglong2(0U, 0U));
    asc_stwt(&dwqe[4], make_ulonglong2(nop1, 0U));
}

template <typename Coop>
__simt_callee__ __forceinline__ void JettyImpl::PublishSq(const Coop& coop) const
{
    coop.sync();
    if (coop.thread_rank() == 0U)
        PublishSq();
    coop.sync();
}

template <int64_t kTimeout>
__simt_callee__ __forceinline__ int32_t JettyImpl::Drain() const
{
    auto* info = Info();
    HCOMM_DEBUG_TRAP_IF(
        info == nullptr || info->cqTailAddr == 0U || info->cqBaseAddr == 0U || info->cqDepth == 0U ||
            info->numCqeBytes == 0U || info->completionTailAddr == 0U || info->cqDoorbellAddr == 0U,
        "invalid SIMT HcommJetty CQ state\n");
    const uint32_t expected = static_cast<uint32_t>(info->packedHead >> 32U);
    auto* tail = reinterpret_cast<__gm__ uint32_t*>(info->cqTailAddr);
    auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
    uint32_t current = *tail;
    uint32_t released = *sqTail;
    while (current != expected) {
        if (PollCqEntry<kTimeout>(current, released) != kPollCqSuccess)
            return -1;
        ++current;
    }
    *tail = current;
    *reinterpret_cast<__gm__ uint32_t*>(info->cqDoorbellAddr) = current & 0xFFFFFFU;
    *sqTail = released;
    return 0;
}

template <int64_t kTimeout, typename Coop>
__simt_callee__ __forceinline__ int32_t JettyImpl::Drain(const Coop& coop) const
{
    coop.sync();
    int32_t result = 0;
    if (coop.thread_rank() == 0U)
        result = Drain<kTimeout>();
    result = asc_shfl(result, 0, HCOMM_SIMT_WARP_SIZE);
    coop.sync();
    return result;
}

__simt_callee__ __forceinline__ void JettyImpl::Attach(
    __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, uint64_t channelAddr)
{
    infoAddr_ = reinterpret_cast<uint64_t>(info);
    stageAddr_ = reinterpret_cast<uint64_t>(stage);
    channel_ = channelAddr;
}

__simt_callee__ __forceinline__ void JettyImpl::Init(
    __ubuf__ HcommJettyInfo* info, __ubuf__ uint8_t* stage, __gm__ void* table, uint32_t index)
{
    const uint64_t tableAddr = reinterpret_cast<uint64_t>(table);
    HCOMM_DEBUG_TRAP_IF(tableAddr == 0U || info == nullptr, "invalid SIMT HcommJetty initialization\n");
    const uint64_t channelAddr = *reinterpret_cast<__gm__ uint64_t*>(tableAddr + index * sizeof(uint64_t));
    Attach(info, stage, channelAddr);
    if (stage != nullptr) {
        auto* batch = reinterpret_cast<__ubuf__ BatchState*>(stage);
        batch->wqebbCount = 0U;
        batch->lastOffset = 0U;
        batch->failed = 0U;
    }
    HCOMM_DEBUG_TRAP_IF(channelAddr == 0U, "invalid SIMT HcommJetty channel\n");
    const uint64_t sqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, sqContextAddr));
    const uint64_t cqCtxAddr = detail::Load<uint64_t>(channelAddr, offsetof(ChannelEntity, cqContextAddr));
    HCOMM_DEBUG_TRAP_IF(sqCtxAddr == 0U || cqCtxAddr == 0U, "invalid SIMT HcommJetty channel context\n");
    auto* sq = reinterpret_cast<__gm__ SqContext*>(sqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
    auto* cq = reinterpret_cast<__gm__ CqContext*>(cqCtxAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
    info->sqBaseAddr = sq->contextInfo.ubJfs.sqVa;
    info->sqHeadAddr = sq->contextInfo.ubJfs.headAddr;
    info->completionTailAddr = sq->contextInfo.ubJfs.tailAddr;
    info->sqDoorbellAddr = sq->contextInfo.ubJfs.dbVa;
    info->cqBaseAddr = cq->contextInfo.ubJfc.scqVa;
    info->cqTailAddr = cq->contextInfo.ubJfc.tailAddr;
    info->cqDoorbellAddr = cq->contextInfo.ubJfc.dbVa;
    info->numWqebbBytes = sq->contextInfo.ubJfs.wqeSize;
    info->numCqeBytes = cq->contextInfo.ubJfc.cqeSize;
    info->sqDepth = sq->contextInfo.ubJfs.sqDepth;
    info->cqDepth = cq->contextInfo.ubJfc.cqDepth;
    HCOMM_DEBUG_TRAP_IF(info->sqHeadAddr == 0U, "invalid SIMT HcommJetty SQ head\n");
    info->packedHead = *reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr);
}

__simt_callee__ __forceinline__ __ubuf__ JettyImpl::BatchState* JettyImpl::Batch() const
{
    return reinterpret_cast<__ubuf__ BatchState*>(stageAddr_);
}

__simt_callee__ __forceinline__ uint32_t JettyImpl::ReserveImplicitSq(const uint32_t reserveWqebbs) const
{
    auto* info = Info();
    auto* sqHead = reinterpret_cast<__ubuf__ uint32_t*>(&info->packedHead);
    auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
    const uint32_t depth = info->sqDepth;
    const uint32_t oldHead = *sqHead;
    // Leave room for the two-BB publication DWQE.
    const uint32_t requiredFreeWqebbs = reserveWqebbs + HCOMM_URMA_DWQE_BB_CNT;
    uint32_t consumed = oldHead - *sqTail;
    while ((consumed > depth || requiredFreeWqebbs > depth - consumed) && PullCqOne()) {
        consumed = oldHead - *sqTail;
    }
    uint32_t head = 0xFFFFFFFFU;
    if (consumed <= depth && requiredFreeWqebbs <= depth - consumed) {
        head = oldHead;
        *sqHead = oldHead + reserveWqebbs;
        implicitBeginHead_ = oldHead;
        implicitWqebbs_ = reserveWqebbs;
        implicitPayloadWqebbs_ = reserveWqebbs;
        implicitActive_ = true;
    }
    return head;
}

__simt_callee__ __forceinline__ uint32_t JettyImpl::ReserveBatchSq(const uint32_t reserveWqebbs) const
{
    if (stageAddr_ == 0U)
        return 0xFFFFFFFFU;
    auto* info = Info();
    auto* batch = Batch();
    // Keep packedHead at the published base while warps claim batch offsets.
    const uint32_t offset = asc_atomic_add(&batch->wqebbCount, reserveWqebbs);
    const uint32_t base = static_cast<uint32_t>(info->packedHead);
    const uint32_t consumed = base - *reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
    const uint32_t requiredFreeWqebbs = reserveWqebbs + HCOMM_URMA_DWQE_BB_CNT;
    if (consumed <= info->sqDepth && offset <= info->sqDepth - consumed &&
        requiredFreeWqebbs <= info->sqDepth - consumed - offset) {
        asc_atomic_max(&batch->lastOffset, offset);
        return base + offset;
    }
    asc_atomic_exch(&batch->failed, 1U);
    return 0xFFFFFFFFU;
}

template <
    uint32_t kSgeNum, HcommPostOpcode kOpcode, bool kDoCommit, uint32_t kInlineBytes, typename Coop, typename Args>
__simt_callee__ __forceinline__ int32_t
JettyImpl::PostSend(const Coop& coop, const HcommPeer& peer, const Args& args) const
{
    static_assert(kOpcode != HcommPostOpcode::kWriteValue || kSgeNum == 0U, "inline writes do not carry SGEs");
    static_assert(
        kOpcode != HcommPostOpcode::kWriteWithNotify || kSgeNum == 1U, "WRITE_WITH_NOTIFY currently supports one SGE");
    static_assert(kSgeNum <= HCOMM_SIMT_MAX_SGES, "invalid SGE count");
    static_assert(
        PostSendPayloadWqebbs<kSgeNum, kOpcode, kInlineBytes>() <= HCOMM_SIMT_SLOT_WQEBBS,
        "post-send payload exceeds the SQ slot");
    static_assert(
        kOpcode != HcommPostOpcode::kWriteValue || kInlineBytes != 0U,
        "inline WRITE requires a compile-time value size");
    const uint32_t rank = static_cast<uint32_t>(coop.thread_rank());
    const uint32_t laneRank = rank % HCOMM_SIMT_WARP_SIZE;
    if constexpr (kDoCommit) {
        if (coop.size() > static_cast<int>(HCOMM_SIMT_WARP_SIZE)) {
            coop.sync();
            return -1;
        }
    }
    if (coop.size() != 1) {
        coop.sync();
    }

    constexpr uint32_t kStaticPayloadWqebbs = PostSendPayloadWqebbs<kSgeNum, kOpcode, kInlineBytes>();
    constexpr bool kDeepEpWarpWrite =
        HcommCoopTraits<Coop>::kWarpCapable && kOpcode == HcommPostOpcode::kWrite && !kDoCommit;
    // Scalar WRITE reserves only its actual payload; warp producers retain their
    // fixed reservation so all lanes can fill one uniform SQ layout.
    constexpr bool kScalarWrite = !HcommCoopTraits<Coop>::kWarpCapable && kOpcode == HcommPostOpcode::kWrite;
    constexpr uint32_t kReservedWqebbs = kDeepEpWarpWrite ? HCOMM_SIMT_SLOT_WQEBBS : kStaticPayloadWqebbs;
    uint32_t reserveWqebbs = kReservedWqebbs;
    uint32_t actualSgeNum = kSgeNum;
    if constexpr (kScalarWrite) {
        actualSgeNum = CountScalarSges<kSgeNum>(args.sges);
        reserveWqebbs =
            (HCOMM_SIMT_SQE_HEADER_WORDS + actualSgeNum * 2U + HCOMM_SIMT_WQEBB_LANES - 1U) / HCOMM_SIMT_WQEBB_LANES;
    }

    // Reserve once per warp; only single-producer posts may reclaim CQ entries.
    uint32_t head = 0xFFFFFFFFU;
    if (laneRank == 0U) {
        if constexpr (kDoCommit || kScalarWrite) {
            head = ReserveImplicitSq(reserveWqebbs);
        } else {
            head = ReserveBatchSq(reserveWqebbs);
        }
    }
    if (coop.size() > 1)
        head = asc_shfl(head, 0, HCOMM_SIMT_WARP_SIZE);
    if (head == 0xFFFFFFFFU) {
        if (coop.size() > 1)
            coop.sync();
        return coop.size() > HCOMM_SIMT_WARP_SIZE ? 0 : -1;
    }

    FillSqWarpLane<kSgeNum, kOpcode, kInlineBytes, kReservedWqebbs>(
        coop, peer, args, head, reserveWqebbs, actualSgeNum);
    asc_threadfence();
    coop.sync();
    if constexpr (kDoCommit) {
        AdvanceSq(coop);
        PublishSq(coop);
    }
    return 0;
}

__simt_callee__ __forceinline__ void JettyImpl::AdvanceBatch() const
{
    auto* info = Info();
    auto* batch = Batch();
    const uint32_t count = batch->wqebbCount;
    if (count == 0U)
        return;
    if (batch->failed != 0U) {
        batch->wqebbCount = 0U;
        batch->lastOffset = 0U;
        batch->failed = 0U;
        return;
    }
    const uint32_t begin = static_cast<uint32_t>(info->packedHead);
    const uint32_t end = begin + count;
    const uint32_t last = begin + batch->lastOffset;
    const uint32_t depth = info->sqDepth;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
    const uint32_t idx = last & (depth - 1U);
    const uint64_t firstWord =
        *reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * HCOMM_SIMT_WQEBB_LANES);
    const uint64_t header = (firstWord & ~0xFFFFULL) | (end & 0xFFFFU) | HCOMM_SIMT_FINAL_FLAGS;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(sq + static_cast<uint64_t>(idx) * HCOMM_SIMT_WQEBB_LANES), header);
    asc_threadfence();
    const uint64_t packed = static_cast<uint64_t>(end) |
                            ((static_cast<uint64_t>(static_cast<uint32_t>(info->packedHead >> 32U) + 1U)) << 32U);
    info->packedHead = packed;
    asc_stwt(reinterpret_cast<__gm__ uint64_t*>(info->sqHeadAddr), packed);
    batch->wqebbCount = 0U;
    batch->lastOffset = 0U;
    batch->failed = 0U;
}

// Poll one CQE and advance the local SQ completion tail. The caller owns CQ tail and
// doorbell updates so this helper can serve both Drain and the incremental reservation path.
template <int64_t kTimeout>
__simt_callee__ __forceinline__ int32_t JettyImpl::PollCqEntry(const uint32_t current, uint32_t& released) const
{
    auto* info = Info();
    auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
        info->cqBaseAddr + static_cast<uint64_t>(info->numCqeBytes) * (current & (info->cqDepth - 1U)));
    const uint32_t phase = (current / info->cqDepth) & 1U;
    uint32_t header = *cqe;
    int64_t retry = 0;
    while (((header >> 2U) & 1U) == phase && retry < kTimeout) {
        asc_threadfence();
        asc_dcci_single(cqe);
        header = *cqe;
        ++retry;
    }
    if (retry >= kTimeout)
        return kPollCqUnavailable;
    if (((header >> 16U) & 0xFFU) != 0U || ((header >> 24U) & 0xFFU) != 0U)
        return -1;
    const uint32_t completed = cqe[1] & (info->sqDepth - 1U);
    released += (completed + info->sqDepth - (released & (info->sqDepth - 1U))) & (info->sqDepth - 1U);
    ++released;
    return kPollCqSuccess;
}

// Reap one completed CQE, matching the SIMD PullCq behavior. This is used only by
// the single-producer path, where no other producer can race the CQ tail update.
__simt_callee__ __forceinline__ bool JettyImpl::PullCqOne() const
{
    auto* info = Info();
    const uint32_t expected = static_cast<uint32_t>(info->packedHead >> 32U);
    auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(info->cqTailAddr);
    auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(info->completionTailAddr);
    uint32_t current = *cqTail;
    if (current == expected)
        return false;
    uint32_t released = *sqTail;
    if (PollCqEntry<>(current, released) != kPollCqSuccess) {
        return false;
    }
    ++current;
    *cqTail = current;
    *reinterpret_cast<__gm__ uint32_t*>(info->cqDoorbellAddr) = current & 0xFFFFFFU;
    *sqTail = released;
    return true;
}

__simt_callee__ __forceinline__ bool JettyImpl::IsValidSge(const JettySge& sge)
{
    return sge.addr != 0U && sge.len > 0;
}

template <uint32_t kSgeNum>
__simt_callee__ __forceinline__ uint32_t JettyImpl::CountScalarSges(const JettySge* sges)
{
    uint32_t count = 0U;
#pragma unroll
    for (uint32_t i = 0U; i < kSgeNum; ++i) {
        count += static_cast<uint32_t>(IsValidSge(sges[i]));
    }
    return count;
}

template <
    uint32_t kSgeNum, HcommPostOpcode kOpcode, uint32_t kInlineBytes, uint32_t kWqebbCount, typename Coop,
    typename Args>
__simt_callee__ __forceinline__ void JettyImpl::FillSqWarpLane(
    const Coop& coop, const HcommPeer& peer, const Args& args, const uint32_t head, const uint32_t runtimeWqebbCount,
    const uint32_t scalarSgeNum) const
{
    constexpr uint32_t kPayloadWords = PostSendPayloadWords<kSgeNum, kOpcode, kInlineBytes>();
    constexpr uint32_t kTotalWords = kWqebbCount * HCOMM_SIMT_WQEBB_LANES;
    constexpr bool kWarpLane = HcommCoopTraits<Coop>::kWarpCapable;
    const uint32_t lane = kWarpLane ? static_cast<uint32_t>(coop.thread_rank()) % HCOMM_SIMT_WARP_SIZE : 0U;
    auto* info = Info();
    const uint32_t depth = info->sqDepth;
    auto* sq = reinterpret_cast<__gm__ uint64_t*>(info->sqBaseAddr);
    uint32_t activeSges = scalarSgeNum;
    if constexpr (kWarpLane) {
        activeSges = kSgeNum;
    }
    JettySge laneSge{};
    if constexpr (kOpcode == HcommPostOpcode::kWrite) {
        if constexpr (kWarpLane) {
            laneSge = args.sges[0];
        }
    }
    constexpr uint32_t kWarpPayloadWords = HCOMM_SIMT_SQE_HEADER_WORDS + kSgeNum * 2U;
    const uint32_t activePayloadWords = kWarpLane ? kWarpPayloadWords : HCOMM_SIMT_SQE_HEADER_WORDS + activeSges * 2U;

    // A warp maps one lane to one SQE word. Scalar producers walk the same layout
    // serially because they have no cooperating lanes.
    const uint32_t totalWords = kWarpLane ? kTotalWords : runtimeWqebbCount * HCOMM_SIMT_WQEBB_LANES;
    const uint32_t roundCount = kWarpLane ? 1U : totalWords;
    for (uint32_t round = 0U; round < roundCount; ++round) {
        const uint32_t word = kWarpLane ? lane : round;
        const bool active = word < totalWords;
        const uint32_t logicalWord = active ? word : 0U;
        const uint32_t bbHead = head + logicalWord / HCOMM_SIMT_WQEBB_LANES;
        const uint32_t sqIdx = bbHead & (depth - 1U);
        uint64_t value = 0U;
        if constexpr (kOpcode == HcommPostOpcode::kWrite) {
            static_assert(kSgeNum > 0U, "WRITE requires at least one SGE");
            const uint32_t sge = logicalWord >= HCOMM_SIMT_SQE_HEADER_WORDS && logicalWord < activePayloadWords ?
                                     (logicalWord - HCOMM_SIMT_SQE_HEADER_WORDS) / 2U :
                                     0U;
            uint64_t sgeAddr = 0U;
            int sgeLen = 0;
            if constexpr (kWarpLane) {
                // Every lane executes the shuffle, including header and padding
                // lanes, so the warp follows one uniform data path.
                sgeAddr = asc_shfl(laneSge.addr, static_cast<int>(sge), HCOMM_SIMT_WARP_SIZE);
                sgeLen = asc_shfl(laneSge.len, static_cast<int>(sge), HCOMM_SIMT_WARP_SIZE);
            } else if (logicalWord >= HCOMM_SIMT_SQE_HEADER_WORDS && logicalWord < activePayloadWords) {
                sgeAddr = args.sges[sge].addr;
                sgeLen = args.sges[sge].len;
            }
            if (logicalWord == 0U) {
                value = HCOMM_SIMT_WRITE_HEADER | sqIdx;
            } else if (logicalWord < HCOMM_SIMT_SQE_HEADER_WORDS) {
                value = detail::PeerWord(peer.Info(), logicalWord, activeSges, args.dst);
            } else if (logicalWord < activePayloadWords) {
                value = (logicalWord & 1U) == 0U ? static_cast<uint64_t>(sgeLen) : sgeAddr;
            } else if constexpr (kWarpLane) {
                if (logicalWord < kTotalWords && logicalWord % HCOMM_SIMT_WQEBB_LANES == 0U &&
                    kWqebbCount > (kWarpPayloadWords + HCOMM_SIMT_WQEBB_LANES - 1U) / HCOMM_SIMT_WQEBB_LANES) {
                    value = HCOMM_SIMT_NOP_HEADER | sqIdx;
                }
            }
        } else if constexpr (kOpcode == HcommPostOpcode::kWriteValue) {
            value = BuildValueWord<kPayloadWords, kInlineBytes>(peer, args, logicalWord, sqIdx);
        } else {
            value = BuildNotifyWord(peer, args, logicalWord, sqIdx);
        }
        if (active) {
            asc_stcg(
                sq + static_cast<uint64_t>(sqIdx) * HCOMM_SIMT_WQEBB_LANES + logicalWord % HCOMM_SIMT_WQEBB_LANES,
                value);
        }
    }
}

template <uint32_t kPayloadWords, uint32_t kInlineBytes>
__simt_callee__ __forceinline__ uint64_t JettyImpl::BuildValueWord(
    const HcommPeer& peer, const detail::InlinePostSendArgs& args, const uint32_t word, const uint32_t sqIdx)
{
    if (word == 0U) {
        return HCOMM_SIMT_WRITE_HEADER | sqIdx | (1ULL << 22U) | (static_cast<uint64_t>(kInlineBytes & 0x3FFU) << 54U);
    }
    if (word < HCOMM_SIMT_SQE_HEADER_WORDS)
        return detail::PeerWord(peer.Info(), word, 0U, args.dst);
    if (word < kPayloadWords) {
        const uint32_t offset = (word - HCOMM_SIMT_SQE_HEADER_WORDS) * sizeof(uint64_t);
        uint64_t result = 0U;
        for (uint32_t byte = 0U; byte < sizeof(uint64_t) && offset + byte < kInlineBytes; ++byte) {
            result |= static_cast<uint64_t>(args.value[offset + byte]) << (byte * 8U);
        }
        return result;
    }
    return 0U;
}

__simt_callee__ __forceinline__ uint64_t JettyImpl::BuildNotifyWord(
    const HcommPeer& peer, const detail::PostSendArgs<true>& args, const uint32_t word, const uint32_t sqIdx)
{
    if (word == 0U) {
        return (HCOMM_SIMT_WRITE_HEADER & ~(0xFFULL << 40U)) | (static_cast<uint64_t>(0x0CU) << 40U) | sqIdx;
    }
    if (word < HCOMM_SIMT_SQE_HEADER_WORDS)
        return detail::PeerWord(peer.Info(), word, 1U, args.dst);
    if (word == HCOMM_SIMT_SQE_HEADER_WORDS) {
        return static_cast<uint64_t>((peer.Info()->tpIdNumSgesRemoteTokenId >> 32U) & 0xFFFFFU) |
               ((peer.Info()->remoteTokenValueUdf & 0xFFFFFFFFULL) << 32U);
    }
    if (word == HCOMM_SIMT_SQE_HEADER_WORDS + 1U)
        return reinterpret_cast<uint64_t>(args.notifyAddr);
    if (word == HCOMM_SIMT_SQE_HEADER_WORDS + 2U)
        return args.notifyValue;
    if (word == HCOMM_SIMT_SQE_HEADER_WORDS + 4U)
        return static_cast<uint32_t>(args.sges[0].len);
    if (word == HCOMM_SIMT_SQE_HEADER_WORDS + 5U)
        return args.sges[0].addr;
    return 0U;
}

__simt_callee__ __forceinline__ void JettyImpl::StoreSqePair(
    __gm__ uint64_t* sq, const uint32_t depth, const uint32_t head, const uint32_t firstWord, const uint64_t first,
    const uint64_t second)
{
    const uint32_t bbOffset = firstWord / HCOMM_SIMT_WQEBB_LANES;
    const uint32_t sqIdx = (head + bbOffset) & (depth - 1U);
    auto* pairs = reinterpret_cast<__gm__ ulonglong2*>(sq + static_cast<uint64_t>(sqIdx) * HCOMM_SIMT_WQEBB_LANES);
    asc_stwt(pairs + (firstWord % HCOMM_SIMT_WQEBB_LANES) / 2U, make_ulonglong2(first, second));
};

} // namespace jetty
} // namespace AscendC::simt

namespace AscendC::simt {
using jetty::HcommJettyInfo;
using jetty::HcommPeer;
using jetty::HcommPeerInfo;
using jetty::JettyImpl;
using jetty::JettySge;
} // namespace AscendC::simt

#endif
