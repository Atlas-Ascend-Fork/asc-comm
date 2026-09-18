/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_H

#include <c_api/sync/sync.h>
#include <c_api/utils/sys_var.h>
#include <c_api/vector_datamove/vector_datamove.h>

#include "../../../hcomm/common/hcomm_inner_def.h"
#include "../../../hcomm/common/hcomm_log.h"

namespace AscendC {

// HcommUrmaJfcCqeCtx first-word field positions, read as a raw uint32_t header by PollCq.
static constexpr uint32_t HCOMM_JETTY_CQE_OWNER_BIT = 2U;
static constexpr uint32_t HCOMM_JETTY_CQE_SUBSTATUS_BIT = 16U;
static constexpr uint32_t HCOMM_JETTY_CQE_STATUS_BIT = 24U;
static constexpr uint32_t HCOMM_JETTY_CQE_FIELD_MASK = 0xFFU;
// The CQ doorbell only takes the low 24 bits of the CQ tail.
static constexpr uint32_t HCOMM_JETTY_CQ_DOORBELL_MASK = 0xFFFFFFU;

__aicore__ inline uint32_t HcommJettyPackSqeFlag(const UrmaWqeEntry& config)
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
           ((config.cqe & 0x1U) << 5U) | ((config.inlineEn & 0x1U) << 6U);
}

__aicore__ inline HcommPeer::HcommPeer(__ubuf__ HcommJettyPeerInfo* peerInfo, GM_ADDR jettyTable, uint32_t jettyIdx)
    : peerInfo(peerInfo)
{
    HCOMM_DEBUG_TRAP_IF(peerInfo == nullptr || jettyTable == nullptr, "peerInfo or jettyTable is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmBypassDCache(table + jettyIdx);
    HCOMM_DEBUG_TRAP_IF(jettyValue == 0U, "jettyTable[%u] is 0\n", jettyIdx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    HCOMM_DEBUG_TRAP_IF(
        jetty->remoteBufferNum == 0U || jetty->remoteBufferAddr == nullptr || jetty->sqContextAddr == nullptr,
        "jetty info is nullptr\n");
    // The last registered buffer carries the shared remote token metadata.
    auto* remoteBuffer = jetty->remoteBufferAddr + (jetty->remoteBufferNum - 1U);
    peerInfo->remoteTokenValue = remoteBuffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    peerInfo->remoteTokenId = remoteBuffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    auto* sqContext = jetty->sqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    peerInfo->tpId = sqContext->contextInfo.ubJfs.tpID;
    auto* remoteEid = reinterpret_cast<const uint32_t*>(sqContext->contextInfo.ubJfs.remoteEID);
    peerInfo->remoteEid[0] = static_cast<uint64_t>(remoteEid[0]) | (static_cast<uint64_t>(remoteEid[1]) << 32U);
    peerInfo->remoteEid[1] = static_cast<uint64_t>(remoteEid[2]) | (static_cast<uint64_t>(remoteEid[3]) << 32U);
    valid = true;
}

__aicore__ inline HcommJettyImpl::HcommJettyImpl(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe, GM_ADDR jettyTable, uint32_t jettyIdx)
    : jettyInfo_(jettyInfo), ubufSqe_(ubufSqe)
{
    HCOMM_DEBUG_TRAP_IF(jettyInfo == nullptr || jettyTable == nullptr, "jettyInfo or jettyTable is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmBypassDCache(table + jettyIdx);
    HCOMM_DEBUG_TRAP_IF(jettyValue == 0U, "jettyTable[%u] is 0\n", jettyIdx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    HCOMM_DEBUG_TRAP_IF(jetty->sqContextAddr == nullptr || jetty->cqContextAddr == nullptr, "jetty info is nullptr\n");
    auto* sqContext = jetty->sqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    auto* cqContext = jetty->cqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    jettyInfo_->sqBaseAddr = sqContext->contextInfo.ubJfs.sqVa;
    jettyInfo_->sqHeadAddr = sqContext->contextInfo.ubJfs.headAddr;
    jettyInfo_->sqTailAddr = sqContext->contextInfo.ubJfs.tailAddr;
    jettyInfo_->sqDoorbellAddr = sqContext->contextInfo.ubJfs.dbVa;
    jettyInfo_->cqBaseAddr = cqContext->contextInfo.ubJfc.scqVa;
    jettyInfo_->cqTailAddr = cqContext->contextInfo.ubJfc.tailAddr;
    jettyInfo_->cqDoorbellAddr = cqContext->contextInfo.ubJfc.dbVa;
    uint64_t packedHead = ReadGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr));
    jettyInfo_->sqHead = static_cast<uint32_t>(packedHead);
    jettyInfo_->expectedCqeCnt = static_cast<uint32_t>(packedHead >> 32U);
    jettyInfo_->numWqebbBytes = sqContext->contextInfo.ubJfs.wqeSize;
    jettyInfo_->numCqeBytes = cqContext->contextInfo.ubJfc.cqeSize;
    jettyInfo_->sqDepth = sqContext->contextInfo.ubJfs.sqDepth;
    jettyInfo_->cqDepth = cqContext->contextInfo.ubJfc.cqDepth;
    valid_ = true;
}

template <HcommUrmaOpCode opCode, auto const& config>
__aicore__ inline void HcommJettyImpl::FillWriteSqeHeader(
    const HcommPeer& peer, GM_ADDR dstAddr, uint32_t inlineMsgLen, uint32_t sgeNum)
{
    auto* info = peer.peerInfo;
    const uint32_t owner = (jettyInfo_->sqHead & jettyInfo_->sqDepth) == 0U ? 1U : 0U;
    // Fill HcommUrmaSqeCtx with one 64-bit store per word: word0/1 pack the bitfields, word5 is rmtAddr.
    __ubuf__ uint64_t* sqeWords = reinterpret_cast<__ubuf__ uint64_t*>(ubufSqe_);
    sqeWords[0] = (static_cast<uint64_t>(HcommJettyPackSqeFlag(config) & 0xFFU) << 16U) | (1U << 28U) | (1U << 29U) |
                  (static_cast<uint64_t>(owner) << 31U) | (static_cast<uint64_t>(opCode) << 40U) |
                  (static_cast<uint64_t>(inlineMsgLen) << 54U);
    sqeWords[1] = (static_cast<uint64_t>(info->tpId) & 0xFFFFFFU) | (static_cast<uint64_t>(sgeNum) << 24U) |
                  ((static_cast<uint64_t>(info->remoteTokenId) & 0xFFFFFU) << 32U);
    sqeWords[2] = info->remoteEid[0];
    sqeWords[3] = info->remoteEid[1];
    sqeWords[4] = static_cast<uint64_t>(info->remoteTokenValue);
    sqeWords[5] = reinterpret_cast<uint64_t>(dstAddr);
}

template <bool withNotify>
__aicore__ inline void HcommJettyImpl::BuildWriteSqe(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes, GM_ADDR notifyAddr,
    uint64_t notifyValue)
{
    constexpr HcommUrmaOpCode opCode = withNotify ? HcommUrmaOpCode::WRITE_WITH_NOTIFY : HcommUrmaOpCode::WRITE;
    FillWriteSqeHeader<opCode, URMA_DEFAULT_CFG>(peer, dstAddr, 0U, 1U);
    __ubuf__ uint64_t* sgeWords = reinterpret_cast<__ubuf__ uint64_t*>(ubufSqe_ + sizeof(HcommUrmaSqeCtx));
    if constexpr (withNotify) {
        // Fill HcommUrmaNotifyCtx with one 64-bit store per word: word1 is notifyAddr, word2 is notifyData.
        sgeWords[0] = (static_cast<uint64_t>(peer.peerInfo->remoteTokenId) & 0xFFFFFU) |
                      (static_cast<uint64_t>(peer.peerInfo->remoteTokenValue) << 32U);
        sgeWords[1] = reinterpret_cast<uint64_t>(notifyAddr);
        sgeWords[2] = notifyValue;
        sgeWords[3] = 0U;
        sgeWords += 4U;
    }
    // Fill HcommUrmaSgeCtx with one 64-bit store per word: word1 is the source va.
    sgeWords[0] = static_cast<uint64_t>(static_cast<uint32_t>(numBytes));
    sgeWords[1] = reinterpret_cast<uint64_t>(srcAddr);
}

template <typename T>
__aicore__ inline void HcommJettyImpl::BuildWriteValueSqe(const HcommPeer& peer, GM_ADDR dstAddr, T value)
{
    FillWriteSqeHeader<HcommUrmaOpCode::WRITE, URMA_INLINE_CFG>(peer, dstAddr, static_cast<uint32_t>(sizeof(T)), 0U);
    *reinterpret_cast<__ubuf__ T*>(ubufSqe_ + sizeof(HcommUrmaSqeCtx)) = value;
}

__aicore__ inline void HcommJettyImpl::CopyWqeToSq(uint32_t currentHead, uint32_t numWqebbs)
{
    uint32_t sqIdx = currentHead & (jettyInfo_->sqDepth - 1U);
    uint32_t first = numWqebbs < jettyInfo_->sqDepth - sqIdx ? numWqebbs : jettyInfo_->sqDepth - sqIdx;
    asc_sync_notify(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_copy_ub2gm_align(
        reinterpret_cast<__gm__ uint8_t*>(
            jettyInfo_->sqBaseAddr + static_cast<uint64_t>(jettyInfo_->numWqebbBytes) * sqIdx),
        ubufSqe_, 1, jettyInfo_->numWqebbBytes * first, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (first < numWqebbs) {
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(jettyInfo_->sqBaseAddr), ubufSqe_ + jettyInfo_->numWqebbBytes * first, 1,
            jettyInfo_->numWqebbBytes * (numWqebbs - first), asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    asc_sync_notify(PIPE_MTE3, PIPE_S, EVENT_ID0);
    asc_sync_wait(PIPE_MTE3, PIPE_S, EVENT_ID0);
}

__aicore__ inline void HcommJettyImpl::AdvanceSq(uint32_t numWqebbs)
{
    jettyInfo_->sqHead += numWqebbs;
    ++jettyInfo_->expectedCqeCnt;
    uint64_t packedHead =
        static_cast<uint64_t>(jettyInfo_->sqHead) | (static_cast<uint64_t>(jettyInfo_->expectedCqeCnt) << 32U);
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr), packedHead);
    AscendC::DataSyncBarrier<AscendC::MemDsbT::DDR>();
}

__aicore__ inline void HcommJettyImpl::RingDoorbell()
{
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->sqDoorbellAddr), jettyInfo_->sqHead);
}

template <int64_t timeoutCycles>
__aicore__ inline void HcommJettyImpl::PollCqWhenSqOverflow()
{
    // Poll once the outstanding CQEs (expected minus completed) come within the threshold of
    // cqDepth. Use >= instead of ==: multiple WQEs may be posted between two checks, so the
    // distance can jump past an exact value and an equality test would never fire.
    constexpr uint32_t nearFullThreshold = 10U;
    constexpr uint32_t reclaimBatch = 100U;
    uint32_t completions = jettyInfo_->expectedCqeCnt;
    uint32_t tail = ReadGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->sqTailAddr));
    if (completions - tail + nearFullThreshold >= jettyInfo_->cqDepth) {
        PollCq<timeoutCycles>(tail + reclaimBatch < completions ? tail + reclaimBatch : completions);
    }
}

