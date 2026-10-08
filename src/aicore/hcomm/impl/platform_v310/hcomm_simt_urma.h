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

// Static queue/Jetty fields and the exclusive producer cursor are shared by all lanes. The
// second 128-byte workspace block holds the final publisher WQE used to ring DWQE.
struct alignas(sizeof(ulonglong2)) HcommSimtBatchStaticContext {
    uint64_t headAddr;
    uint64_t sqTailAddr;
    uint64_t dwqeAddr;
    uint64_t sqBaseAddr;
    uint64_t word1Base;
    uint64_t remoteEidL;
    uint64_t remoteEidH;
    uint64_t tokenWord;
    uint64_t reservedHead;
    uint32_t sqWqeSize;
    uint32_t sqDepth;
    uint32_t cachedSqTail;
    uint32_t itemBb;
    uint32_t groupSize;
    int32_t reservationStatus;
};
static_assert(
    sizeof(HcommSimtBatchStaticContext) <= HCOMM_URMA_DWQE_SIZE,
    "The shared SIMT batch context must fit in the public UB workspace");

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
// Placeholder layout: only the opcode and owner bit are set. As with other unused WQE words, the
// remaining fields are ignored according to the NOP opcode and are not initialized.
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

__simt_callee__ inline void HcommSimtLoadBatchStaticPost(
    __ubuf__ HcommSimtBatchStaticContext* context, HcommSimtResolvedPost& post)
{
    post.headAddr = reinterpret_cast<__gm__ uint64_t*>(context->headAddr);
    post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(context->sqTailAddr);
    post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(context->dwqeAddr);
    post.sqBaseAddr = context->sqBaseAddr;
    post.sqWqeSize = context->sqWqeSize;
    post.meta.sqDepth = context->sqDepth;
}

__simt_callee__ inline __ubuf__ uint64_t* HcommSimtBatchPublisherWords(__ubuf__ HcommSimtBatchStaticContext* context)
{
    return reinterpret_cast<__ubuf__ uint64_t*>(
        reinterpret_cast<__ubuf__ uint8_t*>(context) + HCOMM_SIMT_BATCH_CONTEXT_BYTES);
}

__simt_callee__ inline void HcommSimtFlushBatchLaneSq(
    __ubuf__ HcommSimtBatchStaticContext* context, uint32_t rank, uint32_t size)
{
    const bool publisher = rank + 1U == size;
    const uint32_t curHead = HcommSimtHeadIdx(context->reservedHead) + rank * context->itemBb;
    __gm__ uint8_t* firstBb =
        reinterpret_cast<__gm__ uint8_t*>(context->sqBaseAddr + context->sqWqeSize * (curHead % context->sqDepth));
    __gm__ uint8_t* secondBb = (curHead % context->sqDepth) + 1U == context->sqDepth ?
                                   reinterpret_cast<__gm__ uint8_t*>(context->sqBaseAddr) :
                                   firstBb + HCOMM_URMA_WQE_BB_SIZE;

    // Each producer maintains its own SQ lines before the publisher advances the shared head.
    asc_threadfence();
    asc_dcci_single(firstBb);
    if (publisher || context->itemBb == HCOMM_URMA_DWQE_BB_CNT) {
        asc_dcci_single(secondBb);
    }
    asc_threadfence();
}

template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline uint64_t HcommSimtBatchSqeWord0(
    const __ubuf__ HcommSimtBatchStaticContext* context, uint32_t curHead, uint32_t nextHead,
    uint32_t inlineMsgLen = 0U)
{
    const uint64_t owner = (curHead & context->sqDepth) == 0U ? 1U : 0U;
    return static_cast<uint64_t>(nextHead & 0xFFFFU) |
           (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) | (1ULL << 28U) | (1ULL << 29U) |
           (owner << 31U) | (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U) |
           (static_cast<uint64_t>(inlineMsgLen & 0x3FFU) << 54U);
}

// Batch Emit constructs each final 32-byte SQ vector directly. Publisher vectors are mirrored
// into the shared UB image as they are produced, avoiding a lane-local 128-byte staging array and
// a second pass over it. Only the first three vectors can contain valid fields in the supported
// layouts; the unused final 32 bytes of the DWQE image stay zero after handle creation.
template <bool publisher>
__simt_callee__ inline void HcommSimtStoreBatchVector(
    __ubuf__ HcommSimtBatchStaticContext* context, __gm__ uint8_t* firstBb, __gm__ uint8_t* secondBb,
    uint32_t vectorIdx, uint64_t word0, uint64_t word1, uint64_t word2, uint64_t word3)
{
    __gm__ uint8_t* vectorAddr = vectorIdx < 2U ? firstBb + vectorIdx * sizeof(ulonglong4) : secondBb;
    asc_stwt(reinterpret_cast<__gm__ ulonglong4*>(vectorAddr), make_ulonglong4(word0, word1, word2, word3));
    if constexpr (publisher) {
        __ubuf__ uint64_t* publisherWords = HcommSimtBatchPublisherWords(context) + vectorIdx * 4U;
        publisherWords[0] = word0;
        publisherWords[1] = word1;
        publisherWords[2] = word2;
        publisherWords[3] = word3;
    }
}

__simt_callee__ inline uint32_t HcommSimtBatchTotalBb(uint32_t groupSize, uint32_t itemBb)
{
    return groupSize * itemBb + (itemBb == HCOMM_URMA_WQE_BB_CNT ? HCOMM_URMA_WQE_BB_CNT : 0U);
}

