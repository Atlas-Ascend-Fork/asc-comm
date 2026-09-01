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

// Single-lane Append stores a prepared WQE in every public 128-byte slot. Group Append instead
// stores one compact request per lane; Group Commit reserves the range and lets each lane build
// its final-position WQE, expanding only the publisher to the complete two-BB DWQE image.
enum class HcommSimtBatchKind : uint32_t { DATA, NOTIFY, INLINE, ATOMIC };

struct alignas(sizeof(ulonglong2)) HcommSimtPreparedBatchWqe {
    uint64_t words[HCOMM_SIMT_WQE_WORDS];
};
static_assert(
    sizeof(HcommSimtPreparedBatchWqe) == HCOMM_URMA_DWQE_SIZE,
    "A prepared batch WQE occupies one public 128-byte operation slot");

// A group lane records only request-specific values during Append. Once Commit has reserved the
// final SQ range, the lane turns this descriptor directly into its final SQ WQE. The public slot
// remains 128 bytes so the workspace contract stays unchanged; only the first 64-byte BB is used.
struct alignas(sizeof(ulonglong2)) HcommSimtBatchDescriptor {
    uint64_t header;
    uint64_t remoteAddr;
    uint64_t localAddr;
    uint64_t value0;
    uint64_t value1;
    uint64_t extraAddr;
    HcommSimtBatchKind kind;
};
static_assert(
    sizeof(HcommSimtBatchDescriptor) == HCOMM_URMA_WQE_BB_SIZE,
    "A compact group descriptor must fit in the first half of one public batch slot");

__simt_callee__ inline __ubuf__ HcommSimtBatchDescriptor* HcommSimtBatchDescriptorAt(
    __ubuf__ uint64_t* buffer, uint32_t index)
{
    return reinterpret_cast<__ubuf__ HcommSimtBatchDescriptor*>(
        buffer + static_cast<uint64_t>(index) * HCOMM_SIMT_WQE_WORDS);
}

template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline uint64_t HcommSimtBatchHeader(uint32_t inlineMsgLen = 0U)
{
    return (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) |
        (1ULL << 28U) | (1ULL << 29U) |
        (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U) |
        (static_cast<uint64_t>(inlineMsgLen & 0x3FFU) << 54U);
}

__simt_callee__ inline HcommSimtBatchKind HcommSimtPreparedBatchKind(uint64_t firstWord)
{
    const auto opcode = static_cast<HcommUrmaOpCode>((firstWord >> 40U) & 0xFFU);
    if (opcode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
        return HcommSimtBatchKind::NOTIFY;
    }
    if (opcode == HcommUrmaOpCode::FAA || opcode == HcommUrmaOpCode::CAS) {
        return HcommSimtBatchKind::ATOMIC;
    }
    return ((firstWord >> 54U) & 0x3FFU) != 0U ? HcommSimtBatchKind::INLINE : HcommSimtBatchKind::DATA;
}

__simt_callee__ inline uint32_t HcommSimtPreparedBatchBb(uint64_t firstWord)
{
    const HcommSimtBatchKind kind = HcommSimtPreparedBatchKind(firstWord);
    return kind == HcommSimtBatchKind::DATA || kind == HcommSimtBatchKind::INLINE ?
        HCOMM_URMA_WQE_BB_CNT : HCOMM_URMA_DWQE_BB_CNT;
}

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

// Group Append records only request-specific values. After the collective reservation fixes the
// lane's final SQ position, this routine materializes exactly the words consumed by that opcode.
// Non-publishers keep their natural one/two-BB layout; only the global tail is expanded to the
// complete two-BB DWQE image when Write/Read or inline Write would otherwise occupy one BB.
__simt_callee__ inline void HcommSimtBuildBatchDescriptorStage(
    __ubuf__ HcommSimtBatchDescriptor* item, const HcommSimtPackedSqeTemplate& currentTemplate,
    uint32_t curHead, uint32_t nextHead, bool publisher, HcommSimtWqeStage& stage)
{
    const HcommSimtBatchKind kind = item->kind;
    const uint32_t sgeNum = kind == HcommSimtBatchKind::INLINE ? 0U :
        (kind == HcommSimtBatchKind::DATA && publisher ? 2U : 1U);
    const uint32_t owner = (curHead & currentTemplate.sqDepth) == 0U ? 1U : 0U;
    stage.words[0] = (item->header & ~((1ULL << 31U) | 0xFFFFULL)) |
        static_cast<uint64_t>(nextHead & 0xFFFFU) | (static_cast<uint64_t>(owner) << 31U);
    stage.words[1] = (currentTemplate.word1 & ~(0xFFULL << 24U)) |
        (static_cast<uint64_t>(sgeNum) << 24U);
    stage.words[2] = currentTemplate.remoteEidL;
    stage.words[3] = currentTemplate.remoteEidH;
    stage.words[4] = currentTemplate.tokenWord;
    stage.words[5] = item->remoteAddr;

    if (kind == HcommSimtBatchKind::DATA) {
        const uint32_t len = static_cast<uint32_t>(item->value0);
        if (publisher) {
            const uint32_t firstLen = len >> 1U;
            stage.words[6] = firstLen;
            stage.words[7] = item->localAddr;
            stage.words[8] = len - firstLen;
            stage.words[9] = item->localAddr + firstLen;
        } else {
            stage.words[6] = len;
            stage.words[7] = item->localAddr;
        }
    } else if (kind == HcommSimtBatchKind::NOTIFY) {
        stage.words[6] = static_cast<uint64_t>((currentTemplate.word1 >> 32U) & 0xFFFFFU) |
            ((currentTemplate.tokenWord & 0xFFFFFFFFULL) << 32U);
        stage.words[7] = item->extraAddr;
        stage.words[8] = item->value1;
        stage.words[9] = 0U;
        stage.words[10] = static_cast<uint32_t>(item->value0);
        stage.words[11] = item->localAddr;
    } else if (kind == HcommSimtBatchKind::INLINE) {
        stage.words[6] = item->value0;
        const uint32_t inlineLen = static_cast<uint32_t>((item->header >> 54U) & 0x3FFU);
        if (inlineLen > sizeof(uint64_t)) {
            stage.words[7] = item->value1;
        }
        if (publisher) {
            const uint32_t nopOwner = (nextHead & currentTemplate.sqDepth) == 0U ? 1U : 0U;
            stage.words[8] = static_cast<uint64_t>((nextHead + 1U) & 0xFFFFU) |
                (1ULL << 28U) | (1ULL << 29U) | (static_cast<uint64_t>(nopOwner) << 31U) |
                (static_cast<uint64_t>(static_cast<uint32_t>(HcommUrmaOpCode::NOP)) << 40U);
        }
    } else {
        const uint32_t operandBytes = static_cast<uint32_t>(item->extraAddr);
        stage.words[6] = operandBytes;
        stage.words[7] = item->localAddr;
        if (operandBytes == sizeof(uint32_t)) {
            stage.words[8] = static_cast<uint64_t>(static_cast<uint32_t>(item->value0)) |
                (static_cast<uint64_t>(static_cast<uint32_t>(item->value1)) << 32U);
            stage.words[9] = 0U;
        } else {
            stage.words[8] = item->value0;
            stage.words[9] = item->value1;
        }
    }
}

__simt_callee__ inline void HcommSimtBuildPreparedBatchTail(
    __ubuf__ HcommSimtPreparedBatchWqe* item, const HcommSimtPackedSqeTemplate& currentTemplate,
    uint32_t curHead, uint32_t nextHead, HcommSimtWqeStage& stage)
{
    const uint64_t preparedFirstWord = item->words[0];
    const HcommSimtBatchKind kind = HcommSimtPreparedBatchKind(preparedFirstWord);
    const uint32_t sgeNum = kind == HcommSimtBatchKind::INLINE ? 0U :
        (kind == HcommSimtBatchKind::DATA ? 2U : 1U);
    const uint32_t owner = (curHead & currentTemplate.sqDepth) == 0U ? 1U : 0U;

    // Only fields selected by the opcode/SGE count are consumed by the NIC. This matches the
    // immediate path: overwrite every effective word without clearing the unused tail words.
    stage.words[0] = (preparedFirstWord & ~((1ULL << 31U) | 0xFFFFULL)) |
        static_cast<uint64_t>(nextHead & 0xFFFFU) | (static_cast<uint64_t>(owner) << 31U);
    // Queue placement fields come from the Commit-time context; the remote token remains tied to
    // the registration selected by the lane's handle at Append.
    stage.words[1] = (currentTemplate.word1 & ~((0xFFULL << 24U) | (0xFFFFFULL << 32U))) |
        (static_cast<uint64_t>(sgeNum) << 24U) | (item->words[1] & (0xFFFFFULL << 32U));
    stage.words[2] = currentTemplate.remoteEidL;
    stage.words[3] = currentTemplate.remoteEidH;
    stage.words[4] = item->words[4];
    stage.words[5] = item->words[5];
    if (kind == HcommSimtBatchKind::DATA) {
        const uint32_t len = static_cast<uint32_t>(item->words[6]);
        const uint32_t firstLen = len >> 1U;
        stage.words[6] = firstLen;
        stage.words[7] = item->words[7];
        stage.words[8] = len - firstLen;
        stage.words[9] = item->words[7] + firstLen;
    } else if (kind == HcommSimtBatchKind::NOTIFY) {
#pragma unroll
        for (uint32_t word = 6U; word < 12U; ++word) {
            stage.words[word] = item->words[word];
        }
    } else if (kind == HcommSimtBatchKind::INLINE) {
#pragma unroll
        for (uint32_t word = 6U; word < 8U; ++word) {
            stage.words[word] = item->words[word];
        }
        // Preserve the validated immediate-path NOP encoding: the padding header is based on
        // the WQE's nextHead, even though its bytes occupy the second physical BB.
        const uint32_t nopOwner = (nextHead & currentTemplate.sqDepth) == 0U ? 1U : 0U;
        stage.words[8] = static_cast<uint64_t>((nextHead + 1U) & 0xFFFFU) |
            (1ULL << 28U) | (1ULL << 29U) | (static_cast<uint64_t>(nopOwner) << 31U) |
            (static_cast<uint64_t>(static_cast<uint32_t>(HcommUrmaOpCode::NOP)) << 40U);
    } else {
#pragma unroll
        for (uint32_t word = 6U; word < 10U; ++word) {
            stage.words[word] = item->words[word];
        }
    }
}

// Append has already generated every byte consumed by a non-tail WQE.
// Commit only streams that final image to its reserved SQ slot; no opcode decoding is needed.
template <bool patchPosition>
__simt_callee__ inline void HcommSimtCopyPreparedBatchWqeToSq(
    const HcommSimtResolvedPost& post, __ubuf__ HcommSimtPreparedBatchWqe* item,
    uint32_t itemBb, uint32_t curHead)
{
    __gm__ uint8_t* sqeAddr = reinterpret_cast<__gm__ uint8_t*>(
        post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));
    uint64_t firstWord = item->words[0];
    if constexpr (patchPosition) {
        const uint32_t owner = (curHead & post.meta.sqDepth) == 0U ? 1U : 0U;
        firstWord = (firstWord & ~((1ULL << 31U) | 0xFFFFULL)) |
            static_cast<uint64_t>((curHead + itemBb) & 0xFFFFU) | (static_cast<uint64_t>(owner) << 31U);
    }
    __gm__ ulonglong4* firstBb = reinterpret_cast<__gm__ ulonglong4*>(sqeAddr);
    asc_stwt(&firstBb[0], make_ulonglong4(
        firstWord, item->words[1], item->words[2], item->words[3]));
    asc_stwt(&firstBb[1], make_ulonglong4(
        item->words[4], item->words[5], item->words[6], item->words[7]));
    if (itemBb == HCOMM_URMA_WQE_BB_CNT) {
        return;
    }

    __gm__ uint8_t* secondBbAddr = (curHead % post.meta.sqDepth) == post.meta.sqDepth - 1U ?
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr) : sqeAddr + HCOMM_URMA_WQE_BB_SIZE;
    __gm__ ulonglong4* secondBb = reinterpret_cast<__gm__ ulonglong4*>(secondBbAddr);
    asc_stwt(secondBb, make_ulonglong4(
        item->words[8], item->words[9], item->words[10], item->words[11]));
}

// A group lane has already constructed its final-position WQE in registers. Write only the
// opcode-defined SQ area: one 64-byte BB for data/inline, or the additional 32-byte context for
// notify/atomic. The publisher uses the full-size helper instead because its image is also sent
// through the 128-byte DWQE window.
__simt_callee__ inline void HcommSimtCopyBatchDescriptorStageToSq(
    const HcommSimtResolvedPost& post, const HcommSimtWqeStage& stage, uint32_t itemBb, uint32_t curHead)
{
    __gm__ uint8_t* sqeAddr = reinterpret_cast<__gm__ uint8_t*>(
        post.sqBaseAddr + post.sqWqeSize * (curHead % post.meta.sqDepth));
    __gm__ ulonglong4* firstBb = reinterpret_cast<__gm__ ulonglong4*>(sqeAddr);
    asc_stwt(&firstBb[0], make_ulonglong4(
        stage.words[0], stage.words[1], stage.words[2], stage.words[3]));
    asc_stwt(&firstBb[1], make_ulonglong4(
        stage.words[4], stage.words[5], stage.words[6], stage.words[7]));
    if (itemBb == HCOMM_URMA_WQE_BB_CNT) {
        return;
    }

    __gm__ uint8_t* secondBbAddr = (curHead % post.meta.sqDepth) == post.meta.sqDepth - 1U ?
        reinterpret_cast<__gm__ uint8_t*>(post.sqBaseAddr) : sqeAddr + HCOMM_URMA_WQE_BB_SIZE;
    __gm__ ulonglong4* secondBb = reinterpret_cast<__gm__ ulonglong4*>(secondBbAddr);
    asc_stwt(secondBb, make_ulonglong4(
        stage.words[8], stage.words[9], stage.words[10], stage.words[11]));
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

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::HcommImpl() {}

__simt_callee__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::~HcommImpl() {}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    groupState_ = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    groupStateWords_ = len / sizeof(uint32_t);
    initialized_ = true;
    return HCOMM_SUCCESS;
}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ResolvePost(
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

// Queue placement fields are consumed at Commit, so load one current SQ context there instead of
// relying on the snapshot taken while the batch handle was created.
__simt_callee__ inline void HcommSimtLoadBatchQueuePost(
    ChannelHandle channel, HcommSimtResolvedPost& post)
{
    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
    __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
    post.headAddr = reinterpret_cast<__gm__ uint64_t*>(sqCtx->contextInfo.ubJfs.headAddr);
    post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(sqCtx->contextInfo.ubJfs.tailAddr);
    post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(
        sqCtx->contextInfo.ubJfs.dbVa - HCOMM_URMA_DWQE_DB_OFFSET);
    post.sqBaseAddr = sqCtx->contextInfo.ubJfs.sqVa;
    post.sqWqeSize = sqCtx->contextInfo.ubJfs.wqeSize;
    post.meta.sqDepth = sqCtx->contextInfo.ubJfs.sqDepth;
    post.meta.tpId = sqCtx->contextInfo.ubJfs.tpID;
    __gm__ uint64_t* remoteEid = reinterpret_cast<__gm__ uint64_t*>(&sqCtx->contextInfo.ubJfs.remoteEID[0]);
    post.meta.remoteEidL = remoteEid[0];
    post.meta.remoteEidH = remoteEid[1];
}

__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase)
{
    return MakeBatchHandle(channel, buff, buffLen, remoteBase, 0U, 1U);
}

__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase,
    uint32_t groupRank, uint32_t groupSize)
{
    UbcCtpBatchHandle batchHandle;
    if (!initialized_ || channel == 0U || buff == nullptr || remoteBase == nullptr ||
        groupSize == 0U || groupRank >= groupSize ||
        (reinterpret_cast<uint64_t>(buff) % sizeof(ulonglong2)) != 0U) {
        return batchHandle;
    }

    HcommSimtResolvedPost post;
    __gm__ uint8_t* remoteAddr = reinterpret_cast<__gm__ uint8_t*>(remoteBase);
    // Probe one real byte, like the validated SIMD batch path. A zero-length lookup also
    // matches the one-past-end address of the preceding registration when two buffers are
    // adjacent, which selects the wrong token while still producing a seemingly valid handle.
    if (ResolvePost(channel, remoteAddr, 1U, post) != HCOMM_SUCCESS) {
        return batchHandle;
    }
    const uint32_t totalCapacity = buffLen / HCOMM_URMA_DWQE_SIZE;
    // A bound group contributes exactly one item per lane. Extra caller-owned slots are allowed
    // so one maximum-sized workspace can be reused by smaller groups, but are not part of the batch.
    const bool groupBatch = groupSize > 1U;
    const uint32_t capacity = groupBatch ? 1U : totalCapacity;
    const uint32_t capacityOffset = groupBatch ? groupRank : 0U;
    if (totalCapacity < groupSize || post.meta.sqDepth < HCOMM_URMA_DWQE_BB_CNT) {
        return batchHandle;
    }

    __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
    __gm__ RegedBufferEntity* remoteBuffers =
        reinterpret_cast<__gm__ RegedBufferEntity*>(channelEntity->remoteBufferAddr);
    int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, 1U);
    if (remoteIdx == HCOMM_FAILED) {
        return batchHandle;
    }
    __gm__ RegedBufferEntity* remoteMemInfo = &remoteBuffers[remoteIdx];

    batchHandle.channel = channel;
    batchHandle.buffer = reinterpret_cast<__ubuf__ uint64_t*>(buff) + capacityOffset * HCOMM_SIMT_WQE_WORDS;
    batchHandle.capacity = capacity;
    batchHandle.bufferBbCapacity = capacity * HCOMM_URMA_DWQE_BB_CNT;
    batchHandle.groupRank = groupRank;
    batchHandle.groupSize = groupSize;
    batchHandle.remoteBaseAddr = remoteMemInfo->bufferInfo.rma.addr;
    batchHandle.remoteSize = remoteMemInfo->bufferInfo.rma.size;
    batchHandle.headAddr = post.headAddr;
    batchHandle.sqTailAddr = post.sqTailAddr;
    batchHandle.dwqeAddr = post.dwqeAddr;
    batchHandle.sqBaseAddr = post.sqBaseAddr;
    batchHandle.sqWqeSize = post.sqWqeSize;
    batchHandle.sqDepth = post.meta.sqDepth;
    batchHandle.tpId = post.meta.tpId;
    batchHandle.remoteEidL = post.meta.remoteEidL;
    batchHandle.remoteEidH = post.meta.remoteEidH;
    batchHandle.remoteTokenId = post.meta.remoteTokenId;
    batchHandle.remoteTokenValue = post.meta.remoteTokenValue;
    batchHandle.headSnapshot = *post.headAddr;
    batchHandle.valid = true;
    return batchHandle;
}

template <typename Group>
__simt_callee__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, __gm__ void* remoteBase, const Group& group)
{
    constexpr uint32_t commonValueCount = 14U;
    constexpr uint32_t commonStateWords = commonValueCount * 2U;
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
    UbcCtpBatchHandle leaderHandle;
    uint64_t common[commonValueCount] {};
    if (rank == 0U) {
        leaderHandle = MakeBatchHandle(channel, buff, buffLen, remoteBase, 0U, size);
        common[0] = leaderHandle.channel;
        common[1] = leaderHandle.remoteBaseAddr;
        common[2] = leaderHandle.remoteSize;
        common[3] = reinterpret_cast<uint64_t>(leaderHandle.headAddr);
        common[4] = reinterpret_cast<uint64_t>(leaderHandle.sqTailAddr);
        common[5] = reinterpret_cast<uint64_t>(leaderHandle.dwqeAddr);
        common[6] = leaderHandle.sqBaseAddr;
        common[7] = static_cast<uint64_t>(leaderHandle.sqWqeSize) |
            (static_cast<uint64_t>(leaderHandle.sqDepth) << 32U);
        common[8] = static_cast<uint64_t>(leaderHandle.tpId) |
            (static_cast<uint64_t>(leaderHandle.remoteTokenId) << 32U);
        common[9] = leaderHandle.remoteEidL;
        common[10] = leaderHandle.remoteEidH;
        common[11] = leaderHandle.remoteTokenValue;
        common[12] = leaderHandle.headSnapshot;
        common[13] = leaderHandle.valid ? 1U : 0U;
    }

    if (size <= warpSize) {
#pragma unroll
        for (uint32_t i = 0U; i < commonValueCount; ++i) {
            common[i] = asc_shfl(common[i], 0);
        }
    } else {
        if (groupState_ == nullptr || groupStateWords_ < commonStateWords) {
            UbcCtpBatchHandle invalidHandle;
            return invalidHandle;
        }
        if (rank == 0U) {
#pragma unroll
            for (uint32_t i = 0U; i < commonValueCount; ++i) {
                groupState_[2U * i] = static_cast<uint32_t>(common[i]);
                groupState_[2U * i + 1U] = static_cast<uint32_t>(common[i] >> 32U);
            }
        }
        group.sync();
#pragma unroll
        for (uint32_t i = 0U; i < commonValueCount; ++i) {
            common[i] = static_cast<uint64_t>(groupState_[2U * i]) |
                (static_cast<uint64_t>(groupState_[2U * i + 1U]) << 32U);
        }
        group.sync();
    }

    UbcCtpBatchHandle batchHandle;
    if (common[13] == 0U) {
        return batchHandle;
    }
    batchHandle.channel = common[0];
    batchHandle.buffer = reinterpret_cast<__ubuf__ uint64_t*>(buff) + rank * HCOMM_SIMT_WQE_WORDS;
    batchHandle.capacity = 1U;
    batchHandle.bufferBbCapacity = HCOMM_URMA_DWQE_BB_CNT;
    batchHandle.groupRank = rank;
    batchHandle.groupSize = size;
    batchHandle.remoteBaseAddr = common[1];
    batchHandle.remoteSize = common[2];
    batchHandle.headAddr = reinterpret_cast<__gm__ uint64_t*>(common[3]);
    batchHandle.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(common[4]);
    batchHandle.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(common[5]);
    batchHandle.sqBaseAddr = common[6];
    batchHandle.sqWqeSize = static_cast<uint32_t>(common[7]);
    batchHandle.sqDepth = static_cast<uint32_t>(common[7] >> 32U);
    batchHandle.tpId = static_cast<uint32_t>(common[8]);
    batchHandle.remoteTokenId = static_cast<uint32_t>(common[8] >> 32U);
    batchHandle.remoteEidL = common[9];
    batchHandle.remoteEidH = common[10];
    batchHandle.remoteTokenValue = static_cast<uint32_t>(common[11]);
    batchHandle.headSnapshot = common[12];
    batchHandle.valid = true;
    return batchHandle;
}

// Each operator prepares its request-specific values at final WQE offsets. Group Commit assigns
// the final SQ position from the prefix of all lanes' real BB counts; targetLen is also used for
// remote-range validation.
//
// bbCnt is 2 for every operator except a deferred Write/Read: those are the only WQEs that
// fit in one basic block, and they must stay inside it because the next block may already
// hold another lane's WQE. An immediate Write/Read instead splits its payload across two
// SGEs so the WQE fills the whole 128B DWQE window that publishes it.
template <bool commitFlag, HcommUrmaOpCode opCode, auto const& config>
struct HcommSimtDataDesc {
    static constexpr bool commit = commitFlag;
    static constexpr uint32_t cqeCnt = config.cqe;
    static constexpr uint32_t sgeNum = commitFlag ? 2U : 1U;
    static constexpr uint32_t bbCnt = commitFlag ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* localAddr;
    uint64_t targetLen;

    __simt_callee__ inline void Prepare(__ubuf__ HcommSimtBatchDescriptor* item) const
    {
        item->header = HcommSimtBatchHeader<opCode, config>();
        item->remoteAddr = reinterpret_cast<uint64_t>(remoteAddr);
        item->localAddr = reinterpret_cast<uint64_t>(localAddr);
        item->value0 = targetLen;
        item->kind = HcommSimtBatchKind::DATA;
    }

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
    static constexpr uint32_t cqeCnt = config.cqe;
    static constexpr uint32_t sgeNum = 1U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* localAddr;
    uint64_t targetLen;
    __gm__ uint8_t* notifyAddr;
    uint64_t notifyVal;

    __simt_callee__ inline void Prepare(__ubuf__ HcommSimtBatchDescriptor* item) const
    {
        item->header = HcommSimtBatchHeader<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>();
        item->remoteAddr = reinterpret_cast<uint64_t>(remoteAddr);
        item->localAddr = reinterpret_cast<uint64_t>(localAddr);
        item->value0 = targetLen;
        item->value1 = notifyVal;
        item->extraAddr = reinterpret_cast<uint64_t>(notifyAddr);
        item->kind = HcommSimtBatchKind::NOTIFY;
    }

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
    static constexpr uint32_t cqeCnt = config.cqe;
    static constexpr uint32_t sgeNum = 0U;
    // A deferred inline Write occupies one BB; a committed one occupies two (the second is a NOP
    // that pads the DWQE window to 128B so the doorbell can publish it).
    static constexpr uint32_t bbCnt = commit ? HCOMM_URMA_DWQE_BB_CNT : HCOMM_URMA_WQE_BB_CNT;

    __gm__ uint8_t* remoteAddr;
    T value;
    uint64_t targetLen = sizeof(T);

    __simt_callee__ inline void Prepare(__ubuf__ HcommSimtBatchDescriptor* item) const
    {
        static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
        static_assert(sizeof(T) <= 2U * sizeof(uint64_t), "Group WriteValue<T> supports at most 16 bytes");
        uint64_t payload[2] = {0U, 0U};
        const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
        for (uint32_t byte = 0U; byte < sizeof(T); ++byte) {
            payload[byte / sizeof(uint64_t)] |=
                static_cast<uint64_t>(valueAddr[byte]) << ((byte % sizeof(uint64_t)) * 8U);
        }
        item->header = HcommSimtBatchHeader<HcommUrmaOpCode::WRITE, config>(sizeof(T));
        item->remoteAddr = reinterpret_cast<uint64_t>(remoteAddr);
        item->value0 = payload[0];
        if constexpr (sizeof(T) > sizeof(uint64_t)) {
            item->value1 = payload[1];
        }
        item->kind = HcommSimtBatchKind::INLINE;
    }

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
    static constexpr uint32_t cqeCnt = config.cqe;
    static constexpr uint32_t sgeNum = 1U;
    static constexpr uint32_t bbCnt = HCOMM_URMA_DWQE_BB_CNT;

    __gm__ uint8_t* remoteAddr;
    __gm__ uint8_t* fetchAddr;
    T value;
    T cond;
    uint64_t targetLen = sizeof(T);

    __simt_callee__ inline void Prepare(__ubuf__ HcommSimtBatchDescriptor* item) const
    {
        item->header = HcommSimtBatchHeader<opCode, config>();
        item->remoteAddr = reinterpret_cast<uint64_t>(remoteAddr);
        item->localAddr = reinterpret_cast<uint64_t>(fetchAddr);
        item->value0 = static_cast<uint64_t>(value);
        item->value1 = opCode == HcommUrmaOpCode::CAS ? static_cast<uint64_t>(cond) : 0U;
        item->extraAddr = sizeof(T);
        item->kind = HcommSimtBatchKind::ATOMIC;
    }

    template <uint32_t wordCnt, typename WordPtr>
    __simt_callee__ inline void Store(
        WordPtr words, const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead) const
    {
        HcommSimtWriteAtomicWqe<opCode, T, wordCnt, config>(
            words, remoteAddr, fetchAddr, value, cond, sqeTemplate, curHead, nextHead);
    }
};

__simt_callee__ inline uint32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCq(
    ChannelHandle channel, uint32_t expectIdx)
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
        if (sqDepth == 0U) {
            return 0xFFU;
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
template <bool commit>
__simt_callee__ inline bool HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReservePost(
    ChannelHandle channel, const HcommSimtResolvedPost& post, uint32_t bbCnt, uint64_t& headVal)
{
    // SIMT has no standalone Commit interface, so a deferred post must also leave room for the
    // immediate 2-BB DWQE that will later publish the batch it belongs to.
    constexpr uint32_t extraFreeBbCnt = commit ? 0U : HCOMM_URMA_DWQE_BB_CNT;
    uint32_t requiredFreeBbCnt = bbCnt + extraFreeBbCnt;

    if (HcommSimtTryReserve(post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, 1U, headVal)) {
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
                post.headAddr, post.sqTailAddr, post.meta.sqDepth, bbCnt, requiredFreeBbCnt, 1U, headVal)) {
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
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWqe(ChannelHandle channel, const Desc& desc)
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
    if (!ReservePost<Desc::commit>(channel, post, bbCnt, headVal)) {
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

__simt_callee__ inline bool HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchContainsRemote(
    const UbcCtpBatchHandle& batchHandle, const __gm__ void* addr, uint64_t len) const
{
    if (!batchHandle.valid || addr == nullptr) {
        return false;
    }
    uint64_t targetAddr = reinterpret_cast<uint64_t>(addr);
    if (targetAddr < batchHandle.remoteBaseAddr) {
        return false;
    }
    uint64_t offset = targetAddr - batchHandle.remoteBaseAddr;
    return offset <= batchHandle.remoteSize && len <= batchHandle.remoteSize - offset;
}

template <typename Desc>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchPostWqe(
    UbcCtpBatchHandle& batchHandle, const Desc& desc)
{
    // As in the SIMD batch path, the handle caches one remote token and callers keep every
    // appended remote address inside the registration selected by MakeBatchHandle.
    if (!initialized_ || !batchHandle.valid || batchHandle.prepared >= batchHandle.capacity ||
        batchHandle.preparedBb > batchHandle.bufferBbCapacity ||
        Desc::bbCnt > batchHandle.bufferBbCapacity - batchHandle.preparedBb ||
        desc.targetLen > 0xFFFFFFFFULL) {
        return HCOMM_FAILED;
    }

    if (batchHandle.groupSize > 1U) {
        // A group lane does not know its final SQ position until the collective reservation.
        // Record only the request here and let Commit construct the final WQE directly for that
        // position, following the reserve-then-build structure used by the multi-thread path.
        desc.Prepare(HcommSimtBatchDescriptorAt(batchHandle.buffer, batchHandle.prepared));
    } else {
        // Preserve the validated single-lane path: it prepares the complete WQE during Append.
        constexpr uint32_t wordCnt = Desc::bbCnt * HCOMM_SIMT_BB_WORDS;
        __ubuf__ HcommSimtPreparedBatchWqe* items =
            reinterpret_cast<__ubuf__ HcommSimtPreparedBatchWqe*>(batchHandle.buffer);
        HcommSimtPackedSqeTemplate sqeTemplate;
        sqeTemplate.word1 = static_cast<uint64_t>(batchHandle.tpId & 0xFFFFFFU) |
            (static_cast<uint64_t>(Desc::sgeNum) << 24U) |
            (static_cast<uint64_t>(batchHandle.remoteTokenId & 0xFFFFFU) << 32U);
        sqeTemplate.remoteEidL = batchHandle.remoteEidL;
        sqeTemplate.remoteEidH = batchHandle.remoteEidH;
        sqeTemplate.tokenWord = batchHandle.remoteTokenValue;
        sqeTemplate.sqDepth = batchHandle.sqDepth;
        const uint32_t curHead = HcommSimtHeadIdx(batchHandle.headSnapshot) + batchHandle.preparedBb;
        __ubuf__ HcommSimtPreparedBatchWqe* item = &items[batchHandle.prepared];
        desc.template Store<wordCnt>(item->words, sqeTemplate, curHead, curHead + Desc::bbCnt);
    }
    ++batchHandle.prepared;
    batchHandle.preparedBb += Desc::bbCnt;
    batchHandle.lastBb = Desc::bbCnt;
    batchHandle.preparedCqe += Desc::cqeCnt;
    batchHandle.lastCqe = Desc::cqeCnt;
    return HCOMM_SUCCESS;
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::WRITE, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return BatchPostWqe(
        batchHandle, HcommSimtDataDesc<false, HcommUrmaOpCode::WRITE, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteValueNbi(
    ChannelHandle channel, __gm__ void* dst, T value)
{
    return PostWqe(channel, HcommSimtInlineDesc<T, commit, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteValueNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, T value)
{
    return BatchPostWqe(
        batchHandle, HcommSimtInlineDesc<T, false, config>{reinterpret_cast<__gm__ uint8_t*>(dst), value});
}

// Read swaps dst/src relative to Write: src is the remote address, dst the local one.
template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return PostWqe(
        channel, HcommSimtDataDesc<commit, HcommUrmaOpCode::READ, config>{
                     reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len)
{
    return BatchPostWqe(
        batchHandle, HcommSimtDataDesc<false, HcommUrmaOpCode::READ, config>{
                         reinterpret_cast<__gm__ uint8_t*>(src), reinterpret_cast<__gm__ uint8_t*>(dst), len});
}

template <bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* src, uint64_t len, __gm__ void* notifyAddr,
    uint64_t notifyVal)
{
    return PostWqe(
        channel, HcommSimtNotifyDesc<commit, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                     reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal});
}

template <auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* src, uint64_t len,
    __gm__ void* notifyAddr, uint64_t notifyVal)
{
    return BatchPostWqe(
        batchHandle, HcommSimtNotifyDesc<true, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(src), len,
                         reinterpret_cast<__gm__ uint8_t*>(notifyAddr), notifyVal});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return PostWqe(
        channel, HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::FAA, config>{
                     reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
                     static_cast<T>(0)});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T addVal)
{
    return BatchPostWqe(
        batchHandle, HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::FAA, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), addVal,
                         static_cast<T>(0)});
}

template <typename T, bool commit, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
    ChannelHandle channel, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return PostWqe(
        channel,
        HcommSimtAtomicDesc<T, commit, HcommUrmaOpCode::CAS, config>{
            reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal, compareVal});
}

