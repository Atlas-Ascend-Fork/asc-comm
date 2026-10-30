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
 * \file hcomm_aiv_urma.h
 * \brief Hcomm AIV URMA implementation for V310
 */

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include hcomm/hcomm.h instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_URMA_H

#include "hcomm_aiv_urma_def.h"

#include <c_api/vector_datamove/vector_datamove.h>

#include "../../common/hcomm_inner_def.h"
#include "../../common/hcomm_log.h"
#include "../../common/hcomm_utils.h"

typedef AscendC::HcommUrmaSqeCtx HcommUrmaSqeCtx;
typedef AscendC::HcommUrmaSgeCtx HcommUrmaSgeCtx;
typedef AscendC::HcommUrmaNotifyCtx HcommUrmaNotifyCtx;
typedef AscendC::HcommUrmaJfcCqeCtx HcommUrmaJfcCqeCtx;

namespace AscendC {
__aicore__ inline void HcommUrmaFillNotifyCtx(
    __ubuf__ HcommUrmaNotifyCtx* notifyCtx, const RegedBufferEntity& remoteMemInfo, GM_ADDR notifyAddr,
    uint64_t notifyVal)
{
    uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyTokenId = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    notifyCtx->notifyAddrL = notifyAddrValue & 0xFFFFFFFFU;
    notifyCtx->notifyAddrH = (notifyAddrValue >> 32) & 0xFFFFFFFFU;
    notifyCtx->notifyDataL = notifyVal & 0xFFFFFFFFU;
    notifyCtx->notifyDataH = (notifyVal >> 32) & 0xFFFFFFFFU;
}

template <HcommUrmaOpCode opCode, auto const& config, typename T>
__aicore__ inline void HcommUrmaFillSqeCtx(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr, const SqContext& sqCtx,
    const RegedBufferEntity& remoteMemInfo, uint32_t curHead, GM_ADDR notifyAddr = nullptr,
    const UdmaParams<T>& params = UdmaParams<T>{})
{
    sqeCtx->opcode =
        static_cast<uint32_t>(opCode == HcommUrmaOpCode::WRITE_WITH_REDUCE ? HcommUrmaOpCode::WRITE : opCode);
    sqeCtx->flag = (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
                   ((config.cqe & 0x1U) << 5U) | ((config.inlineEn & 0x1U) << 6U) |
                   (opCode == HcommUrmaOpCode::WRITE_WITH_REDUCE ? HCOMM_URMA_UDF_FLAG : 0U);
    sqeCtx->nf = 0;
    sqeCtx->tokenEn = 1;
    sqeCtx->rmtJettyType = 1;
    uint32_t baseBlockCount = sqCtx.contextInfo.ubJfs.sqDepth;
    sqeCtx->owner = (curHead & baseBlockCount) == 0 ? 1 : 0;
    sqeCtx->targetHint = 0;
    if constexpr (config.inlineEn == 1) {
        sqeCtx->inlineMsgLen = sizeof(T);
        sqeCtx->sgeNum = 0;
        __ubuf__ T* inlineAddr = (__ubuf__ T*)((__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx));
        *inlineAddr = params.value;
    } else {
        sqeCtx->inlineMsgLen = 0;
        sqeCtx->sgeNum = 1;
    }
    sqeCtx->tpId = sqCtx.contextInfo.ubJfs.tpID;
    sqeCtx->rmtJettyOrSegId = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    sqeCtx->rmtTokenValue = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeCtx->rmtAddrLOrTokenId = remoteAddrValue & 0xFFFFFFFFU;
    sqeCtx->rmtAddrHOrTokenValue = (remoteAddrValue >> 32) & 0xFFFFFFFFU;
    auto rmtEid = reinterpret_cast<const uint64_t*>(sqCtx.contextInfo.ubJfs.remoteEID);
    sqeCtx->rmtEidL = rmtEid[0];
    sqeCtx->rmtEidH = rmtEid[1];
    sqeCtx->udfType = 0;
    sqeCtx->reduceDataType = 0;
    sqeCtx->reduceOpcode = 0;
    if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx =
            (__ubuf__ HcommUrmaNotifyCtx*)((__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx));
        HcommUrmaFillNotifyCtx(notifyCtx, remoteMemInfo, notifyAddr, params.value);
    } else if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_REDUCE) {
        sqeCtx->reduceDataType = params.reduceDataType;
        sqeCtx->reduceOpcode = params.reduceOpcode;
    }
}

template <HcommUrmaOpCode opCode, auto const& config>
__aicore__ inline void HcommUrmaFillBatchSqeCtx(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr, const UbcBatchHandle& batchHandle, uint32_t sgeNum)
{
    static_assert(
        opCode == HcommUrmaOpCode::WRITE || opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY ||
            opCode == HcommUrmaOpCode::READ,
        "Batch SQE fast fill only supports Write, WriteWithNotify and Read");
    static_assert(config.inlineEn == 0, "Batch SQE fast fill does not support inline data");
    static_assert(
        sizeof(HcommUrmaSqeCtx) == 6U * sizeof(uint64_t), "Batch SQE fast fill requires the current 6-QW SQE layout");

    constexpr uint32_t flag =
        (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) | ((config.cqe & 0x1U) << 5U);
    constexpr uint32_t flagShift = 16U;
    constexpr uint32_t tokenEnShift = 28U;
    constexpr uint32_t rmtJettyTypeShift = 29U;
    constexpr uint32_t opcodeShift = 8U;
    constexpr uint32_t sgeNumShift = 24U;
    constexpr uint32_t remoteTokenIdMask = 0xFFFFFU;
    constexpr uint32_t tpIdMask = 0xFFFFFFU;

    constexpr uint32_t firstDw = (flag << flagShift) | (1U << tokenEnShift) | (1U << rmtJettyTypeShift);
    constexpr uint32_t secondDw = static_cast<uint32_t>(opCode) << opcodeShift;
    constexpr uint64_t sqeWord0 = static_cast<uint64_t>(firstDw) | (static_cast<uint64_t>(secondDw) << 32U);

    const uint32_t tpId = batchHandle.remoteInfo.tpId;
    const uint32_t tokenId = batchHandle.remoteInfo.tokenId;
    const uint32_t tokenValue = batchHandle.remoteInfo.tokenValue;
    const uint64_t remoteEidLow = batchHandle.remoteInfo.remoteEidLow;
    const uint64_t remoteEidHigh = batchHandle.remoteInfo.remoteEidHigh;
    const uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    __ubuf__ uint64_t* sqeWords = reinterpret_cast<__ubuf__ uint64_t*>(sqeCtx);

    // Write complete QWs so all reserved fields and sqeBbIdx are initialized without bit-field RMW stores.
    uint32_t thirdDw = (tpId & tpIdMask) | (sgeNum << sgeNumShift);
    uint32_t fourthDw = tokenId & remoteTokenIdMask;

    sqeWords[0] = sqeWord0;
    sqeWords[1] = static_cast<uint64_t>(thirdDw) | (static_cast<uint64_t>(fourthDw) << 32U);
    sqeWords[2] = remoteEidLow;
    sqeWords[3] = remoteEidHigh;
    sqeWords[4] = static_cast<uint64_t>(tokenValue);
    sqeWords[5] = remoteAddrValue;
}