// Advance the private producer cursor without publishing it. The cached tail is sufficient on the
// normal path; it is refreshed from GM only when the cached free range has been exhausted.
__simt_callee__ inline bool HcommSimtPlanBatchFromHead(__ubuf__ HcommSimtBatchStaticContext* context, uint64_t headVal)
{
    const uint32_t totalBb = HcommSimtBatchTotalBb(context->groupSize, context->itemBb);
    const uint32_t head = HcommSimtHeadIdx(headVal);
    uint32_t usedBb = head - context->cachedSqTail;
    if (usedBb > context->sqDepth || totalBb > context->sqDepth - usedBb) {
        asc_dcci_single(reinterpret_cast<__gm__ void*>(context->sqTailAddr));
        context->cachedSqTail = *reinterpret_cast<__gm__ uint32_t*>(context->sqTailAddr);
        usedBb = head - context->cachedSqTail;
    }
    context->reservedHead = headVal;
    context->reservationStatus =
        usedBb <= context->sqDepth && totalBb <= context->sqDepth - usedBb ? HCOMM_SUCCESS : HCOMM_FAILED;
    return context->reservationStatus == HCOMM_SUCCESS;
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

__simt_callee__ inline void HcommSimtRingBatchDwqe(__gm__ uint8_t* dwqeAddr, const __ubuf__ uint64_t* words)
{
    asc_threadfence();
    __gm__ ulonglong2* dstWords = reinterpret_cast<__gm__ ulonglong2*>(dwqeAddr);
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(ulonglong2); ++i) {
        const uint32_t word = i * 2U;
        asc_stwt(&dstWords[i], make_ulonglong2(words[word], words[word + 1U]));
    }
    asc_threadfence();
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
    asc_threadfence();
}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::HcommImpl() {}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::~HcommImpl() {}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
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

__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb)
{
    return MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb, 0U, 1U);
}

__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb,
    uint32_t groupRank, uint32_t groupSize)
{
    UbcCtpBatchHandle batchHandle;
    constexpr uint32_t workspaceBytes = HCOMM_SIMT_BATCH_CONTEXT_BYTES + HCOMM_SIMT_BATCH_ITEM_BYTES;
    if (buff == nullptr || buffLen < workspaceBytes ||
        (itemBb != HCOMM_URMA_WQE_BB_CNT && itemBb != HCOMM_URMA_DWQE_BB_CNT)) {
        return batchHandle;
    }
    __ubuf__ HcommSimtBatchStaticContext* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(buff);
    // Publisher WQEs fill the first three vectors; the full DWQE copy also includes the fourth.
    __ubuf__ uint64_t* publisherWords = HcommSimtBatchPublisherWords(context);
#pragma unroll
    for (uint32_t word = 3U * sizeof(ulonglong4) / sizeof(uint64_t);
         word < HCOMM_SIMT_BATCH_ITEM_BYTES / sizeof(uint64_t); ++word) {
        publisherWords[word] = 0U;
    }
    context->headAddr = 0U;
    HcommSimtResolvedPost post;
    __gm__ uint8_t* remoteAddr = reinterpret_cast<__gm__ uint8_t*>(remoteBase);
    if (ResolvePost(channel, remoteAddr, 1U, post) != HCOMM_SUCCESS) {
        return batchHandle;
    }

    context->headAddr = reinterpret_cast<uint64_t>(post.headAddr);
    context->sqTailAddr = reinterpret_cast<uint64_t>(post.sqTailAddr);
    context->dwqeAddr = reinterpret_cast<uint64_t>(post.dwqeAddr);
    context->sqBaseAddr = post.sqBaseAddr;
    context->word1Base = static_cast<uint64_t>(post.meta.tpId & 0xFFFFFFU) |
                         (static_cast<uint64_t>(post.meta.remoteTokenId & 0xFFFFFU) << 32U);
    context->remoteEidL = post.meta.remoteEidL;
    context->remoteEidH = post.meta.remoteEidH;
    context->tokenWord = post.meta.remoteTokenValue;
    context->sqWqeSize = post.sqWqeSize;
    context->sqDepth = post.meta.sqDepth;
    asc_dcci_single(post.sqTailAddr);
    asc_dcci_single(post.headAddr);
    context->cachedSqTail = *post.sqTailAddr;
    context->itemBb = itemBb;
    context->groupSize = groupSize;
    HcommSimtPlanBatchFromHead(context, *post.headAddr);

    batchHandle.channel = channel;
    batchHandle.context = reinterpret_cast<__ubuf__ uint64_t*>(context);
    batchHandle.groupRank = groupRank;
    batchHandle.groupSize = groupSize;
    return batchHandle;
}

template <typename Group>
__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, uint32_t itemBb,
    const Group& group)
{
    UbcCtpBatchHandle batchHandle;
    constexpr uint32_t workspaceBytes = HCOMM_SIMT_BATCH_CONTEXT_BYTES + HCOMM_SIMT_BATCH_ITEM_BYTES;
    // All lanes must supply the same arguments; reject invalid storage before any lane reads it.
    if (buff == nullptr || buffLen < workspaceBytes ||
        (itemBb != HCOMM_URMA_WQE_BB_CNT && itemBb != HCOMM_URMA_DWQE_BB_CNT)) {
        return batchHandle;
    }
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    if (rank == 0U) {
        (void)MakeBatchHandle(channel, buff, buffLen, remoteBase, itemBb, 0U, size);
    }
    group.sync();

    __ubuf__ HcommSimtBatchStaticContext* context = reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(buff);
    if (context->headAddr == 0U) {
        return batchHandle;
    }
    batchHandle.channel = channel;
    batchHandle.context = reinterpret_cast<__ubuf__ uint64_t*>(context);
    batchHandle.groupRank = rank;
    batchHandle.groupSize = size;
    return batchHandle;
}

