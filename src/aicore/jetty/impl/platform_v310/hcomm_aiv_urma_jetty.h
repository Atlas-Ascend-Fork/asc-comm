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

#include "../../../hcomm/common/hcomm_inner_def.h"
#include <c_api/sys_var/sys_var.h>
#include <c_api/sync/sync.h>
#include <c_api/vector_datamove/vector_datamove.h>

namespace AscendC {

static constexpr uint32_t HCOMM_JETTY_WRITE_SQE_FLAG = 5U | (1U << 3U) | (1U << 5U);
static constexpr uint32_t HCOMM_JETTY_WRITE_VALUE_SQE_FLAG = HCOMM_JETTY_WRITE_SQE_FLAG | (1U << 6U);

__aicore__ __forceinline__ HcommPeer::HcommPeer(
    __ubuf__ HcommJettyPeerInfo* peerInfo, __gm__ void* jettyTable,
    uint32_t jettyIdx)
    : peerInfo(peerInfo)
{
    if (peerInfo == nullptr || jettyTable == nullptr) {
        return;
    }
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmByPassDCache(table + jettyIdx);
    if (jettyValue == 0U) {
        return;
    }
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    if (jetty->remoteBufferNum == 0U || jetty->remoteBufferAddr == nullptr || jetty->sqContextAddr == nullptr) {
        return;
    }
    // The last registered buffer carries the shared remote token metadata.
    auto remoteBuffer = jetty->remoteBufferAddr[jetty->remoteBufferNum - 1U];
    if (remoteBuffer.type != REGED_BUFFER_RMA ||
        remoteBuffer.bufferInfo.rma.addr == 0U || remoteBuffer.bufferInfo.rma.size == 0U) {
        return;
    }
    peerInfo->remoteTokenValueUdf = remoteBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    peerInfo->remoteBaseAddr = remoteBuffer.bufferInfo.rma.addr;
    peerInfo->remoteBufferSize = remoteBuffer.bufferInfo.rma.size;
    auto sqContext = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    peerInfo->tpIdNumSgesRemoteTokenId =
        static_cast<uint64_t>(sqContext.contextInfo.ubJfs.tpID) |
        (static_cast<uint64_t>(remoteBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId) << 32U);
    auto remoteEid = reinterpret_cast<const uint64_t*>(sqContext.contextInfo.ubJfs.remoteEID);
    peerInfo->remoteEid[0] = remoteEid[0];
    peerInfo->remoteEid[1] = remoteEid[1];
    valid = true;
}

__aicore__ __forceinline__ HcommJettyImpl::HcommJettyImpl(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
    __gm__ void* jettyTable, uint32_t jettyIdx)
    : jettyInfo_(jettyInfo), ubufSqe_(ubufSqe)
{
    if (jettyInfo == nullptr || jettyTable == nullptr) {
        return;
    }
    auto* table = reinterpret_cast<__gm__ uint64_t*>(jettyTable);
    uint64_t jettyValue = ReadGmByPassDCache(table + jettyIdx);
    if (jettyValue == 0U) {
        return;
    }
    auto* jetty = reinterpret_cast<__gm__ ChannelEntity*>(jettyValue);
    if (jetty->sqContextAddr == nullptr || jetty->cqContextAddr == nullptr) {
        return;
    }
    auto sqContext = jetty->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqContext = jetty->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    jettyInfo_->sqBaseAddr = sqContext.contextInfo.ubJfs.sqVa;
    jettyInfo_->sqHeadAddr = sqContext.contextInfo.ubJfs.headAddr;
    jettyInfo_->completionTailAddr = sqContext.contextInfo.ubJfs.tailAddr;
    jettyInfo_->sqDoorbellAddr = sqContext.contextInfo.ubJfs.dbVa;
    jettyInfo_->cqBaseAddr = cqContext.contextInfo.ubJfc.scqVa;
    jettyInfo_->cqTailAddr = cqContext.contextInfo.ubJfc.tailAddr;
    jettyInfo_->cqDoorbellAddr = cqContext.contextInfo.ubJfc.dbVa;
    jettyInfo_->packedHead = ReadGmByPassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr));
    jettyInfo_->numWqebbBytes = sqContext.contextInfo.ubJfs.wqeSize;
    jettyInfo_->numCqeBytes = cqContext.contextInfo.ubJfc.cqeSize;
    jettyInfo_->sqDepth = sqContext.contextInfo.ubJfs.sqDepth;
    jettyInfo_->cqDepth = cqContext.contextInfo.ubJfc.cqDepth;
    if (jettyInfo_->sqBaseAddr == 0U || jettyInfo_->sqHeadAddr == 0U || jettyInfo_->sqDoorbellAddr == 0U ||
        jettyInfo_->cqBaseAddr == 0U || jettyInfo_->cqTailAddr == 0U || jettyInfo_->cqDoorbellAddr == 0U ||
        jettyInfo_->numWqebbBytes == 0U || jettyInfo_->numCqeBytes == 0U || jettyInfo_->sqDepth == 0U ||
        jettyInfo_->cqDepth == 0U) {
        return;
    }
    valid_ = true;
}