template <typename T, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
    UbcCtpBatchHandle& batchHandle, __gm__ void* dst, __gm__ void* fetchAddr, T compareVal, T swapVal)
{
    return BatchPostWqe(
        batchHandle, HcommSimtAtomicDesc<T, true, HcommUrmaOpCode::CAS, config>{
                         reinterpret_cast<__gm__ uint8_t*>(dst), reinterpret_cast<__gm__ uint8_t*>(fetchAddr), swapVal,
                         compareVal});
}

__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchCommit(
    UbcCtpBatchHandle& batchHandle)
{
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profileBegin = clock();
#endif
    if (!initialized_ || !batchHandle.valid || batchHandle.prepared == 0U ||
        batchHandle.preparedCqe != batchHandle.prepared || batchHandle.lastCqe != 1U ||
        batchHandle.lastBb == 0U) {
        return HCOMM_FAILED;
    }

    HcommSimtResolvedPost post;
    HcommSimtLoadBatchQueuePost(batchHandle.channel, post);
    post.meta.remoteTokenId = batchHandle.remoteTokenId;
    post.meta.remoteTokenValue = batchHandle.remoteTokenValue;
    if (*post.headAddr != batchHandle.headSnapshot) {
        return HCOMM_FAILED;
    }

    const uint32_t totalBb = batchHandle.preparedBb +
        (batchHandle.lastBb == HCOMM_URMA_WQE_BB_CNT ? HCOMM_URMA_WQE_BB_CNT : 0U);
    uint64_t headVal = 0U;
    if (!HcommSimtTryPlanExclusiveReservation(
            post.headAddr, post.sqTailAddr, post.meta.sqDepth, totalBb, headVal)) {
        return HCOMM_FAILED;
    }

    const uint32_t batchHead = HcommSimtHeadIdx(headVal);
    HcommSimtPackedSqeTemplate sqeTemplate;
    HcommSimtBuildPackedSqeTemplate<0U>(post.meta, sqeTemplate);
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profilePlanEnd = clock();
#endif
    __ubuf__ HcommSimtPreparedBatchWqe* items =
        reinterpret_cast<__ubuf__ HcommSimtPreparedBatchWqe*>(batchHandle.buffer);
    uint32_t sqOffsetBb = 0U;
    uint32_t tailHead = 0U;
    HcommSimtWqeStage tailStage;
    for (uint32_t item = 0U; item < batchHandle.prepared; ++item) {
        __ubuf__ HcommSimtPreparedBatchWqe* batchItem = &items[item];
        const uint32_t itemBb = HcommSimtPreparedBatchBb(batchItem->words[0]);
        const bool publish = item + 1U == batchHandle.prepared;
        const uint32_t commitBb = publish ? HCOMM_URMA_DWQE_BB_CNT : itemBb;
        const uint32_t curHead = batchHead + sqOffsetBb;
        if (publish) {
            tailHead = curHead;
            HcommSimtBuildPreparedBatchTail(
                batchItem, sqeTemplate, curHead, curHead + commitBb, tailStage);
        } else {
            HcommSimtCopyPreparedBatchWqeToSq<false>(post, batchItem, itemBb, curHead);
        }
        sqOffsetBb += commitBb;
    }
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profileEmitEnd = clock();
#endif
    HcommSimtPublishReservedStage<HCOMM_URMA_DWQE_BB_CNT, false>(
        post, tailHead, tailStage);
    asc_threadfence();
    const uint64_t delta = (static_cast<uint64_t>(batchHandle.prepared) << 32U) | totalBb;
    const uint64_t publishedHead = headVal + delta;
    *post.headAddr = publishedHead;
    HcommSimtRingDwqe(post.dwqeAddr, tailStage);
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profilePublishEnd = clock();
    batchHandle.profilePlanCycles += profilePlanEnd - profileBegin;
    batchHandle.profileEmitCycles += profileEmitEnd - profilePlanEnd;
    batchHandle.profilePublishCycles += profilePublishEnd - profileEmitEnd;
#endif

    batchHandle.headSnapshot = publishedHead;
    batchHandle.prepared = 0U;
    batchHandle.preparedBb = 0U;
    batchHandle.lastBb = 0U;
    batchHandle.preparedCqe = 0U;
    batchHandle.lastCqe = 0U;
    return HCOMM_SUCCESS;
}

template <typename Group>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchCommit(
    UbcCtpBatchHandle& batchHandle, const Group& group)
{
    const uint32_t rank = static_cast<uint32_t>(group.thread_rank());
    const uint32_t size = static_cast<uint32_t>(group.size());
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profileBegin = rank == 0U ? clock() : 0U;
#endif
    constexpr uint32_t readyBit = 1U << 31U;
    constexpr uint32_t stateHeaderWords = 4U;
    constexpr uint32_t contextStateWords = 15U;
    constexpr uint32_t sharedStateWords = stateHeaderWords + contextStateWords;
    constexpr uint32_t stateWordsPerWarp = 2U;
    const uint32_t warpCount = (size + warpSize - 1U) / warpSize;
    const uint32_t requiredStateWords = size <= warpSize ? stateHeaderWords :
        sharedStateWords + stateWordsPerWarp * warpCount;
    if (size <= 1U || groupState_ == nullptr || groupStateWords_ < requiredStateWords) {
        return HCOMM_FAILED;
    }

    const bool localReady = initialized_ && batchHandle.valid && batchHandle.groupSize == size &&
        batchHandle.groupRank == rank &&
        batchHandle.capacity == 1U && batchHandle.prepared == 1U && batchHandle.preparedCqe == 1U &&
        batchHandle.lastCqe == 1U && batchHandle.preparedBb == batchHandle.lastBb &&
        (batchHandle.lastBb == HCOMM_URMA_WQE_BB_CNT || batchHandle.lastBb == HCOMM_URMA_DWQE_BB_CNT);
    int32_t status = HCOMM_SUCCESS;
    uint64_t headVal = 0U;
    uint32_t laneInclusiveBb = batchHandle.preparedBb;
    uint32_t warpBbBase = 0U;
    uint32_t totalBb = 0U;
    const uint32_t publisherRank = size - 1U;
    const uint32_t laneInWarp = rank % warpSize;
    const uint32_t warpIndex = rank / warpSize;
    const uint32_t warpLaneCount = size - warpIndex * warpSize < warpSize ?
        size - warpIndex * warpSize : warpSize;
    HcommSimtResolvedPost post{};
    if (rank == 0U) {
        post.headAddr = batchHandle.headAddr;
        post.sqTailAddr = batchHandle.sqTailAddr;
        post.dwqeAddr = batchHandle.dwqeAddr;
        post.sqBaseAddr = batchHandle.sqBaseAddr;
        post.sqWqeSize = batchHandle.sqWqeSize;
        post.meta.sqDepth = batchHandle.sqDepth;
        post.meta.tpId = batchHandle.tpId;
        post.meta.remoteEidL = batchHandle.remoteEidL;
        post.meta.remoteEidH = batchHandle.remoteEidH;
    }
    if (size <= warpSize) {
        if (asc_all(localReady ? 1 : 0) == 0) {
            return HCOMM_FAILED;
        }
        for (uint32_t offset = 1U; offset < size; offset <<= 1U) {
            const uint32_t priorBb = asc_shfl_up(laneInclusiveBb, offset);
            if (rank >= offset) {
                laneInclusiveBb += priorBb;
            }
        }
        const int32_t lastLane = static_cast<int32_t>(size - 1U);
        totalBb = asc_shfl(laneInclusiveBb, lastLane);
        const uint32_t lastBb = asc_shfl(batchHandle.lastBb, lastLane);
        totalBb += lastBb == HCOMM_URMA_WQE_BB_CNT ? HCOMM_URMA_WQE_BB_CNT : 0U;
        if (rank == 0U) {
            status = *post.headAddr == batchHandle.headSnapshot &&
                HcommSimtTryPlanExclusiveReservation(
                    post.headAddr, post.sqTailAddr, post.meta.sqDepth, totalBb, headVal) ?
                HCOMM_SUCCESS : HCOMM_FAILED;
        }
        status = asc_shfl(status, 0);
        headVal = asc_shfl(headVal, 0);
    } else {
        for (uint32_t offset = 1U; offset < warpSize; offset <<= 1U) {
            const uint32_t priorBb = asc_shfl_up(laneInclusiveBb, offset);
            if (laneInWarp >= offset) {
                laneInclusiveBb += priorBb;
            }
        }
        const bool warpReady = asc_all(localReady ? 1 : 0) != 0;
        if (laneInWarp + 1U == warpLaneCount) {
            const uint32_t stateBase = sharedStateWords + stateWordsPerWarp * warpIndex;
            groupState_[stateBase] = laneInclusiveBb | (warpReady ? readyBit : 0U);
            groupState_[stateBase + 1U] = batchHandle.lastBb;
        }
        group.sync();

        if (rank == 0U) {
            uint32_t lastBb = 0U;
            for (uint32_t warp = 0U; warp < warpCount; ++warp) {
                const uint32_t stateBase = sharedStateWords + stateWordsPerWarp * warp;
                const uint32_t encodedBb = groupState_[stateBase];
                if ((encodedBb & readyBit) == 0U) {
                    status = HCOMM_FAILED;
                }
                groupState_[stateBase] = totalBb;
                totalBb += encodedBb & ~readyBit;
                lastBb = groupState_[stateBase + 1U];
            }
            totalBb += lastBb == HCOMM_URMA_WQE_BB_CNT ? HCOMM_URMA_WQE_BB_CNT : 0U;
            status = status == HCOMM_SUCCESS && *post.headAddr == batchHandle.headSnapshot &&
                HcommSimtTryPlanExclusiveReservation(
                    post.headAddr, post.sqTailAddr, post.meta.sqDepth, totalBb, headVal) ?
                HCOMM_SUCCESS : HCOMM_FAILED;
            groupState_[0] = static_cast<uint32_t>(status);
            groupState_[1] = totalBb;
            groupState_[2] = static_cast<uint32_t>(headVal);
            groupState_[3] = static_cast<uint32_t>(headVal >> 32U);

            // The same collective that publishes the reservation also broadcasts the queue
            // context. Keeping both in one shared-state epoch removes a second full CTA sync.
            const uint64_t headAddr = reinterpret_cast<uint64_t>(post.headAddr);
            const uint64_t tailAddr = reinterpret_cast<uint64_t>(post.sqTailAddr);
            const uint64_t dwqeAddr = reinterpret_cast<uint64_t>(post.dwqeAddr);
            groupState_[4] = static_cast<uint32_t>(headAddr);
            groupState_[5] = static_cast<uint32_t>(headAddr >> 32U);
            groupState_[6] = static_cast<uint32_t>(tailAddr);
            groupState_[7] = static_cast<uint32_t>(tailAddr >> 32U);
            groupState_[8] = static_cast<uint32_t>(dwqeAddr);
            groupState_[9] = static_cast<uint32_t>(dwqeAddr >> 32U);
            groupState_[10] = static_cast<uint32_t>(post.sqBaseAddr);
            groupState_[11] = static_cast<uint32_t>(post.sqBaseAddr >> 32U);
            groupState_[12] = post.sqWqeSize;
            groupState_[13] = post.meta.sqDepth;
            groupState_[14] = post.meta.tpId;
            groupState_[15] = static_cast<uint32_t>(post.meta.remoteEidL);
            groupState_[16] = static_cast<uint32_t>(post.meta.remoteEidL >> 32U);
            groupState_[17] = static_cast<uint32_t>(post.meta.remoteEidH);
            groupState_[18] = static_cast<uint32_t>(post.meta.remoteEidH >> 32U);
        }
        group.sync();
        status = static_cast<int32_t>(groupState_[0]);
        totalBb = groupState_[1];
        headVal = static_cast<uint64_t>(groupState_[2]) | (static_cast<uint64_t>(groupState_[3]) << 32U);
        warpBbBase = groupState_[sharedStateWords + stateWordsPerWarp * warpIndex];
    }
    if (status != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }

    if (size <= warpSize) {
        post.headAddr = reinterpret_cast<__gm__ uint64_t*>(
            asc_shfl(reinterpret_cast<uint64_t>(post.headAddr), 0));
        post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(
            asc_shfl(reinterpret_cast<uint64_t>(post.sqTailAddr), 0));
        post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(
            asc_shfl(reinterpret_cast<uint64_t>(post.dwqeAddr), 0));
        post.sqBaseAddr = asc_shfl(post.sqBaseAddr, 0);
        post.sqWqeSize = asc_shfl(post.sqWqeSize, 0);
        post.meta.sqDepth = asc_shfl(post.meta.sqDepth, 0);
        post.meta.tpId = asc_shfl(post.meta.tpId, 0);
        post.meta.remoteEidL = asc_shfl(post.meta.remoteEidL, 0);
        post.meta.remoteEidH = asc_shfl(post.meta.remoteEidH, 0);
    } else {
        post.headAddr = reinterpret_cast<__gm__ uint64_t*>(
            static_cast<uint64_t>(groupState_[4]) | (static_cast<uint64_t>(groupState_[5]) << 32U));
        post.sqTailAddr = reinterpret_cast<__gm__ uint32_t*>(
            static_cast<uint64_t>(groupState_[6]) | (static_cast<uint64_t>(groupState_[7]) << 32U));
        post.dwqeAddr = reinterpret_cast<__gm__ uint8_t*>(
            static_cast<uint64_t>(groupState_[8]) | (static_cast<uint64_t>(groupState_[9]) << 32U));
        post.sqBaseAddr = static_cast<uint64_t>(groupState_[10]) |
            (static_cast<uint64_t>(groupState_[11]) << 32U);
        post.sqWqeSize = groupState_[12];
        post.meta.sqDepth = groupState_[13];
        post.meta.tpId = groupState_[14];
        post.meta.remoteEidL = static_cast<uint64_t>(groupState_[15]) |
            (static_cast<uint64_t>(groupState_[16]) << 32U);
        post.meta.remoteEidH = static_cast<uint64_t>(groupState_[17]) |
            (static_cast<uint64_t>(groupState_[18]) << 32U);
    }
    post.meta.remoteTokenId = batchHandle.remoteTokenId;
    post.meta.remoteTokenValue = batchHandle.remoteTokenValue;
    const uint64_t delta = (static_cast<uint64_t>(size) << 32U) | totalBb;
    const uint64_t publishedHead = headVal + delta;

#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profilePlanEnd = rank == 0U ? clock() : 0U;
#endif

    const uint32_t laneHead = HcommSimtHeadIdx(headVal) + warpBbBase + laneInclusiveBb - batchHandle.preparedBb;
    __ubuf__ HcommSimtBatchDescriptor* batchItem =
        HcommSimtBatchDescriptorAt(batchHandle.buffer, 0U);
    const uint32_t itemBb = batchHandle.preparedBb;
    const bool publisher = rank == publisherRank;
    HcommSimtPackedSqeTemplate sqeTemplate;
    HcommSimtBuildPackedSqeTemplate<0U>(post.meta, sqeTemplate);
    HcommSimtWqeStage tailStage;
    if (publisher) {
#pragma unroll
        for (uint32_t word = 0U; word < HCOMM_SIMT_WQE_WORDS; ++word) {
            tailStage.words[word] = 0U;
        }
        HcommSimtBuildBatchDescriptorStage(
            batchItem, sqeTemplate, laneHead, laneHead + HCOMM_URMA_DWQE_BB_CNT, true, tailStage);
        HcommSimtPublishReservedStage<HCOMM_URMA_DWQE_BB_CNT, false>(
            post, laneHead, tailStage);
    } else {
        HcommSimtWqeStage laneStage;
        HcommSimtBuildBatchDescriptorStage(
            batchItem, sqeTemplate, laneHead, laneHead + itemBb, false, laneStage);
        HcommSimtCopyBatchDescriptorStageToSq(post, laneStage, itemBb, laneHead);
    }
    asc_threadfence();
    group.sync();
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    const uint64_t profileEmitEnd = rank == 0U ? clock() : 0U;
#endif
    if (publisher) {
        // The same lane owns the fully built tail and publishes the collectively reserved range.
        *post.headAddr = publishedHead;
        HcommSimtRingDwqe(post.dwqeAddr, tailStage);
    }
    group.sync();
#if defined(HCOMM_SIMT_BATCH_STAGE_PROFILE)
    if (rank == 0U) {
        const uint64_t profilePublishEnd = clock();
        batchHandle.profilePlanCycles += profilePlanEnd - profileBegin;
        batchHandle.profileEmitCycles += profileEmitEnd - profilePlanEnd;
        batchHandle.profilePublishCycles += profilePublishEnd - profileEmitEnd;
    }
#endif

    batchHandle.headSnapshot = publishedHead;
    batchHandle.prepared = 0U;
    batchHandle.preparedBb = 0U;
    batchHandle.lastBb = 0U;
    batchHandle.preparedCqe = 0U;
    batchHandle.lastCqe = 0U;
    return HCOMM_SUCCESS;
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

template <auto pipe>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(UbcCtpBatchHandle& batchHandle)
{
    if (!batchHandle.valid || batchHandle.prepared != 0U) {
        return HCOMM_FAILED;
    }
    return Drain<pipe>(batchHandle.channel);
}

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_SIMT_URMA_H
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_URMA_H
#endif