// Each operator prepares its request-specific values at final WQE offsets. The same-BB contract
// lets Group Commit derive every final SQ position directly from group rank.
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

template <bool publisher, bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtEmitFinalBatchWqe(
    __ubuf__ HcommSimtBatchStaticContext* context, __gm__ uint8_t* firstBb, __gm__ uint8_t* secondBb, uint32_t curHead,
    const HcommSimtDataDesc<commitFlag, opCode, config>& desc)
{
    constexpr uint32_t sgeNum = publisher ? 2U : HcommSimtDataDesc<commitFlag, opCode, config>::sgeNum;
    const uint32_t nextHead = curHead + (publisher ? HCOMM_URMA_DWQE_BB_CNT : context->itemBb);
    const uint64_t word0 = HcommSimtBatchSqeWord0<opCode, config>(context, curHead, nextHead);
    const uint64_t word1 = context->word1Base | (static_cast<uint64_t>(sgeNum) << 24U);
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 0U, word0, word1, context->remoteEidL, context->remoteEidH);
    if constexpr (publisher) {
        const uint32_t firstLen = static_cast<uint32_t>(desc.targetLen >> 1U);
        const uint32_t lastLen = static_cast<uint32_t>(desc.targetLen - firstLen);
        HcommSimtStoreBatchVector<true>(
            context, firstBb, secondBb, 1U, context->tokenWord, reinterpret_cast<uint64_t>(desc.remoteAddr), firstLen,
            reinterpret_cast<uint64_t>(desc.localAddr));
        HcommSimtStoreBatchVector<true>(
            context, firstBb, secondBb, 2U, lastLen, reinterpret_cast<uint64_t>(desc.localAddr + firstLen), 0U, 0U);
    } else {
        HcommSimtStoreBatchVector<false>(
            context, firstBb, secondBb, 1U, context->tokenWord, reinterpret_cast<uint64_t>(desc.remoteAddr),
            static_cast<uint32_t>(desc.targetLen), reinterpret_cast<uint64_t>(desc.localAddr));
    }
}

template <bool publisher, bool commitFlag, auto const& config>
__simt_callee__ inline void HcommSimtEmitFinalBatchWqe(
    __ubuf__ HcommSimtBatchStaticContext* context, __gm__ uint8_t* firstBb, __gm__ uint8_t* secondBb, uint32_t curHead,
    const HcommSimtNotifyDesc<commitFlag, config>& desc)
{
    const uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;
    const uint64_t word0 =
        HcommSimtBatchSqeWord0<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(context, curHead, nextHead);
    const uint64_t word1 = context->word1Base | (1ULL << 24U);
    const uint64_t notifyToken =
        static_cast<uint64_t>((word1 >> 32U) & 0xFFFFFU) | ((context->tokenWord & 0xFFFFFFFFULL) << 32U);
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 0U, word0, word1, context->remoteEidL, context->remoteEidH);
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 1U, context->tokenWord, reinterpret_cast<uint64_t>(desc.remoteAddr), notifyToken,
        reinterpret_cast<uint64_t>(desc.notifyAddr));
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 2U, desc.notifyVal, 0U, static_cast<uint32_t>(desc.targetLen),
        reinterpret_cast<uint64_t>(desc.localAddr));
}

template <bool publisher, typename T, bool commitFlag, auto const& config>
__simt_callee__ inline void HcommSimtEmitFinalBatchWqe(
    __ubuf__ HcommSimtBatchStaticContext* context, __gm__ uint8_t* firstBb, __gm__ uint8_t* secondBb, uint32_t curHead,
    const HcommSimtInlineDesc<T, commitFlag, config>& desc)
{
    static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
    static_assert(
        sizeof(T) <= HCOMM_URMA_WQE_BB_SIZE - sizeof(HcommUrmaSqeCtx),
        "WriteValue<T>: sizeof(T) must be less equal than primitive type");
    const uint32_t nextHead = curHead + (publisher ? HCOMM_URMA_DWQE_BB_CNT : context->itemBb);
    const uint64_t word0 =
        HcommSimtBatchSqeWord0<HcommUrmaOpCode::WRITE, config>(context, curHead, nextHead, sizeof(T));
    uint64_t payload[2] = {0U, 0U};
    const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&desc.value);
#pragma unroll
    for (uint32_t byte = 0U; byte < sizeof(T); ++byte) {
        payload[byte / sizeof(uint64_t)] |= static_cast<uint64_t>(valueAddr[byte]) << ((byte % sizeof(uint64_t)) * 8U);
    }
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 0U, word0, context->word1Base, context->remoteEidL, context->remoteEidH);
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 1U, context->tokenWord, reinterpret_cast<uint64_t>(desc.remoteAddr), payload[0],
        payload[1]);
    if constexpr (publisher) {
        const uint64_t owner = (nextHead & context->sqDepth) == 0U ? 1U : 0U;
        const uint64_t nopWord = static_cast<uint64_t>((nextHead + 1U) & 0xFFFFU) | (1ULL << 28U) | (1ULL << 29U) |
                                 (owner << 31U) |
                                 (static_cast<uint64_t>(static_cast<uint32_t>(HcommUrmaOpCode::NOP)) << 40U);
        HcommSimtStoreBatchVector<true>(context, firstBb, secondBb, 2U, nopWord, 0U, 0U, 0U);
    }
}

