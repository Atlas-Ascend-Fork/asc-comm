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
 * \file hcomm_simt_urma.h
 * \brief Hcomm SIMT URMA implementation
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H

#include "hcomm_simt_urma_def.h"

#include "../../common/hcomm_simt_inner_def.h"
#include "../../common/hcomm_simt_utils.h"

namespace AscendC::simt {

// Bit layout of HcommUrmaSqeCtx::flag, matching the SIMD path's expression in
// HcommUrmaFillSqeCtx. URMA_DEFAULT_CFG evaluates to 0b00101101.
template <auto const& config>
__simt_callee__ inline uint32_t HcommSimtUrmaFlag()
{
    return (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
           ((config.cqe & 0x1U) << 5U) | ((config.inlineEn & 0x1U) << 6U);
}

constexpr uint32_t HCOMM_URMA_FLAG_CQE_BIT = 1U << 5U;

__simt_callee__ inline void HcommSimtClear128(__gm__ uint8_t* addr)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        ((__gm__ uint64_t*)addr)[i] = 0U;
    }
}

// Clears a single BB. Deferred posts occupy one BB, so clearing 128B would wipe the
// neighbouring slot another lane just reserved.
__simt_callee__ inline void HcommSimtClear64(__gm__ uint8_t* addr)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t); ++i) {
        ((__gm__ uint64_t*)addr)[i] = 0U;
    }
}

__simt_callee__ inline void HcommSimtCopy128(__gm__ uint8_t* dst, __gm__ uint8_t* src)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        ((__gm__ uint64_t*)dst)[i] = ((__gm__ uint64_t*)src)[i];
    }
}

template <auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillNopWqe(
    __gm__ uint8_t* wqeAddr, __gm__ const SqContext* sqCtx, uint32_t curHead, uint32_t nextHead)
{
    __gm__ HcommUrmaSqeCtx* sqeCtx = (__gm__ HcommUrmaSqeCtx*)wqeAddr;
    sqeCtx->sqeBbIdx = static_cast<uint16_t>(nextHead);
    sqeCtx->opcode = static_cast<uint32_t>(HcommUrmaOpCode::NOP);
    // NOP only pads the DWQE, so clear the CQE bit to keep one CQE per real WQE.
    sqeCtx->flag = HcommSimtUrmaFlag<config>() & ~HCOMM_URMA_FLAG_CQE_BIT;
    sqeCtx->owner = (curHead & sqCtx->contextInfo.ubJfs.sqDepth) == 0U ? 1U : 0U;
}

// Fills the SQE header of one BB. bbIdx is the head index of this BB and drives the owner bit;
// nextHead is the PI the hardware should advance to once this BB is consumed. SIMT must carry the
// PI in sqeBbIdx because the doorbell is a DWQE copy rather than a register write.
template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillSqeCtx(
    __gm__ uint8_t* bbAddr, __gm__ uint8_t* remoteAddr, __gm__ const SqContext* sqCtx,
    __gm__ const RegedBufferEntity* remoteMemInfo, uint32_t bbIdx, uint32_t nextHead)
{
    __gm__ HcommUrmaSqeCtx* sqeCtx = (__gm__ HcommUrmaSqeCtx*)bbAddr;
    sqeCtx->sqeBbIdx = static_cast<uint16_t>(nextHead);
    sqeCtx->opcode = static_cast<uint32_t>(opCode);
    sqeCtx->flag = HcommSimtUrmaFlag<config>();
    sqeCtx->nf = 0U;
    sqeCtx->tokenEn = 1U;
    sqeCtx->rmtJettyType = 1U;
    sqeCtx->owner = (bbIdx & sqCtx->contextInfo.ubJfs.sqDepth) == 0U ? 1U : 0U;
    sqeCtx->targetHint = 0U;
    sqeCtx->inlineMsgLen = 0U;
    sqeCtx->tpId = sqCtx->contextInfo.ubJfs.tpID;
    sqeCtx->sgeNum = sgeNum;
    sqeCtx->rmtJettyOrSegId = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    sqeCtx->rmtTokenValue = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;

    uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeCtx->rmtAddrLOrTokenId = static_cast<uint32_t>(remoteAddrValue & 0xFFFFFFFFU);
    sqeCtx->rmtAddrHOrTokenValue = static_cast<uint32_t>((remoteAddrValue >> 32U) & 0xFFFFFFFFU);
    sqeCtx->rmtEidL = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[0]);
    sqeCtx->rmtEidH = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[sizeof(uint64_t)]);
}