template <int64_t timeoutCycles>
__aicore__ inline int32_t HcommJettyImpl::PollCq(uint32_t expectedTail)
{
    uint32_t currentTail = ReadGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqTailAddr));
    while (currentTail != expectedTail) {
        auto* cqe = reinterpret_cast<__gm__ uint32_t*>(
            jettyInfo_->cqBaseAddr +
            static_cast<uint64_t>(jettyInfo_->numCqeBytes) * (currentTail & (jettyInfo_->cqDepth - 1U)));
        uint32_t phase = (currentTail / jettyInfo_->cqDepth) & 1U;
        uint64_t start = static_cast<uint64_t>(asc_get_system_cycle());
        uint32_t header;
        do {
            header = ReadGmBypassDCache(cqe);
            if (((header >> HCOMM_JETTY_CQE_OWNER_BIT) & 1U) != phase) {
                break;
            }
            if (static_cast<uint64_t>(asc_get_system_cycle()) - start >= static_cast<uint64_t>(timeoutCycles)) {
                HCOMM_KERNEL_LOG(
                    KERNEL_ERROR, "HcommJetty PollCq timeout, cqTail=%u, expectedTail=%u, cqe phase=%u\n", currentTail,
                    expectedTail, phase);
                trap();
            }
            Nop<300>();
        } while (true);
        if (((header >> HCOMM_JETTY_CQE_SUBSTATUS_BIT) & HCOMM_JETTY_CQE_FIELD_MASK) != 0U ||
            ((header >> HCOMM_JETTY_CQE_STATUS_BIT) & HCOMM_JETTY_CQE_FIELD_MASK) != 0U) {
            return HCOMM_FAILED;
        }
        ++currentTail;
    }
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqTailAddr), currentTail);
    WriteGmBypassDCache(
        reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqDoorbellAddr), currentTail & HCOMM_JETTY_CQ_DOORBELL_MASK);
    WriteGmBypassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->sqTailAddr), currentTail);
    AscendC::DataSyncBarrier<AscendC::MemDsbT::DDR>();
    return HCOMM_SUCCESS;
}