template <bool publisher, typename T, bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtEmitFinalBatchWqe(
    __ubuf__ HcommSimtBatchStaticContext* context, __gm__ uint8_t* firstBb, __gm__ uint8_t* secondBb, uint32_t curHead,
    const HcommSimtAtomicDesc<T, commitFlag, opCode, config>& desc)
{
    const uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;
    const uint64_t word0 = HcommSimtBatchSqeWord0<opCode, config>(context, curHead, nextHead);
    const uint64_t word1 = context->word1Base | (1ULL << 24U);
    uint64_t operand0 = static_cast<uint64_t>(desc.value);
    uint64_t operand1 = opCode == HcommUrmaOpCode::CAS ? static_cast<uint64_t>(desc.cond) : 0U;
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        operand0 = static_cast<uint64_t>(static_cast<uint32_t>(desc.value)) |
                   (static_cast<uint64_t>(
                        static_cast<uint32_t>(opCode == HcommUrmaOpCode::CAS ? desc.cond : static_cast<T>(0)))
                    << 32U);
        operand1 = 0U;
    }
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 0U, word0, word1, context->remoteEidL, context->remoteEidH);
    HcommSimtStoreBatchVector<publisher>(
        context, firstBb, secondBb, 1U, context->tokenWord, reinterpret_cast<uint64_t>(desc.remoteAddr), sizeof(T),
        reinterpret_cast<uint64_t>(desc.fetchAddr));
    HcommSimtStoreBatchVector<publisher>(context, firstBb, secondBb, 2U, operand0, operand1, 0U, 0U);
}

template <typename Desc>
__simt_callee__ inline int32_t HcommSimtAppendFinalBatchWqe(
    __ubuf__ HcommSimtBatchStaticContext* context, uint32_t rank, uint32_t size, const Desc& desc)
{
    static_assert(Desc::cqeCnt == 1U, "SIMT batch requests require CQE");
    if (context->reservationStatus != HCOMM_SUCCESS || context->itemBb != Desc::bbCnt) {
        return HCOMM_FAILED;
    }
    HcommSimtResolvedPost post;
    HcommSimtLoadBatchStaticPost(context, post);
    const bool publisher = rank + 1U == size;
    const uint32_t curHead = HcommSimtHeadIdx(context->reservedHead) + rank * context->itemBb;
    __gm__ uint8_t* firstBb =
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));
    __gm__ uint8_t* secondBb = (curHead % post.meta.sqDepth) + 1U == post.meta.sqDepth ?
                                   reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr) :
                                   firstBb + HCOMM_URMA_WQE_BB_SIZE;
    if (publisher) {
        HcommSimtEmitFinalBatchWqe<true>(context, firstBb, secondBb, curHead, desc);
    } else {
        HcommSimtEmitFinalBatchWqe<false>(context, firstBb, secondBb, curHead, desc);
    }
    asc_threadfence();
    return HCOMM_SUCCESS;
}

