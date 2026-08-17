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

// A WQE that occupies a single basic block must not write past it: the next block may
// already hold another lane's WQE.
template <uint32_t byteSize>
__simt_callee__ inline void HcommSimtCopyUbToGm(__gm__ uint8_t* dst, __ubuf__ uint8_t* src)
{
    static_assert(byteSize % sizeof(uint64_t) == 0U, "UB WQE copy must use whole words");
    static_assert(byteSize <= HCOMM_URMA_DWQE_SIZE, "UB WQE copy must stay inside the DWQE window");
#pragma unroll
    for (uint32_t i = 0; i < byteSize / sizeof(uint64_t); ++i) {
        asc_stwt(((__gm__ uint64_t*)dst) + i, ((__ubuf__ uint64_t*)src)[i]);
    }
}

template <uint32_t bbCnt>
__simt_callee__ inline void HcommSimtCopyUbWqeToSq(
    uint64_t sqBaseAddr, __gm__ uint8_t* sqeAddr, uint32_t sqDepth, uint32_t curHead, __ubuf__ uint8_t* src)
{
    static_assert(
        bbCnt == HCOMM_URMA_WQE_BB_CNT || bbCnt == HCOMM_URMA_DWQE_BB_CNT,
        "SIMT SQ copy supports one-BB or two-BB WQEs");
    if constexpr (bbCnt == HCOMM_URMA_DWQE_BB_CNT) {
        // A logical 2-BB WQE remains contiguous across the physical SQ boundary.
        if ((curHead % sqDepth) == sqDepth - 1U) {
            HcommSimtCopyUbToGm<HCOMM_URMA_WQE_BB_SIZE>(sqeAddr, src);
            HcommSimtCopyUbToGm<HCOMM_URMA_WQE_BB_SIZE>(
                reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr), src + HCOMM_URMA_WQE_BB_SIZE);
            return;
        }
    }
    HcommSimtCopyUbToGm<bbCnt * HCOMM_URMA_WQE_BB_SIZE>(sqeAddr, src);
}

template <bool commit>
__simt_callee__ inline bool HcommSimtTryReservePost(
    const HcommSimtUbResolvedPost& post, uint32_t bbCnt, uint64_t& headVal)
{
    // SIMT has no standalone Commit API, so a deferred post must leave enough SQ
    // space for the immediate 2-BB DWQE that will publish the accumulated batch.
    uint32_t requiredFreeBbCnt = bbCnt + (commit ? 0U : HCOMM_URMA_DWQE_BB_CNT);
    return HcommSimtTryReserve(
        post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, 1U, headVal);
}

template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbSqeCtx(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, const HcommSimtUbPostMeta& meta, uint32_t curHead,
    uint32_t nextHead)
{
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeAddr;
    sqeCtx->sqeBbIdx = static_cast<uint16_t>(nextHead);
    sqeCtx->opcode = static_cast<uint32_t>(opCode);
    sqeCtx->flag = HcommSimtUrmaFlag<config>();
    sqeCtx->tokenEn = 1U;
    sqeCtx->rmtJettyType = 1U;
    sqeCtx->owner = (curHead & meta.sqDepth) == 0U ? 1U : 0U;
    sqeCtx->tpId = meta.tpId;
    sqeCtx->sgeNum = sgeNum;
    sqeCtx->rmtJettyOrSegId = meta.remoteTokenId;
    sqeCtx->rmtTokenValue = meta.remoteTokenValue;

    uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeCtx->rmtAddrLOrTokenId = static_cast<uint32_t>(remoteAddrValue & 0xFFFFFFFFU);
    sqeCtx->rmtAddrHOrTokenValue = static_cast<uint32_t>((remoteAddrValue >> 32U) & 0xFFFFFFFFU);
    sqeCtx->rmtEidL = meta.remoteEidL;
    sqeCtx->rmtEidH = meta.remoteEidH;
}

template <auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbWriteWithNotifyWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal, const HcommSimtUbPostMeta& meta, uint32_t curHead,
    uint32_t nextHead)
{
    HcommSimtUrmaFillUbSqeCtx<HcommUrmaOpCode::WRITE_WITH_NOTIFY, 1U, config>(
        wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaNotifyCtx* notifyCtx = (__ubuf__ HcommUrmaNotifyCtx*)(wqeAddr + sizeof(HcommUrmaSqeCtx));
    uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyTokenId = meta.remoteTokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = meta.remoteTokenValue;
    notifyCtx->notifyAddrL = static_cast<uint32_t>(notifyAddrValue & 0xFFFFFFFFU);
    notifyCtx->notifyAddrH = static_cast<uint32_t>((notifyAddrValue >> 32U) & 0xFFFFFFFFU);
    notifyCtx->notifyDataL = static_cast<uint32_t>(notifyVal & 0xFFFFFFFFU);
    notifyCtx->notifyDataH = static_cast<uint32_t>((notifyVal >> 32U) & 0xFFFFFFFFU);

    __ubuf__ HcommUrmaSgeCtx* sgeCtx =
        (__ubuf__ HcommUrmaSgeCtx*)((__ubuf__ uint8_t*)notifyCtx + sizeof(HcommUrmaNotifyCtx));
    sgeCtx->len = static_cast<uint32_t>(len);
    sgeCtx->tokenId = 0U;
    sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
}

// Write/Read carry no notify or AMO tail, so the SGEs directly follow the SQE header.
// sgeNum == 2 splits the payload so an immediate post fills the whole 128B DWQE.
template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbDataWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    HcommSimtUrmaFillUbSqeCtx<opCode, sgeNum, config>(wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)(wqeAddr + sizeof(HcommUrmaSqeCtx));
    if constexpr (sgeNum == 1U) {
        sgeCtx->len = static_cast<uint32_t>(len);
        sgeCtx->tokenId = 0U;
        sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);
        __ubuf__ HcommUrmaSgeCtx* lastSgeCtx = sgeCtx + 1;

        sgeCtx->len = firstLen;
        sgeCtx->tokenId = 0U;
        sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);

        lastSgeCtx->len = lastLen;
        lastSgeCtx->tokenId = 0U;
        lastSgeCtx->va = reinterpret_cast<uint64_t>(localAddr + firstLen);

        // An immediate data post copies the entire 128-byte DWQE window. The
        // unused tail must not retain fields from an earlier WQE.
#pragma unroll
        for (uint32_t i = 10U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
            reinterpret_cast<__ubuf__ uint64_t*>(wqeAddr)[i] = 0U;
        }
    }
}

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbAtomicWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead)
{
    HcommSimtUrmaFillUbSqeCtx<opCode, 1U, config>(wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)(wqeAddr + sizeof(HcommUrmaSqeCtx));
    sgeCtx->len = sizeof(T);
    sgeCtx->tokenId = 0U;
    sgeCtx->va = reinterpret_cast<uint64_t>(fetchAddr);

    // Always overwrite 16B: FAA/CAS and uint32_t/uint64_t can reuse one slot safely.
    __ubuf__ T* amoData = (__ubuf__ T*)((__ubuf__ uint8_t*)sgeCtx + sizeof(HcommUrmaSgeCtx));
    amoData[0] = value;
    amoData[1] = opCode == HcommUrmaOpCode::CAS ? cond : static_cast<T>(0);
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        amoData[2] = 0U;
        amoData[3] = 0U;
    }
}

union alignas(sizeof(uint64_t)) HcommSimtLocalWqeStage {
    uint64_t words[HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t)];
};
static_assert(
    sizeof(HcommSimtLocalWqeStage) == HCOMM_URMA_DWQE_SIZE,
    "SIMT lane-private WQE stage must match the 128-byte DWQE window");

__simt_callee__ inline void HcommSimtStoreLocalWqe(__gm__ uint8_t* dst, const HcommSimtLocalWqeStage& stage)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[i];
    }
}

__simt_callee__ inline void HcommSimtStoreLocalWqeBb(
    __gm__ uint8_t* dst, const HcommSimtLocalWqeStage& stage, uint32_t srcWordOffset)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[srcWordOffset + i];
    }
}

__simt_callee__ inline void HcommSimtStoreLocalWqeToSq(
    uint64_t sqBaseAddr, __gm__ uint8_t* sqeAddr, uint32_t sqDepth, uint32_t curHead,
    const HcommSimtLocalWqeStage& stage)
{
    if ((curHead % sqDepth) != sqDepth - 1U) {
        HcommSimtStoreLocalWqe(sqeAddr, stage);
        return;
    }

    constexpr uint32_t wordsPerBb = HCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t);
    HcommSimtStoreLocalWqeBb(sqeAddr, stage, 0U);
    HcommSimtStoreLocalWqeBb(reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr), stage, wordsPerBb);
}

__simt_callee__ inline void HcommSimtStoreLocalDwqeStwt16(__gm__ uint8_t* dst, const HcommSimtLocalWqeStage& stage)
{
    __gm__ ulonglong2* dstWords = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        uint32_t wordIdx = i * 2U;
        asc_stwt(&dstWords[i], make_ulonglong2(stage.words[wordIdx], stage.words[wordIdx + 1U]));
    }
}

__simt_callee__ inline void HcommSimtCopyUb128ToDwqeStwt16(__gm__ uint8_t* dst, __ubuf__ uint8_t* src)
{
    __ubuf__ uint64_t* srcWords = reinterpret_cast<__ubuf__ uint64_t*>(src);
    __gm__ ulonglong2* dstWords = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        uint32_t wordIdx = i * 2U;
        asc_stwt(&dstWords[i], make_ulonglong2(srcWords[wordIdx], srcWords[wordIdx + 1U]));
    }
}

template <uint32_t sgeNum>
__simt_callee__ inline void HcommSimtBuildPackedSqeTemplate(
    const HcommSimtUbPostMeta& meta, HcommSimtPackedSqeTemplate& sqeTemplate)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    sqeTemplate.word1 = static_cast<uint64_t>(meta.tpId & 0xFFFFFFU) | (static_cast<uint64_t>(sgeNum) << 24U) |
                        (static_cast<uint64_t>(meta.remoteTokenId & 0xFFFFFU) << 32U);
    sqeTemplate.remoteEidL = meta.remoteEidL;
    sqeTemplate.remoteEidH = meta.remoteEidH;
    sqeTemplate.tokenWord = meta.remoteTokenValue;
    sqeTemplate.sqDepth = meta.sqDepth;
}

template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtStoreLocalSqeHeader(
    uint64_t* words, __gm__ uint8_t* remoteAddr, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead,
    uint32_t nextHead)
{
    uint64_t owner = (curHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(nextHead & 0xFFFFU) |
               (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) | (1ULL << 28U) | (1ULL << 29U) |
               (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U);
    words[1] = sqeTemplate.word1;
    words[2] = sqeTemplate.remoteEidL;
    words[3] = sqeTemplate.remoteEidH;
    words[4] = sqeTemplate.tokenWord;
    words[5] = reinterpret_cast<uint64_t>(remoteAddr);
}

template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtBuildLocalDataWqe(
    HcommSimtLocalWqeStage& stage, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    HcommSimtStoreLocalSqeHeader<opCode, config>(stage.words, remoteAddr, sqeTemplate, curHead, nextHead);
    if constexpr (sgeNum == 1U) {
        stage.words[6] = static_cast<uint64_t>(static_cast<uint32_t>(len));
        stage.words[7] = reinterpret_cast<uint64_t>(localAddr);
#pragma unroll
        for (uint32_t i = 8U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
            stage.words[i] = 0U;
        }
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);
        stage.words[6] = static_cast<uint64_t>(firstLen);
        stage.words[7] = reinterpret_cast<uint64_t>(localAddr);
        stage.words[8] = static_cast<uint64_t>(lastLen);
        stage.words[9] = reinterpret_cast<uint64_t>(localAddr + firstLen);
#pragma unroll
        for (uint32_t i = 10U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
            stage.words[i] = 0U;
        }
    }
}

__simt_callee__ inline void HcommSimtPublishLocalWqe(
    uint64_t sqBaseAddr, __gm__ uint8_t* sqeAddr, uint32_t sqDepth, uint32_t curHead, __gm__ uint8_t* dwqeAddr,
    const HcommSimtLocalWqeStage& stage)
{
    HcommSimtStoreLocalWqeToSq(sqBaseAddr, sqeAddr, sqDepth, curHead, stage);
    asc_threadfence();
    HcommSimtStoreLocalDwqeStwt16(dwqeAddr, stage);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
}

__simt_callee__ inline void HcommSimtRingDwqeFromUbStwt16(__gm__ uint8_t* dwqeAddr, __ubuf__ uint8_t* wqeAddr)
{
    asc_threadfence();
    HcommSimtCopyUb128ToDwqeStwt16(dwqeAddr, wqeAddr);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::HcommImpl() {}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::~HcommImpl() {}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    uint32_t linearLane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    uint64_t laneCount = static_cast<uint64_t>(blockDim.x) * blockDim.y * blockDim.z;
    uint64_t wqeBytes = laneCount * HCOMM_URMA_DWQE_SIZE;
    uint64_t cacheRequired = ((wqeBytes + laneCount * sizeof(HcommSimtUbRemoteCache) + HCOMM_URMA_DWQE_SIZE - 1U) &
                              ~(static_cast<uint64_t>(HCOMM_URMA_DWQE_SIZE) - 1U)) +
                             HCOMM_SIMT_UB_POST_CONTEXT_BYTES;

    wqeItem_ = nullptr;
    ubPostContext_ = nullptr;
    ubRemoteCache_ = nullptr;
    ubPostContextReady_ = false;
    if (buff == nullptr || wqeBytes > len) {
        return HCOMM_FAILED;
    }
    wqeItem_ = buff + linearLane * HCOMM_URMA_DWQE_SIZE;

    if (cacheRequired <= len) {
        __ubuf__ uint8_t* remoteCacheBase = buff + wqeBytes;
        ubRemoteCache_ = reinterpret_cast<__ubuf__ HcommSimtUbRemoteCache*>(remoteCacheBase) + linearLane;
        ubRemoteCache_->valid = 0U;

        uint64_t contextOffset = (wqeBytes + laneCount * sizeof(HcommSimtUbRemoteCache) + HCOMM_URMA_DWQE_SIZE - 1U) &
                                 ~(static_cast<uint64_t>(HCOMM_URMA_DWQE_SIZE) - 1U);
        ubPostContext_ = reinterpret_cast<__ubuf__ HcommSimtUbPostContext*>(buff + contextOffset);
        if (linearLane == 0U) {
            ubPostContext_->channel = 0U;
            ubPostContext_->state = HCOMM_SIMT_UB_CONTEXT_EMPTY;
        }
        asc_syncthreads();
    }
    return HCOMM_SUCCESS;
}

__simt_callee__ inline void HcommImpl<COMM_PROTOCOL_UBC_CTP>::InitUbPostContext(ChannelHandle channel)
{
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    ubPostContext_->remoteBuffersAddr = channelEntity->remoteBufferAddr;
    ubPostContext_->remoteBufferNum = channelEntity->remoteBufferNum;

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    ubPostContext_->headAddr = sqCtx->contextInfo.ubJfs.headAddr;
    ubPostContext_->sqTailAddr = sqCtx->contextInfo.ubJfs.tailAddr;
    ubPostContext_->sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
    ubPostContext_->sqWqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    ubPostContext_->sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    ubPostContext_->tpId = sqCtx->contextInfo.ubJfs.tpID;
    ubPostContext_->remoteEidL = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[0]);
    ubPostContext_->remoteEidH = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[sizeof(uint64_t)]);
    ubPostContext_->dwqeAddr = sqCtx->contextInfo.ubJfs.dbVa - HCOMM_URMA_DWQE_DB_OFFSET;
    ubPostContext_->channel = channel;
}

__simt_callee__ inline bool HcommImpl<COMM_PROTOCOL_UBC_CTP>::EnsureUbPostContext(ChannelHandle channel)
{
    if (ubPostContext_ == nullptr) {
        return false;
    }
    if (ubPostContextReady_) {
        return ubPostContext_->channel == channel;
    }

    __ubuf__ uint32_t* stateAddr = &ubPostContext_->state;
    uint32_t state = asc_atomic_cas(stateAddr, HCOMM_SIMT_UB_CONTEXT_READY, HCOMM_SIMT_UB_CONTEXT_READY);
    if (state == HCOMM_SIMT_UB_CONTEXT_READY) {
        ubPostContextReady_ = ubPostContext_->channel == channel;
        return ubPostContextReady_;
    }

    uint32_t previous = asc_atomic_cas(stateAddr, HCOMM_SIMT_UB_CONTEXT_EMPTY, HCOMM_SIMT_UB_CONTEXT_INITIALIZING);
    if (previous == HCOMM_SIMT_UB_CONTEXT_EMPTY) {
        InitUbPostContext(channel);
        asc_threadfence_block();
        (void)asc_atomic_exch(stateAddr, HCOMM_SIMT_UB_CONTEXT_READY);
        ubPostContextReady_ = true;
        return true;
    }

    // Avoid spinning in a potentially diverged branch; this post can use the GM fallback.
    return false;
}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ResolveUbPostContext(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, uint64_t len, HcommSimtUbResolvedPost& post)
{
    __gm__ RegedBufferEntity* remoteBuffers = nullptr;
    uint32_t remoteBufferNum = 0U;
    __gm__ SqContext* sqCtx = nullptr;
    bool useUbPostContext = EnsureUbPostContext(channel);
    if (useUbPostContext) {
        remoteBuffers = reinterpret_cast<__gm__ RegedBufferEntity*>(ubPostContext_->remoteBuffersAddr);
        remoteBufferNum = ubPostContext_->remoteBufferNum;
    } else {
        __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
        remoteBuffers = reinterpret_cast<__gm__ RegedBufferEntity*>(channelEntity->remoteBufferAddr);
        remoteBufferNum = channelEntity->remoteBufferNum;
        __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
        sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    }

    uint32_t remoteTokenId = 0U;
    uint32_t remoteTokenValue = 0U;
    bool tokenResolved = false;
    uint64_t targetAddr = reinterpret_cast<uint64_t>(remoteAddr);
    if (useUbPostContext && ubRemoteCache_ != nullptr && ubRemoteCache_->valid != 0U &&
        targetAddr >= ubRemoteCache_->baseAddr) {
        uint64_t offset = targetAddr - ubRemoteCache_->baseAddr;
        if (offset <= ubRemoteCache_->size && len <= ubRemoteCache_->size - offset) {
            remoteTokenId = ubRemoteCache_->tokenId;
            remoteTokenValue = ubRemoteCache_->tokenValue;
            tokenResolved = true;
        }
    }

    if (!tokenResolved) {
        int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, remoteBufferNum, remoteAddr, len);
        if (remoteIdx == HCOMM_FAILED) {
            return HCOMM_FAILED;
        }

        __gm__ RegedBufferEntity* remoteMemInfo = &remoteBuffers[remoteIdx];
        remoteTokenId = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
        remoteTokenValue = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        if (useUbPostContext && ubRemoteCache_ != nullptr) {
            ubRemoteCache_->baseAddr = remoteMemInfo->bufferInfo.rma.addr;
            ubRemoteCache_->size = remoteMemInfo->bufferInfo.rma.size;
            ubRemoteCache_->tokenId = remoteTokenId;
            ubRemoteCache_->tokenValue = remoteTokenValue;
            ubRemoteCache_->valid = 1U;
        }
    }

    if (useUbPostContext) {
        post.headAddr = reinterpret_cast<__gm__ uint64_t*>(ubPostContext_->headAddr);
        post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(ubPostContext_->sqTailAddr);
        post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(ubPostContext_->dwqeAddr);
        post.sqBaseAddr = ubPostContext_->sqBaseAddr;
        post.sqWqeSize = ubPostContext_->sqWqeSize;
        post.meta.sqDepth = ubPostContext_->sqDepth;
        post.meta.tpId = ubPostContext_->tpId;
        post.meta.remoteEidL = ubPostContext_->remoteEidL;
        post.meta.remoteEidH = ubPostContext_->remoteEidH;
    } else {
        post.headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
        post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(sqCtx->contextInfo.ubJfs.tailAddr);
        post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(sqCtx->contextInfo.ubJfs.dbVa - HCOMM_URMA_DWQE_DB_OFFSET);
        post.sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
        post.sqWqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
        post.meta.sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
        post.meta.tpId = sqCtx->contextInfo.ubJfs.tpID;
        post.meta.remoteEidL = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[0]);
        post.meta.remoteEidH = HcommSimtLoadLe64(&sqCtx->contextInfo.ubJfs.remoteEID[sizeof(uint64_t)]);
    }
    post.meta.remoteTokenId = remoteTokenId;
    post.meta.remoteTokenValue = remoteTokenValue;
    return HCOMM_SUCCESS;
}

template <bool commit>
__simt_callee__ inline bool HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReservePost(
    ChannelHandle channel, const HcommSimtUbResolvedPost& post, uint32_t bbCnt, uint64_t& headVal)
{
    uint32_t requiredFreeBbCnt = bbCnt + (commit ? 0U : HCOMM_URMA_DWQE_BB_CNT);
    if (post.meta.sqDepth == 0U || requiredFreeBbCnt > post.meta.sqDepth) {
        return false;
    }

    if (HcommSimtTryReservePost<commit>(post, bbCnt, headVal)) {
        return true;
    }

    // The fast path found no SQ space. Consume one completed CQE at a time so
    // PollCq can advance sqTail, then retry the reservation. Waiting for all
    // submitted WQEs is unsafe because wqeCnt may include deferred WQEs that a
    // later immediate DWQE has not published yet.
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ CqContext* cqContexts = reinterpret_cast<__gm__ CqContext*>(channelEntity->cqContextAddr);
    __gm__ CqContext* cqCtx = &cqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* cqTailAddr = reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.tailAddr);
    while (true) {
        uint32_t cqTail = *cqTailAddr;
        if (cqTail == HcommSimtWqeCnt(headVal) || PollCq(channel, cqTail + 1U) != HCOMM_SUCCESS) {
            return false;
        }
        if (HcommSimtTryReservePost<commit>(post, bbCnt, headVal)) {
            return true;
        }
    }
}

template <bool commit, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostSend(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len)
{
    if (wqeItem_ == nullptr) {
        return HCOMM_FAILED;
    }

    HcommSimtUbResolvedPost post;
    if (ResolveUbPostContext(channel, remoteAddr, len, post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    // A deferred Write/Read needs a single SGE in one BB. An immediate one is published
    // through the 128B DWQE window, so the payload is split across two SGEs to fill it.
    constexpr uint32_t sgeNum = commit ? 2U : 1U;
    constexpr uint32_t bbCnt = commit ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;

    uint64_t headVal = 0U;
    if (!ReservePost<commit>(channel, post, bbCnt, headVal)) {
        return HCOMM_FAILED;
    }
    uint32_t curHead = HcommSimtHeadIdx(headVal);
    uint32_t nextHead = curHead + bbCnt;

    uint64_t sqBaseAddr = post.sqBaseAddr;
    uint32_t wqeSize = post.sqWqeSize;
    uint32_t sqDepth = post.meta.sqDepth;
    __gm__ uint8_t* sqeAddr = reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));

    HcommSimtUrmaFillUbDataWqe<opCode, sgeNum, config>(
        wqeItem_, remoteAddr, localAddr, len, post.meta, curHead, nextHead);
    HcommSimtCopyUbWqeToSq<bbCnt>(sqBaseAddr, sqeAddr, sqDepth, curHead, wqeItem_);
    if constexpr (commit) {
        HcommSimtRingDwqeFromUbStwt16(post.dwqeAddr, wqeItem_);
    }
    asc_threadfence();
    return HCOMM_SUCCESS;
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWriteWithNotify(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal)
{
    if (wqeItem_ == nullptr) {
        return HCOMM_FAILED;
    }

    HcommSimtUbResolvedPost post;
    if (ResolveUbPostContext(channel, remoteAddr, len, post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    uint64_t headVal = 0U;
    if (!ReservePost<commit>(channel, post, HCOMM_URMA_DWQE_BB_CNT, headVal)) {
        return HCOMM_FAILED;
    }
    uint32_t curHead = HcommSimtHeadIdx(headVal);
    uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;

    uint64_t sqBaseAddr = post.sqBaseAddr;
    uint32_t wqeSize = post.sqWqeSize;
    uint32_t sqDepth = post.meta.sqDepth;
    __gm__ uint8_t* sqeAddr = reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));
    HcommSimtUrmaFillUbWriteWithNotifyWqe<config>(
        wqeItem_, remoteAddr, localAddr, len, notifyAddr, notifyVal, post.meta, curHead, nextHead);
    HcommSimtCopyUbWqeToSq<HCOMM_URMA_DWQE_BB_CNT>(sqBaseAddr, sqeAddr, sqDepth, curHead, wqeItem_);
    if constexpr (commit) {
        HcommSimtRingDwqeFromUbStwt16(post.dwqeAddr, wqeItem_);
    }
    asc_threadfence();
    return HCOMM_SUCCESS;
}

template <typename T, bool commit, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostAtomic(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond)
{
    static_assert(
        sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t),
        "Atomic operation only supports 32-bit or 64-bit data types");
    if (wqeItem_ == nullptr) {
        return HCOMM_FAILED;
    }
    HcommSimtUbResolvedPost post;
    if (ResolveUbPostContext(channel, remoteAddr, sizeof(T), post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    uint64_t headVal = 0U;
    if (!ReservePost<commit>(channel, post, HCOMM_URMA_DWQE_BB_CNT, headVal)) {
        return HCOMM_FAILED;
    }
    uint32_t curHead = HcommSimtHeadIdx(headVal);
    uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;

    uint64_t sqBaseAddr = post.sqBaseAddr;
    uint32_t wqeSize = post.sqWqeSize;
    uint32_t sqDepth = post.meta.sqDepth;
    __gm__ uint8_t* sqeAddr = reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));
    // Keep the complete Atomic WQE in the explicit per-lane UB slot. Publishing a
    // lane-local C++ object with ordinary stores can lose the operand words even
    // though the WQE header and fetch SGE are consumed successfully.
    HcommSimtUrmaFillUbAtomicWqe<opCode, T, config>(
        wqeItem_, remoteAddr, fetchAddr, value, cond, post.meta, curHead, nextHead);
    HcommSimtCopyUbWqeToSq<HCOMM_URMA_DWQE_BB_CNT>(sqBaseAddr, sqeAddr, sqDepth, curHead, wqeItem_);
    if constexpr (commit) {
        HcommSimtRingDwqeFromUbStwt16(post.dwqeAddr, wqeItem_);
    }
    asc_threadfence();
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

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostSend<commit, HcommUrmaOpCode::WRITE, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len);
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostSend<commit, HcommUrmaOpCode::READ, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len);
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return PostWriteWithNotify<commit, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
        reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal);
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return PostAtomic<T, commit, HcommUrmaOpCode::FAA, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
        static_cast<T>(0));
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return PostAtomic<T, commit, HcommUrmaOpCode::CAS, config>(
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