__aicore__ inline bool HcommJettyImpl::Ready()
{
    return valid_ && jettyInfo_ != nullptr && ubufSqe_ != nullptr && jettyInfo_->sqDepth != 0U &&
           jettyInfo_->cqDepth != 0U;
}

template <int64_t timeoutCycles, bool doCommit>
__aicore__ inline int32_t HcommJettyImpl::PostSqe(uint32_t numWqebbs)
{
    uint32_t head = jettyInfo_->sqHead;
    PollCqWhenSqOverflow<timeoutCycles>();
    AdvanceSq(numWqebbs);
    CopyWqeToSq(head, numWqebbs);
    if constexpr (doCommit) {
        RingDoorbell();
    }
    return HCOMM_SUCCESS;
}

template <int64_t timeoutCycles, bool withNotify, bool doCommit>
__aicore__ inline int32_t HcommJettyImpl::PostWrite(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes, GM_ADDR notifyAddr,
    uint64_t notifyValue, uint32_t numWqebbs)
{
    HCOMM_DEBUG_TRAP_IF(!Ready(), "HcommJetty PostWrite on an unready jetty\n");
    BuildWriteSqe<withNotify>(peer, dstAddr, srcAddr, numBytes, notifyAddr, notifyValue);
    return PostSqe<timeoutCycles, doCommit>(numWqebbs);
}

template <int64_t timeoutCycles, typename T, bool doCommit>
__aicore__ inline int32_t HcommJettyImpl::PostWriteValue(
    const HcommPeer& peer, GM_ADDR dstAddr, T value, uint32_t numWqebbs)
{
    HCOMM_DEBUG_TRAP_IF(!Ready(), "HcommJetty PostWriteValue on an unready jetty\n");
    BuildWriteValueSqe<T>(peer, dstAddr, value);
    return PostSqe<timeoutCycles, doCommit>(numWqebbs);
}

template <int64_t timeoutCycles>
__aicore__ inline int32_t HcommJettyImpl::Drain()
{
    HCOMM_DEBUG_TRAP_IF(!Ready() || jettyInfo_->numCqeBytes == 0U, "HcommJetty Drain on an unready jetty\n");
    uint64_t packedHead = ReadGmBypassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr));
    return PollCq<timeoutCycles>(static_cast<uint32_t>(packedHead >> 32U));
}

} // namespace AscendC

#endif