// Shared CQ completion implementation. The owner is a caller-selected lane, not a lock.
__simt_callee__ inline __gm__ HcommSimtCompletionState* HcommSimtCompletionHeader(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionState*>(set.workspace);
}
__simt_callee__ inline __gm__ HcommSimtCompletionChannel* HcommSimtCompletionChannels(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionChannel*>(set.workspace + 128U);
}
__simt_callee__ inline __gm__ HcommSimtCompletionCq* HcommSimtCompletionCqs(const UbcCtpCompletionSet& set)
{
    return reinterpret_cast<__gm__ HcommSimtCompletionCq*>(
        set.workspace + 128ULL * (1ULL + HcommSimtCompletionHeader(set)->channelCount));
}
__simt_callee__ inline bool HcommSimtCompletionReady(const UbcCtpCompletionSet& set)
{
    return set.workspace != nullptr && HcommSimtCompletionHeader(set)->version == HCOMM_SIMT_COMPLETION_VERSION &&
           HcommSimtCompletionHeader(set)->fault == 0U;
}
__simt_callee__ inline void HcommSimtCompletionFlush(const UbcCtpCompletionSet& set)
{
    asc_threadfence();
    auto* state = HcommSimtCompletionHeader(set);
    const uint64_t bytes = 128ULL * (1ULL + state->channelCount + state->cqCount);
    // All fields fit in the first 64B of each aligned record (asserted in the definition).
    // Padding and unused CQ records contain no accounting state.
    for (uint64_t offset = 0U; offset < bytes; offset += 128U)
        asc_dcci_single(set.workspace + offset);
    asc_threadfence();
}
__simt_callee__ inline void HcommSimtCompletionAcknowledge(const UbcCtpCompletionSet& set)
{
    // Per-Jetty accounting and SQ reclamation must be durable before releasing CQ slots.
    HcommSimtCompletionFlush(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    for (uint32_t q = 0U; q < HcommSimtCompletionHeader(set)->cqCount; ++q) {
        auto* cq = &cqs[q];
        if (cq->consumed == cq->acknowledged)
            continue;
        *reinterpret_cast<__gm__ uint32_t*>(cq->dbAddr) = cq->consumed & 0xFFFFFFU;
        asc_threadfence();
        cq->acknowledged = cq->consumed;
        asc_dcci_single(cq);
    }
    asc_threadfence();
}
__simt_callee__ inline void HcommSimtCompletionFail(
    UbcCtpCompletionSet& set, HcommSimtCompletionFault fault, uint32_t q, uint32_t channel, uint32_t sequence,
    uint32_t w0 = 0U, uint32_t w1 = 0U, uint32_t w2 = 0U)
{
    auto* state = HcommSimtCompletionHeader(set);
    state->fault = static_cast<uint32_t>(fault);
    state->errorCq = q;
    state->errorChannel = channel;
    state->errorSequence = sequence;
    state->errorWord0 = w0;
    state->errorWord1 = w1;
    state->errorWord2 = w2;
}
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::InitCompletionSet(
    UbcCtpCompletionSet& set, __gm__ ChannelHandle* channels, uint32_t count, __gm__ uint8_t* workspace, uint64_t bytes)
{
    set.workspace = nullptr;
    // Keep the runtime calculation in SIMT; the public constexpr size query is also host-usable.
    const uint64_t required = count == 0U || count > 65535U ? 0ULL : 128ULL * (1ULL + 2ULL * count);
    if (channels == nullptr || workspace == nullptr || required == 0U || bytes < required ||
        (reinterpret_cast<uint64_t>(workspace) & 127U) != 0U)
        return HCOMM_FAILED;
    // Caller passes fresh zeroed storage. Refuse reinitializing a live or failed set.
    asc_dcci_single(workspace);
    if (*reinterpret_cast<__gm__ uint32_t*>(workspace) != 0U)
        return HCOMM_FAILED;
    set.workspace = workspace;
    for (uint64_t offset = 0U; offset < required; offset += sizeof(uint64_t))
        *reinterpret_cast<__gm__ uint64_t*>(workspace + offset) = 0U;
    auto* state = HcommSimtCompletionHeader(set);
    state->channelCount = count;
    state->errorCq = HCOMM_SIMT_COMPLETION_NONE;
    state->errorChannel = HCOMM_SIMT_COMPLETION_NONE;
    auto* entries = HcommSimtCompletionChannels(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    for (uint32_t i = 0U; i < count; ++i) {
        if (channels[i] == 0U) {
            state->fault = 1U;
            break;
        }
        auto* channel = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channels[i]);
        if (channel->sqNum == 0U || channel->cqNum == 0U || channel->sqContextAddr == 0U ||
            channel->cqContextAddr == 0U) {
            state->fault = 1U;
            break;
        }
        auto* sq = reinterpret_cast<__gm__ SqContext*>(channel->sqContextAddr);
        auto* cq = reinterpret_cast<__gm__ CqContext*>(channel->cqContextAddr);
        auto* entry = &entries[i];
        entry->channel = channels[i];
        entry->sqBase = sq->contextInfo.ubJfs.sqVa;
        entry->headAddr = sq->contextInfo.ubJfs.headAddr;
        entry->tailAddr = sq->contextInfo.ubJfs.tailAddr;
        entry->jfsId = sq->contextInfo.ubJfs.jfsID;
        entry->sqDepth = sq->contextInfo.ubJfs.sqDepth;
        const uint32_t cqDepth = cq->contextInfo.ubJfc.cqDepth;
        if (sq->type != SQ_CONTEXT_TYPE_UB_JFS || cq->type != CQ_CONTEXT_TYPE_UB_JFC || entry->sqBase == 0U ||
            entry->headAddr == 0U || entry->tailAddr == 0U || (entry->headAddr & 7U) != 0U ||
            (entry->tailAddr & 3U) != 0U || entry->jfsId > 0xFFFFFU || entry->sqDepth < 2U ||
            (entry->sqDepth & (entry->sqDepth - 1U)) != 0U || cqDepth < 2U || (cqDepth & (cqDepth - 1U)) != 0U ||
            cq->contextInfo.ubJfc.scqVa == 0U || cq->contextInfo.ubJfc.dbVa == 0U ||
            cq->contextInfo.ubJfc.tailAddr == 0U || cq->contextInfo.ubJfc.cqeSize < 12U ||
            (cq->contextInfo.ubJfc.cqeSize & 3U) != 0U) {
            state->fault = 1U;
            break;
        }
        auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
        auto* tail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
        auto* cqTail = reinterpret_cast<__gm__ uint32_t*>(cq->contextInfo.ubJfc.tailAddr);
        asc_dcci_single(head);
        asc_dcci_single(tail);
        asc_dcci_single(cqTail);
        if (*head != 0ULL || *tail != 0U || *cqTail != 0U) {
            state->fault = 1U;
            break;
        }
        uint32_t q = 0U;
        for (; q < state->cqCount; ++q) {
            const bool sameBase = cqs[q].base == cq->contextInfo.ubJfc.scqVa;
            const bool sameId = cqs[q].jfcId == cq->contextInfo.ubJfc.jfcID;
            if (sameBase != sameId) {
                state->fault = 1U;
                break;
            }
            if (sameBase) {
                if (cqs[q].depth != cqDepth || cqs[q].cqeSize != cq->contextInfo.ubJfc.cqeSize)
                    state->fault = 1U;
                break;
            }
        }
        if (state->fault != 0U)
            break;
        if (q == state->cqCount) {
            cqs[q].base = cq->contextInfo.ubJfc.scqVa;
            cqs[q].dbAddr = cq->contextInfo.ubJfc.dbVa;
            cqs[q].depth = cqDepth;
            cqs[q].cqeSize = cq->contextInfo.ubJfc.cqeSize;
            cqs[q].jfcId = cq->contextInfo.ubJfc.jfcID;
            ++state->cqCount;
        }
        entry->cqIndex = q;
        for (uint32_t prior = 0U; prior < i; ++prior) {
            if (entries[prior].channel == entry->channel || entries[prior].sqBase == entry->sqBase ||
                entries[prior].headAddr == entry->headAddr || entries[prior].tailAddr == entry->tailAddr ||
                (entries[prior].cqIndex == q && entries[prior].jfsId == entry->jfsId))
                state->fault = 1U;
        }
        if (state->fault != 0U)
            break;
    }
    state->version = HCOMM_SIMT_COMPLETION_VERSION;
    HcommSimtCompletionFlush(set);
    return state->fault == 0U ? HCOMM_SUCCESS : HCOMM_FAILED;
}
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PrepareCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t wqeCount, uint32_t bbCount)
{
    if (!HcommSimtCompletionReady(set) || channelIndex >= HcommSimtCompletionHeader(set)->channelCount)
        return HCOMM_FAILED;
    auto* entries = HcommSimtCompletionChannels(set);
    auto* entry = &entries[channelIndex];
    // One admitted window per Jetty. No requirement that any other Jetty be idle.
    if (wqeCount == 0U || bbCount < wqeCount || static_cast<uint64_t>(bbCount) > 2ULL * wqeCount ||
        bbCount >= entry->sqDepth || entry->completed != entry->target || wqeCount > 0xFFFFFFFFU - entry->target ||
        bbCount > 0xFFFFFFFFU - entry->targetBb)
        return HCOMM_FAILED;
    auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
    auto* tail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
    asc_dcci_single(head);
    asc_dcci_single(tail);
    const uint64_t expectedHead = (static_cast<uint64_t>(entry->target) << 32U) | entry->targetBb;
    if (*head != expectedHead || *tail != entry->targetBb)
        return HCOMM_FAILED;
    uint64_t pending = wqeCount;
    for (uint32_t i = 0U; i < HcommSimtCompletionHeader(set)->channelCount; ++i) {
        if (entries[i].cqIndex == entry->cqIndex)
            pending += entries[i].target - entries[i].completed;
    }
    if (pending >= HcommSimtCompletionCqs(set)[entry->cqIndex].depth)
        return HCOMM_FAILED;
    entry->target += wqeCount;
    entry->targetBb += bbCount;
    asc_threadfence();
    asc_dcci_single(entry);
    asc_threadfence();
    return HCOMM_SUCCESS;
}
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Progress(
    UbcCtpCompletionSet& set, uint32_t budget, uint32_t& processed)
{
    processed = 0U;
    if (!HcommSimtCompletionReady(set) || budget == 0U)
        return HCOMM_FAILED;
    auto* state = HcommSimtCompletionHeader(set);
    auto* entries = HcommSimtCompletionChannels(set);
    auto* cqs = HcommSimtCompletionCqs(set);
    // Set operations have one owner. Publish cursor changes at the call boundary,
    // before acknowledgement, instead of storing the round-robin cursor for every CQE.
    const uint32_t channelCount = state->channelCount;
    const uint32_t cqCount = state->cqCount;
    uint32_t nextCq = state->nextCq;
    uint32_t singleConsumed = cqCount == 1U ? cqs[0].consumed : 0U;
    uint32_t unavailable = 0U;
    while (processed < budget && unavailable < cqCount) {
        const uint32_t q = nextCq;
        nextCq = q + 1U == cqCount ? 0U : q + 1U;
        auto* cq = &cqs[q];
        const uint32_t sequence = cqCount == 1U ? singleConsumed : cq->consumed;
        auto* words = reinterpret_cast<__gm__ volatile uint32_t*>(
            cq->base + static_cast<uint64_t>(cq->cqeSize) * (sequence & (cq->depth - 1U)));
        asc_dcci_single(const_cast<__gm__ uint32_t*>(words));
        const uint32_t w0 = words[0];
        if (((w0 >> 2U) & 1U) == ((sequence / cq->depth) & 1U)) {
            ++unavailable;
            continue;
        }
        const uint32_t w1 = words[1], w2 = words[2];
        const uint32_t localNum = (w1 >> 16U) | ((w2 & 0xFU) << 16U);
        uint32_t match = HCOMM_SIMT_COMPLETION_NONE;
        for (uint32_t i = 0U; i < channelCount; ++i) {
            if (entries[i].cqIndex == q && entries[i].jfsId == localNum) {
                match = i;
                break;
            }
        }
        if ((w0 & 3U) != 2U || match == HCOMM_SIMT_COMPLETION_NONE) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::IDENTITY, q, match, sequence, w0, w1, w2);
            break;
        }
        if ((w0 & 0xFFFF0000U) != 0U) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::CQE, q, match, sequence, w0, w1, w2);
            break;
        }
        auto* entry = &entries[match];
        if (entry->completed == entry->target) {
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::COUNT, q, match, sequence, w0, w1, w2);
            break;
        }
        if (entry->completed + 1U == entry->target) {
            // The window's own successful completions prove its full BB range reclaimable.
            // Never apply another Jetty's entryIdx or release an incomplete window.
            auto* head = reinterpret_cast<__gm__ uint64_t*>(entry->headAddr);
            asc_dcci_single(head);
            const uint64_t expectedHead = (static_cast<uint64_t>(entry->target) << 32U) | entry->targetBb;
            if (*head != expectedHead) {
                HcommSimtCompletionFail(set, HcommSimtCompletionFault::COUNT, q, match, sequence, w0, w1, w2);
                break;
            }
            auto* sqTail = reinterpret_cast<__gm__ uint32_t*>(entry->tailAddr);
            *sqTail = entry->targetBb;
            asc_threadfence();
            asc_dcci_single(sqTail);
            asc_threadfence();
        }
        ++entry->completed;
        if (cqCount == 1U)
            ++singleConsumed;
        else
            ++cq->consumed;
        ++processed;
        unavailable = 0U;
    }
    state->nextCq = nextCq;
    if (cqCount == 1U)
        cqs[0].consumed = singleConsumed;
    if (processed != 0U || state->fault != 0U) {
        HcommSimtCompletionAcknowledge(set);
    }
    return state->fault == 0U ? HCOMM_SUCCESS : HCOMM_FAILED;
}
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WaitCompletion(
    UbcCtpCompletionSet& set, uint32_t channelIndex, uint32_t maxIdlePolls)
{
    if (!HcommSimtCompletionReady(set) || channelIndex >= HcommSimtCompletionHeader(set)->channelCount ||
        maxIdlePolls == 0U)
        return HCOMM_FAILED;
    auto* entry = &HcommSimtCompletionChannels(set)[channelIndex];
    uint32_t idle = 0U;
    while (entry->completed != entry->target) {
        const uint32_t before = entry->completed;
        uint32_t processed = 0U;
        if (Progress(set, 64U, processed) != HCOMM_SUCCESS)
            return HCOMM_FAILED;
        // Traffic from a fast Jetty cannot reset the slow Jetty's idle limit forever.
        idle = entry->completed != before ? 0U : idle + 1U;
        if (idle >= maxIdlePolls) {
            auto* cq = &HcommSimtCompletionCqs(set)[entry->cqIndex];
            HcommSimtCompletionFail(set, HcommSimtCompletionFault::TIMEOUT, entry->cqIndex, channelIndex, cq->consumed);
            HcommSimtCompletionAcknowledge(set);
            return HCOMM_FAILED;
        }
    }
    return HCOMM_SUCCESS;
}
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(UbcCtpCompletionSet& set)
{
    if (!HcommSimtCompletionReady(set))
        return HCOMM_FAILED;
    for (uint32_t i = 0U; i < HcommSimtCompletionHeader(set)->channelCount; ++i) {
        if (WaitCompletion(set, i) != HCOMM_SUCCESS)
            return HCOMM_FAILED;
    }
    return HCOMM_SUCCESS;
}
// End shared CQ completion implementation.

template <bool refreshCache>
__simt_callee__ inline uint32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCq(ChannelHandle channel, uint32_t expectIdx)
{
    if (expectIdx == 0U) {
        return HCOMM_SUCCESS;
    }
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ CqContext* cqContexts = reinterpret_cast<__gm__ CqContext*>(channelEntity->cqContextAddr);
    __gm__ CqContext* cqCtx = &cqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* tailAddr = (__gm__ uint32_t*)cqCtx->contextInfo.ubJfc.tailAddr;
    if constexpr (refreshCache) {
        asc_dcci_single(tailAddr);
    }
    uint32_t curTail = *tailAddr;

    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint32_t* sqTailAddr = (__gm__ uint32_t*)sqCtx->contextInfo.ubJfs.tailAddr;
    if constexpr (refreshCache) {
        asc_dcci_single(sqTailAddr);
    }
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
        if constexpr (refreshCache) {
            asc_dcci_single(cqeWords);
        }
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
        if constexpr (refreshCache) {
            if (sqDepth == 0U) {
                return 0xFFU;
            }
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
    if constexpr (refreshCache) {
        asc_threadfence();
        asc_dcci_single(tailAddr);
        asc_dcci_single(sqTailAddr);
        asc_threadfence();
    }
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
        if (cqTail == HcommSimtWqeCnt(headVal) || PollCq<false>(channel, cqTail + 1U) != HCOMM_SUCCESS) {
            return false;
        }
        if (HcommSimtTryReserve(
                post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, cqeCnt, headVal)) {
            return true;
        }
    }
}

