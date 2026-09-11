/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software; you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_JETTY_H

#include <c_api/sys_var/sys_var.h>
#include <c_api/sync/sync.h>
#include <c_api/utils/enum.h>
#include <c_api/vector_datamove/vector_datamove.h>

#include "../../../hcomm/common/hcomm_inner_def.h"
#include "../../../hcomm/common/hcomm_log.h"

namespace AscendC {

__aicore__ inline uint32_t HcommJettyPackSqeFlag(const UrmaWqeEntry& config)
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
        ((config.cqe & 0x1U) << 5U) | ((config.inlineEn & 0x1U) << 6U);
}

__aicore__ inline HcommPeer::HcommPeer(
    __ubuf__ HcommJettyPeerInfo* peerInfo, GM_ADDR jettyTable, uint32_t jettyIdx)
    : peerInfo(peerInfo)
{
    HCOMM_DEBUG_TRAP_IF(peerInfo == nullptr || jettyTable == nullptr, "peerInfo or jettyTable is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmByPassDCache(table + jettyIdx);
    HCOMM_DEBUG_TRAP_IF(jettyValue == 0U, "jettyTable[%u] is 0\n", jettyIdx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    HCOMM_DEBUG_TRAP_IF(
        jetty->remoteBufferNum == 0U || jetty->remoteBufferAddr == nullptr || jetty->sqContextAddr == nullptr,
        "jetty info is nullptr\n");
    // The last registered buffer carries the shared remote token metadata.
    auto* remoteBuffer = jetty->remoteBufferAddr + (jetty->remoteBufferNum - 1U);
    peerInfo->remoteTokenValueUdf = remoteBuffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    auto* sqContext = jetty->sqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    peerInfo->tpIdNumSgesRemoteTokenId =
        static_cast<uint64_t>(sqContext->contextInfo.ubJfs.tpID) |
        (static_cast<uint64_t>(remoteBuffer->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
    auto* remoteEid = reinterpret_cast<const uint32_t*>(sqContext->contextInfo.ubJfs.remoteEID);
    peerInfo->remoteEid[0] = static_cast<uint64_t>(remoteEid[0]) | (static_cast<uint64_t>(remoteEid[1]) << 32U);
    peerInfo->remoteEid[1] = static_cast<uint64_t>(remoteEid[2]) | (static_cast<uint64_t>(remoteEid[3]) << 32U);
    valid = true;
}

__aicore__ inline HcommJettyImpl::HcommJettyImpl(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
    GM_ADDR jettyTable, uint32_t jettyIdx)
    : jettyInfo_(jettyInfo), ubufSqe_(ubufSqe)
{
    HCOMM_DEBUG_TRAP_IF(jettyInfo == nullptr || jettyTable == nullptr, "jettyInfo or jettyTable is nullptr\n");
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmByPassDCache(table + jettyIdx);
    HCOMM_DEBUG_TRAP_IF(jettyValue == 0U, "jettyTable[%u] is 0\n", jettyIdx);
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    HCOMM_DEBUG_TRAP_IF(jetty->sqContextAddr == nullptr || jetty->cqContextAddr == nullptr, "jetty info is nullptr\n");
    auto* sqContext = jetty->sqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    auto* cqContext = jetty->cqContextAddr + HCOMM_URMA_DEFAULT_QP_IDX;
    jettyInfo_->sqBaseAddr = sqContext->contextInfo.ubJfs.sqVa;
    jettyInfo_->sqHeadAddr = sqContext->contextInfo.ubJfs.headAddr;
    jettyInfo_->completionTailAddr = sqContext->contextInfo.ubJfs.tailAddr;
    jettyInfo_->sqDoorbellAddr = sqContext->contextInfo.ubJfs.dbVa;
    jettyInfo_->cqBaseAddr = cqContext->contextInfo.ubJfc.scqVa;
    jettyInfo_->cqTailAddr = cqContext->contextInfo.ubJfc.tailAddr;
    jettyInfo_->cqDoorbellAddr = cqContext->contextInfo.ubJfc.dbVa;
    jettyInfo_->packedHead = ReadGmByPassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr));
    jettyInfo_->numWqebbBytes = sqContext->contextInfo.ubJfs.wqeSize;
    jettyInfo_->numCqeBytes = cqContext->contextInfo.ubJfc.cqeSize;
    jettyInfo_->sqDepth = sqContext->contextInfo.ubJfs.sqDepth;
    jettyInfo_->cqDepth = cqContext->contextInfo.ubJfc.cqDepth;
    valid_ = true;
}

template <bool kWithNotify>
__aicore__ inline bool HcommJettyImpl::BuildWriteSqe(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
    uint32_t currentHead, GM_ADDR notifyAddr, uint64_t notifyValue)
{
    auto* info = peer.peerInfo;
    const uint32_t tpId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId);
    const uint32_t remoteTokenId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId >> 32U);
    const uint32_t remoteTokenValue = static_cast<uint32_t>(info->remoteTokenValueUdf);
    const uint32_t opcode = kWithNotify ? static_cast<uint32_t>(HcommUrmaOpCode::WRITE_WITH_NOTIFY) :
        static_cast<uint32_t>(HcommUrmaOpCode::WRITE);
    const uint32_t owner = (currentHead & jettyInfo_->sqDepth) == 0U ? 1U : 0U;
    // Fill HcommUrmaSqeCtx with one 64-bit store per word: word0/1 pack the bitfields, word5 is rmtAddr.
    __ubuf__ uint64_t* sqeWords = reinterpret_cast<__ubuf__ uint64_t*>(ubufSqe_);
    sqeWords[0] = (static_cast<uint64_t>(HcommJettyPackSqeFlag(URMA_DEFAULT_CFG) & 0xFFU) << 16U) |
        (1U << 28U) | (1U << 29U) | (static_cast<uint64_t>(owner) << 31U) |
        (static_cast<uint64_t>(opcode) << 40U);
    sqeWords[1] = (static_cast<uint64_t>(tpId) & 0xFFFFFFU) | (1U << 24U) |
        ((static_cast<uint64_t>(remoteTokenId) & 0xFFFFFU) << 32U);
    sqeWords[2] = info->remoteEid[0];
    sqeWords[3] = info->remoteEid[1];
    sqeWords[4] = static_cast<uint64_t>(remoteTokenValue);
    sqeWords[5] = reinterpret_cast<uint64_t>(dstAddr);
    __ubuf__ uint64_t* sgeWords = reinterpret_cast<__ubuf__ uint64_t*>(ubufSqe_ + sizeof(HcommUrmaSqeCtx));
    if constexpr (kWithNotify) {
        // Fill HcommUrmaNotifyCtx with one 64-bit store per word: word1 is notifyAddr, word2 is notifyData.
        sgeWords[0] = (static_cast<uint64_t>(remoteTokenId) & 0xFFFFFU) |
            (static_cast<uint64_t>(remoteTokenValue) << 32U);
        sgeWords[1] = reinterpret_cast<uint64_t>(notifyAddr);
        sgeWords[2] = notifyValue;
        sgeWords[3] = 0U;
        sgeWords += 4U;
    }
    // Fill HcommUrmaSgeCtx with one 64-bit store per word: word1 is the source va.
    sgeWords[0] = static_cast<uint64_t>(static_cast<uint32_t>(numBytes));
    sgeWords[1] = reinterpret_cast<uint64_t>(srcAddr);
    return true;
}