// Fills sgeNum SGEs after the SQE header. With sgeNum == 2 the payload is split in half so the
// 128B DWQE carries the data across both SGEs.
template <uint32_t sgeNum>
__simt_callee__ inline void HcommSimtUrmaFillSgeCtx(__gm__ uint8_t* bbAddr, __gm__ uint8_t* localAddr, uint64_t len)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    __gm__ HcommUrmaSgeCtx* sgeCtx = (__gm__ HcommUrmaSgeCtx*)(bbAddr + sizeof(HcommUrmaSqeCtx));
    if constexpr (sgeNum == 1U) {
        sgeCtx->len = static_cast<uint32_t>(len);
        sgeCtx->tokenId = 0U;
        sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);

        __gm__ HcommUrmaSgeCtx* lastSgeCtx = sgeCtx + 1;

        sgeCtx->len = firstLen;
        sgeCtx->tokenId = 0U;
        sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);

        lastSgeCtx->len = lastLen;
        lastSgeCtx->tokenId = 0U;
        lastSgeCtx->va = reinterpret_cast<uint64_t>(localAddr + firstLen);
    }
}

// Deferred submission: a single BB in the SQ, no doorbell.
template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillDeferredDataWqe(
    __gm__ uint8_t* slotAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ const SqContext* sqCtx, __gm__ const RegedBufferEntity* remoteMemInfo, uint32_t curHead)
{
    HcommSimtUrmaFillSqeCtx<opCode, 1U, config>(
        slotAddr, remoteAddr, sqCtx, remoteMemInfo, curHead, curHead + HCOMM_URMA_WQE_BB_CNT);
    HcommSimtUrmaFillSgeCtx<1U>(slotAddr, localAddr, len);
}

// Immediate submission: fills the 128B DWQE with the data split across two SGEs.
template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillDwqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ const SqContext* sqCtx, __gm__ const RegedBufferEntity* remoteMemInfo, uint32_t curHead, uint32_t nextHead)
{
    HcommSimtUrmaFillSqeCtx<opCode, 2U, config>(wqeAddr, remoteAddr, sqCtx, remoteMemInfo, curHead, nextHead);
    HcommSimtUrmaFillSgeCtx<2U>(wqeAddr, localAddr, len);
}

__simt_callee__ inline void HcommSimtUrmaFillNotifyCtx(
    __gm__ HcommUrmaNotifyCtx* notifyCtx, __gm__ const RegedBufferEntity* remoteMemInfo,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal)
{
    notifyCtx->notifyTokenId = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyAddrL = static_cast<uint32_t>(notifyAddrValue & 0xFFFFFFFFU);
    notifyCtx->notifyAddrH = static_cast<uint32_t>((notifyAddrValue >> 32U) & 0xFFFFFFFFU);
    notifyCtx->notifyDataL = static_cast<uint32_t>(notifyVal & 0xFFFFFFFFU);
    notifyCtx->notifyDataH = static_cast<uint32_t>((notifyVal >> 32U) & 0xFFFFFFFFU);
}

template <auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillWriteWithNotifyWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal, __gm__ const SqContext* sqCtx,
    __gm__ const RegedBufferEntity* remoteMemInfo, uint32_t curHead, uint32_t nextHead)
{
    HcommSimtClear128(wqeAddr);
    HcommSimtUrmaFillSqeCtx<HcommUrmaOpCode::WRITE_WITH_NOTIFY, 1U, config>(
        wqeAddr, remoteAddr, sqCtx, remoteMemInfo, curHead, nextHead);

    __gm__ HcommUrmaNotifyCtx* notifyCtx =
        reinterpret_cast<__gm__ HcommUrmaNotifyCtx*>(wqeAddr + sizeof(HcommUrmaSqeCtx));
    HcommSimtUrmaFillNotifyCtx(notifyCtx, remoteMemInfo, notifyAddr, notifyVal);

    __gm__ HcommUrmaSgeCtx* sgeCtx = reinterpret_cast<__gm__ HcommUrmaSgeCtx*>(
        reinterpret_cast<__gm__ uint8_t*>(notifyCtx) + sizeof(HcommUrmaNotifyCtx));
    sgeCtx->len = static_cast<uint32_t>(len);
    sgeCtx->tokenId = 0U;
    sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
}

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillAtomicWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    __gm__ const SqContext* sqCtx, __gm__ const RegedBufferEntity* remoteMemInfo, uint32_t curHead,
    uint32_t nextHead)
{
    HcommSimtClear128(wqeAddr);
    HcommSimtUrmaFillSqeCtx<opCode, 1U, config>(
        wqeAddr, remoteAddr, sqCtx, remoteMemInfo, curHead, nextHead);

    __gm__ HcommUrmaSgeCtx* sgeCtx =
        reinterpret_cast<__gm__ HcommUrmaSgeCtx*>(wqeAddr + sizeof(HcommUrmaSqeCtx));
    sgeCtx->len = sizeof(T);
    sgeCtx->tokenId = 0U;
    sgeCtx->va = reinterpret_cast<uint64_t>(fetchAddr);

    __gm__ uint8_t* amoDataAddr = reinterpret_cast<__gm__ uint8_t*>(sgeCtx) + sizeof(HcommUrmaSgeCtx);
    HcommSimtStoreValue(amoDataAddr, static_cast<uint64_t>(value), sizeof(T));
    if constexpr (opCode == HcommUrmaOpCode::CAS) {
        HcommSimtStoreValue(amoDataAddr + sizeof(T), static_cast<uint64_t>(cond), sizeof(T));
    }
}