template <uint32_t bbCnt, bool commit>
__simt_callee__ inline void HcommSimtPublishReservedStage(
    const HcommSimtResolvedPost& post, uint32_t curHead, const HcommSimtWqeStage& stage)
{
    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));
    HcommSimtCopyStageWqeToSq<bbCnt>(post.sqBaseAddr, sqeAddr, post.meta.sqDepth, curHead, stage);
    if constexpr (commit) {
        HcommSimtRingDwqe(post.dwqeAddr, stage);
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

    HcommSimtPackedSqeTemplate sqeTemplate;
    HcommSimtBuildPackedSqeTemplate<Desc::sgeNum>(post.meta, sqeTemplate);

    // Stage the WQE in lane-private storage, then copy it into the SQ. The SQ copy is what the
    // NIC always reads, so it happens on every path; a committed post then publishes the
    // doorbell out of the same staged bytes.
    HcommSimtWqeStage stage;
    desc.template Store<wordCnt>(stage.words, sqeTemplate, curHead, nextHead);
    HcommSimtPublishReservedStage<bbCnt, Desc::commit>(post, curHead, stage);
    return HCOMM_SUCCESS;
}

template <typename Desc>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchPostWqe(
    UbcCtpBatchHandle& batchHandle, const Desc& desc)
{
    __ubuf__ HcommSimtBatchStaticContext* context =
        reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batchHandle.context);
    return HcommSimtAppendFinalBatchWqe(context, 0U, 1U, desc);
}