template <bool kWithNotify>
__aicore__ __forceinline__ bool HcommJettyImpl::BuildWriteSqe(
    const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes,
    uint32_t currentHead, __gm__ void* notifyAddr, uint64_t notifyValue)
{
    if (!valid_ || !peer.valid || jettyInfo_ == nullptr || ubufSqe_ == nullptr || peer.peerInfo == nullptr ||
        srcAddr == nullptr) {
        return false;
    }
    auto* info = peer.peerInfo;
    if (numBytes > 0xFFFFFFFFULL) {
        return false;
    }
    uint64_t remoteAddr = reinterpret_cast<uint64_t>(dstAddr);
    auto* sqe = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(ubufSqe_);
    sqe->sqeBbIdx = 0;
    sqe->flag = HCOMM_JETTY_WRITE_SQE_FLAG;
    sqe->rsv0 = 0;
    sqe->nf = 0;
    sqe->tokenEn = 1;
    sqe->rmtJettyType = 1;
    sqe->owner = (currentHead & jettyInfo_->sqDepth) == 0U ? 1U : 0U;
    sqe->targetHint = 0;
    sqe->opcode = static_cast<uint32_t>(kWithNotify ? HcommUrmaOpCode::WRITE_WITH_NOTIFY : HcommUrmaOpCode::WRITE);
    sqe->rsv1 = 0;
    sqe->inlineMsgLen = 0;
    sqe->tpId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId);
    sqe->sgeNum = 1;
    sqe->rmtJettyOrSegId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId >> 32U);
    sqe->rsv2 = 0;
    sqe->rmtEidL = info->remoteEid[0];
    sqe->rmtEidH = info->remoteEid[1];
    sqe->rmtTokenValue = static_cast<uint32_t>(info->remoteTokenValueUdf);
    sqe->udfType = 0;
    sqe->reduceDataType = 0;
    sqe->reduceOpcode = 0;
    sqe->rsv3 = 0;
    sqe->rmtAddrLOrTokenId = static_cast<uint32_t>(remoteAddr);
    sqe->rmtAddrHOrTokenValue = static_cast<uint32_t>(remoteAddr >> 32U);
    __ubuf__ uint8_t* sgeAddr = ubufSqe_ + sizeof(HcommUrmaSqeCtx);
    if constexpr (kWithNotify) {
        auto* notify = reinterpret_cast<__ubuf__ HcommUrmaNotifyCtx*>(sgeAddr);
        uint64_t remoteNotifyAddr = reinterpret_cast<uint64_t>(notifyAddr);
        notify->notifyTokenId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId >> 32U);
        notify->rsv = 0;
        notify->notifyTokenValue = static_cast<uint32_t>(info->remoteTokenValueUdf);
        notify->notifyAddrL = static_cast<uint32_t>(remoteNotifyAddr);
        notify->notifyAddrH = static_cast<uint32_t>(remoteNotifyAddr >> 32U);
        notify->notifyDataL = static_cast<uint32_t>(notifyValue);
        notify->notifyDataH = static_cast<uint32_t>(notifyValue >> 32U);
        notify->rsv2[0] = 0;
        notify->rsv2[1] = 0;
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    auto* sge = reinterpret_cast<__ubuf__ HcommUrmaSgeCtx*>(sgeAddr);
    sge->len = static_cast<uint32_t>(numBytes);
    sge->tokenId = 0;
    sge->va = reinterpret_cast<uint64_t>(srcAddr);
    return true;
}

template <typename T>
__aicore__ __forceinline__ bool HcommJettyImpl::BuildWriteValueSqe(
    const HcommPeer& peer, __gm__ void* dstAddr, T value, uint32_t currentHead)
{
    if (!valid_ || !peer.valid || jettyInfo_ == nullptr || ubufSqe_ == nullptr || peer.peerInfo == nullptr) {
        return false;
    }
    auto* info = peer.peerInfo;
    const uint64_t remoteAddr = reinterpret_cast<uint64_t>(dstAddr);
    auto* sqe = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(ubufSqe_);
    sqe->sqeBbIdx = 0;
    sqe->flag = HCOMM_JETTY_WRITE_VALUE_SQE_FLAG;
    sqe->rsv0 = 0;
    sqe->nf = 0;
    sqe->tokenEn = 1;
    sqe->rmtJettyType = 1;
    sqe->owner = (currentHead & jettyInfo_->sqDepth) == 0U ? 1U : 0U;
    sqe->targetHint = 0;
    sqe->opcode = static_cast<uint32_t>(HcommUrmaOpCode::WRITE);
    sqe->rsv1 = 0;
    sqe->inlineMsgLen = sizeof(T);
    sqe->tpId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId);
    sqe->sgeNum = 0;
    sqe->rmtJettyOrSegId = static_cast<uint32_t>(info->tpIdNumSgesRemoteTokenId >> 32U);
    sqe->rsv2 = 0;
    sqe->rmtEidL = info->remoteEid[0];
    sqe->rmtEidH = info->remoteEid[1];
    sqe->rmtTokenValue = static_cast<uint32_t>(info->remoteTokenValueUdf);
    sqe->udfType = 0;
    sqe->reduceDataType = 0;
    sqe->reduceOpcode = 0;
    sqe->rsv3 = 0;
    sqe->rmtAddrLOrTokenId = static_cast<uint32_t>(remoteAddr);
    sqe->rmtAddrHOrTokenValue = static_cast<uint32_t>(remoteAddr >> 32U);
    auto* inlineAddr = reinterpret_cast<__ubuf__ T*>(ubufSqe_ + sizeof(HcommUrmaSqeCtx));
    *inlineAddr = value;
    return true;
}

__aicore__ __forceinline__ void HcommJettyImpl::CopySqeToSq(uint32_t currentHead, uint32_t numWqebbs)
{
    uint32_t sqIdx = currentHead & (jettyInfo_->sqDepth - 1U);
    uint32_t first = numWqebbs < jettyInfo_->sqDepth - sqIdx ? numWqebbs : jettyInfo_->sqDepth - sqIdx;
    asc_sync_notify(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_sync_wait(PIPE_S, PIPE_MTE3, EVENT_ID0);
    asc_copy_ub2gm_align(reinterpret_cast<__gm__ uint8_t*>(jettyInfo_->sqBaseAddr +
        static_cast<uint64_t>(jettyInfo_->numWqebbBytes) * sqIdx), ubufSqe_, 1,
        jettyInfo_->numWqebbBytes * first, static_cast<uint8_t>(ST_L2CacheType::L2_CACHE_HINT_NOTALLOC_CI), 0, 0);
    if (first < numWqebbs) {
        asc_copy_ub2gm_align(reinterpret_cast<__gm__ uint8_t*>(jettyInfo_->sqBaseAddr),
            ubufSqe_ + jettyInfo_->numWqebbBytes * first, 1,
            jettyInfo_->numWqebbBytes * (numWqebbs - first),
            static_cast<uint8_t>(ST_L2CacheType::L2_CACHE_HINT_NOTALLOC_CI), 0, 0);
    }
    asc_sync_notify(PIPE_MTE3, PIPE_S, EVENT_ID0);
    asc_sync_wait(PIPE_MTE3, PIPE_S, EVENT_ID0);
}

__aicore__ __forceinline__ void HcommJettyImpl::AdvanceSq(uint32_t numWqebbs)
{
    uint32_t newHead = static_cast<uint32_t>(jettyInfo_->packedHead) + numWqebbs;
    uint32_t completions = static_cast<uint32_t>(jettyInfo_->packedHead >> 32U) + 1U;
    jettyInfo_->packedHead = static_cast<uint64_t>(newHead) | (static_cast<uint64_t>(completions) << 32U);
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint64_t*>(jettyInfo_->sqHeadAddr), jettyInfo_->packedHead);
}