__simt_callee__ inline void HcommSimtRingDwqe(__gm__ SqContext* sqCtx, __gm__ uint8_t* sqeAddr)
{
    asc_threadfence();
    __gm__ uint8_t* dwqeAddr = (__gm__ uint8_t*)(sqCtx->contextInfo.ubJfs.dbVa - HCOMM_URMA_DWQE_DB_OFFSET);
    HcommSimtCopy128(dwqeAddr, sqeAddr);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + 64);
}

__simt_callee__ inline void HcommSimtSyncBeforeRing(__gm__ uint64_t* headAddr, uint32_t nextHead)
{
    (void)headAddr;
    (void)nextHead;
    asc_syncthreads();
}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::HcommImpl() {}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::~HcommImpl() {}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    (void)buff;
    (void)len;
    return HCOMM_SUCCESS;
}

template <bool commit, auto commitPipe, auto reqPipe, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostSend(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len)
{
    (void)commitPipe;
    (void)reqPipe;

    // HcommUrmaSgeCtx::len is 32-bit.
    if (len > 0xFFFFFFFFULL) {
        return HCOMM_FAILED;
    }
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
    int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, len);
    if (remoteIdx == HCOMM_FAILED) {
        return HCOMM_FAILED;
    }

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = (__gm__ uint64_t*)sqCtx->contextInfo.ubJfs.headAddr;
    // Deferred post occupies one BB; immediate post occupies the two-BB DWQE.
    constexpr uint32_t bbCnt = commit ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;
    uint64_t headVal = HcommSimtReserve(headAddr, bbCnt, 1U);
    uint32_t curHead = HcommSimtHeadIdx(headVal);
    uint32_t nextHead = curHead + bbCnt;

    uint64_t sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
    uint32_t wqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqeAddr = (__gm__ uint8_t*)(sqBaseAddr + wqeSize * (curHead % sqDepth));

    if constexpr (commit) {
        HcommSimtUrmaFillDwqe<opCode, config>(
            sqeAddr, remoteAddr, localAddr, len, sqCtx, &remoteBuffers[remoteIdx], curHead, nextHead);
        HcommSimtSyncBeforeRing(headAddr, nextHead);
        HcommSimtRingDwqe(sqCtx, sqeAddr);
    } else {
        HcommSimtUrmaFillDeferredDataWqe<opCode, config>(
            sqeAddr, remoteAddr, localAddr, len, sqCtx, &remoteBuffers[remoteIdx], curHead);
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWriteWithNotify(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal)
{
    (void)commitPipe;
    (void)reqPipe;

    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
    int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, len);
    if (remoteIdx == HCOMM_FAILED) {
        return HCOMM_FAILED;
    }

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
    uint32_t curHead = 0U;
    if (hasReservedDwqeSlot_) {
        curHead = reservedDwqeHead_;
        hasReservedDwqeSlot_ = false;
    } else {
        curHead = HcommSimtHeadIdx(HcommSimtReserve(headAddr, HCOMM_URMA_DWQE_BB_CNT, 1U));
    }
    uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;

    uint64_t sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
    uint32_t wqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));

    HcommSimtUrmaFillWriteWithNotifyWqe<config>(
        sqeAddr, remoteAddr, localAddr, len, notifyAddr, notifyVal, sqCtx, &remoteBuffers[remoteIdx], curHead,
        nextHead);
    if constexpr (commit) {
        HcommSimtSyncBeforeRing(headAddr, nextHead);
        if (PublishDwqeBatch(headAddr) != HCOMM_SUCCESS) {
            return HCOMM_FAILED;
        }
        HcommSimtRingDwqe(sqCtx, sqeAddr);
    } else {
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostAtomic(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond)
{
    (void)commitPipe;
    (void)reqPipe;

    static_assert(sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t),
        "Atomic operation only supports 32-bit or 64-bit data types");
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
    int32_t remoteIdx =
        HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, sizeof(T));

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
    uint32_t curHead = 0U;
    if (hasReservedDwqeSlot_) {
        curHead = reservedDwqeHead_;
        hasReservedDwqeSlot_ = false;
    } else {
        curHead = HcommSimtHeadIdx(HcommSimtReserve(headAddr, HCOMM_URMA_DWQE_BB_CNT, 1U));
    }
    uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;

    uint64_t sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
    uint32_t wqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));

    HcommSimtUrmaFillAtomicWqe<opCode, T, config>(
        sqeAddr, remoteAddr, fetchAddr, value, cond, sqCtx, &remoteBuffers[remoteIdx], curHead, nextHead);
    if constexpr (commit) {
        HcommSimtSyncBeforeRing(headAddr, nextHead);
        if (PublishDwqeBatch(headAddr) != HCOMM_SUCCESS) {
            return HCOMM_FAILED;
        }
        HcommSimtRingDwqe(sqCtx, sqeAddr);
    } else {
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

// Aggregate fixed 2-BB/1-CQE DWQE WQEs in shared UB and assign their slots
// from a common head snapshot. Check whether this group crosses the physical
// SQ ring before touching the shared batch allocator. Crossing groups keep the
// original per-WQE reservation order used by the SIMT posting path.
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReserveDwqeBatch(
    ChannelHandle channel, __ubuf__ uint32_t* state, bool active,
    uint32_t activeWqeCnt, uint32_t commitLane)
{
    constexpr uint32_t kBbCnt = 0U;
    constexpr uint32_t kCqeCnt = 1U;
    constexpr uint32_t kHeadLo = 2U;
    constexpr uint32_t kHeadHi = 3U;
    constexpr uint32_t kStatus = 4U;
    constexpr uint32_t kUseLegacyPath = 1U;
    uint32_t linearLane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    if (linearLane == 0U) {
        state[kBbCnt] = 0U;
        state[kCqeCnt] = 0U;
        state[kHeadLo] = 0U;
        state[kHeadHi] = 0U;
        state[kStatus] = HCOMM_SUCCESS;
    }
    asc_syncthreads();

    // This check must precede the shared UB atomic allocator. When the group
    // wraps the SQ ring, subsequent Post calls must be indistinguishable from
    // the original per-WQE path.
    if (linearLane == 0U) {
        __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
        __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
        __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
        __gm__ uint64_t* headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
        __gm__ uint32_t* sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(sqCtx->contextInfo.ubJfs.tailAddr);
        uint64_t headVal = *headAddr;
        uint32_t curHead = HcommSimtHeadIdx(headVal);
        uint32_t sqTail = *sqTailAddr;
        uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
        uint32_t batchBbCnt = activeWqeCnt * HCOMM_URMA_DWQE_BB_CNT;
        if (sqDepth == 0U || curHead - sqTail + batchBbCnt > sqDepth) {
            state[kStatus] = HCOMM_FAILED;
        } else if ((curHead % sqDepth) + batchBbCnt > sqDepth) {
            state[kStatus] = kUseLegacyPath;
        } else {
            state[kHeadLo] = curHead;
            state[kHeadHi] = HcommSimtWqeCnt(headVal);
        }
    }
    asc_syncthreads();

    if (state[kStatus] == kUseLegacyPath) {
        if (active) {
            hasReservedDwqeSlot_ = false;
            hasDeferredDwqeBatch_ = false;
        }
        return HCOMM_SUCCESS;
    }
    if (state[kStatus] != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }

    uint32_t bbOffset = 0U;
    if (active && linearLane != commitLane) {
        bbOffset = asc_atomic_add(&state[kBbCnt], HCOMM_URMA_DWQE_BB_CNT);
        (void)asc_atomic_add(&state[kCqeCnt], 1U);
    }
    asc_syncthreads();

    if (active && linearLane == commitLane) {
        bbOffset = state[kBbCnt];
        state[kBbCnt] += HCOMM_URMA_DWQE_BB_CNT;
        state[kCqeCnt] += 1U;
    }
    asc_syncthreads();

    if (active) {
        reservedDwqeHead_ = state[kHeadLo] + bbOffset;
        hasReservedDwqeSlot_ = true;
        hasDeferredDwqeBatch_ = true;
        deferredDwqeHeadVal_ = static_cast<uint64_t>(state[kHeadLo]) |
            (static_cast<uint64_t>(state[kHeadHi]) << 32U);
        deferredDwqeBbCnt_ = state[kBbCnt];
        deferredDwqeCqeCnt_ = state[kCqeCnt];
    }
    asc_syncthreads();
    return HCOMM_SUCCESS;
}

// The batch head is published only after every deferred lane has fenced its
// SQ write and reached the final commit barrier.
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PublishDwqeBatch(__gm__ uint64_t* headAddr)
{
    if (!hasDeferredDwqeBatch_) {
        return HCOMM_SUCCESS;
    }
    (void)deferredDwqeHeadVal_;
    (void)HcommSimtReserve(headAddr, deferredDwqeBbCnt_, deferredDwqeCqeCnt_);
    hasDeferredDwqeBatch_ = false;
    return HCOMM_SUCCESS;
}

__simt_callee__ inline uint32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCq(ChannelHandle channel, uint32_t expectIdx)
{
    if (expectIdx == 0U) {
        return HCOMM_SUCCESS;
    }
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ CqContext* cqContexts = reinterpret_cast<__gm__ CqContext*>(channelEntity->cqContextAddr);
    __gm__ CqContext* cqCtx = &cqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* tailAddr = (__gm__ uint32_t*)cqCtx->contextInfo.ubJfc.tailAddr;
    uint32_t curTail = *tailAddr;

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* sqTailAddr = (__gm__ uint32_t*)sqCtx->contextInfo.ubJfs.tailAddr;
    uint32_t sqTail = *sqTailAddr;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;

    uint64_t cqBaseAddr = cqCtx->contextInfo.ubJfc.scqVa;
    uint32_t cqeSize = cqCtx->contextInfo.ubJfc.cqeSize;
    uint32_t cqDepth = cqCtx->contextInfo.ubJfc.cqDepth;
    while (curTail != expectIdx) {
        __gm__ HcommUrmaJfcCqeCtx* cqeAddr = (__gm__ HcommUrmaJfcCqeCtx*)(cqBaseAddr + cqeSize * (curTail % cqDepth));
        uint32_t validOwner = (curTail / cqDepth) & 1U;
        uint32_t retry = 0U;
        while (((validOwner ^ cqeAddr->owner) == 0U) && retry < HCOMM_SIMT_MAX_CQ_RETRY) {
            asc_threadfence();
            asc_dcci_single(cqeAddr);
            ++retry;
        }
        if (retry >= HCOMM_SIMT_MAX_CQ_RETRY) {
            // No CQE arrived: the WQE was never consumed.
            return 0xFFU;
        }
        uint8_t status = cqeAddr->status & 0xFFU;
        uint8_t subStatus = cqeAddr->substatus & 0xFFU;
        if (status != 0U || subStatus != 0U) {
            constexpr uint8_t statusShift = 8U;
            return (static_cast<uint32_t>(status) << statusShift) | subStatus;
        }
        if (sqDepth == 0U) {
            return 0xFFU;
        }
        uint32_t sqTailSlot = sqTail % sqDepth;
        uint32_t completedSlot = cqeAddr->entryIdx % sqDepth;
        sqTail += (completedSlot + sqDepth - sqTailSlot) % sqDepth + 1U;
        ++curTail;
    }

    *tailAddr = curTail;
    __gm__ uint32_t* cqDbAddr = (__gm__ uint32_t*)cqCtx->contextInfo.ubJfc.dbVa;
    *cqDbAddr = curTail & 0xFFFFFFU;

    *sqTailAddr = sqTail;
    return HCOMM_SUCCESS;
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len);
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    (void)channel;
    (void)dst;
    (void)value;
    return 0;
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::READ, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len);
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return PostWriteWithNotify<commit, commitPipe, reqPipe, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
        reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal);
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return PostAtomic<T, commit, commitPipe, reqPipe, HcommUrmaOpCode::FAA, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
        static_cast<T>(0));
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return PostAtomic<T, commit, commitPipe, reqPipe, HcommUrmaOpCode::CAS, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal,
        compareVal);
}

template <auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(ChannelHandle channel)
{
    (void)pipe;
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = (__gm__ uint64_t*)sqCtx->contextInfo.ubJfs.headAddr;

    // wqeCnt counts only CQE-producing WQEs, so it is directly comparable with the CQ tail.
    uint64_t headVal = *headAddr;
    uint32_t wqeCnt = HcommSimtWqeCnt(headVal);
    uint32_t pollRet = PollCq(channel, wqeCnt);
    return pollRet == HCOMM_SUCCESS ? HCOMM_SUCCESS : HCOMM_FAILED;
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif
