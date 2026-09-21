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
static_assert(sizeof(HcommSimtWqeStage) == HCOMM_URMA_DWQE_SIZE, "The WQE stage must match the 128-byte DWQE window");

// A WQE that occupies a single basic block must not write past it: the next block may
// already hold another lane's WQE.
template <uint32_t byteSize>
__simt_callee__ inline void HcommSimtCopyStageToSq(__gm__ uint8_t* dst, const HcommSimtWqeStage& stage)
{
    static_assert(byteSize % sizeof(uint64_t) == 0U, "WQE copy must use whole words");
    static_assert(byteSize <= HCOMM_URMA_DWQE_SIZE, "WQE copy must stay inside the DWQE window");

    __gm__ ulonglong2* dstWords = reinterpret_cast<__gm__ ulonglong2*>(dst);
#pragma unroll
    for (uint32_t i = 0; i < byteSize / sizeof(ulonglong2); ++i) {
        uint32_t wordIdx = i * 2U;
        asc_stwt(&dstWords[i], make_ulonglong2(stage.words[wordIdx], stage.words[wordIdx + 1U]));
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

// Copies a staged WQE into its reserved SQ slot. A 2-BB WQE landing on the last slot of the ring
// is split: the first basic block goes to the slot, the second wraps to the base of the SQ. The
// WQE stays logically contiguous for the NIC, which reads the ring modulo its depth, but the
// bytes must not run off the end of the SQ allocation.
template <uint32_t bbCnt>
__simt_callee__ inline void HcommSimtCopyStageWqeToSq(
    uint64_t sqBaseAddr, __gm__ uint8_t* sqeAddr, uint32_t sqDepth, uint32_t curHead, const HcommSimtWqeStage& stage)
{
    static_assert(
        bbCnt == HCOMM_URMA_WQE_BB_CNT || bbCnt == HCOMM_URMA_DWQE_BB_CNT,
        "SIMT SQ copy supports one-BB or two-BB WQEs");
    if constexpr (bbCnt == HCOMM_URMA_DWQE_BB_CNT) {
        if ((curHead % sqDepth) == sqDepth - 1U) {
            HcommSimtWqeStage secondBb;
#pragma unroll
            for (uint32_t i = 0; i < HCOMM_SIMT_BB_WORDS; ++i) {
                secondBb.words[i] = stage.words[HCOMM_SIMT_BB_WORDS + i];
            }
            HcommSimtCopyStageToSq<HCOMM_URMA_WQE_BB_SIZE>(sqeAddr, stage);
            HcommSimtCopyStageToSq<HCOMM_URMA_WQE_BB_SIZE>(reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr), secondBb);
            return;
        }
    }
    HcommSimtCopyStageToSq<bbCnt * HCOMM_URMA_WQE_BB_SIZE>(sqeAddr, stage);
}

// Words a layout leaves unused are not cleared: the NIC reads only as far as the fields the
// SQE header describes, so stale bytes from an earlier post on the same slot are never consumed.
// Each writer below instead asserts that its own fields fit the window.
template <uint32_t usedWords, uint32_t wordCnt>
__simt_callee__ inline constexpr void HcommSimtAssertWqeFits()
{
    static_assert(usedWords <= wordCnt, "WQE layout must not overflow its basic-block window");
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
    // The owner bit flips each lap of the ring so the NIC can tell a freshly written slot from
    // the previous lap's. `head & sqDepth` isolates the lap parity only because sqDepth is a power
    // of two -- it is `(head / sqDepth) & 1` -- and a non-power-of-two depth would silently yield
    // the wrong owner, letting the NIC consume stale WQEs. PollCq masks on the same assumption.
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
        HcommSimtAssertWqeFits<HCOMM_SIMT_SQE_WORDS + HCOMM_SIMT_SGE_WORDS, wordCnt>();
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);
        HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS>(words, localAddr, firstLen);
        HcommSimtWriteSge<HCOMM_SIMT_SQE_WORDS + HCOMM_SIMT_SGE_WORDS>(words, localAddr + firstLen, lastLen);
        HcommSimtAssertWqeFits<HCOMM_SIMT_SQE_WORDS + 2U * HCOMM_SIMT_SGE_WORDS, wordCnt>();
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
    HcommSimtAssertWqeFits<sgeWord + HCOMM_SIMT_SGE_WORDS, wordCnt>();
}

// Writes a NOP WQE into the second basic block (words 8..15) of a 2BB staging buffer. A NOP
// occupies one BB and produces no CQE; it pads the DWQE window when a 1BB inline Write needs to be
// published through the doorbell.
//
// Placeholder layout: only the opcode and owner bit are set. The remaining fields are zeroed as a
// safe default and may need tuning on real hardware for the NIC to accept the NOP without error.
template <uint32_t wordCnt, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteNopWqe(
    WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t nextHead)
{
    static_assert(wordCnt == HCOMM_SIMT_WQE_WORDS, "NOP padding requires a 2BB staging buffer");
    constexpr uint32_t nopFirstWord = HCOMM_SIMT_BB_WORDS; // Second BB starts at word 8

    // The NOP occupies slot nextHead, one BB beyond the inline Write at curHead, so its owner bit
    // is computed from nextHead. Same power-of-two assumption as HcommSimtWriteSqeHeader.
    uint64_t owner = (nextHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;

    // Minimal NOP header: opcode=0x11, owner bit, nextHead index.
    words[nopFirstWord] = static_cast<uint64_t>((nextHead + 1U) & 0xFFFFU) |
                          (1ULL << 28U) | // descriptor valid flag (bit 28)
                          (1ULL << 29U) | // another control bit, copied from Write header
                          (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(HcommUrmaOpCode::NOP)) << 40U);
}

// Packs the inline payload into the whole 64-bit words that follow the SQE header. A type
// shorter than a word occupies the low bytes of the first payload word, because the SQE
// header is a whole number of words long.
//
// When commit is true the WQE spans two basic blocks: the inline Write, then a NOP padding the
// DWQE window to 128B, because a 1BB WQE cannot be published through the doorbell on its own.
template <typename T, uint32_t wordCnt, auto const& config, bool commit, typename WordPtr>
__simt_callee__ inline void HcommSimtWriteInlineWqe(
    WordPtr words, __gm__ uint8_t* remoteAddr, T value, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead,
    uint32_t nextHead)
{
    constexpr uint32_t payloadWords = (sizeof(T) + sizeof(uint64_t) - 1U) / sizeof(uint64_t);
    HcommSimtAssertWqeFits<HCOMM_SIMT_INLINE_FIRST_WORD + payloadWords, wordCnt>();
    HcommSimtWriteSqeHeader<HcommUrmaOpCode::WRITE, 0U, config>(
        words, remoteAddr, sqeTemplate, curHead, nextHead, sizeof(T));

    // Each payload word is assembled in a register and then assigned, so this does not depend
    // on the target words having been zeroed first. A type shorter than the payload area leaves
    // the bytes above it zero, because the accumulator starts at zero.
    const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
    for (uint32_t word = 0; word < payloadWords; ++word) {
        uint64_t packed = 0U;
#pragma unroll
        for (uint32_t byte = 0; byte < sizeof(uint64_t); ++byte) {
            uint32_t idx = word * sizeof(uint64_t) + byte;
            if (idx < sizeof(T)) {
                packed |= static_cast<uint64_t>(valueAddr[idx]) << (byte * 8U);
            }
        }
        words[HCOMM_SIMT_INLINE_FIRST_WORD + word] = packed;
    }

    // Pad the DWQE window with a NOP so the doorbell has a full 128B to publish.
    if constexpr (commit) {
        HcommSimtWriteNopWqe<wordCnt>(words, sqeTemplate, nextHead);
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
    HcommSimtAssertWqeFits<amoWord + 2U, wordCnt>();
}

// The doorbell window must be written with 128-bit stores. With plain 64-bit stores the NIC has
// been observed to consume the header and the fetch SGE while losing the atomic operand words
// behind them.
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
    const HcommSimtPostMeta& meta, HcommSimtPackedSqeTemplate& sqeTemplate)
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

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::HcommImpl() {}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::~HcommImpl() {}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    // Nothing to set up: the WQE is staged on the stack for the duration of a post and every
    // post resolves its channel from global memory, so there is no state to hand out, publish,
    // or synchronize here. The buff and len parameters are kept for API compatibility but ignored.
    (void)buff;
    (void)len;
    initialized_ = true;
    return HCOMM_SUCCESS;
}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ResolvePost(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, uint64_t len, HcommSimtResolvedPost& post)
{
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ RegedBufferEntity* remoteBuffers =
        reinterpret_cast<__gm__ RegedBufferEntity*>(channelEntity->remoteBufferAddr);
    int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, len);
    if (remoteIdx == HCOMM_FAILED) {
        return HCOMM_FAILED;
    }
    __gm__ RegedBufferEntity* remoteMemInfo = &remoteBuffers[remoteIdx];

    // The 64-byte used range (offset 8..71) crosses a cache line, and bulk-reading it as an
    // indexed array costs more instructions than direct field access because the layout has holes.
    // The per-field reads are grouped tightly so the compiler can see the locality.
    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    uint64_t sqVa = sqCtx->contextInfo.ubJfs.sqVa;
    uint64_t headAddr = sqCtx->contextInfo.ubJfs.headAddr;
    uint64_t sqTailAddr = sqCtx->contextInfo.ubJfs.tailAddr;
    uint64_t dbVa = sqCtx->contextInfo.ubJfs.dbVa;
    uint32_t wqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    uint32_t sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    uint32_t tpID = sqCtx->contextInfo.ubJfs.tpID;
    __gm__ uint64_t* remoteEid = reinterpret_cast<__gm__ uint64_t*>(&sqCtx->contextInfo.ubJfs.remoteEID[0]);
    uint64_t remoteEidL = remoteEid[0];
    uint64_t remoteEidH = remoteEid[1];

    post.headAddr = reinterpret_cast<__gm__ uint64_t*>(headAddr);
    post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(sqTailAddr);
    post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(dbVa - HCOMM_URMA_DWQE_DB_OFFSET);
    post.sqBaseAddr = sqVa;
    post.sqWqeSize = wqeSize;
    post.meta.sqDepth = sqDepth;
    post.meta.tpId = tpID;
    post.meta.remoteEidL = remoteEidL;
    post.meta.remoteEidH = remoteEidH;
    post.meta.remoteTokenId = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    post.meta.remoteTokenValue = remoteMemInfo->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
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
    static constexpr uint32_t cqeCnt = config.cqe;

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
    static constexpr uint32_t cqeCnt = config.cqe;

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
    // A deferred inline Write occupies one BB; a committed one occupies two (the second is a NOP
    // that pads the DWQE window to 128B so the doorbell can publish it).
    static constexpr uint32_t bbCnt = commit ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;
    static constexpr uint32_t cqeCnt = config.cqe;

    __gm__ uint8_t* remoteAddr;
    T value;
    uint64_t targetLen = sizeof(T);

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
        static_assert(
            sizeof(T) <= HCOMM_URMA_WQE_BB_SIZE - sizeof(HcommUrmaSqeCtx),
            "WriteValue<T>: sizeof(T) must be less equal than primitive type");
        HcommSimtWriteInlineWqe<T, wordCnt, config, commit>(words, remoteAddr, value, sqeTemplate, curHead, nextHead);
    }
};

template <typename T, bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
struct HcommSimtAtomicDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t sgeNum = 1U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;
    static constexpr uint32_t cqeCnt = config.cqe;

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

__simt_callee__ inline uint32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCq(ChannelHandle channel, uint32_t expectIdx)
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
    // Both depths are powers of two (see the owner-bit computation in HcommSimtWriteSqeHeader),
    // so the wrap arithmetic below uses masks instead of modulo. Depth is loop-invariant, so the
    // masks are computed once here.
    uint32_t cqMask = cqDepth - 1U;
    uint32_t sqMask = sqDepth - 1U;
    while (curTail != expectIdx) {
        // Read the CQE header as raw 32-bit words instead of through the bitfield struct. This
        // lets the compiler see the reads are contiguous and generate tighter code. owner sits at
        // bit 2 of word 0, status/substatus at bits 24/16, entryIdx in the low 16 bits of word 1.
        __gm__ uint32_t* cqeWords = (__gm__ uint32_t*)(cqBaseAddr + cqeSize * (curTail & cqMask));
        uint32_t validOwner = (curTail / cqDepth) & 1U;
        uint32_t retry = 0U;
        while ((((cqeWords[0] >> 2U) & 1U) == validOwner) && retry < HCOMM_SIMT_MAX_CQ_RETRY) {
            asc_threadfence();
            asc_dcci_single(cqeWords);
            ++retry;
        }
        if (retry >= HCOMM_SIMT_MAX_CQ_RETRY) {
            // No CQE arrived: the WQE was never consumed.
            return 0xFFU;
        }
        uint32_t cqeHead = cqeWords[0];
        uint32_t status = (cqeHead >> 24U) & 0xFFU;
        uint32_t subStatus = (cqeHead >> 16U) & 0xFFU;
        if (status != 0U || subStatus != 0U) {
            constexpr uint8_t statusShift = 8U;
            return (status << statusShift) | subStatus;
        }
        uint32_t sqTailSlot = sqTail & sqMask;
        uint32_t completedSlot = (cqeWords[1] & 0xFFFFU) & sqMask;
        // The parentheses matter: & binds looser than +, so `x & sqMask + 1U` would compute
        // `x & (sqMask + 1U)` -- a single-bit test against sqDepth rather than a wrap-around
        // distance. The distance from the current tail slot to the completed slot, plus one for
        // the completed block itself, is how far the tail advances.
        sqTail += ((completedSlot + sqDepth - sqTailSlot) & sqMask) + 1U;
        ++curTail;
    }

    *tailAddr = curTail;
    __gm__ uint32_t* cqDbAddr = (__gm__ uint32_t*)cqCtx->contextInfo.ubJfc.dbVa;
    *cqDbAddr = curTail & 0xFFFFFFU;

    *sqTailAddr = sqTail;
    return HCOMM_SUCCESS;
}

// Claims SQ space for one submission. The fast path is a single reservation attempt; when the
// queue is full it consumes completed CQEs one at a time to let PollCq advance sqTail, retrying
// after each. Waiting for every submitted WQE would be wrong: wqeCnt may include deferred WQEs
// that no doorbell has published yet, so those completions never arrive.
template <typename Desc>
__simt_callee__ inline bool HcommImpl<COMM_PROTOCOL_UB_CTP>::ReservePost(
    ChannelHandle channel, const HcommSimtResolvedPost& post, uint64_t& headVal)
{
    constexpr bool commit = Desc::commit;
    constexpr uint32_t bbCnt = Desc::bbCnt;
    constexpr uint32_t cqeCnt = Desc::cqeCnt;
    // SIMT has no standalone Commit interface, so a deferred post must also leave room for the
    // immediate 2-BB DWQE that will later publish the batch it belongs to.
    constexpr uint32_t extraFreeBbCnt = commit ? 0U : HCOMM_URMA_DWQE_BB_CNT;
    uint32_t requiredFreeBbCnt = bbCnt + extraFreeBbCnt;

    if (HcommSimtTryReserve(
            post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, cqeCnt, headVal)) {
        return true;
    }
    if (post.meta.sqDepth == 0U || requiredFreeBbCnt > post.meta.sqDepth) {
        return false;
    }

    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ CqContext* cqContexts = reinterpret_cast<__gm__ CqContext*>(channelEntity->cqContextAddr);
    __gm__ CqContext* cqCtx = &cqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* cqTailAddr = reinterpret_cast<__gm__ uint32_t*>(cqCtx->contextInfo.ubJfc.tailAddr);
    while (true) {
        // headVal was refreshed by the failed attempt, so its wqeCnt is the number of CQEs the
        // channel has submitted so far. Once the CQ tail has reached it there is nothing left to
        // reap and the queue cannot free up any further.
        uint32_t cqTail = *cqTailAddr;
        if (cqTail == HcommSimtWqeCnt(headVal) || PollCq(channel, cqTail + 1U) != HCOMM_SUCCESS) {
            return false;
        }
        if (HcommSimtTryReserve(
                post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, cqeCnt, headVal)) {
            return true;
        }
    }
}

// The single posting path: resolve the target, claim SQ space, lay the WQE down and, when the
// task is committed, ring the doorbell. Every step is lane-local; a caller that needs several
// lanes to post must serialize them itself.
template <typename Desc>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PostWqe(ChannelHandle channel, const Desc& desc)
{
    constexpr uint32_t bbCnt = Desc::bbCnt;
    constexpr uint32_t wordCnt = bbCnt * HCOMM_SIMT_BB_WORDS;
    static_assert(bbCnt == HCOMM_URMA_WQE_BB_CNT || bbCnt == HCOMM_URMA_DWQE_BB_CNT, "A WQE spans one or two BBs");
    // Only a deferred task may occupy a single basic block: an immediate one is published
    // through the 128B DWQE window, which is two blocks wide.
    static_assert(Desc::commit ? bbCnt == HCOMM_URMA_DWQE_BB_CNT : true, "An immediate WQE must fill the DWQE window");

    if (!initialized_) {
        return HCOMM_FAILED;
    }
    // HcommUrmaSgeCtx::len is 32-bit.
    if (desc.targetLen > 0xFFFFFFFFULL) {
        return HCOMM_FAILED;
    }

    HcommSimtResolvedPost post;
    if (ResolvePost(channel, desc.remoteAddr, desc.targetLen, post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }

    uint64_t headVal = 0U;
    if (!ReservePost<Desc>(channel, post, headVal)) {
        return HCOMM_FAILED;
    }
    uint32_t curHead = HcommSimtHeadIdx(headVal);
    uint32_t nextHead = curHead + bbCnt;

    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));

    HcommSimtPackedSqeTemplate sqeTemplate;
    HcommSimtBuildPackedSqeTemplate<Desc::sgeNum>(post.meta, sqeTemplate);

    // Stage the WQE in lane-private storage, then copy it into the SQ. The SQ copy is what the
    // NIC always reads, so it happens on every path; a committed post then publishes the
    // doorbell out of the same staged bytes.
    HcommSimtWqeStage stage;
    desc.template Store<wordCnt>(stage.words, sqeTemplate, curHead, nextHead);
    HcommSimtCopyStageWqeToSq<bbCnt>(post.sqBaseAddr, sqeAddr, post.meta.sqDepth, curHead, stage);

    // A deferred post needs no fence of its own: its SQ bytes are not published until some later
    // committed post rings the doorbell, and that post's fence orders every write issued before
    // it, this one included. Only the doorbell needs ordering, so only it fences.
    if constexpr (Desc::commit) {
        HcommSimtRingDwqe(post.dwqeAddr, stage);
    }
    return HCOMM_SUCCESS;
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::WRITE, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return PostWqe(channel, HcommSimtInlineDesc<T, commit, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::READ, config>{
                     reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return PostWqe(
        channel, HcommSimtNotifyDesc<commit, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                     reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return PostWqe(
        channel, HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::FAA, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
                     static_cast<T>(0)});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return PostWqe(
        channel,
        HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::CAS, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal, compareVal});
}

template <auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(ChannelHandle channel)
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