__aicore__ inline void HcommUrmaFillBatchNotifyCtx(
    __ubuf__ HcommUrmaNotifyCtx* notifyCtx, const UbcBatchHandle& batchHandle, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    const uint32_t tokenId = batchHandle.remoteInfo.tokenId;
    const uint32_t tokenValue = batchHandle.remoteInfo.tokenValue;
    const uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyTokenId = tokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = tokenValue;
    notifyCtx->notifyAddrL = notifyAddrValue & 0xFFFFFFFFU;
    notifyCtx->notifyAddrH = (notifyAddrValue >> 32) & 0xFFFFFFFFU;
    notifyCtx->notifyDataL = notifyVal & 0xFFFFFFFFU;
    notifyCtx->notifyDataH = (notifyVal >> 32) & 0xFFFFFFFFU;
}

template <HcommUrmaOpCode opCode, typename T>
__aicore__ inline void HcommUrmaFillSgeCtx(
    __ubuf__ HcommUrmaSgeCtx* sgeCtx, uint64_t messageLen, __gm__ uint8_t* localAddr, UdmaParams<T> params)
{
    sgeCtx->len = static_cast<uint32_t>(messageLen);
    sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
    if constexpr (opCode == HcommUrmaOpCode::FAA) {
        __ubuf__ T* addDataAddr = (__ubuf__ T*)((__ubuf__ uint8_t*)sgeCtx + sizeof(HcommUrmaSgeCtx));
        *addDataAddr = params.value;
    } else if constexpr (opCode == HcommUrmaOpCode::CAS) {
        __ubuf__ T* swapDataAddr = (__ubuf__ T*)((__ubuf__ uint8_t*)sgeCtx + sizeof(HcommUrmaSgeCtx));
        *swapDataAddr = params.value;
        __ubuf__ T* cmpDataAddr = (__ubuf__ T*)((__ubuf__ uint8_t*)swapDataAddr + sizeof(T));
        *cmpDataAddr = params.cond;
    }
}

__aicore__ inline __gm__ uint32_t* HcommUrmaGetLockAddr(__gm__ ChannelEntity* channelEntity)
{
    // cqContextAddr shares a cache line with the channel counters. Read it bypassing DataCache to avoid caching
    // stale counters before the lock is acquired, then use the default CQ context's ubJfc.headAddr as the lock address.
    __gm__ uint64_t* cqContextsAddrField = reinterpret_cast<__gm__ uint64_t*>(
        reinterpret_cast<__gm__ uint8_t*>(channelEntity) + offsetof(ChannelEntity, cqContextAddr));
    uint64_t cqContextsAddr = ld_dev(cqContextsAddrField, 0);
    __gm__ CqContext* cqContext = reinterpret_cast<__gm__ CqContext*>(cqContextsAddr) + HCOMM_URMA_DEFAULT_QP_IDX;
    uint64_t lockAddrValue = cqContext->contextInfo.ubJfc.headAddr;
    return reinterpret_cast<__gm__ uint32_t*>(lockAddrValue);
}

__aicore__ inline void HcommUrmaDumpAmoCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, uint32_t atomicLen)
{
    HCOMM_DEBUG_TRAP_IF(sqeCtx == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    auto opcode = sqeCtx->opcode;
    if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::FAA)) {
        __ubuf__ uint8_t* amoDataAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx);
        uint64_t addValue = (atomicLen == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amoDataAddr) :
                                                              *(__ubuf__ uint64_t*)amoDataAddr;
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: addValue:0x%llx \n", addValue);
    } else if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::CAS)) {
        __ubuf__ uint8_t* amoDataAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx);
        uint64_t swapValue = (atomicLen == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amoDataAddr) :
                                                               *(__ubuf__ uint64_t*)amoDataAddr;
        uint64_t condValue = (atomicLen == sizeof(uint32_t)) ?
                                 static_cast<uint64_t>(*(__ubuf__ uint32_t*)(amoDataAddr + atomicLen)) :
                                 *(__ubuf__ uint64_t*)(amoDataAddr + atomicLen);
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: condValue:0x%llx, swapValue:0x%llx \n", condValue, swapValue);
    }
}

__aicore__ inline void HcommUrmaDumpSgeCtx(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __ubuf__ uint8_t* sgeAddr, uint32_t atomicLen)
{
    HCOMM_DEBUG_TRAP_IF(sqeCtx == nullptr || sgeAddr == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)sgeAddr;
    for (uint32_t i = 0; i < sqeCtx->sgeNum; i++) {
        auto sgeLen = sgeCtx->len;
        auto sgeRmtAddr = sgeCtx->va;
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: sge idx: %d, va: %p sge_len: %d \n", i, sgeRmtAddr, sgeLen);
        sgeCtx++;
    }
    HcommUrmaDumpAmoCtx(sqeCtx, atomicLen);
}

__aicore__ inline void HcommUrmaDumpNotifyCtx(__ubuf__ HcommUrmaNotifyCtx* notifyCtx)
{
    auto notifyTokenId = notifyCtx->notifyTokenId;
    auto notifyTokenValue = notifyCtx->notifyTokenValue;
    auto notifyAddrL = notifyCtx->notifyAddrL;
    auto notifyAddrH = notifyCtx->notifyAddrH;
    auto notifyDataL = notifyCtx->notifyDataL;
    auto notifyDataH = notifyCtx->notifyDataH;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA WQE: notifyTokenId: %x notifyTokenValue: %x notifyAddrL: %x notifyAddrH: %x notifyDataL: %x "
        "notifyDataH: %x \n",
        notifyTokenId, notifyTokenValue, notifyAddrL, notifyAddrH, notifyDataL, notifyDataH);
}