template <typename Desc, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchPostWqe(
    UbcCtpBatchHandle& batchHandle, const Desc& desc, const Group& group)
{
    // BatchCommit synchronizes the group before the publisher advances the shared head.
    (void)group;
    __ubuf__ HcommSimtBatchStaticContext* context =
        reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batchHandle.context);
    return HcommSimtAppendFinalBatchWqe(context, batchHandle.groupRank, batchHandle.groupSize, desc);
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::WRITE, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return BatchPostWqe(
        batchHandle, HcommSimtDataDesc<false, HcommUrmaOpCode::WRITE, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtDataDesc<false, HcommUrmaOpCode::WRITE, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len},
        group);
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return PostWqe(channel, HcommSimtInlineDesc<T, commit, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value)
{
    return BatchPostWqe(
        batchHandle, HcommSimtInlineDesc<T, false, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value, const Group& group)
{
    return BatchPostWqe(
        batchHandle, HcommSimtInlineDesc<T, false, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value}, group);
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

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return BatchPostWqe(
        batchHandle, HcommSimtDataDesc<false, HcommUrmaOpCode::READ, config>{
                         reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, const Group& group)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtDataDesc<false, HcommUrmaOpCode::READ, config>{
            reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len},
        group);
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

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return BatchPostWqe(
        batchHandle, HcommSimtNotifyDesc<true, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                         reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal});
}

template <auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal, const Group& group)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtNotifyDesc<true, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
            reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal},
        group);
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

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return BatchPostWqe(
        batchHandle, HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::FAA, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
                         static_cast<T>(0)});
}

template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal, const Group& group)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::FAA, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
            static_cast<T>(0)},
        group);
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

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::CAS, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal, compareVal});
}

template <typename T, auto const& config, typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal,
    const Group& group)
{
    return BatchPostWqe(
        batchHandle,
        HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::CAS, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal, compareVal},
        group);
}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchCommit(UbcCtpBatchHandle& batchHandle)
{
    __ubuf__ HcommSimtBatchStaticContext* context =
        reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batchHandle.context);
    if (context->reservationStatus != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    HcommSimtResolvedPost post;
    HcommSimtLoadBatchStaticPost(context, post);
    const uint64_t publishedHead = context->reservedHead + ((1ULL << 32U) | HcommSimtBatchTotalBb(1U, context->itemBb));
    *post.headAddr = publishedHead;
    asc_dcci_single(post.headAddr);
    asc_threadfence();
    HcommSimtRingBatchDwqe(post.dwqeAddr, HcommSimtBatchPublisherWords(context));
    HcommSimtPlanBatchFromHead(context, publishedHead);
    return HCOMM_SUCCESS;
}

template <typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchCommit(
    UbcCtpBatchHandle& batchHandle, const Group& group)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    const bool publisher = rank + 1U == size;
    __ubuf__ HcommSimtBatchStaticContext* context =
        reinterpret_cast<__ubuf__ HcommSimtBatchStaticContext*>(batchHandle.context);
    if (context->reservationStatus != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    // Append has ordered every lane's SQ stores before the publisher advances the shared head.
    group.sync();
    if (publisher) {
        HcommSimtResolvedPost post;
        HcommSimtLoadBatchStaticPost(context, post);
        const uint32_t totalBb = HcommSimtBatchTotalBb(size, context->itemBb);
        const uint64_t publishedHead = context->reservedHead + ((static_cast<uint64_t>(size) << 32U) | totalBb);
        *post.headAddr = publishedHead;
        asc_dcci_single(post.headAddr);
        asc_threadfence();
        HcommSimtRingBatchDwqe(post.dwqeAddr, HcommSimtBatchPublisherWords(context));
        HcommSimtPlanBatchFromHead(context, publishedHead);
    }
    group.sync();
    return HCOMM_SUCCESS;
}

template <bool refreshCache, auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::DrainChannel(ChannelHandle channel)
{
    (void)pipe;
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    __gm__ uint64_t* headAddr = (__gm__ uint64_t*)sqCtx->contextInfo.ubJfs.headAddr;

    // wqeCnt counts only CQE-producing WQEs, so it is directly comparable with the CQ tail.
    if constexpr (refreshCache) {
        asc_dcci_single(headAddr);
    }
    uint64_t headVal = *headAddr;
    uint32_t wqeCnt = HcommSimtWqeCnt(headVal);
    uint32_t pollRet = PollCq<refreshCache>(channel, wqeCnt);
    return pollRet == HCOMM_SUCCESS ? HCOMM_SUCCESS : HCOMM_FAILED;
}

template <auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(ChannelHandle channel)
{
    return DrainChannel<false, pipe>(channel);
}

template <auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(UbcCtpBatchHandle& batchHandle)
{
    return DrainChannel<true, pipe>(batchHandle.channel);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif
