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

template <uint32_t byteSize>
__simt_callee__ inline void HcommSimtClearUb(__ubuf__ uint8_t* addr)
{
    static_assert(byteSize % sizeof(uint64_t) == 0U, "UB WQE clear must use whole words");
#pragma unroll
    for (uint32_t i = 0; i < byteSize / sizeof(uint64_t); ++i) {
        ((__ubuf__ uint64_t*)addr)[i] = 0U;
    }
}

__simt_callee__ inline void HcommSimtCopyUb128ToGm(__gm__ uint8_t* dst, __ubuf__ uint8_t* src)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        ((__gm__ uint64_t*)dst)[i] = ((__ubuf__ uint64_t*)src)[i];
    }
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
        ((__gm__ uint64_t*)dst)[i] = ((__ubuf__ uint64_t*)src)[i];
    }
}

// Each lane owns one UB slot. Init establishes an all-zero template; then only
// a Notify/Atomic layout transition needs sanitizing because their tails overlap.
__simt_callee__ inline void HcommSimtPrepareUbNotifyWqe(
    __ubuf__ uint8_t* wqeAddr, HcommSimtUbWqeKind& wqeKind)
{
    if (wqeKind == HcommSimtUbWqeKind::ATOMIC) {
        HcommSimtClearUb<sizeof(HcommUrmaNotifyCtx)>(wqeAddr + sizeof(HcommUrmaSqeCtx));
    }
    wqeKind = HcommSimtUbWqeKind::NOTIFY;
}

__simt_callee__ inline void HcommSimtPrepareUbAtomicWqe(
    __ubuf__ uint8_t* wqeAddr, HcommSimtUbWqeKind& wqeKind)
{
    if (wqeKind == HcommSimtUbWqeKind::NOTIFY) {
        constexpr uint32_t atomicActiveSize = sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx) +
                                              2U * sizeof(uint64_t);
        HcommSimtClearUb<HCOMM_URMA_DWQE_SIZE - atomicActiveSize>(wqeAddr + atomicActiveSize);
    }
    wqeKind = HcommSimtUbWqeKind::ATOMIC;
}

// An inline WQE carries its payload in the WQE itself rather than through an SGE, so the
// active tail length depends on sizeof(T). Clear the whole area past the SQE header on any
// transition into INLINE, and also between two INLINE posts, because a previous larger
// value would otherwise leave bytes behind.
__simt_callee__ inline void HcommSimtPrepareUbInlineWqe(
    __ubuf__ uint8_t* wqeAddr, HcommSimtUbWqeKind& wqeKind)
{
    if (wqeKind != HcommSimtUbWqeKind::EMPTY) {
        HcommSimtClearUb<HCOMM_URMA_DWQE_SIZE - sizeof(HcommUrmaSqeCtx)>(
            wqeAddr + sizeof(HcommUrmaSqeCtx));
    }
    wqeKind = HcommSimtUbWqeKind::INLINE;
}

// Write/Read leave everything after their SGEs untouched, so any longer tail written by a
// previous Notify/Atomic on the same slot must be cleared. DATA1 and DATA2 differ only in
// whether the second SGE is live, so switching between them needs no extra sanitizing.
template <uint32_t sgeNum>
__simt_callee__ inline void HcommSimtPrepareUbDataWqe(
    __ubuf__ uint8_t* wqeAddr, HcommSimtUbWqeKind& wqeKind)
{
    constexpr HcommSimtUbWqeKind dataKind =
        sgeNum == 1U ? HcommSimtUbWqeKind::DATA1 : HcommSimtUbWqeKind::DATA2;
    if (wqeKind != HcommSimtUbWqeKind::DATA1 && wqeKind != HcommSimtUbWqeKind::DATA2 &&
        wqeKind != HcommSimtUbWqeKind::EMPTY) {
        constexpr uint32_t dataActiveSize = sizeof(HcommUrmaSqeCtx) + 2U * sizeof(HcommUrmaSgeCtx);
        HcommSimtClearUb<HCOMM_URMA_DWQE_SIZE - dataActiveSize>(wqeAddr + dataActiveSize);
    }
    wqeKind = dataKind;
}

template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbSqeCtx(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, const HcommSimtUbPostMeta& meta,
    uint32_t curHead, uint32_t nextHead)
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
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal, const HcommSimtUbPostMeta& meta,
    uint32_t curHead, uint32_t nextHead, HcommSimtUbWqeKind& wqeKind)
{
    HcommSimtPrepareUbNotifyWqe(wqeAddr, wqeKind);
    HcommSimtUrmaFillUbSqeCtx<HcommUrmaOpCode::WRITE_WITH_NOTIFY, 1U, config>(
        wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaNotifyCtx* notifyCtx =
        (__ubuf__ HcommUrmaNotifyCtx*)(wqeAddr + sizeof(HcommUrmaSqeCtx));
    uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyTokenId = meta.remoteTokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = meta.remoteTokenValue;
    notifyCtx->notifyAddrL = static_cast<uint32_t>(notifyAddrValue & 0xFFFFFFFFU);
    notifyCtx->notifyAddrH = static_cast<uint32_t>((notifyAddrValue >> 32U) & 0xFFFFFFFFU);
    notifyCtx->notifyDataL = static_cast<uint32_t>(notifyVal & 0xFFFFFFFFU);
    notifyCtx->notifyDataH = static_cast<uint32_t>((notifyVal >> 32U) & 0xFFFFFFFFU);

    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)(
        (__ubuf__ uint8_t*)notifyCtx + sizeof(HcommUrmaNotifyCtx));
    sgeCtx->len = static_cast<uint32_t>(len);
    sgeCtx->tokenId = 0U;
    sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
}

// Write/Read carry no notify or AMO tail, so the SGEs directly follow the SQE header.
// sgeNum == 2 splits the payload so an immediate post fills the whole 128B DWQE.
template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbDataWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead, HcommSimtUbWqeKind& wqeKind)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    HcommSimtPrepareUbDataWqe<sgeNum>(wqeAddr, wqeKind);
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
    }
}

template <typename T>
__simt_callee__ inline void HcommSimtStoreUbInlineValue(__ubuf__ uint8_t* addr, T value)
{
    const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
    for (uint32_t i = 0; i < sizeof(T); ++i) {
        addr[i] = valueAddr[i];
    }
}

template <typename T, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbWriteValueWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, T value,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead,
    HcommSimtUbWqeKind& wqeKind)
{
    HcommSimtPrepareUbInlineWqe(wqeAddr, wqeKind);
    HcommSimtUrmaFillUbSqeCtx<HcommUrmaOpCode::WRITE, 0U, config>(
        wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeAddr;
    sqeCtx->inlineMsgLen = sizeof(T);

    HcommSimtStoreUbInlineValue(wqeAddr + sizeof(HcommUrmaSqeCtx), value);
}

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbAtomicWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead, HcommSimtUbWqeKind& wqeKind)
{
    HcommSimtPrepareUbAtomicWqe(wqeAddr, wqeKind);
    HcommSimtUrmaFillUbSqeCtx<opCode, 1U, config>(wqeAddr, remoteAddr, meta, curHead, nextHead);

    __ubuf__ HcommUrmaSgeCtx* sgeCtx =
        (__ubuf__ HcommUrmaSgeCtx*)(wqeAddr + sizeof(HcommUrmaSqeCtx));
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

static_assert(sizeof(HcommUrmaSqeCtx) == 48U, "Packed SIMT SQE assumes a 48-byte SQE context");
static_assert(sizeof(HcommUrmaNotifyCtx) == 32U, "Packed SIMT Notify assumes a 32-byte notify context");
static_assert(sizeof(HcommUrmaSgeCtx) == 16U, "Packed SIMT SQE assumes a 16-byte SGE context");

union alignas(sizeof(uint64_t)) HcommSimtLocalWqeStage {
    uint64_t words[HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t)];
};
static_assert(sizeof(HcommSimtLocalWqeStage) == HCOMM_URMA_DWQE_SIZE,
    "SIMT lane-private WQE stage must match the 128-byte DWQE window");

__simt_callee__ inline void HcommSimtStoreLocalWqe(
    __gm__ uint8_t* dst, const HcommSimtLocalWqeStage& stage)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        reinterpret_cast<__gm__ uint64_t*>(dst)[i] = stage.words[i];
    }
}

__simt_callee__ inline void HcommSimtCopyUb128ToDwqeStwt16(
    __gm__ uint8_t* dst, __ubuf__ uint8_t* src)
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
    // sgeNum == 0 is the inline case: the payload rides in the WQE, so there is no SGE.
    static_assert(sgeNum <= 2U, "SIMT URMA supports at most two SGEs per WQE");
    sqeTemplate.word1 = static_cast<uint64_t>(meta.tpId & 0xFFFFFFU) |
                        (static_cast<uint64_t>(sgeNum) << 24U) |
                        (static_cast<uint64_t>(meta.remoteTokenId & 0xFFFFFU) << 32U);
    sqeTemplate.remoteEidL = meta.remoteEidL;
    sqeTemplate.remoteEidH = meta.remoteEidH;
    sqeTemplate.tokenWord = meta.remoteTokenValue;
    sqeTemplate.sqDepth = meta.sqDepth;
}

// inlineMsgLen occupies bits 54..63 of the first word and stays 0 for every WQE that carries
// its payload through an SGE. Only the inline Write path passes a non-zero length.
template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtStorePackedSqeHeader(
    __gm__ uint64_t* words, __gm__ uint8_t* remoteAddr,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead,
    uint32_t inlineMsgLen = 0U)
{
    uint64_t owner = (curHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(nextHead & 0xFFFFU) |
               (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) |
               (1ULL << 28U) | (1ULL << 29U) | (owner << 31U) |
               (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U) |
               (static_cast<uint64_t>(inlineMsgLen & 0x3FFU) << 54U);
    words[1] = sqeTemplate.word1;
    words[2] = sqeTemplate.remoteEidL;
    words[3] = sqeTemplate.remoteEidH;
    words[4] = sqeTemplate.tokenWord;
    words[5] = reinterpret_cast<uint64_t>(remoteAddr);
}

template <auto const& config>
__simt_callee__ inline void HcommSimtStorePackedNotifyWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal, const HcommSimtPackedSqeTemplate& sqeTemplate,
    uint32_t curHead, uint32_t nextHead)
{
    __gm__ uint64_t* words = reinterpret_cast<__gm__ uint64_t*>(wqeAddr);
    HcommSimtStorePackedSqeHeader<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
        words, remoteAddr, sqeTemplate, curHead, nextHead);
    words[6] = static_cast<uint64_t>((sqeTemplate.word1 >> 32U) & 0xFFFFFU) |
               ((sqeTemplate.tokenWord & 0xFFFFFFFFULL) << 32U);
    words[7] = reinterpret_cast<uint64_t>(notifyAddr);
    words[8] = notifyVal;
    words[9] = 0U;
    words[10] = static_cast<uint64_t>(static_cast<uint32_t>(len));
    words[11] = reinterpret_cast<uint64_t>(localAddr);
#pragma unroll
    for (uint32_t i = 12U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0U;
    }
}

template <HcommUrmaOpCode opCode, uint32_t sgeNum, auto const& config>
__simt_callee__ inline void HcommSimtStorePackedDataWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    static_assert(sgeNum == 1U || sgeNum == 2U, "SIMT URMA supports one or two SGEs per WQE");
    __gm__ uint64_t* words = reinterpret_cast<__gm__ uint64_t*>(wqeAddr);
    HcommSimtStorePackedSqeHeader<opCode, config>(words, remoteAddr, sqeTemplate, curHead, nextHead);
    if constexpr (sgeNum == 1U) {
        // A one-SGE WQE occupies a single 64B basic block. Writing beyond words[7] would
        // clobber the next block, which may already hold another lane's WQE.
        words[6] = static_cast<uint64_t>(static_cast<uint32_t>(len));
        words[7] = reinterpret_cast<uint64_t>(localAddr);
    } else {
        uint32_t firstLen = static_cast<uint32_t>(len >> 1);
        uint32_t lastLen = static_cast<uint32_t>(len - firstLen);
        words[6] = static_cast<uint64_t>(firstLen);
        words[7] = reinterpret_cast<uint64_t>(localAddr);
        words[8] = static_cast<uint64_t>(lastLen);
        words[9] = reinterpret_cast<uint64_t>(localAddr + firstLen);
#pragma unroll
        for (uint32_t i = 10U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
            words[i] = 0U;
        }
    }
}

// Packs the inline payload into the whole 64-bit words that follow the SQE header. A type
// shorter than a word occupies the low bytes of words[6], matching the byte-wise copy the
// struct-field path performs, because the SQE header is a whole number of words long.
template <typename T>
__simt_callee__ inline void HcommSimtInlineValueWords(T value, uint64_t* words, uint32_t wordCnt)
{
    const uint8_t* valueAddr = reinterpret_cast<const uint8_t*>(&value);
#pragma unroll
    for (uint32_t i = 0; i < wordCnt; ++i) {
        words[i] = 0U;
    }
#pragma unroll
    for (uint32_t i = 0; i < sizeof(T); ++i) {
        words[i / sizeof(uint64_t)] |=
            static_cast<uint64_t>(valueAddr[i]) << ((i % sizeof(uint64_t)) * 8U);
    }
}

// Inline payload starts at words[6] because the SQE header is 48 bytes.
constexpr uint32_t HCOMM_SIMT_INLINE_FIRST_WORD = sizeof(HcommUrmaSqeCtx) / sizeof(uint64_t);

template <typename T, auto const& config>
__simt_callee__ inline void HcommSimtStorePackedWriteValueWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, T value,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    constexpr uint32_t payloadWords =
        HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t) - HCOMM_SIMT_INLINE_FIRST_WORD;
    __gm__ uint64_t* words = reinterpret_cast<__gm__ uint64_t*>(wqeAddr);
    HcommSimtStorePackedSqeHeader<HcommUrmaOpCode::WRITE, config>(
        words, remoteAddr, sqeTemplate, curHead, nextHead, sizeof(T));

    uint64_t payload[payloadWords];
    HcommSimtInlineValueWords(value, payload, payloadWords);
#pragma unroll
    for (uint32_t i = 0; i < payloadWords; ++i) {
        words[HCOMM_SIMT_INLINE_FIRST_WORD + i] = payload[i];
    }
}

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtStorePackedAtomicWqe(
    __gm__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    __gm__ uint64_t* words = reinterpret_cast<__gm__ uint64_t*>(wqeAddr);
    HcommSimtStorePackedSqeHeader<opCode, config>(words, remoteAddr, sqeTemplate, curHead, nextHead);
    words[6] = static_cast<uint64_t>(sizeof(T));
    words[7] = reinterpret_cast<uint64_t>(fetchAddr);
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        words[8] = static_cast<uint64_t>(static_cast<uint32_t>(value)) |
                   (static_cast<uint64_t>(static_cast<uint32_t>(
                        opCode == HcommUrmaOpCode::CAS ? cond : static_cast<T>(0))) << 32U);
        words[9] = 0U;
    } else {
        words[8] = static_cast<uint64_t>(value);
        words[9] = opCode == HcommUrmaOpCode::CAS ? static_cast<uint64_t>(cond) : 0U;
    }
#pragma unroll
    for (uint32_t i = 10U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        words[i] = 0U;
    }
}

template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtStoreLocalSqeHeader(
    uint64_t* words, __gm__ uint8_t* remoteAddr,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead,
    uint32_t inlineMsgLen = 0U)
{
    uint64_t owner = (curHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(nextHead & 0xFFFFU) |
               (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) |
               (1ULL << 28U) | (1ULL << 29U) | (owner << 31U) |
               (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U) |
               (static_cast<uint64_t>(inlineMsgLen & 0x3FFU) << 54U);
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
    HcommSimtStoreLocalSqeHeader<opCode, config>(
        stage.words, remoteAddr, sqeTemplate, curHead, nextHead);
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

// The stage is an uninitialized local, so HcommSimtInlineValueWords zeroes the whole payload
// area before scattering the value into it.
template <typename T, auto const& config>
__simt_callee__ inline void HcommSimtBuildLocalWriteValueWqe(
    HcommSimtLocalWqeStage& stage, __gm__ uint8_t* remoteAddr, T value,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    constexpr uint32_t payloadWords =
        HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t) - HCOMM_SIMT_INLINE_FIRST_WORD;
    HcommSimtStoreLocalSqeHeader<HcommUrmaOpCode::WRITE, config>(
        stage.words, remoteAddr, sqeTemplate, curHead, nextHead, sizeof(T));
    HcommSimtInlineValueWords(value, &stage.words[HCOMM_SIMT_INLINE_FIRST_WORD], payloadWords);
}

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtBuildLocalAtomicWqe(
    HcommSimtLocalWqeStage& stage, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr,
    T value, T cond, const HcommSimtPackedSqeTemplate& sqeTemplate,
    uint32_t curHead, uint32_t nextHead)
{
    HcommSimtStoreLocalSqeHeader<opCode, config>(
        stage.words, remoteAddr, sqeTemplate, curHead, nextHead);
    stage.words[6] = static_cast<uint64_t>(sizeof(T));
    stage.words[7] = reinterpret_cast<uint64_t>(fetchAddr);
    if constexpr (sizeof(T) == sizeof(uint32_t)) {
        stage.words[8] = static_cast<uint64_t>(static_cast<uint32_t>(value)) |
                         (static_cast<uint64_t>(static_cast<uint32_t>(
                              opCode == HcommUrmaOpCode::CAS ? cond : static_cast<T>(0))) << 32U);
        stage.words[9] = 0U;
    } else {
        stage.words[8] = static_cast<uint64_t>(value);
        stage.words[9] = opCode == HcommUrmaOpCode::CAS ? static_cast<uint64_t>(cond) : 0U;
    }
#pragma unroll
    for (uint32_t i = 10U; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
        stage.words[i] = 0U;
    }
}

__simt_callee__ inline void HcommSimtPublishLocalWqe(
    __gm__ uint8_t* sqeAddr, __gm__ uint8_t* dwqeAddr,
    const HcommSimtLocalWqeStage& stage)
{
    HcommSimtStoreLocalWqe(sqeAddr, stage);
    asc_threadfence();
    HcommSimtStoreLocalWqe(dwqeAddr, stage);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
}

__simt_callee__ inline void HcommSimtRingDwqeFromUb(__gm__ uint8_t* dwqeAddr, __ubuf__ uint8_t* wqeAddr)
{
    asc_threadfence();
    HcommSimtCopyUb128ToGm(dwqeAddr, wqeAddr);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + 64);
}

// Coalesce concurrent fixed-2BB immediate posts issued by one complete warp.
// The leading lane reserves the entire group with one GM CAS; every lane derives
// its SQ position from its active rank, and the last lane publishes the final PI.
__simt_callee__ inline bool HcommSimtReserveImmediateWarp(
    ChannelHandle channel, __gm__ uint64_t* headAddr, uint32_t sqDepth,
    uint32_t& curHead, bool& publishDwqe)
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
    uint64_t wqeBytes = laneCount * HCOMM_URMA_DWQE_SIZE;
    uint64_t cacheRequired =
        ((wqeBytes + laneCount * sizeof(HcommSimtUbRemoteCache) + HCOMM_URMA_DWQE_SIZE - 1U) &
         ~(static_cast<uint64_t>(HCOMM_URMA_DWQE_SIZE) - 1U)) +
        HCOMM_SIMT_UB_POST_CONTEXT_BYTES;

    wqeItem_ = nullptr;
    wqeKind_ = HcommSimtUbWqeKind::EMPTY;
    ubPostContext_ = nullptr;
    ubRemoteCache_ = nullptr;
    ubPostContextReady_ = false;
    if (buff == nullptr || wqeBytes > len) {
        return HCOMM_FAILED;
    }
    wqeItem_ = buff + linearLane * HCOMM_URMA_DWQE_SIZE;
    HcommSimtClearUb<HCOMM_URMA_DWQE_SIZE>(wqeItem_);

    if (cacheRequired <= len) {
        __ubuf__ uint8_t* remoteCacheBase = buff + wqeBytes;
        ubRemoteCache_ = reinterpret_cast<__ubuf__ HcommSimtUbRemoteCache*>(remoteCacheBase) + linearLane;
        ubRemoteCache_->valid = 0U;

        uint64_t contextOffset =
            (wqeBytes + laneCount * sizeof(HcommSimtUbRemoteCache) + HCOMM_URMA_DWQE_SIZE - 1U) &
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
    uint32_t state = asc_atomic_cas(
        stateAddr, HCOMM_SIMT_UB_CONTEXT_READY, HCOMM_SIMT_UB_CONTEXT_READY);
    if (state == HCOMM_SIMT_UB_CONTEXT_READY) {
        ubPostContextReady_ = ubPostContext_->channel == channel;
        return ubPostContextReady_;
    }

    uint32_t previous = asc_atomic_cas(
        stateAddr, HCOMM_SIMT_UB_CONTEXT_EMPTY, HCOMM_SIMT_UB_CONTEXT_INITIALIZING);
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
    constexpr uint32_t wqeBytes = bbCnt * HCOMM_URMA_WQE_BB_SIZE;

    __gm__ uint64_t* headAddr = post.headAddr;
    uint32_t curHead = 0U;
    bool useImmediateWarp = false;
    bool publishImmediateWarp = false;
    // ReserveDwqeBatch hands out fixed two-BB slots, so only an immediate post can consume
    // one. A deferred Write/Read occupies a single BB and would leave the slot's second BB
    // holding stale content, so it always reserves its own space.
    if (commit && hasReservedDwqeSlot_) {
        curHead = reservedDwqeHead_;
        hasReservedDwqeSlot_ = false;
    } else {
        if constexpr (commit) {
            useImmediateWarp = HcommSimtReserveImmediateWarp(
                channel, headAddr, post.meta.sqDepth, curHead, publishImmediateWarp);
        }
        if (!useImmediateWarp) {
            curHead = HcommSimtHeadIdx(HcommSimtReserve(headAddr, bbCnt, 1U));
        }
    }
    uint32_t nextHead = curHead + bbCnt;

    uint64_t sqBaseAddr = post.sqBaseAddr;
    uint32_t wqeSize = post.sqWqeSize;
    uint32_t sqDepth = post.meta.sqDepth;
    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));

    if constexpr (commit) {
        uint32_t blockLaneCount = blockDim.x * blockDim.y * blockDim.z;
        if (blockLaneCount == 1U && !hasDeferredDwqeBatch_) {
            HcommSimtPackedSqeTemplate sqeTemplate;
            HcommSimtBuildPackedSqeTemplate<sgeNum>(post.meta, sqeTemplate);
            HcommSimtLocalWqeStage localStage;
            HcommSimtBuildLocalDataWqe<opCode, sgeNum, config>(
                localStage, remoteAddr, localAddr, len, sqeTemplate, curHead, nextHead);
            HcommSimtPublishLocalWqe(sqeAddr, post.dwqeAddr, localStage);
            return HCOMM_SUCCESS;
        }
    }

    if (useImmediateWarp && !publishImmediateWarp) {
        HcommSimtPackedSqeTemplate sqeTemplate;
        HcommSimtBuildPackedSqeTemplate<sgeNum>(post.meta, sqeTemplate);
        HcommSimtStorePackedDataWqe<opCode, sgeNum, config>(
            sqeAddr, remoteAddr, localAddr, len, sqeTemplate, curHead, nextHead);
    } else {
        HcommSimtUrmaFillUbDataWqe<opCode, sgeNum, config>(
            wqeItem_, remoteAddr, localAddr, len, post.meta, curHead, nextHead, wqeKind_);
        HcommSimtCopyUbToGm<wqeBytes>(sqeAddr, wqeItem_);
    }
    if constexpr (commit) {
        if (useImmediateWarp) {
            asc_threadfence();
            if (publishImmediateWarp) {
                HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
            }
            return HCOMM_SUCCESS;
        }
        // A single-lane post is ordered by HcommSimtRingDwqeFromUb's thread fence.
        // A deferred batch still needs the block barrier to wait for all SQ writes.
        if (hasDeferredDwqeBatch_) {
            HcommSimtSyncBeforeRing(headAddr, nextHead);
        }
        if (PublishDwqeBatch(headAddr) != HCOMM_SUCCESS) {
            return HCOMM_FAILED;
        }
        HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
    } else {
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

template <bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWriteWithNotify(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* localAddr, uint64_t len,
    __gm__ uint8_t* notifyAddr, uint64_t notifyVal)
{
    return HCOMM_SUCCESS;
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostWriteValue(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, T value)
{
    (void)commitPipe;
    (void)reqPipe;

    static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
    static_assert(sizeof(T) <= HCOMM_URMA_DWQE_SIZE - sizeof(HcommUrmaSqeCtx),
        "Inline value must fit in the WQE payload area");
    if (wqeItem_ == nullptr) {
        return HCOMM_FAILED;
    }

    HcommSimtUbResolvedPost post;
    if (ResolveUbPostContext(channel, remoteAddr, sizeof(T), post) != HCOMM_SUCCESS) {
        return HCOMM_FAILED;
    }
    __gm__ uint64_t* headAddr = post.headAddr;
    uint32_t curHead = 0U;
    bool useImmediateWarp = false;
    bool publishImmediateWarp = false;
    if (hasReservedDwqeSlot_) {
        curHead = reservedDwqeHead_;
        hasReservedDwqeSlot_ = false;
    } else {
        if constexpr (commit) {
            useImmediateWarp = HcommSimtReserveImmediateWarp(
                channel, headAddr, post.meta.sqDepth, curHead, publishImmediateWarp);
        }
        if (!useImmediateWarp) {
            curHead = HcommSimtHeadIdx(HcommSimtReserve(headAddr, HCOMM_URMA_DWQE_BB_CNT, 1U));
        }
    }
    uint32_t nextHead = curHead + HCOMM_URMA_DWQE_BB_CNT;

    uint64_t sqBaseAddr = post.sqBaseAddr;
    uint32_t wqeSize = post.sqWqeSize;
    uint32_t sqDepth = post.meta.sqDepth;
    __gm__ uint8_t* sqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + wqeSize * (curHead % sqDepth));

    if constexpr (commit) {
        uint32_t blockLaneCount = blockDim.x * blockDim.y * blockDim.z;
        if (blockLaneCount == 1U && !hasDeferredDwqeBatch_) {
            HcommSimtPackedSqeTemplate sqeTemplate;
            HcommSimtBuildPackedSqeTemplate<0U>(post.meta, sqeTemplate);
            HcommSimtLocalWqeStage localStage;
            HcommSimtBuildLocalWriteValueWqe<T, config>(
                localStage, remoteAddr, value, sqeTemplate, curHead, nextHead);
            HcommSimtPublishLocalWqe(sqeAddr, post.dwqeAddr, localStage);
            return HCOMM_SUCCESS;
        }
    }

    if (useImmediateWarp && !publishImmediateWarp) {
        HcommSimtPackedSqeTemplate sqeTemplate;
        HcommSimtBuildPackedSqeTemplate<0U>(post.meta, sqeTemplate);
        HcommSimtStorePackedWriteValueWqe<T, config>(
            sqeAddr, remoteAddr, value, sqeTemplate, curHead, nextHead);
    } else {
        HcommSimtUrmaFillUbWriteValueWqe<T, config>(
            wqeItem_, remoteAddr, value, post.meta, curHead, nextHead, wqeKind_);
        HcommSimtCopyUb128ToGm(sqeAddr, wqeItem_);
    }
    if constexpr (commit) {
        if (useImmediateWarp) {
            asc_threadfence();
            if (publishImmediateWarp) {
                HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
            }
            return HCOMM_SUCCESS;
        }
        // A single-lane post is ordered by HcommSimtRingDwqeFromUb's thread fence.
        // A deferred batch still needs the block barrier to wait for all SQ writes.
        if (hasDeferredDwqeBatch_) {
            HcommSimtSyncBeforeRing(headAddr, nextHead);
        }
        if (PublishDwqeBatch(headAddr) != HCOMM_SUCCESS) {
            return HCOMM_FAILED;
        }
        HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
    } else {
        asc_threadfence();
    }
    return HCOMM_SUCCESS;
}

template <typename T, bool commit, auto commitPipe, auto reqPipe, HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostAtomic(
    ChannelHandle channel, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond)
{
    return HCOMM_SUCCESS;
}

// Aggregate fixed 2-BB/1-CQE DWQE WQEs in shared UB and assign their slots
// from a common head snapshot. The snapshot is not an atomic SQ claim, so only
// one batch may be in flight on this SQ until PublishDwqeBatch updates the head.
// Check whether this group crosses the physical SQ ring before touching the
// shared batch allocator. Crossing groups keep the original per-WQE reservation
// order used by the SIMT posting path.
__simt_callee__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReserveDwqeBatch(
    ChannelHandle channel, __ubuf__ uint32_t* state, bool active,
    uint32_t activeWqeCnt, uint32_t commitLane)
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
    return PostWriteValue<T, commit, commitPipe, reqPipe, config>(
        channel, reinterpret_cast<__gm__ uint8_t*>(dst), value);
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
