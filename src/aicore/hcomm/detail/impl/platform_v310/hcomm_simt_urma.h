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

__simt_callee__ inline void HcommSimtClear128(__gm__ uint8_t* addr)
{
#pragma unroll
    for (uint32_t i = 0; i < HCOMM_URMA_DWQE_SIZE / sizeof(uint64_t); ++i) {
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

template <HcommUrmaOpCode opCode, auto const& config>
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
    sqeCtx->sgeNum = 1U;
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
    HcommSimtUrmaFillUbSqeCtx<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
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

template <HcommUrmaOpCode opCode, typename T, auto const& config>
__simt_callee__ inline void HcommSimtUrmaFillUbAtomicWqe(
    __ubuf__ uint8_t* wqeAddr, __gm__ uint8_t* remoteAddr, __gm__ uint8_t* fetchAddr, T value, T cond,
    const HcommSimtUbPostMeta& meta, uint32_t curHead, uint32_t nextHead, HcommSimtUbWqeKind& wqeKind)
{
    HcommSimtPrepareUbAtomicWqe(wqeAddr, wqeKind);
    HcommSimtUrmaFillUbSqeCtx<opCode, config>(wqeAddr, remoteAddr, meta, curHead, nextHead);

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

__simt_callee__ inline void HcommSimtRingDwqeAt(__gm__ uint8_t* dwqeAddr, __gm__ uint8_t* sqeAddr)
{
    asc_threadfence();
    HcommSimtCopy128(dwqeAddr, sqeAddr);
    asc_dcci_single(dwqeAddr);
    asc_dcci_single(dwqeAddr + 64);
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

__simt_callee__ inline void HcommSimtBuildPackedSqeTemplate(
    const HcommSimtUbPostMeta& meta, HcommSimtPackedSqeTemplate& sqeTemplate)
{
    sqeTemplate.word1 = static_cast<uint64_t>(meta.tpId & 0xFFFFFFU) | (1ULL << 24U) |
                        (static_cast<uint64_t>(meta.remoteTokenId & 0xFFFFFU) << 32U);
    sqeTemplate.remoteEidL = meta.remoteEidL;
    sqeTemplate.remoteEidH = meta.remoteEidH;
    sqeTemplate.tokenWord = meta.remoteTokenValue;
    sqeTemplate.sqDepth = meta.sqDepth;
}

template <HcommUrmaOpCode opCode, auto const& config>
__simt_callee__ inline void HcommSimtStoreLocalSqeHeader(
    uint64_t* words, __gm__ uint8_t* remoteAddr,
    const HcommSimtPackedSqeTemplate& sqeTemplate, uint32_t curHead, uint32_t nextHead)
{
    uint64_t owner = (curHead & sqeTemplate.sqDepth) == 0U ? 1U : 0U;
    words[0] = static_cast<uint64_t>(nextHead & 0xFFFFU) |
               (static_cast<uint64_t>(HcommSimtUrmaFlag<config>() & 0xFFU) << 16U) |
               (1ULL << 28U) | (1ULL << 29U) | (owner << 31U) |
               (static_cast<uint64_t>(static_cast<uint32_t>(opCode) & 0xFFU) << 40U);
    words[1] = sqeTemplate.word1;
    words[2] = sqeTemplate.remoteEidL;
    words[3] = sqeTemplate.remoteEidH;
    words[4] = sqeTemplate.tokenWord;
    words[5] = reinterpret_cast<uint64_t>(remoteAddr);
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

__simt_callee__ inline void HcommSimtRingDwqe(__gm__ SqContext* sqCtx, __gm__ uint8_t* sqeAddr)
{
    __gm__ uint8_t* dwqeAddr = (__gm__ uint8_t*)(sqCtx->contextInfo.ubJfs.dbVa - HCOMM_URMA_DWQE_DB_OFFSET);
    HcommSimtRingDwqeAt(dwqeAddr, sqeAddr);
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
    uint64_t laneRequired = (static_cast<uint64_t>(linearLane) + 1U) * HCOMM_URMA_DWQE_SIZE;
    uint64_t cacheRequired =
        ((wqeBytes + laneCount * sizeof(HcommSimtUbRemoteCache) + HCOMM_URMA_DWQE_SIZE - 1U) &
         ~(static_cast<uint64_t>(HCOMM_URMA_DWQE_SIZE) - 1U)) +
        HCOMM_SIMT_UB_POST_CONTEXT_BYTES;

    wqeItem_ = nullptr;
    wqeKind_ = HcommSimtUbWqeKind::EMPTY;
    ubPostContext_ = nullptr;
    ubRemoteCache_ = nullptr;
    ubPostContextReady_ = false;
    if (buff == nullptr || laneRequired > len) {
        return HCOMM_SUCCESS;
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

    HcommSimtUbResolvedPost post;
    if (ResolveUbPostContext(channel, remoteAddr, len, post) != HCOMM_SUCCESS) {
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
        if (blockLaneCount == 1U && !hasDeferredDwqeBatch_ && wqeItem_ != nullptr) {
            HcommSimtUrmaFillUbWriteWithNotifyWqe<config>(
                wqeItem_, remoteAddr, localAddr, len, notifyAddr, notifyVal,
                post.meta, curHead, nextHead, wqeKind_);
            HcommSimtCopyUb128ToGm(sqeAddr, wqeItem_);
            asc_threadfence();
            HcommSimtCopyUb128ToDwqeStwt16(post.dwqeAddr, wqeItem_);
            asc_dcci_single(post.dwqeAddr);
            asc_dcci_single(post.dwqeAddr + HCOMM_URMA_WQE_BB_SIZE);
            return HCOMM_SUCCESS;
        }
    }

    if (wqeItem_ != nullptr) {
        HcommSimtUrmaFillUbWriteWithNotifyWqe<config>(
            wqeItem_, remoteAddr, localAddr, len, notifyAddr, notifyVal, post.meta, curHead, nextHead, wqeKind_);
        HcommSimtCopyUb128ToGm(sqeAddr, wqeItem_);
    } else {
        __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
        __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
        int32_t remoteIdx = HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, len);
        __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
        __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
        HcommSimtUrmaFillWriteWithNotifyWqe<config>(
            sqeAddr, remoteAddr, localAddr, len, notifyAddr, notifyVal, sqCtx, &remoteBuffers[remoteIdx], curHead,
            nextHead);
    }
    if constexpr (commit) {
        if (useImmediateWarp) {
            asc_threadfence();
            if (publishImmediateWarp) {
                if (wqeItem_ != nullptr) {
                    HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
                } else {
                    HcommSimtRingDwqeAt(post.dwqeAddr, sqeAddr);
                }
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
        if (wqeItem_ != nullptr) {
            HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
        } else {
            HcommSimtRingDwqeAt(post.dwqeAddr, sqeAddr);
        }
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
            HcommSimtBuildPackedSqeTemplate(post.meta, sqeTemplate);
            HcommSimtLocalWqeStage localStage;
            HcommSimtBuildLocalAtomicWqe<opCode, T, config>(
                localStage, remoteAddr, fetchAddr, value, cond,
                sqeTemplate, curHead, nextHead);
            HcommSimtPublishLocalWqe(sqeAddr, post.dwqeAddr, localStage);
            return HCOMM_SUCCESS;
        }
    }

    if (wqeItem_ != nullptr) {
        HcommSimtUrmaFillUbAtomicWqe<opCode, T, config>(
            wqeItem_, remoteAddr, fetchAddr, value, cond, post.meta, curHead, nextHead, wqeKind_);
        HcommSimtCopyUb128ToGm(sqeAddr, wqeItem_);
    } else {
        __gm__ HcommSimtChannelEntity* channelEntity = reinterpret_cast<__gm__ HcommSimtChannelEntity*>(channel);
        __gm__ RegedBufferEntity* remoteBuffers = HcommSimtRemoteBuffers(channel);
        int32_t remoteIdx =
            HcommSimtFindBufferIdx(remoteBuffers, channelEntity->remoteBufferNum, remoteAddr, sizeof(T));
        __gm__ SqContext* sqContexts = reinterpret_cast<__gm__ SqContext*>(channelEntity->sqContextAddr);
        __gm__ SqContext* sqCtx = &sqContexts[HCOMM_URMA_DEFAULT_QP_IDX];
        HcommSimtUrmaFillAtomicWqe<opCode, T, config>(
            sqeAddr, remoteAddr, fetchAddr, value, cond, sqCtx, &remoteBuffers[remoteIdx], curHead, nextHead);
    }
    if constexpr (commit) {
        if (useImmediateWarp) {
            asc_threadfence();
            if (publishImmediateWarp) {
                if (wqeItem_ != nullptr) {
                    HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
                } else {
                    HcommSimtRingDwqeAt(post.dwqeAddr, sqeAddr);
                }
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
        if (wqeItem_ != nullptr) {
            HcommSimtRingDwqeFromUb(post.dwqeAddr, wqeItem_);
        } else {
            HcommSimtRingDwqeAt(post.dwqeAddr, sqeAddr);
        }
    } else {
        asc_threadfence();
    }
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
