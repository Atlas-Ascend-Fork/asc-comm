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

// Every post stages its WQE here, in lane-private storage that lives only for the duration of
// the call. The window is always a full DWQE (two basic blocks) so that a committed post can
// ring the doorbell straight out of it; a deferred one-BB post simply leaves the upper half
// untouched and never copies it.
struct alignas(sizeof(ulonglong2)) HcommSimtWqeStage {
    uint64_t words[HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t)];
};
static_assert(
    sizeof(HcommSimtWqeStage) == HCOMM_URMA_DWQE_SIZE, "The WQE stage must match the 128-byte DWQE window");

// A WQE that occupies a single basic block must not write past it: the next block may
// already hold another lane's WQE.
template <uint32_t byteSize>
__simt_callee__ inline void HcommSimtCopyStageToSq(__gm__ uint8_t* dst, const HcommSimtWqeStage& stage)
{
    static_assert(byteSize % sizeof(uint64_t) == 0U, "WQE copy must use whole words");
    static_assert(byteSize <= HCOMM_URMA_DWQE_SIZE, "WQE copy must stay inside the DWQE window");
#pragma unroll
    for (uint32_t i = 0; i < byteSize / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[i];
    }
}

static_assert(sizeof(HcommUrmaSqeCtx) == 48U, "Packed SIMT SQE assumes a 48-byte SQE context");
static_assert(sizeof(HcommUrmaNotifyCtx) == 32U, "Packed SIMT Notify assumes a 32-byte notify context");
static_assert(sizeof(HcommUrmaSgeCtx) == 16U, "Packed SIMT SQE assumes a 16-byte SGE context");

// Word offsets of the URMA WQE areas. Every layout below writes whole 64-bit words rather
// than struct bit-fields, so these offsets are the single description of the WQE layout;
// the structs above only pin down the sizes they are derived from.
constexpr uint32_t HCOMM_SIMT_WQE_WORDS = HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t);
constexpr uint32_t HCOMM_SIMT_BB_WORDS = HCOMM_URMA_WQE_BB_SIZE / sizeof(uint64_t);
constexpr uint32_t HCOMM_SIMT_SQE_WORDS = sizeof(HcommUrmaSqeCtx) / sizeof(uint64_t);
constexpr uint32_t HCOMM_SIMT_NOTIFY_WORDS = sizeof(HcommUrmaNotifyCtx) / sizeof(uint64_t);
constexpr uint32_t HCOMM_SIMT_SGE_WORDS = sizeof(HcommUrmaSgeCtx) / sizeof(uint64_t);
// The inline payload and the first SGE both start right after the SQE header.
constexpr uint32_t HCOMM_SIMT_INLINE_FIRST_WORD = HCOMM_SIMT_SQE_WORDS;

// Zero-fill the words a layout leaves unused. Every post writes its whole basic-block
// window, so a WQE slot never carries bytes over from the previous post on that slot.
template <uint32_t firstWord, uint32_t wordCnt, typename WordPtr>
__simt_callee__ inline void HcommSimtPadWqeWords(WordPtr words)
{
    static_assert(firstWord <= wordCnt, "WQE layout must not overflow its basic-block window");
#pragma unroll
    for (uint32_t i = firstWord; i < wordCnt; ++i) {
        words[i] = 0U;
    }
}

// The SQE header, identical for every opcode. Bit positions match HcommUrmaSqeCtx:
// word 0 holds sqeBbIdx/flag/tokenEn/rmtJettyType/owner/opcode/inlineMsgLen, word 1 holds
// tpId/sgeNum/rmtJettyOrSegId, words 2..3 the remote EID, word 4 the token value and
// word 5 the 64-bit remote address. inlineMsgLen stays 0 unless the payload rides inline.
template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteSqeHeader(
    WordPtr words, __gm__ uint8_t* remoteAddr, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead,
    uint32_t nextHead, uint32_t inlineMsgLen = 0U)
{
    static_assert(sgeNum <= 2U, "SIMT URMA supports at most two SGEs per WQE");
    uint64_t owner = (curHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(nextHead & 0xFFFFU) |
               (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) | (1ULL << 28U) | (1ULL << 29U) |
               (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U) |
               (static_cast<uint64_t>(inlineMsgLen & 0x3FFU) << 54U);
    words[1] = sqeTemplate.word1;
    words[2] = sqeTemplate.remoteEidL;
    words[3] = sqeTemplate.remoteEidH;
    words[4] = sqeTemplate.tokenWord;
    words[5] = reinterpret_cast<uint64_t>(remoteAddr);
}

// One SGE: the 32-bit length in the low half of the first word (tokenId, the high half,
// stays 0) and the local address in the second.
template <uint32_t firstWord, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteSge(WordPtr words, __gm__ uint8_t* localAddr, uint32_t len)
{
    words[firstWord] = static_cast<uint64_t>(len);
    words[firstWord + 1U] = reinterpret_cast<uint64_t>(localAddr);
}

// Write/Read. sgeNum == 2 splits the payload so an immediate post fills the whole 128B
// DWQE window; sgeNum == 1 keeps the WQE inside one basic block, which is what a deferred
// Write/Read needs because the next block may already hold another lane's WQE.
template <HcommUrmaOpCode opCode, uint32_t sgeNum, uint32_t wordCnt, auto const& config, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteDataWqe(
    WordPtr words, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    HcommSimtWriteSqeHeader<opCode, sgeNum, config>(words, remoteAddr, sqeTemplate, curHead, nextHead);
    if constexpr (sgeNum == 1U) {
        HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS>(words, localAddr, static_cast<uint32_t>(len));
        HcommSimtPadWqeWords<HCOMM_SIMT_SQE_WORDS + HCOMM_SIMT_SGE_WORDS, wordCnt>(words);
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);
        HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS>(words, localAddr, firstLen);
        HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS + HCOMM_SIMT_SGE_WORDS>(words, localAddr + firstLen, lastLen);
        HcommSimtPadWqeWords<HCOMM_SIMT_SQE_WORDS + 2U * HCOMM_SIMT_SGE_WORDS, wordCnt>(words);
    }
}

// Write-with-notify: a 32-byte notify context between the SQE header and the single SGE.
// The notify token repeats the remote token, so both fields come from the SQE template.
template <uint32_t wordCnt, auto const& config, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteNotifyWqe(
    WordPtr words, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len, __gm__ uint8_t* notifyAddr,
    uint64_t notifyVal, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    constexpr uint32_t notifyWord = HCOMM_SIMT_SQE_WORDS;
    constexpr uint32_t sgeWord = notifyWord + HCOMM_SIMT_NOTIFY_WORDS;
    HcommSimtWriteSqeHeader<HcommUrmaOpCode::WRITE_WITH_NOTIFY, 1U, config>(
        words, remoteAddr, sqeTemplate, curHead, nextHead);
    words[notifyWord] =
        static_cast<uint64_t>((sqeTemplate.word1 >> 32U) & 0xFFFFFU) | ((sqeTemplate.tokenWord & 0xFFFFFFFFULL) << 32U);
    words[notifyWord + 1U] = reinterpret_cast<uint64_t>(notifyAddr);
    words[notifyWord + 2U] = notifyVal;
    words[notifyWord + 3U] = 0U;
    HcommSimtWriteSge<sgeWord>(words, localAddr, static_cast<uint32_t>(len));
    HcommSimtPadWqeWords<sgeWord + HCOMM_SIMT_SGE_WORDS, wordCnt>(words);
}

// Packs the inline payload into the whole 64-bit words that follow the SQE header. A type
// shorter than a word occupies the low bytes of the first payload word, because the SQE
// header is a whole number of words long.
template <typename T, uint32_t wordCnt, auto const& config, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteInlineWqe(
    WordPtr words, __gm__ uint8_t* remoteAddr, T value, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead,
    uint32_t nextHead)
{
    HcommSimtWriteSqeHeader<HcommUrmaOpCode::WRITE, 0U, config>(
        words, remoteAddr, sqeTemplate, curHead, nextHead, sizeof(T));
    HcommSimtPadWqeWords<HCOMM_SIMT_INLINE_FIRST_WORD, wordCnt>(words);

    const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
    for (uint32_t i = 0; i < sizeof(T); ++i) {
        uint32_t word = HCOMM_SIMT_INLINE_FIRST_WORD + i / sizeof(uint64_t);
        words[word] |= static_cast<uint64_t>(valueAddr[i]) << ((i % sizeof(uint64_t)) * 8U);
    }
}

// FAA/CAS: one SGE pointing at the local fetch buffer, then the 16-byte AMO operand area.
// A 32-bit type packs both operands into one word; CAS puts the compare value second.
template <HcommUrmaOpCode opCode, typename T, uint32_t wordCnt, auto const& config, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteAtomicWqe(
    WordPtr words, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    static_assert(
        sizeof(T) == sizeof(uint32_t) || sizeof(T) == sizeof(uint64_t),
        "Atomic operation only supports 32-bit or 64-bit data types");
    constexpr uint32_t amoWord = HCOMM_SIMT_SQE_WORDS + HCOMM_SIMT_SGE_WORDS;
    HcommSimtWriteSqeHeader<opCode, 1U, config>(words, remoteAddr, sqeTemplate, curHead, nextHead);
    HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS>(words, fetchAddr, sizeof(T));
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        words[amoWord] =
            static_cast<uint64_t>(static_cast<uint32_t>(value)) |
            (static_cast<uint64_t>(static_cast<uint32_t>(opCode == HcommUrmaOpCode::CAS ? cond : static_cast<T>(0)))
             << 32U);
        words[amoWord + 1U] = 0U;
    } else {
        words[amoWord] = static_cast<uint64_t>(value);
        words[amoWord + 1U] = opCode == HcommUrmaOpCode::CAS ? static_cast<uint64_t>(cond) : 0U;
    }
    HcommSimtPadWqeWords<amoWord + 2U, wordCnt>(words);
}

// The doorbell window is written with 128-bit stores. Plain 64-bit stores are not enough: an
// earlier revision published the WQE from lane-private storage with ordinary stores and the NIC
// consumed the header and the fetch SGE while losing the atomic operand words behind them.
__simt_callee__ inline void HcommSimtCopyStageToDwqe(__gm__ uint8_t* dst, const HcommSimtWqeStage& stage)
{
    __gm__ ulonglong2* dstWords = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        uint32_t wordIdx = i * 2U;
        asc_stwt(&dstWords[i], make_ulonglong2(stage.words[wordIdx], stage.words[wordIdx + 1U]));
    }
}

template <uint32_t sgeNum>
__simt_callee__ inline void HcommSimtBuildPackedSqeTemplate(
    const HcommSimtUbPostMeta& meta, HcommSimtPackedSqeTemplate& sqeTemplate)
{
    // sgeNum == 0 is the inline case: the payload rides in the WQE, so there is no SGE.
    static_assert(sgeNum <= 2U, "SIMT URMA supports at most two SGEs per WQE");
    sqeTemplate.word1 = static_cast<uint64_t>(meta.tpId & 0xFFFFFFU) | (static_cast<uint64_t>(sgeNum) << 24U) |
                        (static_cast<uint64_t>(meta.remoteTokenId & 0xFFFFFU) << 32U);
    sqeTemplate.remoteEidL = meta.remoteEidL;
    sqeTemplate.remoteEidH = meta.remoteEidH;
    sqeTemplate.tokenWord = meta.remoteTokenValue;
    sqeTemplate.sqDepth = meta.sqDepth;
}

// Rings the doorbell by copying the staged 128B WQE into the DWQE window. The fence orders the
// preceding SQ write against the doorbell; the two cache invalidations cover both basic blocks
// of the window.
__simt_callee__ inline void HcommSimtRingDwqe(__gm__ uint8_t* dwqeAddr, const HcommSimtWqeStage& stage)
{
    asc_threadfence();
    HcommSimtCopyStageToDwqe(dwqeAddr, stage);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
}

// Coalesce concurrent fixed-2BB immediate posts issued by one complete warp.
// The leading lane reserves the entire group with one GM CAS; every lane derives
// its SQ position from its active rank, and the last lane publishes the final PI.
__simt_callee__ inline bool HcommSimtReserveImmediateWarp(
    ChannelHandle channel, __gm__ uint64_t* headAddr, uint32_t sqDepth, uint32_t& curHead, bool& publishDwqe)
{
    constexpr uint32_t warpLaneCount = 32U;
    uint32_t blockLaneCount = blockDim.x * blockDim.y * blockDim.z;
    if (blockLaneCount > warpLaneCount || sqDepth == 0U) {
        return false;
    }

    uint32_t linearLane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    uint32_t laneInWarp = linearLane & (warpLaneCount - 1U);
    uint32_t activeMask = asc_activemask();
    uint32_t blockMask = blockLaneCount == warpLaneCount ? 0xFFFFFFFFU : ((1U << blockLaneCount) - 1U);
    if (activeMask != blockMask) {
        return false;
    }
    uint32_t activeCount = static_cast<uint32_t>(__popc(activeMask));
    if (activeCount <= 1U) {
        return false;
    }

    int32_t leaderLane = __ffs(static_cast<int32_t>(activeMask)) - 1;
    ChannelHandle leaderChannel = asc_shfl(channel, leaderLane);
    if ((asc_ballot(channel == leaderChannel) & activeMask) != activeMask) {
        return false;
    }

    uint64_t baseHeadVal = 0U;
    uint32_t reserved = 0U;
    if (static_cast<int32_t>(laneInWarp) == leaderLane) {
        uint32_t batchBbCount = activeCount * HCOMM_URMA_DWQE_BB_CNT;
        uint64_t delta = (static_cast<uint64_t>(activeCount) << 32U) | batchBbCount;
        uint64_t expected = *headAddr;
        while (true) {
            uint32_t baseHead = HcommSimtHeadIdx(expected);
            if ((baseHead % sqDepth) + batchBbCount > sqDepth) {
                break;
            }
            uint64_t previous = asc_atomic_cas(headAddr, expected, expected + delta);
            if (previous == expected) {
                baseHeadVal = expected;
                reserved = 1U;
                break;
            }
            expected = previous;
        }
    }
    reserved = asc_shfl(reserved, leaderLane);
    if (reserved == 0U) {
        return false;
    }
    baseHeadVal = asc_shfl(baseHeadVal, leaderLane);

    uint32_t lowerLaneMask = laneInWarp == 0U ? 0U : ((1U << laneInWarp) - 1U);
    uint32_t activeRank = static_cast<uint32_t>(__popc(activeMask & lowerLaneMask));
    curHead = HcommSimtHeadIdx(baseHeadVal) + activeRank * HCOMM_URMA_DWQE_BB_CNT;
    publishDwqe = activeRank + 1U == activeCount;
    return true;
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
    uint32_t linearLane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    uint64_t laneCount = static_cast<uint64_t>(blockDim.x) * blockDim.y * blockDim.z;
    // The WQE itself is staged in lane-private storage for the duration of a post, so the
    // workspace only holds the two optional caches: one remote-registration entry per lane and
    // one block-shared post context, the latter aligned to its own 128-byte region.
    uint64_t cacheBytes = laneCount * sizeof(HcommSimtUbRemoteCache);
    uint64_t contextOffset = (cacheBytes + HCOMM_SIMT_UB_POST_CONTEXT_BYTES - 1U) &
                             ~(static_cast<uint64_t>(HCOMM_SIMT_UB_POST_CONTEXT_BYTES) - 1U);
    uint64_t cacheRequired = contextOffset + HCOMM_SIMT_UB_POST_CONTEXT_BYTES;

    initialized_ = true;
    ubPostContext_ = nullptr;
    ubRemoteCache_ = nullptr;
    ubPostContextReady_ = false;
    hasReservedDwqeBurst_ = false;
    reservedDwqeBurstBaseHead_ = 0U;
    reservedDwqeBurstNextHead_ = 0U;
    reservedDwqeBurstRemaining_ = 0U;
    reservedDwqeBurstHeadAddr_ = nullptr;
    reservedDwqeBurstChannel_ = 0U;
    reservedDwqeBurstRemoteBase_ = 0U;
    reservedDwqeBurstRemoteSize_ = 0U;
    // Posting never needs the workspace, so a caller that supplies none, or too little for the
    // caches, still gets a usable channel: every post then resolves its metadata from global
    // memory. ReserveDwqeBurst is the one interface that requires the remote cache.
    if (buff == nullptr || cacheRequired > len) {
        return HCOMM_SUCCESS;
    }

    ubRemoteCache_ = reinterpret_cast<__ubuf__ HcommSimtUbRemoteCache*>(buff) + linearLane;
    ubRemoteCache_->valid = 0U;

    ubPostContext_ = reinterpret_cast<__ubuf__ HcommSimtUbPostContext*>(buff + contextOffset);
    if (linearLane == 0U) {
        ubPostContext_->channel = 0U;
        ubPostContext_->state = HCOMM_SIMT_UB_CONTEXT_EMPTY;
    }
    asc_syncthreads();
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

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReserveDwqeBurst(
    ChannelHandle channel, __gm__ void* targetAddr, uint64_t len, uint32_t wqeCnt)
{
    if (wqeCnt == 0U || wqeCnt > 0xFFFFFFFFU / HCOMM_URMA_DWQE_BB_CNT || hasReservedDwqeBurst_ ||
        hasDeferredDwqeBatch_ || hasReservedDwqeSlot_ || ubRemoteCache_ == nullptr) {
        return HCOMM_FAILED;
    }

    HcommSimtUbResolvedPost resolvedPost;
    if (ResolveUbPostContext(channel, reinterpret_cast<__gm__ uint8_t*>(targetAddr), len, resolvedPost) !=
            HCOMM_SUCCESS ||
        ubRemoteCache_->valid == 0U) {
        return HCOMM_FAILED;
    }

    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
    __gm__ uint32_t* tailAddr = reinterpret_cast<__gm__ uint32_t*>(sqCtx->contextInfo.ubJfs.tailAddr);
    uint32_t curHead = HcommSimtHeadIdx(*headAddr);
    uint32_t sqTail = *tailAddr;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    uint32_t burstBbCnt = wqeCnt * HCOMM_URMA_DWQE_BB_CNT;
    uint32_t usedBbCnt = curHead - sqTail;
    if (sqDepth == 0U || usedBbCnt > sqDepth || burstBbCnt > sqDepth - usedBbCnt) {
        return HCOMM_FAILED;
    }

    hasReservedDwqeBurst_ = true;
    reservedDwqeBurstBaseHead_ = curHead;
    reservedDwqeBurstNextHead_ = curHead;
    reservedDwqeBurstRemaining_ = wqeCnt;
    reservedDwqeBurstHeadAddr_ = headAddr;
    reservedDwqeBurstChannel_ = channel;
    reservedDwqeBurstRemoteBase_ = ubRemoteCache_->baseAddr;
    reservedDwqeBurstRemoteSize_ = ubRemoteCache_->size;
    reservedDwqeBurstPost_ = resolvedPost;
    HcommSimtBuildPackedSqeTemplate<1U>(resolvedPost.meta, reservedDwqeBurstSqeTemplate_);
    hasDeferredDwqeBatch_ = true;
    deferredDwqeBbCnt_ = burstBbCnt;
    deferredDwqeCqeCnt_ = wqeCnt;
    return HCOMM_SUCCESS;
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
        post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(ubPostContext_->dwqeAddr);
        post.sqBaseAddr = ubPostContext_->sqBaseAddr;
        post.sqWqeSize = ubPostContext_->sqWqeSize;
        post.meta.sqDepth = ubPostContext_->sqDepth;
        post.meta.tpId = ubPostContext_->tpId;
        post.meta.remoteEidL = ubPostContext_->remoteEidL;
        post.meta.remoteEidH = ubPostContext_->remoteEidH;
    } else {
        post.headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
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

// One descriptor per operator. Each carries the operator's SQ footprint as compile-time
// constants and knows how to lay its own words out; everything else about posting is shared
// by PostWqe. targetLen is the length PostWqe validates against the registered remote range.
//
// bbCnt is 2 for every operator except a deferred Write/Read: those are the only WQEs that
// fit in one basic block, and they must stay inside it because the next block may already
// hold another lane's WQE. An immediate Write/Read instead splits its payload across two
// SGEs so the WQE fills the whole 128B DWQE window that publishes it.
template <bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
struct HcommSimtDataDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t sgeNum = commitFlag ? 2U : 1U;
    static constexpr uint32_t bbCnt = commitFlag ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;
    static constexpr bool burstCapable = false;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* localAddr;
    uint64_t targetLen;

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        HcommSimtWriteDataWqe<opCode, sgeNum, wordCnt, config>(
            words, remoteAddr, localAddr, targetLen, sqeTemplate, curHead, nextHead);
    }
};

template <bool commitFlag, auto const& config>
struct HcommSimtNotifyDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t sgeNum = 1U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;
    static constexpr bool burstCapable = true;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* localAddr;
    uint64_t targetLen;
    __gm__ uint8_t* notifyAddr;
    uint64_t notifyVal;

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        HcommSimtWriteNotifyWqe<wordCnt, config>(
            words, remoteAddr, localAddr, targetLen, notifyAddr, notifyVal, sqeTemplate, curHead, nextHead);
    }
};

template <typename T, bool commitFlag, auto const& config>
struct HcommSimtInlineDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t sgeNum = 0U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;
    static constexpr bool burstCapable = false;

    __gm__ uint8_t* remoteAddr;
    T value;
    uint64_t targetLen = sizeof(T);

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
        static_assert(
            sizeof(T) <= HCOMM_URMA_DWQE_SIZE - sizeof(HcommUrmaSqeCtx),
            "Inline value must fit in the WQE payload area");
        HcommSimtWriteInlineWqe<T, wordCnt, config>(words, remoteAddr, value, sqeTemplate, curHead, nextHead);
    }
};

template <typename T, bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
struct HcommSimtAtomicDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t sgeNum = 1U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;
    static constexpr bool burstCapable = true;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* fetchAddr;
    T value;
    T cond;
    uint64_t targetLen = sizeof(T);

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        HcommSimtWriteAtomicWqe<opCode, T, wordCnt, config>(
            words, remoteAddr, fetchAddr, value, cond, sqeTemplate, curHead, nextHead);
    }
};

// Aggregate fixed 2-BB/1-CQE DWQE WQEs in shared UB and assign their slots
// from a common head snapshot. The snapshot is not an atomic SQ claim, so only
// one batch may be in flight on this SQ until PublishDwqeBatch updates the head.
// Check whether this group crosses the physical SQ ring before touching the
// shared batch allocator. Crossing groups keep the original per-WQE reservation
// order used by the SIMT posting path.
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReserveDwqeBatch(
    ChannelHandle channel, __ubuf__ uint32_t* state, bool active, uint32_t activeWqeCnt, uint32_t commitLane)
{
    constexpr uint32_t kBbCnt = 0U;
    constexpr uint32_t kCqeCnt = 1U;
    constexpr uint32_t kHeadLo = 2U;
    constexpr uint32_t kStatus = 3U;
    constexpr uint32_t kUseLegacyPath = 1U;
    uint32_t linearLane = threadIdx.x + blockDim.x * (threadIdx.y + blockDim.y * threadIdx.z);
    if (linearLane == 0U) {
        state[kBbCnt] = 0U;
        state[kCqeCnt] = 0U;
        state[kHeadLo] = 0U;
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
        deferredDwqeBbCnt_ = state[kBbCnt];
        deferredDwqeCqeCnt_ = state[kCqeCnt];
    }
    asc_syncthreads();
    return HCOMM_SUCCESS;
}

// The batch head is atomically advanced once, only after every deferred lane
// has fenced its SQ write and reached the final commit barrier.
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PublishDwqeBatch(__gm__ uint64_t* headAddr)
{
    if (!hasDeferredDwqeBatch_) {
        return HCOMM_SUCCESS;
    }
    if (hasReservedDwqeBurst_ && (reservedDwqeBurstRemaining_ != 0U || headAddr != reservedDwqeBurstHeadAddr_)) {
        return HCOMM_FAILED;
    }
    uint64_t previousHead = HcommSimtReserve(headAddr, deferredDwqeBbCnt_, deferredDwqeCqeCnt_);
    if (hasReservedDwqeBurst_ && HcommSimtHeadIdx(previousHead) != reservedDwqeBurstBaseHead_) {
        hasDeferredDwqeBatch_ = false;
        hasReservedDwqeBurst_ = false;
        reservedDwqeBurstHeadAddr_ = nullptr;
        reservedDwqeBurstChannel_ = 0U;
        return HCOMM_FAILED;
    }
    hasDeferredDwqeBatch_ = false;
    hasReservedDwqeBurst_ = false;
    reservedDwqeBurstHeadAddr_ = nullptr;
    reservedDwqeBurstChannel_ = 0U;
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

// The single posting path: resolve the target, claim SQ space, lay the WQE down and, when the
// task is committed, ring the doorbell. The four reservation sources are mutually exclusive
// and tried in this order: an open sequential burst, a slot handed out by ReserveDwqeBatch,
// warp coalescing, and finally a plain atomic reservation.
template <typename Desc>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWqe(ChannelHandle channel, const Desc& desc)
{
    constexpr uint32_t bbCnt = Desc::bbCnt;
    constexpr uint32_t wordCnt = bbCnt * HCOMM_SIMT_BB_WORDS;
    constexpr uint32_t wqeBytes = bbCnt * HCOMM_URMA_WQE_BB_SIZE;
    static_assert(bbCnt == HCOMM_URMA_WQE_BB_CNT || bbCnt == HCOMM_URMA_DWQE_BB_CNT, "A WQE spans one or two BBs");
    // Only a deferred task may occupy a single basic block: an immediate one is published
    // through the 128B DWQE window, which is two blocks wide.
    static_assert(Desc::commit ? bbCnt == HCOMM_URMA_DWQE_BB_CNT : true, "An immediate WQE must fill the DWQE window");
    // ReserveDwqeBurst builds its cached template with one SGE.
    static_assert(Desc::burstCapable ? Desc::sgeNum == 1U : true, "A burst-carried WQE must use exactly one SGE");

    if (!initialized_) {
        return HCOMM_FAILED;
    }
    // HcommUrmaSgeCtx::len is 32-bit.
    if (desc.targetLen > 0xFFFFFFFFULL) {
        return HCOMM_FAILED;
    }

    // A burst reserves one contiguous SQ range for one registered buffer, so a task it cannot
    // carry must not be posted while it is open: that task would claim SQ space the burst
    // already owns.
    bool usingBurst = false;
    if (hasReservedDwqeBurst_) {
        if constexpr (!Desc::burstCapable) {
            return HCOMM_FAILED;
        } else {
            uint64_t targetAddr = reinterpret_cast<uint64_t>(desc.remoteAddr);
            if (channel != reservedDwqeBurstChannel_ || targetAddr < reservedDwqeBurstRemoteBase_) {
                return HCOMM_FAILED;
            }
            uint64_t offset = targetAddr - reservedDwqeBurstRemoteBase_;
            if (offset > reservedDwqeBurstRemoteSize_ || desc.targetLen > reservedDwqeBurstRemoteSize_ - offset) {
                return HCOMM_FAILED;
            }
            usingBurst = true;
        }
    }

    HcommSimtUbResolvedPost post;
    if (usingBurst) {
        post = reservedDwqeBurstPost_;
    } else if (ResolveUbPostContext(channel, desc.remoteAddr, desc.targetLen, post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }

    __gm__ uint64_t* headAddr = post.headAddr;
    uint32_t curHead = 0U;
    bool useImmediateWarp = false;
    bool publishImmediateWarp = false;
    if (usingBurst) {
        if (reservedDwqeBurstRemaining_ == 0U || headAddr != reservedDwqeBurstHeadAddr_) {
            return HCOMM_FAILED;
        }
        curHead = reservedDwqeBurstNextHead_;
        reservedDwqeBurstNextHead_ += bbCnt;
        --reservedDwqeBurstRemaining_;
    } else if (hasReservedDwqeSlot_ && bbCnt == HCOMM_URMA_DWQE_BB_CNT) {
        // ReserveDwqeBatch hands out fixed two-BB slots. A one-BB task cannot consume one:
        // it would leave the slot's second block holding stale content.
        curHead = reservedDwqeHead_;
        hasReservedDwqeSlot_ = false;
    } else {
        if constexpr (Desc::commit) {
            useImmediateWarp =
                HcommSimtReserveImmediateWarp(channel, headAddr, post.meta.sqDepth, curHead, publishImmediateWarp);
        }
        if (!useImmediateWarp) {
            curHead = HcommSimtHeadIdx(HcommSimtReserve(headAddr, bbCnt, 1U));
        }
    }
    uint32_t nextHead = curHead + bbCnt;

    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));

    HcommSimtPackedSqeTemplate sqeTemplate;
    if (usingBurst) {
        sqeTemplate = reservedDwqeBurstSqeTemplate_;
    } else {
        HcommSimtBuildPackedSqeTemplate<Desc::sgeNum>(post.meta, sqeTemplate);
    }

    // Stage the WQE in lane-private storage, then copy it into the SQ. The SQ copy is what the
    // NIC always reads, so it happens on every path; the doorbell, when this lane rings it, is
    // published out of the same staged bytes.
    const bool ringDoorbell = Desc::commit && (!useImmediateWarp || publishImmediateWarp);
    HcommSimtWqeStage stage;
    desc.template Store<wordCnt>(stage.words, sqeTemplate, curHead, nextHead);
    HcommSimtCopyStageToSq<wqeBytes>(sqeAddr, stage);

    if constexpr (Desc::commit) {
        if (useImmediateWarp) {
            asc_threadfence();
            if (ringDoorbell) {
                HcommSimtRingDwqe(post.dwqeAddr, stage);
            }
            return HCOMM_SUCCESS;
        }
        // A single-lane post is ordered by HcommSimtRingDwqe's thread fence.
        // A deferred batch still needs the block barrier to wait for all SQ writes.
        if (hasDeferredDwqeBatch_) {
            HcommSimtSyncBeforeRing(headAddr, nextHead);
        }
        if (PublishDwqeBatch(headAddr) != HCOMM_SUCCESS) {
            return HCOMM_FAILED;
        }
        HcommSimtRingDwqe(post.dwqeAddr, stage);
    } else if (!usingBurst) {
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::WRITE, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(channel, HcommSimtInlineDesc<T, commit, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::READ, config>{
                     reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(
        channel, HcommSimtNotifyDesc<commit, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                     reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal});
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(
        channel, HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::FAA, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
                     static_cast<T>(0)});
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    (void)commitPipe;
    (void)reqPipe;
    return PostWqe(
        channel, HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::CAS, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal,
                     compareVal});
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