__aicore__ inline void HcommUrmaDumpWqeCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, uint32_t atomicLen)
{
    HCOMM_DEBUG_TRAP_IF(sqeCtx == nullptr, "Hcomm URMA WQE: nullptr pointer \n");
    auto sqeBbIdx = sqeCtx->sqeBbIdx;
    auto flag = sqeCtx->flag;
    auto rsv0 = sqeCtx->rsv0;
    auto nf = sqeCtx->nf;
    auto tokenEn = sqeCtx->tokenEn;
    auto rmtJettyType = sqeCtx->rmtJettyType;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sqe_bb_idx: %x flag: %x rsv0: %x nf: %x token_en: %x rmt_jetty_type: %x \n",
        sqeBbIdx, flag, rsv0, nf, tokenEn, rmtJettyType);
    auto owner = sqeCtx->owner;
    auto targetHint = sqeCtx->targetHint;
    auto opcode = sqeCtx->opcode;
    auto rsv1 = sqeCtx->rsv1;
    auto inlineMsgLen = sqeCtx->inlineMsgLen;
    auto tpId = sqeCtx->tpId;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: owner: %x target_hint: %x opcode: %x rsv1: %x inline_msg_len: %x tp_id: %x \n",
        owner, targetHint, opcode, rsv1, inlineMsgLen, tpId);
    auto sgeNum = sqeCtx->sgeNum;
    auto rmtJettyOrSegId = sqeCtx->rmtJettyOrSegId;
    auto rsv2 = sqeCtx->rsv2;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sge_num: %x rmt_jetty_or_seg_id: %x rsv2: %x \n", sgeNum, rmtJettyOrSegId, rsv2);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: rmt_eid: %x, %x \n", sqeCtx->rmtEidL, sqeCtx->rmtEidH);
    auto rmtTokenValue = sqeCtx->rmtTokenValue;
    auto udfType = sqeCtx->udfType;
    auto reduceDataType = sqeCtx->reduceDataType;
    auto reduceOpcode = sqeCtx->reduceOpcode;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: rmt_token_value: %x udf_type: %x reduce_data_type: %x reduce_opcode: %x \n",
        rmtTokenValue, udfType, reduceDataType, reduceOpcode);
    auto rmtAddrLOrTokenId = sqeCtx->rmtAddrLOrTokenId;
    auto rmtAddrHOrTokenValue = sqeCtx->rmtAddrHOrTokenValue;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: rmt_addr_l_or_token_id: %x rmt_addr_h_or_token_value: %x \n", rmtAddrLOrTokenId,
        rmtAddrHOrTokenValue);
    __ubuf__ uint8_t* sgeAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx);
    if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::WRITE_WITH_NOTIFY)) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx = (__ubuf__ HcommUrmaNotifyCtx*)sgeAddr;
        HcommUrmaDumpNotifyCtx(notifyCtx);
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    HcommUrmaDumpSgeCtx(sqeCtx, sgeAddr, atomicLen);
}

__aicore__ inline void HcommUrmaDumpCqeCtx(__gm__ uint32_t* cqeWords)
{
    HCOMM_DEBUG_TRAP_IF(cqeWords == nullptr, "Hcomm URMA CQE: nullptr pointer \n");
    uint32_t dw0 = ld_dev(cqeWords, 0);
    uint32_t dw1 = ld_dev(cqeWords + 1, 0);
    uint32_t dw2 = ld_dev(cqeWords + 2, 0);
    uint32_t dw3 = ld_dev(cqeWords + 3, 0);
    HCOMM_KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA CQE: DW0 - sR: %d, isJetty: %d, owner: %d, inlineEn: %d, "
        "opcode: %d, fd: %d, substatus: %d, status: %d \n",
        dw0 & 1U, (dw0 >> 1U) & 1U, (dw0 >> 2U) & 1U, (dw0 >> 3U) & 1U, (dw0 >> 4U) & 0x7U, (dw0 >> 7U) & 1U,
        (dw0 >> 16U) & 0xFFU, (dw0 >> 24U) & 0xFFU);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW1 - entryIdx: %d, localNumL: %d \n", dw1 & 0xFFFFU, dw1 >> 16U);
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW2 - localNumH: %d, rmtIdx: %d \n", dw2 & 0xFU, (dw2 >> 4U) & 0xFFFFFU);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW3 - tpn: %d \n", dw3 & 0xFFFFFFU);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW4 - byteCnt: %d \n", ld_dev(cqeWords + 4, 0));
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW5-DW6 - userData: 0x%x%x \n", ld_dev(cqeWords + 6, 0), ld_dev(cqeWords + 5, 0));
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW7-DW10 - rmtEid: [0x%x, 0x%x, 0x%x, 0x%x] \n", ld_dev(cqeWords + 7, 0),
        ld_dev(cqeWords + 8, 0), ld_dev(cqeWords + 9, 0), ld_dev(cqeWords + 10, 0));
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW11-DW12 - data: 0x%x%x \n", ld_dev(cqeWords + 12, 0), ld_dev(cqeWords + 11, 0));
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW13-DW15 - inlineData: [0x%x, 0x%x, 0x%x] \n", ld_dev(cqeWords + 13, 0),
        ld_dev(cqeWords + 14, 0), ld_dev(cqeWords + 15, 0));
}

__aicore__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::HcommImpl() {}

__aicore__ inline HcommImpl<COMM_PROTOCOL_UB_CTP>::~HcommImpl() {}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    HCOMM_DEBUG_TRAP_IF(
        (reinterpret_cast<uintptr_t>(buff) & (HCOMM_URMA_ADDR_ALIGNMENT - 1U)) != 0U,
        "Hcomm Init failed: buff is not 32-byte aligned. addr = %llu\n",
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(buff)));
    HCOMM_DEBUG_TRAP_IF(len < HCOMM_URMA_TMP_BUF_SIZE, "Hcomm Init failed: len = %u\n", len);

    wqeItem_ = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    HCOMM_DEBUG_TRAP_IF(
        len > buff.GetSize() * static_cast<uint32_t>(sizeof(T)), "Hcomm Init failed: len = %u, tensorSize = %llu\n",
        len, static_cast<uint64_t>(buff.GetSize()) * sizeof(T));
    return Init(reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), len);
}

__aicore__ inline void HcommUrmaResolveBatchRemote(
    const MultiChannelRemoteInfo& remoteInfo, GM_ADDR, BatchRemoteInfo& resolvedRemoteInfo)
{
    auto* remoteBuffers = reinterpret_cast<RegedBufferEntity*>(remoteInfo.remoteBufferAddr);
    HCOMM_DEBUG_TRAP_IF(remoteBuffers == nullptr || remoteInfo.remoteBufferNum == 0U, "Invalid remote buffers\n");

    resolvedRemoteInfo.tokenId =
        remoteBuffers[HCOMM_URMA_DEFAULT_BUFFER_IDX].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    resolvedRemoteInfo.tokenValue =
        remoteBuffers[HCOMM_URMA_DEFAULT_BUFFER_IDX].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    resolvedRemoteInfo.tpId = remoteInfo.tpId;
    resolvedRemoteInfo.remoteEidLow = remoteInfo.remoteEidLow;
    resolvedRemoteInfo.remoteEidHigh = remoteInfo.remoteEidHigh;
}

__aicore__ inline UbcBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    ChannelHandle channel, __ubuf__ uint8_t* buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr)
{
    (void)localAddr;
    __gm__ ChannelEntity* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto remoteEid = reinterpret_cast<const uint64_t*>(sqCtx.contextInfo.ubJfs.remoteEID);
    MultiChannelRemoteInfo remoteSource{};
    remoteSource.remoteBufferAddr = reinterpret_cast<uint64_t>(channelEntity->remoteBufferAddr);
    remoteSource.remoteBufferNum = channelEntity->remoteBufferNum;
    remoteSource.tpId = sqCtx.contextInfo.ubJfs.tpID;
    remoteSource.remoteEidLow = remoteEid[0];
    remoteSource.remoteEidHigh = remoteEid[1];

    UbcBatchHandle batchHandle{};
    HcommUrmaResolveBatchRemote(remoteSource, remoteAddr, batchHandle.remoteInfo);
    batchHandle.channelHandle = channel;
    batchHandle.sqContext = sqCtx;
    batchHandle.cqContext = cqCtx;
    batchHandle.cursor.sqHead = channelEntity->sqHead;
    batchHandle.cursor.sqTail = channelEntity->sqTail;
    batchHandle.cursor.cqHead = channelEntity->cqHead;
    batchHandle.cursor.cqTail = channelEntity->cqTail;
    batchHandle.cursor.preSqCnt = 0U;
    batchHandle.buffer.rawBuffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    batchHandle.buffer.bufferCapacity = buffLen / HCOMM_URMA_WQEBB_SIZE;
    return batchHandle;
}

__aicore__ inline UbcMultiBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    MultiChannelHandle multiChannel, __ubuf__ uint8_t* buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr)
{
    (void)remoteAddr;
    (void)localAddr;
    HCOMM_DEBUG_TRAP_IF(multiChannel == MultiChannelHandle{}, "Hcomm MakeBatchHandle failed with empty handle\n");

    auto* multiChannelEntity = reinterpret_cast<__gm__ MultiChannelEntity*>(static_cast<uint64_t>(multiChannel));
    ChannelHandle channel = multiChannelEntity->channelHandle;
    HCOMM_DEBUG_TRAP_IF(
        channel == 0U || multiChannelEntity->channelNum == 0U || multiChannelEntity->remoteInfoAddr == 0U,
        "Hcomm MakeBatchHandle failed with invalid multi channel entity\n");
    auto* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];

    UbcMultiBatchHandle multiBatchHandle{};
    multiBatchHandle.handle.sqContext = sqCtx;
    multiBatchHandle.handle.cqContext = cqCtx;
    multiBatchHandle.handle.cursor.sqHead = channelEntity->sqHead;
    multiBatchHandle.handle.cursor.sqTail = channelEntity->sqTail;
    multiBatchHandle.handle.cursor.cqHead = channelEntity->cqHead;
    multiBatchHandle.handle.cursor.cqTail = channelEntity->cqTail;
    multiBatchHandle.handle.buffer.rawBuffer = reinterpret_cast<__ubuf__ uint32_t*>(buff);
    multiBatchHandle.handle.buffer.bufferCapacity = buffLen / HCOMM_URMA_WQEBB_SIZE;
    multiBatchHandle.channelHandle = channel;
    multiBatchHandle.channelNum = multiChannelEntity->channelNum;
    multiBatchHandle.remoteInfoAddr = multiChannelEntity->remoteInfoAddr;
    return multiBatchHandle;
}

template <typename U>
__aicore__ inline UbcBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr)
{
    return MakeBatchHandle(
        channel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buffLen, remoteAddr, localAddr);
}

template <typename U>
__aicore__ inline UbcMultiBatchHandle HcommImpl<COMM_PROTOCOL_UB_CTP>::MakeBatchHandle(
    MultiChannelHandle multiChannel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr,
    GM_ADDR localAddr)
{
    return MakeBatchHandle(
        multiChannel, reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr()), buffLen, remoteAddr, localAddr);
}

__aicore__ inline UbcBatchHandle& HcommImpl<COMM_PROTOCOL_UB_CTP>::GetHandleRef(
    UbcBatchHandle& batchHandle, uint32_t channelIndex, GM_ADDR remoteAddr)
{
    (void)channelIndex;
    (void)remoteAddr;
    return batchHandle;
}

__aicore__ inline UbcBatchHandle& HcommImpl<COMM_PROTOCOL_UB_CTP>::GetHandleRef(
    UbcMultiBatchHandle& multiBatchHandle, uint32_t channelIndex, GM_ADDR remoteAddr)
{
    multiBatchHandle.handle.remoteInfo = BatchRemoteInfo{};
    multiBatchHandle.handle.channelHandle = 0U;
    HCOMM_DEBUG_TRAP_IF(
        channelIndex >= multiBatchHandle.channelNum, "Hcomm GetHandleRef failed with invalid channel index\n");

    auto* remoteInfos = reinterpret_cast<__gm__ MultiChannelRemoteInfo*>(multiBatchHandle.remoteInfoAddr);
    MultiChannelRemoteInfo remoteSource{};
    remoteSource.remoteBufferAddr = remoteInfos[channelIndex].remoteBufferAddr;
    remoteSource.remoteBufferNum = remoteInfos[channelIndex].remoteBufferNum;
    remoteSource.remoteEidLow = remoteInfos[channelIndex].remoteEidLow;
    remoteSource.remoteEidHigh = remoteInfos[channelIndex].remoteEidHigh;
    remoteSource.tpId = remoteInfos[channelIndex].tpId;
    HcommUrmaResolveBatchRemote(remoteSource, remoteAddr, multiBatchHandle.handle.remoteInfo);
    multiBatchHandle.handle.channelHandle = multiBatchHandle.channelHandle;
    return multiBatchHandle.handle;
}

template <HcommUrmaOpCode opCode, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchPostSend(
    UbcBatchHandle& batchHandle, GM_ADDR remoteAddr, GM_ADDR localAddr, uint32_t len, GM_ADDR notifyAddr,
    uint64_t notifyVal)
{
    static_assert(
        opCode == HcommUrmaOpCode::WRITE || opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY ||
            opCode == HcommUrmaOpCode::READ,
        "BatchPostSend only supports Write, WriteWithNotify and Read");
    static_assert(config.cqe == 0 || config.cqe == 1, "BatchPostSend supports cqe values 0 and 1 only");
    static_assert(config.inlineEn == 0, "BatchPostSend does not support inline data");

    constexpr bool withNotify = opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY;
    constexpr uint32_t wqeBytes =
        sizeof(HcommUrmaSqeCtx) + (withNotify ? sizeof(HcommUrmaNotifyCtx) : 0U) + sizeof(HcommUrmaSgeCtx);
    constexpr uint32_t wqebbCount = (wqeBytes + HCOMM_URMA_WQEBB_SIZE - 1U) / HCOMM_URMA_WQEBB_SIZE;

    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    HCOMM_DEBUG_TRAP_IF(
        preSqCnt > batchHandle.buffer.bufferCapacity || wqebbCount > batchHandle.buffer.bufferCapacity - preSqCnt,
        "Hcomm BatchPostSend failed with insufficient buffer\n");

    // Contract: descriptors and remote addresses are valid for the channel/MR selected by batchHandle.
    // These caller guarantees are not revalidated here.
    __ubuf__ uint8_t* currentWqeAddr =
        reinterpret_cast<__ubuf__ uint8_t*>(batchHandle.buffer.rawBuffer) + preSqCnt * HCOMM_URMA_WQEBB_SIZE;
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(currentWqeAddr);

    HcommUrmaFillBatchSqeCtx<opCode, config>(sqeCtx, reinterpret_cast<__gm__ uint8_t*>(remoteAddr), batchHandle, 1U);

    __ubuf__ uint8_t* sgeAddr = currentWqeAddr + sizeof(HcommUrmaSqeCtx);
    if constexpr (withNotify) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx = reinterpret_cast<__ubuf__ HcommUrmaNotifyCtx*>(sgeAddr);
        HcommUrmaFillBatchNotifyCtx(notifyCtx, batchHandle, notifyAddr, notifyVal);
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = reinterpret_cast<__ubuf__ HcommUrmaSgeCtx*>(sgeAddr);
    HcommUrmaFillSgeCtx<opCode>(sgeCtx, len, reinterpret_cast<__gm__ uint8_t*>(localAddr), UdmaParams<uint64_t>{});

    batchHandle.cursor.preSqCnt = preSqCnt + wqebbCount;
    if constexpr (config.cqe == 1) {
        batchHandle.cursor.cqHead++;
    }
    HCOMM_DEBUG_FUNC(HcommUrmaDumpWqeCtx, sqeCtx, sizeof(uint64_t));
    return HCOMM_SUCCESS;
}

template <HcommUrmaOpCode opCode, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchPostSend(
    UbcBatchHandle& batchHandle, GM_ADDR remoteAddr, const BufDesc* localDescs, uint32_t sgeNum, GM_ADDR notifyAddr,
    uint64_t notifyVal)
{
    static_assert(
        opCode == HcommUrmaOpCode::WRITE || opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY ||
            opCode == HcommUrmaOpCode::READ,
        "BatchPostSend only supports Write, WriteWithNotify and Read");
    static_assert(config.cqe == 0 || config.cqe == 1, "BatchPostSend supports cqe values 0 and 1 only");
    static_assert(config.inlineEn == 0, "BatchPostSend does not support inline data");

    constexpr bool withNotify = opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY;
    HCOMM_DEBUG_TRAP_IF(
        sgeNum < HCOMM_URMA_BATCH_MIN_SGE_NUM ||
            sgeNum > (withNotify ? HCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM : HCOMM_URMA_BATCH_DATA_MAX_SGE_NUM),
        "Hcomm BatchPostSend failed with invalid SGE count sgeNum=%u validRange=[%u, %u]\n", sgeNum,
        HCOMM_URMA_BATCH_MIN_SGE_NUM,
        withNotify ? HCOMM_URMA_BATCH_NOTIFY_MAX_SGE_NUM : HCOMM_URMA_BATCH_DATA_MAX_SGE_NUM);

    constexpr uint32_t fixedBytes = sizeof(HcommUrmaSqeCtx) + (withNotify ? sizeof(HcommUrmaNotifyCtx) : 0U);
    uint32_t wqeBytes = fixedBytes + sgeNum * sizeof(HcommUrmaSgeCtx);
    uint32_t wqebbCount = (wqeBytes + HCOMM_URMA_WQEBB_SIZE - 1U) / HCOMM_URMA_WQEBB_SIZE;
    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    HCOMM_DEBUG_TRAP_IF(
        preSqCnt > batchHandle.buffer.bufferCapacity || wqebbCount > batchHandle.buffer.bufferCapacity - preSqCnt,
        "Hcomm BatchPostSend failed with insufficient buffer\n");

    // Contract: descriptors and remote addresses are valid for the channel/MR selected by batchHandle.
    // These caller guarantees are not revalidated here.
    __ubuf__ uint8_t* currentWqeAddr =
        reinterpret_cast<__ubuf__ uint8_t*>(batchHandle.buffer.rawBuffer) + preSqCnt * HCOMM_URMA_WQEBB_SIZE;
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(currentWqeAddr);

    HcommUrmaFillBatchSqeCtx<opCode, config>(
        sqeCtx, reinterpret_cast<__gm__ uint8_t*>(remoteAddr), batchHandle, sgeNum);

    __ubuf__ uint8_t* sgeAddr = currentWqeAddr + sizeof(HcommUrmaSqeCtx);
    if constexpr (withNotify) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx = reinterpret_cast<__ubuf__ HcommUrmaNotifyCtx*>(sgeAddr);
        HcommUrmaFillBatchNotifyCtx(notifyCtx, batchHandle, notifyAddr, notifyVal);
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = reinterpret_cast<__ubuf__ HcommUrmaSgeCtx*>(sgeAddr);
    for (uint32_t i = 0U; i < sgeNum; ++i) {
        HcommUrmaFillSgeCtx<opCode>(
            &sgeCtx[i], localDescs[i].len, reinterpret_cast<__gm__ uint8_t*>(localDescs[i].addr),
            UdmaParams<uint64_t>{});
    }

    batchHandle.cursor.preSqCnt = preSqCnt + wqebbCount;
    if constexpr (config.cqe == 1) {
        batchHandle.cursor.cqHead++;
    }
    HCOMM_DEBUG_FUNC(HcommUrmaDumpWqeCtx, sqeCtx, sizeof(uint64_t));
    return HCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return BatchPostSend<HcommUrmaOpCode::WRITE, config>(batchHandle, dst, src, len);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum)
{
    return BatchPostSend<HcommUrmaOpCode::WRITE, config>(batchHandle, dst, srcDescs, srcNum);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return BatchPostSend<HcommUrmaOpCode::READ, config>(batchHandle, src, dst, len);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    UbcBatchHandle& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src)
{
    return BatchPostSend<HcommUrmaOpCode::READ, config>(batchHandle, src, dstDescs, dstNum);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    return BatchPostSend<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(batchHandle, dst, src, len, notifyAddr, notifyVal);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum, GM_ADDR notifyAddr,
    uint64_t notifyVal)
{
    return BatchPostSend<HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
        batchHandle, dst, srcDescs, srcNum, notifyAddr, notifyVal);
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCqWhenCqOverflow(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead, uint32_t cqeCnt)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t cqTail = channelEntity->cqTail;
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    uint32_t sqDepth = sqCtx.contextInfo.ubJfs.sqDepth;
    if (((cqeCnt + POLL_CQ_THRESHOLD) & (cqDepth - 1U)) == (cqTail & (cqDepth - 1U))) {
        uint32_t idx = (cqTail + NUM_CQE_PER_POLL_CQ) > cqeCnt ? cqeCnt : cqTail + NUM_CQE_PER_POLL_CQ;
        HCOMM_KERNEL_LOG(
            KERNEL_INFO, "Hcomm URMA queue overflow sqHead=%u cqeCnt=%u cqTail=%u idx=%u sqDepth=%u cqDepth=%u \n",
            sqHead, cqeCnt, cqTail, idx, sqDepth, cqDepth);
        (void)PollCq(channel, idx);
    }
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCqWhenSqOverflow(
    ChannelHandle channel, const SqContext& sqCtx, uint32_t sqHead)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t sqTail = channelEntity->sqTail;
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    uint32_t sqDepth = sqCtx.contextInfo.ubJfs.sqDepth;
    uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
    if ((uint32_t)outstanding + POLL_CQ_THRESHOLD >= sqDepth) {
        (void)PollCq<true>(channel, channelEntity->cqHead, sqHead, sqDepth, POLL_CQ_THRESHOLD);
    }
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UB_CTP>::CommitImpl(
    ChannelHandle channel, const SqContext& sqCtx, uint32_t sqHead, uint32_t cqeCnt)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    SyncAction<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    st_dev(sqHead, reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.dbVa), 0);
    PollCqWhenCqOverflow(channel, sqCtx, cqCtx, sqHead, cqeCnt);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, HcommUrmaOpCode opCode, auto const& config, typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PostSend(
    ChannelHandle channel, GM_ADDR remoteAddr, GM_ADDR localAddr, uint64_t len, GM_ADDR notifyAddr,
    const UdmaParams<T>& params)
{
    (void)commitPipe;
    (void)reqPipe;

    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t curHead = channelEntity->sqHead;
    uint32_t cqeCnt = channelEntity->cqHead;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA PostSend resolved remote buffer curHead=%u sqDepth=%u \n", curHead,
        sqCtx.contextInfo.ubJfs.sqDepth);
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    PollCqWhenSqOverflow(channel, sqCtx, curHead);

    // write SQE
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeItem_;
    auto remoteMemInfo = channelEntity->remoteBufferAddr[HCOMM_URMA_DEFAULT_BUFFER_IDX];
    HcommUrmaFillSqeCtx<opCode, config>(
        sqeCtx, (__gm__ uint8_t*)remoteAddr, sqCtx, remoteMemInfo, curHead, notifyAddr, params);

    if constexpr (config.inlineEn == 0) {
        // write SGE
        __ubuf__ uint8_t* sgeAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx);
        if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
            sgeAddr += sizeof(HcommUrmaNotifyCtx);
        }
        __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)sgeAddr;
        HcommUrmaFillSgeCtx<opCode>(sgeCtx, len, (__gm__ uint8_t*)localAddr, params);
    }

    // SQE & SGE cache flush
    uint64_t sqBaseAddr = sqCtx.contextInfo.ubJfs.sqVa;
    uint32_t wqeSize = sqCtx.contextInfo.ubJfs.wqeSize;
    uint32_t baseBlockCount = sqCtx.contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqeAddr = (__gm__ uint8_t*)(sqBaseAddr + wqeSize * (curHead & (baseBlockCount - 1U)));

    constexpr uint32_t wqeBbCnt = (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY || opCode == HcommUrmaOpCode::FAA ||
                                   opCode == HcommUrmaOpCode::CAS) ?
                                      2U :
                                      1U;
    SyncAction<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    if constexpr (wqeBbCnt == 2) {
        if (unlikely((curHead & (baseBlockCount - 1U)) == baseBlockCount - 1U)) {
            uint32_t wqeSizePerU32 = wqeSize / sizeof(uint32_t);
            asc_copy_ub2gm_align(
                sqeAddr, reinterpret_cast<__ubuf__ uint8_t*>(wqeItem_), 1, wqeSize,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
            asc_copy_ub2gm_align(
                reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr),
                reinterpret_cast<__ubuf__ uint8_t*>(wqeItem_ + wqeSizePerU32), 1, wqeSize,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        } else {
            asc_copy_ub2gm_align(
                sqeAddr, reinterpret_cast<__ubuf__ uint8_t*>(wqeItem_), 1, wqeSize * wqeBbCnt,
                asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
        }
    } else {
        asc_copy_ub2gm_align(
            sqeAddr, reinterpret_cast<__ubuf__ uint8_t*>(wqeItem_), 1, wqeSize * wqeBbCnt,
            asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }

    if constexpr (config.cqe != 0) {
        cqeCnt++;
        channelEntity->cqHead = cqeCnt;
    }
    curHead += wqeBbCnt;
    channelEntity->sqHead = curHead;

    if constexpr (commit) {
        CommitImpl(channel, sqCtx, curHead, cqeCnt);
    }
    HCOMM_DEBUG_FUNC(HcommUrmaDumpWqeCtx, sqeCtx, sizeof(T));
    return HCOMM_SUCCESS;
}

template <bool sqSafeMode>
__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCqImpl(
    uint64_t cqBaseAddr, uint32_t cqeSize, uint32_t cqDepth, uint32_t expectIdx, uint32_t& curTail, uint32_t& sqTail,
    uint32_t sqHead, uint32_t sqDepth, uint32_t threshold)
{
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA PollCq enter expectIdx=%u curTail=%u cqDepth=%u \n", expectIdx, curTail, cqDepth);

#if defined(UT_TEST)
    curTail = expectIdx;
    sqTail = expectIdx;
#else
    uint32_t sqMask = sqDepth - 1U;
    while (true) {
        bool shouldContinue = false;
        if constexpr (sqSafeMode) {
            uint16_t outstanding = (uint16_t)((uint16_t)(sqHead & 0xFFFFU) - (uint16_t)(sqTail & 0xFFFFU));
            // Poll early when SQ is near-full, but only while CQEs remain un-consumed (curTail != expectIdx).
            shouldContinue = (uint32_t)outstanding + threshold >= sqDepth && curTail != expectIdx;
        } else {
            // Keep polling while CQEs remain, until reaching expectIdx.
            shouldContinue = curTail != expectIdx;
        }
        if (!shouldContinue) {
            break;
        }

        __gm__ uint32_t* cqe =
            (__gm__ uint32_t*)(cqBaseAddr + static_cast<uint64_t>(cqeSize) * (curTail & (cqDepth - 1U)));
        uint32_t validOwner = (curTail / cqDepth) & 1U;
        uint32_t times = 0;
        uint32_t ret = HCOMM_SUCCESS;
        // Read DW0 and DW1 in one 64-bit ld_dev; owner is DW0 bit 2, entryIdx is in the upper word.
        uint64_t cqeWords = ld_dev(reinterpret_cast<__gm__ uint64_t*>(cqe), 0);
        uint32_t cqeFlags = static_cast<uint32_t>(cqeWords);
        while (((cqeFlags >> 2U) & 1U) == validOwner && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            if (times != 0 && times % HCOMM_URMA_POLL_CQ_HEARTBEAT_TIMES == 0) {
                HCOMM_KERNEL_LOG(
                    KERNEL_WARN, "Hcomm URMA Poll CQ heartbeat times=%u curTail=%u expectIdx=%u \n", times, curTail,
                    expectIdx);
            }
            cqeWords = ld_dev(reinterpret_cast<__gm__ uint64_t*>(cqe), 0);
            cqeFlags = static_cast<uint32_t>(cqeWords);
            times++;
        }
        if (times >= HCOMM_URMA_MAX_RETRY_TIMES) {
            HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Poll CQ timeout curTail=%u expectIdx=%u \n", curTail, expectIdx);
            HCOMM_DEBUG_FUNC(HcommUrmaDumpCqeCtx, cqe);
            ret = 0xFFU;
        } else {
            // check CQE status
            uint32_t status = (cqeFlags >> 24U) & 0xFFU;
            uint32_t subStatus = (cqeFlags >> 16U) & 0xFFU;
            constexpr uint8_t statusShift = 8;
            if (status != 0 || subStatus != 0) {
                HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA CQE failed status=%u subStatus=%u \n", status, subStatus);
                HCOMM_DEBUG_FUNC(HcommUrmaDumpCqeCtx, cqe);
                ret = (status << statusShift) | subStatus;
            }
        }
        if (ret != HCOMM_SUCCESS) {
            HCOMM_KERNEL_LOG(KERNEL_ERROR, "Poll CQ error, status=%u.\n", ret);
            Trap();
            return ret;
        }
        uint32_t entryIdx = static_cast<uint32_t>(cqeWords >> 32U) & 0xFFFFU;
        sqTail += ((entryIdx - sqTail) & sqMask) + 1U;
        curTail++;
    }
#endif
    return HCOMM_SUCCESS;
}

template <bool sqSafeMode>
__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PollCq(
    ChannelHandle channel, uint32_t expectIdx, uint32_t sqHead, uint32_t sqDepth, uint32_t threshold)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t curTail = channelEntity->cqTail;
    uint32_t sqTail = channelEntity->sqTail;

    uint32_t ret = PollCqImpl<sqSafeMode>(
        cqCtx.contextInfo.ubJfc.scqVa, cqCtx.contextInfo.ubJfc.cqeSize, cqCtx.contextInfo.ubJfc.cqDepth, expectIdx,
        curTail, sqTail, sqHead, sqDepth, threshold);
    if (ret != HCOMM_SUCCESS) {
        return ret;
    }

    // update CQ tail
    channelEntity->cqTail = curTail;
    channelEntity->sqTail = sqTail;

    // ring CQ doorbell
    st_dev(curTail & 0xFFFFFFU, (__gm__ uint32_t*)cqCtx.contextInfo.ubJfc.dbVa, 0);

    return HCOMM_SUCCESS;
}

__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::PollBatchCq(UbcBatchHandle& batchHandle, uint32_t expectIdx)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }

    uint32_t curTail = batchHandle.cursor.cqTail;
    uint32_t ret = PollCqImpl<false>(
        batchHandle.cqContext.contextInfo.ubJfc.scqVa, batchHandle.cqContext.contextInfo.ubJfc.cqeSize,
        batchHandle.cqContext.contextInfo.ubJfc.cqDepth, expectIdx, curTail, batchHandle.cursor.sqTail, 0U,
        batchHandle.sqContext.contextInfo.ubJfs.sqDepth, 0U);
    if (ret != HCOMM_SUCCESS) {
        return ret;
    }

    batchHandle.cursor.cqTail = curTail;
    auto* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    channelEntity->sqTail = batchHandle.cursor.sqTail;
    channelEntity->cqTail = curTail;
    st_dev(curTail & 0xFFFFFFU, reinterpret_cast<__gm__ uint32_t*>(batchHandle.cqContext.contextInfo.ubJfc.dbVa), 0);
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(channel, dst, src, len);
}

template <typename T, HcommUrmaReduceOp reduceOp, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteReduceNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t count)
{
    constexpr uint32_t reduceDataType = HCOMM_URMA_REDUCE_DATA_TYPE<T>;
    static_assert(
        reduceDataType != HCOMM_URMA_INVALID_REDUCE_DATA_TYPE,
        "WriteReduceNbi only supports int8_t, int16_t, int32_t, uint32_t, half, float and bfloat16_t");
    static_assert(
        reduceOp == HcommUrmaReduceOp::MAX || reduceOp == HcommUrmaReduceOp::MIN || reduceOp == HcommUrmaReduceOp::SUM,
        "WriteReduceNbi only supports MAX, MIN and SUM");
    static_assert(config.inlineEn == 0, "WriteReduceNbi does not support inline data in WQE");
    UdmaParams<uint32_t> params{};
    params.reduceDataType = reduceDataType;
    params.reduceOpcode = static_cast<uint32_t>(reduceOp);
    uint64_t len = count * sizeof(T);
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE_WITH_REDUCE, config>(
        channel, dst, src, len, nullptr, params);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::READ, config>(channel, src, dst, len);
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
    UdmaParams<T> params{value, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(
        channel, dst, nullptr, sizeof(T), nullptr, params);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    UdmaParams<uint64_t> params{notifyVal, 0, 0, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
        channel, dst, src, len, notifyAddr, params);
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicFAA(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T addVal)
{
    static_assert(
        std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value || std::is_same<T, int64_t>::value ||
            std::is_same<T, uint64_t>::value,
        "AtomicFAA only supports int32_t, uint32_t, int64_t, uint64_t");
    UdmaParams<T> params{addVal, 0, 0, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::FAA, config>(
        channel, dst, fetchAddr, sizeof(T), nullptr, params);
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::AtomicCAS(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR fetchAddr, T compareVal, T swapVal)
{
    static_assert(
        std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value || std::is_same<T, int64_t>::value ||
            std::is_same<T, uint64_t>::value,
        "AtomicCAS only supports int32_t, uint32_t, int64_t, uint64_t");
    UdmaParams<T> params{swapVal, compareVal, 0, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::CAS, config>(
        channel, dst, fetchAddr, sizeof(T), nullptr, params);
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Commit(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    CommitImpl(channel, sqCtx, channelEntity->sqHead, channelEntity->cqHead);
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchCommit(UbcBatchHandle& batchHandle)
{
    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    HCOMM_DEBUG_TRAP_IF(
        preSqCnt == 0 || preSqCnt > batchHandle.buffer.bufferCapacity,
        "Hcomm BatchCommit failed with invalid WQEBB count\n");

    uint32_t sqDepth = batchHandle.sqContext.contextInfo.ubJfs.sqDepth;
    auto* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    uint32_t cqDepth = batchHandle.cqContext.contextInfo.ubJfc.cqDepth;
    uint32_t sqOutstanding = batchHandle.cursor.sqHead - batchHandle.cursor.sqTail;
    bool sqOverflow = sqOutstanding > sqDepth || preSqCnt > sqDepth - sqOutstanding;
    uint32_t cqAfterCommit = batchHandle.cursor.cqHead - batchHandle.cursor.cqTail;
    bool cqOverflow = cqAfterCommit > cqDepth;

    HCOMM_DEBUG_TRAP_IF(
        sqDepth == 0 || preSqCnt >= sqDepth,
        "Hcomm BatchCommit failed because batch must be smaller than SQ capacity\n");
    uint32_t sqHead = batchHandle.cursor.sqHead;
    uint32_t startSlot = sqHead & (sqDepth - 1);
    uint32_t tailWqebbCount = sqDepth - startSlot;
    uint32_t firstWqebbCount = preSqCnt < tailWqebbCount ? preSqCnt : tailWqebbCount;
    uint64_t sqBaseAddr = batchHandle.sqContext.contextInfo.ubJfs.sqVa;
    __gm__ uint8_t* firstWqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + static_cast<uint64_t>(startSlot) * HCOMM_URMA_WQEBB_SIZE);

    SyncAction<PIPE_S, PIPE_MTE3>(EVENT_ID7);
    // Copy the first segment from startSlot to the end of the circular SQ, or the whole batch if it does not wrap.
    asc_copy_ub2gm_align(
        firstWqeAddr, reinterpret_cast<__ubuf__ uint8_t*>(batchHandle.buffer.rawBuffer), 1,
        firstWqebbCount * HCOMM_URMA_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);

    uint32_t secondWqebbCount = preSqCnt - firstWqebbCount;
    if (secondWqebbCount != 0) {
        // The batch crosses the SQ boundary; copy the remaining WQEBBs to the beginning of the circular SQ.
        asc_copy_ub2gm_align(
            reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr),
            reinterpret_cast<__ubuf__ uint8_t*>(batchHandle.buffer.rawBuffer) + firstWqebbCount * HCOMM_URMA_WQEBB_SIZE,
            1, secondWqebbCount * HCOMM_URMA_WQEBB_SIZE, asc_store_l2_cache_mode::NOTALLOC_CLEAN, 0, 0);
    }

    uint32_t newSqHead = sqHead + preSqCnt;
    batchHandle.cursor.sqHead = newSqHead;
    channelEntity->sqHead = newSqHead;
    channelEntity->cqHead = batchHandle.cursor.cqHead;
    SyncAction<PIPE_MTE3, PIPE_S>(EVENT_ID7);
    st_dev(newSqHead, reinterpret_cast<__gm__ uint32_t*>(batchHandle.sqContext.contextInfo.ubJfs.dbVa), 0);

    batchHandle.cursor.preSqCnt = 0;
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::BatchCommit(UbcMultiBatchHandle& batchHandle)
{
    return BatchCommit(batchHandle.handle);
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t ret = PollCq(channel, channelEntity->cqHead);
    HCOMM_DEBUG_TRAP_IF(
        ret != HCOMM_SUCCESS, "Hcomm URMA Drain by channel failed channel=%lu pollRet=%u \n", channel, ret);
    return ret;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(UbcBatchHandle& batchHandle)
{
    (void)pipe;
    HCOMM_DEBUG_TRAP_IF(
        batchHandle.channelHandle == 0U || batchHandle.buffer.bufferCapacity == 0U || batchHandle.cursor.preSqCnt != 0U,
        "Hcomm Batch Drain failed with invalid batch handle\n");

    uint32_t ret = PollBatchCq(batchHandle, batchHandle.cursor.cqHead);
    HCOMM_DEBUG_TRAP_IF(ret != HCOMM_SUCCESS, "Hcomm URMA Drain by batch handle failed pollRet=%u \n", ret);
    return ret;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Drain(UbcMultiBatchHandle& batchHandle)
{
    return Drain<pipe>(batchHandle.handle);
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Lock(ChannelHandle channel)
{
    HCOMM_DEBUG_TRAP_IF(
        channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U, "Hcomm Lock failed with invalid channel\n");

    __gm__ ChannelEntity* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint32_t* lockAddr = HcommUrmaGetLockAddr(channelEntity);
    while (AtomicCas(lockAddr, HCOMM_LOCK_FREE, HCOMM_LOCK_HELD) != HCOMM_LOCK_FREE) {
        // Back off for 800 cycles before retrying to reduce atomic contention.
        Nop<800>();
    }
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UB_CTP>::Unlock(ChannelHandle channel)
{
    HCOMM_DEBUG_TRAP_IF(
        channel == 0U || (channel & (alignof(ChannelEntity) - 1U)) != 0U, "Hcomm Unlock failed with invalid channel\n");

    __gm__ ChannelEntity* channelEntity = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    __gm__ uint8_t* counterAddr = reinterpret_cast<__gm__ uint8_t*>(channelEntity) + offsetof(ChannelEntity, sqHead);
    // Flush the four contiguous channel counters: sqHead, sqTail, cqHead, and cqTail.
    CacheWriteThrough<uint8_t>(counterAddr, 4U * sizeof(uint32_t));

    __gm__ uint32_t* lockAddr = HcommUrmaGetLockAddr(channelEntity);
    uint32_t oldValue = AtomicCas(lockAddr, HCOMM_LOCK_HELD, HCOMM_LOCK_FREE);
    return oldValue == HCOMM_LOCK_HELD ? HCOMM_SUCCESS : HCOMM_FAILED;
}

} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H
#endif