__aicore__ __forceinline__ void HcommJettyImpl::RingDoorbell()
{
    WriteGmByPassDCache(reinterpret_cast<__gm__ uint32_t*>(jettyInfo_->sqDoorbellAddr),
        static_cast<uint32_t>(jettyInfo_->packedHead));
}

template <int64_t kNumTimeoutCycles>
__aicore__ __forceinline__ void HcommJettyImpl::PollCqWhenSqOverflow()
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
__aicore__ __forceinline__ int32_t HcommJettyImpl::PollCq(uint32_t expectedTail)
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
__aicore__ __forceinline__ int32_t HcommJettyImpl::PostWrite(
    const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes,
    __gm__ void* notifyAddr, uint64_t notifyValue, uint32_t numWqebbs)
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
__aicore__ __forceinline__ int32_t HcommJettyImpl::PostWriteValue(
    const HcommPeer& peer, __gm__ void* dstAddr, T value, uint32_t numWqebbs)
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
__aicore__ __forceinline__ int32_t HcommJettyImpl::Drain()
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

__aicore__ __forceinline__ HcommJetty::HcommJetty(
    __ubuf__ HcommJettyInfo* jettyInfo, __ubuf__ uint8_t* ubufSqe,
    __gm__ void* jettyTable, uint32_t jettyIdx)
    : impl_(jettyInfo, ubufSqe, jettyTable, jettyIdx)
{}

template <int64_t kNumTimeoutCycles, bool kDoCommit>
__aicore__ __forceinline__ int32_t HcommJetty::Write(
    const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes)
{
    return impl_.PostWrite<kNumTimeoutCycles, false, kDoCommit>(
        peer, dstAddr, srcAddr, numBytes, nullptr, 0U, kNumWriteWqebbs);
}

template <int64_t kNumTimeoutCycles, typename T, bool kDoCommit>
__aicore__ __forceinline__ int32_t HcommJetty::WriteValue(
    const HcommPeer& peer, __gm__ void* dstAddr, T value)
{
    static_assert(sizeof(T) <= kNumWriteWqebbs * 64U - sizeof(HcommUrmaSqeCtx),
        "WriteValue value is too large for one WQEBB");
    return impl_.PostWriteValue<kNumTimeoutCycles, T, kDoCommit>(
        peer, dstAddr, value, kNumWriteWqebbs);
}

template <int64_t kNumTimeoutCycles>
__aicore__ __forceinline__ int32_t HcommJetty::WriteWithNotify(
    const HcommPeer& peer, __gm__ void* dstAddr, __gm__ void* srcAddr, uint64_t numBytes,
    __gm__ void* notifyAddr, uint64_t notifyValue)
{
    return impl_.PostWrite<kNumTimeoutCycles, true, true>(
        peer, dstAddr, srcAddr, numBytes, notifyAddr, notifyValue, kNumWriteWithNotifyWqebbs);
}

template <int64_t kNumTimeoutCycles>
__aicore__ __forceinline__ int32_t HcommJetty::Drain()
{
    return impl_.Drain<kNumTimeoutCycles>();
}

__aicore__ __forceinline__ void HcommJetty::AdvanceSq(uint32_t numWqebbs)
{
    impl_.AdvanceSq(numWqebbs);
}

} // namespace AscendC

#endif