template <typename T>
__aicore__ inline bool HcommJettyImpl::BuildWriteValueSqe(
    const HcommPeer& peer, GM_ADDR dstAddr, T value, uint32_t currentHead)
{
    auto* info = peer.peerInfo;
    const uint32_t tpId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId);
    const uint32_t remoteTokenId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId >> 32U);
    const uint32_t remoteTokenValue = static_cast<uint32_t>(info->remoteTokenValueUdf);
    const uint32_t owner = (currentHead & jettyInfo_->sqDepth) == 0U ? 1U : 0U;
    // Fill HcommUrmaSqeCtx with one 64-bit store per word: word0/1 pack the bitfields, word5 is rmtAddr.
    __ubuf__ uint64_t* sqeWords = reinterpret_cast<__ubuf__ uint64_t*>(ubufSqe_);
    sqeWords[0] = (static_cast<uint64_t>(HcommJettyPackSqeFlag(URMA_INLINE_CFG) & 0xFFU) << 16U) |
        (1U << 28U) | (1U << 29U) | (static_cast<uint64_t>(owner) << 31U) |
        (static_cast<uint64_t>(static_cast<uint32_t>(HcommUrmaOpCode::WRITE)) << 40U) |
        (static_cast<uint64_t>(sizeof(T)) << 54U);
    sqeWords[1] = (static_cast<uint64_t>(tpId) & 0xFFFFFFU) |
        ((static_cast<uint64_t>(remoteTokenId) & 0xFFFFFU) << 32U);
    sqeWords[2] = info->remoteEid[0];
    sqeWords[3] = info->remoteEid[1];
    sqeWords[4] = static_cast<uint64_t>(remoteTokenValue);
    sqeWords[5] = reinterpret_cast<uint64_t>(dstAddr);
    *reinterpret_cast<__ubuf__ T*>(ubufSqe_ + sizeof(HcommUrmaSqeCtx)) = value;
    return true;
}

__aicore__ inline void HcommJettyImpl::CopySqeToSq(uint32_t currentHead, uint32_t numWqebbs)
{
    uint32_t sqIdx = currentHead & (jettyInfo_->sqDepth - 1U);
    uint32_t first = numWqebbs < jettyInfo_->sqDepth - sqIdx ? numWqebbs : jettyInfo_->sqDepth - sqIdx;
    asc_sync_notify(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_copy_ub2gm_align(reinterpret_cast<__gm__ uint8_t*>(jettyInfo_->sqBaseAddr +
        static_cast<uint64_t>(jettyInfo_->numWqebbBytes) * sqIdx), ubufSqe_, 1,
        jettyInfo_->numWqebbBytes * first, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    if (first < numWqebbs) {
        asc_copy_ub2gm_align(reinterpret_cast<__gm__ uint8_t*>(jettyInfo_->sqBaseAddr),
            ubufSqe_ + jettyInfo_->numWqebbBytes * first, 1,
            jettyInfo_->numWqebbBytes * (numWqebbs - first),
            asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }
    asc_sync_notify(PIPE_MTE3, PIPE_S, EVENT_ID0);
    asc_sync_wait(PIPE_MTE3, PIPE_S, EVENT_ID0);
}

__aicore__ inline void HcommJettyImpl::AdvanceSq(uint32_t numWqebbs)
{
    uint32_t newHead = static_cast<uint32_t>(jettyInfo_->packedHead) + numWqebbs;
    uint32_t completions = static_cast<uint32_t>(jettyInfo_->packedHead >> 32U) + 1U;
    jettyInfo_->packedHead = static_cast<uint64_t>(newHead) | (static_cast<uint64_t>(completions) << 32U);
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr), jettyInfo_->packedHead);
}

__aicore__ inline void HcommJettyImpl::RingDoorbell()
{
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->sqDoorbellAddr),
        static_cast<uint32_t>(jettyInfo_->packedHead));
}

template <int64_t kNumTimeoutCycles>
__aicore__ inline void HcommJettyImpl::PollCqWhenSqOverflow()
{
    constexpr uint32_t threshold = 10U;
    constexpr uint32_t pollCount = 100U;
    uint32_t completions = static_cast<uint32_t>(jettyInfo_->packedHead >> 32U);
    uint32_t tail = ReadGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->completionTailAddr));
    if ((completions + threshold) % jettyInfo_->cqDepth == tail % jettyInfo_->cqDepth) {
        PollCq<kNumTimeoutCycles>(tail + pollCount < completions ? tail + pollCount : completions);
    }
}

template <int64_t kNumTimeoutCycles>
__aicore__ inline int32_t HcommJettyImpl::PollCq(uint32_t expectedTail)
{
    uint32_t currentTail = ReadGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqTailAddr));
    while (currentTail != expectedTail) {
        auto* cqe = reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqBaseAddr +
            static_cast<uint64_t>(jettyInfo_->numCqeBytes) * (currentTail & (jettyInfo_->cqDepth - 1U)));
        uint32_t phase = (currentTail / jettyInfo_->cqDepth) & 1U;
        uint64_t start = static_cast<uint64_t>(asc_get_system_cycle());
        uint32_t header;
        do {
            header = ReadGmByPassDCache(cqe);
            if (((header >> 2U) & 1U) != phase) {
                break;
            }
            if (static_cast<uint64_t>(asc_get_system_cycle()) - start >= static_cast<uint64_t>(kNumTimeoutCycles)) {
                trap();
            }
            Nop<300>();
        } while (true);
        if (((header >> 16U) & 0xFFU) != 0U || ((header >> 24U) & 0xFFU) != 0U) {
            return HCOMM_FAILED;
        }
        ++currentTail;
    }
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqTailAddr), currentTail);
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->cqDoorbellAddr), currentTail & 0xFFFFFFU);
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->completionTailAddr), currentTail);
    return HCOMM_SUCCESS;
}

template <int64_t kNumTimeoutCycles, bool kWithNotify, bool kDoCommit>
__aicore__ inline int32_t HcommJettyImpl::PostWrite(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
    GM_ADDR notifyAddr, uint64_t notifyValue, uint32_t numWqebbs)
{
    if (!valid_ || jettyInfo_ == nullptr || ubufSqe_ == nullptr || jettyInfo_->sqDepth == 0U ||
        jettyInfo_->cqDepth == 0U) {
        return HCOMM_FAILED;
    }
    uint32_t head = static_cast<uint32_t>(jettyInfo_->packedHead);
    PollCqWhenSqOverflow<kNumTimeoutCycles>();
    if (!BuildWriteSqe<kWithNotify>(peer, dstAddr, srcAddr, numBytes, head, notifyAddr, notifyValue)) {
        return HCOMM_FAILED;
    }
    CopySqeToSq(head, numWqebbs);
    AdvanceSq(numWqebbs);
    if constexpr (kDoCommit) {
        RingDoorbell();
    }
    return HCOMM_SUCCESS;
}

template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit>
__aicore__ inline int32_t HcommJettyImpl::PostWriteValue(
    const HcommPeer& peer, GM_ADDR dstAddr, T value, uint32_t numWqebbs)
{
    if (!valid_ || jettyInfo_ == nullptr || ubufSqe_ == nullptr || jettyInfo_->sqDepth == 0U ||
        jettyInfo_->cqDepth == 0U) {
        return HCOMM_FAILED;
    }
    const uint32_t head = static_cast<uint32_t>(jettyInfo_->packedHead);
    PollCqWhenSqOverflow<kNumTimeoutCycles>();
    if (!BuildWriteValueSqe<T>(peer, dstAddr, value, head)) {
        return HCOMM_FAILED;
    }
    CopySqeToSq(head, numWqebbs);
    AdvanceSq(numWqebbs);
    if constexpr (kDoCommit) {
        RingDoorbell();
    }
    return HCOMM_SUCCESS;
}

template <int64_t kNumTimeoutCycles>
__aicore__ inline int32_t HcommJettyImpl::Drain()
{
    if (!valid_ || jettyInfo_ == nullptr) {
        return HCOMM_FAILED;
    }
    if (jettyInfo_->cqDepth == 0U || jettyInfo_->numCqeBytes == 0U) {
        return HCOMM_FAILED;
    }
    uint64_t head = ReadGmByPassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr));
    return PollCq<kNumTimeoutCycles>(static_cast<uint32_t>(head >> 32U));
}

__aicore__ inline HcommJetty::HcommJetty(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
    GM_ADDR jettyTable, uint32_t jettyIdx)
    : impl_(jettyInfo, ubufSqe, jettyTable, jettyIdx)
{}

template <int64_t kNumTimeoutCycles, bool kDoCommit>
__aicore__ inline int32_t HcommJetty::Write(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes)
{
    return impl_.PostWrite<kNumTimeoutCycles, false, kDoCommit>(
        peer, dstAddr, srcAddr, numBytes, nullptr, 0U, kNumWriteWqebbs);
}

template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit>
__aicore__ inline int32_t HcommJetty::WriteValue(
    const HcommPeer& peer, GM_ADDR dstAddr, T value)
{
    static_assert(sizeof(T) <= kNumWriteWqebbs * 64U - sizeof(HcommUrmaSqeCtx),
        "WriteValue value is too large for one WQEBB");
    return impl_.PostWriteValue<kNumTimeoutCycles, T, kDoCommit>(
        peer, dstAddr, value, kNumWriteWqebbs);
}

template <int64_t kNumTimeoutCycles>
__aicore__ inline int32_t HcommJetty::WriteWithNotify(
    const HcommPeer& peer, GM_ADDR dstAddr, GM_ADDR srcAddr, uint64_t numBytes,
    GM_ADDR notifyAddr, uint64_t notifyValue)
{
    return impl_.PostWrite<kNumTimeoutCycles, true, true>(
        peer, dstAddr, srcAddr, numBytes, notifyAddr, notifyValue, kNumWriteWithNotifyWqebbs);
}

template <int64_t kNumTimeoutCycles>
__aicore__ inline int32_t HcommJetty::Drain()
{
    return impl_.Drain<kNumTimeoutCycles>();
}

__aicore__ inline void HcommJetty::AdvanceSq(uint32_t numWqebbs)
{
    impl_.AdvanceSq(numWqebbs);
}

__aicore__ inline void HcommJetty::RingDoorbell()
{
    impl_.RingDoorbell();
}

} // namespace AscendC

#endif
