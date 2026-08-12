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

#include "../../common/hcomm_inner_def.h"
#include "../../common/hcomm_utils.h"

typedef AscendC::HcommUrmaSqeCtx HcommUrmaSqeCtx;
typedef AscendC::HcommUrmaSgeCtx HcommUrmaSgeCtx;
typedef AscendC::HcommUrmaNotifyCtx HcommUrmaNotifyCtx;
typedef AscendC::HcommUrmaJfcCqeCtx HcommUrmaJfcCqeCtx;

namespace AscendC {
namespace {
static_assert(sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx) == 64U, "Batch Write WQE must occupy one BB");
static_assert(sizeof(HcommUrmaJfcCqeCtx) == 64U, "Batch CQE size must be 64 bytes");

__aicore__ inline uint32_t HcommUrmaBatchAlignUp(uint32_t value)
{
    return (value + HCOMM_URMA_BATCH_ALIGN_SIZE - 1U) & ~(HCOMM_URMA_BATCH_ALIGN_SIZE - 1U);
}

__aicore__ inline bool HcommUrmaIsPowerOfTwo(uint32_t value) { return value != 0U && (value & (value - 1U)) == 0U; }

__aicore__ inline __ubuf__ HcommUrmaBatchState* HcommUrmaGetBatchState(BatchHandle handle)
{
    return reinterpret_cast<__ubuf__ HcommUrmaBatchState*>(handle.stateAddr);
}

__aicore__ inline bool HcommUrmaIsValidBatchState(const __ubuf__ HcommUrmaBatchState* state)
{
    return state != nullptr && state->magic == HCOMM_URMA_BATCH_MAGIC && state->version == HCOMM_URMA_BATCH_VERSION;
}

__aicore__ inline LocalTensor<uint32_t> HcommUrmaMakeUbTensor(
    __ubuf__ uint8_t* address, uint32_t len, uint32_t position)
{
    TBuffAddr tensorAddr{};
    tensorAddr.logicPos = static_cast<uint8_t>(position);
    tensorAddr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(address));
    tensorAddr.dataLen = len;
#if defined(UT_TEST)
    tensorAddr.absAddr = reinterpret_cast<uint8_t*>(address);
#endif
    LocalTensor<uint32_t> tensor;
    tensor.SetAddr(tensorAddr);
    return tensor;
}

__aicore__ inline bool HcommUrmaUsesBatchQueue(
    const __ubuf__ HcommUrmaBatchState* state, const SqContext& sqCtx, const CqContext& cqCtx)
{
    return sqCtx.type == SQ_CONTEXT_TYPE_UB_JFS && cqCtx.type == CQ_CONTEXT_TYPE_UB_JFC &&
           sqCtx.contextInfo.ubJfs.sqVa == state->sqVa && sqCtx.contextInfo.ubJfs.headAddr == state->sqHeadAddr &&
           sqCtx.contextInfo.ubJfs.tailAddr == state->sqTailAddr && sqCtx.contextInfo.ubJfs.dbVa == state->sqDbVa &&
           sqCtx.contextInfo.ubJfs.wqeSize == state->wqeSize && sqCtx.contextInfo.ubJfs.sqDepth == state->sqDepth &&
           cqCtx.contextInfo.ubJfc.scqVa == state->cqVa && cqCtx.contextInfo.ubJfc.headAddr == state->cqHeadAddr &&
           cqCtx.contextInfo.ubJfc.tailAddr == state->cqTailAddr && cqCtx.contextInfo.ubJfc.dbVa == state->cqDbVa &&
           cqCtx.contextInfo.ubJfc.cqeSize == state->cqeSize && cqCtx.contextInfo.ubJfc.cqDepth == state->cqDepth;
}

__aicore__ inline void HcommUrmaUpdateBatchTail(__ubuf__ HcommUrmaBatchState* state, uint32_t tail)
{
    st_dev(tail, reinterpret_cast<__gm__ uint32_t*>(state->sqTailAddr), 0);
    st_dev(tail, reinterpret_cast<__gm__ uint32_t*>(state->cqTailAddr), 0);
    st_dev(tail & 0xFFFFFFU, reinterpret_cast<__gm__ uint32_t*>(state->cqDbVa), 0);
}
} // namespace

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

__aicore__ inline void HcommUrmaDumpAmoCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, uint32_t atomicLen)
{
    if (sqeCtx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    auto opcode = sqeCtx->opcode;
    if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::FAA)) {
        __ubuf__ uint8_t* amoDataAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx);
        uint64_t addValue = (atomicLen == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amoDataAddr) :
                                                              *(__ubuf__ uint64_t*)amoDataAddr;
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: addValue:0x%llx \n", addValue);
    } else if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::CAS)) {
        __ubuf__ uint8_t* amoDataAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx);
        uint64_t swapValue = (atomicLen == sizeof(uint32_t)) ? static_cast<uint64_t>(*(__ubuf__ uint32_t*)amoDataAddr) :
                                                               *(__ubuf__ uint64_t*)amoDataAddr;
        uint64_t condValue = (atomicLen == sizeof(uint32_t)) ?
                                 static_cast<uint64_t>(*(__ubuf__ uint32_t*)(amoDataAddr + atomicLen)) :
                                 *(__ubuf__ uint64_t*)(amoDataAddr + atomicLen);
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: condValue:0x%llx, swapValue:0x%llx \n", condValue, swapValue);
    }
}

__aicore__ inline void HcommUrmaDumpSgeCtx(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __ubuf__ uint8_t* sgeAddr, uint32_t atomicLen)
{
    if (sqeCtx == nullptr || sgeAddr == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)sgeAddr;
    for (uint32_t i = 0; i < sqeCtx->sgeNum; i++) {
        auto sgeLen = sgeCtx->len;
        auto sgeRmtAddr = sgeCtx->va;
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: sge idx: %d, va: %p sge_len: %d \n", i, sgeRmtAddr, sgeLen);
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
    KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA WQE: notifyTokenId: %x notifyTokenValue: %x notifyAddrL: %x notifyAddrH: %x notifyDataL: %x "
        "notifyDataH: %x \n",
        notifyTokenId, notifyTokenValue, notifyAddrL, notifyAddrH, notifyDataL, notifyDataH);
}

__aicore__ inline void HcommUrmaDumpWqeCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, uint32_t atomicLen)
{
    if (sqeCtx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: nullptr pointer \n");
        return;
    }
    auto sqeBbIdx = sqeCtx->sqeBbIdx;
    auto flag = sqeCtx->flag;
    auto rsv0 = sqeCtx->rsv0;
    auto nf = sqeCtx->nf;
    auto tokenEn = sqeCtx->tokenEn;
    auto rmtJettyType = sqeCtx->rmtJettyType;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sqe_bb_idx: %x flag: %x rsv0: %x nf: %x token_en: %x rmt_jetty_type: %x \n",
        sqeBbIdx, flag, rsv0, nf, tokenEn, rmtJettyType);
    auto owner = sqeCtx->owner;
    auto targetHint = sqeCtx->targetHint;
    auto opcode = sqeCtx->opcode;
    auto rsv1 = sqeCtx->rsv1;
    auto inlineMsgLen = sqeCtx->inlineMsgLen;
    auto tpId = sqeCtx->tpId;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: owner: %x target_hint: %x opcode: %x rsv1: %x inline_msg_len: %x tp_id: %x \n",
        owner, targetHint, opcode, rsv1, inlineMsgLen, tpId);
    auto sgeNum = sqeCtx->sgeNum;
    auto rmtJettyOrSegId = sqeCtx->rmtJettyOrSegId;
    auto rsv2 = sqeCtx->rsv2;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: sge_num: %x rmt_jetty_or_seg_id: %x rsv2: %x \n", sgeNum, rmtJettyOrSegId, rsv2);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: rmt_eid: %x, %x \n", sqeCtx->rmtEidL, sqeCtx->rmtEidH);
    auto rmtTokenValue = sqeCtx->rmtTokenValue;
    auto udfType = sqeCtx->udfType;
    auto reduceDataType = sqeCtx->reduceDataType;
    auto reduceOpcode = sqeCtx->reduceOpcode;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA WQE: rmt_token_value: %x udf_type: %x reduce_data_type: %x reduce_opcode: %x \n",
        rmtTokenValue, udfType, reduceDataType, reduceOpcode);
    auto rmtAddrLOrTokenId = sqeCtx->rmtAddrLOrTokenId;
    auto rmtAddrHOrTokenValue = sqeCtx->rmtAddrHOrTokenValue;
    KERNEL_LOG(
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

__aicore__ inline void HcommUrmaDumpCqeCtx(__ubuf__ HcommUrmaJfcCqeCtx* cqeCtx)
{
    if (cqeCtx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: nullptr pointer \n");
        return;
    }
    uint32_t sR = cqeCtx->sR;
    uint32_t isJetty = cqeCtx->isJetty;
    uint32_t owner = cqeCtx->owner;
    uint32_t inlineEn = cqeCtx->inlineEn;
    uint32_t opcode = cqeCtx->opcode;
    uint32_t fd = cqeCtx->fd;
    uint32_t substatus = cqeCtx->substatus;
    uint32_t status = cqeCtx->status;
    uint32_t entryIdx = cqeCtx->entryIdx;
    uint32_t localNumL = cqeCtx->localNumL;
    uint32_t localNumH = cqeCtx->localNumH;
    uint32_t rmtIdx = cqeCtx->rmtIdx;
    uint32_t tpn = cqeCtx->tpn;
    KERNEL_LOG(
        KERNEL_INFO,
        "Hcomm URMA CQE: DW0 - sR: %d, isJetty: %d, owner: %d, inlineEn: %d, "
        "opcode: %d, fd: %d, substatus: %d, status: %d \n",
        sR, isJetty, owner, inlineEn, opcode, fd, substatus, status);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW1 - entryIdx: %d, localNumL: %d \n", entryIdx, localNumL);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW2 - localNumH: %d, rmtIdx: %d \n", localNumH, rmtIdx);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW3 - tpn: %d \n", tpn);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW4 - byteCnt: %d \n", cqeCtx->byteCnt);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW5-DW6 - userData: 0x%x%x \n", cqeCtx->userDataH, cqeCtx->userDataL);
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW7-DW10 - rmtEid: [0x%x, 0x%x, 0x%x, 0x%x] \n", cqeCtx->rmtEid[0],
        cqeCtx->rmtEid[1], cqeCtx->rmtEid[2], cqeCtx->rmtEid[3]);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW11-DW12 - data: 0x%x%x \n", cqeCtx->dataH, cqeCtx->dataL);
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA CQE: DW13-DW15 - inlineData: [0x%x, 0x%x, 0x%x] \n", cqeCtx->inlineData[0],
        cqeCtx->inlineData[1], cqeCtx->inlineData[2]);
}

__aicore__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::HcommImpl() {}

__aicore__ inline HcommImpl<COMM_PROTOCOL_UBC_CTP>::~HcommImpl() {}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    if (len < HCOMM_URMA_TMP_BUF_SIZE) {
        return HCOMM_FAILED;
    }

    __ubuf__ uint8_t* alignedBuff = (__ubuf__ uint8_t*)AlignAddrTo32Bytes(buff);
    TBuffAddr addr;
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(alignedBuff));
    addr.dataLen = len;
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t*>(alignedBuff);
#endif
    wqeItem_.SetAddr(addr);
    cqeItem_ = wqeItem_[HCOMM_URMA_WQE_U32_NUM];
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    if (len < HCOMM_URMA_TMP_BUF_SIZE || len > buff.GetSize()) {
        return HCOMM_FAILED;
    }

    wqeItem_ = buff.template ReinterpretCast<uint32_t>();
    cqeItem_ = wqeItem_[HCOMM_URMA_WQE_U32_NUM];
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline BatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    MultiChannelHandle multiChannel, const LocalTensor<T>& buff, uint32_t buffLen)
{
    BatchHandle invalidHandle{0U};
    uint64_t tensorLen = static_cast<uint64_t>(buff.GetSize()) * sizeof(T);
    uint32_t position = static_cast<uint32_t>(buff.GetPosition());
    if (multiChannel == 0U || buffLen == 0U || buffLen > tensorLen ||
        GetPhyType(static_cast<TPosition>(position)) != Hardware::UB) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with invalid parameter\n");
        return invalidHandle;
    }

    __ubuf__ uint8_t* rawBuffer = reinterpret_cast<__ubuf__ uint8_t*>(buff.GetPhyAddr());
    __ubuf__ uint8_t* alignedBuffer = AlignAddrTo32Bytes(rawBuffer);
    uint32_t alignOffset = static_cast<uint32_t>(alignedBuffer - rawBuffer);
    if (buffLen <= alignOffset) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with insufficient aligned buffer\n");
        return invalidHandle;
    }

    auto* anchorChannel = reinterpret_cast<__gm__ ChannelEntity*>(multiChannel);
    if (anchorChannel->engine != COMM_ENGINE_AIV || anchorChannel->protocol != COMM_PROTOCOL_UBC_CTP ||
        anchorChannel->sqNum == 0U || anchorChannel->cqNum == 0U || anchorChannel->sqContextAddr == nullptr ||
        anchorChannel->cqContextAddr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with invalid anchor channel\n");
        return invalidHandle;
    }

    auto sqCtx = anchorChannel->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqCtx = anchorChannel->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    constexpr uint32_t batchWqeSize = sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx);
    if (sqCtx.type != SQ_CONTEXT_TYPE_UB_JFS || cqCtx.type != CQ_CONTEXT_TYPE_UB_JFC ||
        sqCtx.contextInfo.ubJfs.sqVa == 0U || sqCtx.contextInfo.ubJfs.headAddr == 0U ||
        sqCtx.contextInfo.ubJfs.tailAddr == 0U || sqCtx.contextInfo.ubJfs.dbVa == 0U ||
        cqCtx.contextInfo.ubJfc.scqVa == 0U || cqCtx.contextInfo.ubJfc.headAddr == 0U ||
        cqCtx.contextInfo.ubJfc.tailAddr == 0U || cqCtx.contextInfo.ubJfc.dbVa == 0U ||
        sqCtx.contextInfo.ubJfs.wqeSize != batchWqeSize || !HcommUrmaIsPowerOfTwo(sqCtx.contextInfo.ubJfs.sqDepth) ||
        cqCtx.contextInfo.ubJfc.cqeSize != sizeof(HcommUrmaJfcCqeCtx) ||
        !HcommUrmaIsPowerOfTwo(cqCtx.contextInfo.ubJfc.cqDepth)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with invalid shared queue\n");
        return invalidHandle;
    }

    uint32_t sqHead =
        static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.headAddr), 0));
    uint32_t sqTail =
        static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.tailAddr), 0));
    uint32_t cqHead =
        static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(cqCtx.contextInfo.ubJfc.headAddr), 0));
    uint32_t cqTail =
        static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(cqCtx.contextInfo.ubJfc.tailAddr), 0));
    uint32_t usedSq = sqHead - sqTail;
    uint32_t usedCq = cqHead - cqTail;
    if (sqHead != cqHead || sqTail != cqTail || usedSq > sqCtx.contextInfo.ubJfs.sqDepth ||
        usedCq > cqCtx.contextInfo.ubJfc.cqDepth) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with inconsistent shared queue index\n");
        return invalidHandle;
    }

    uint32_t availableLen = buffLen - alignOffset;
    uint32_t cqeOffset = HcommUrmaBatchAlignUp(sizeof(HcommUrmaBatchState));
    uint32_t wqeOffset = HcommUrmaBatchAlignUp(cqeOffset + cqCtx.contextInfo.ubJfc.cqeSize);
    if (availableLen < wqeOffset + batchWqeSize) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with insufficient batch buffer\n");
        return invalidHandle;
    }

    auto* state = reinterpret_cast<__ubuf__ HcommUrmaBatchState*>(alignedBuffer);
    state->magic = HCOMM_URMA_BATCH_MAGIC;
    state->version = HCOMM_URMA_BATCH_VERSION;
    state->multiChannel = multiChannel;
    state->sqVa = sqCtx.contextInfo.ubJfs.sqVa;
    state->sqHeadAddr = sqCtx.contextInfo.ubJfs.headAddr;
    state->sqTailAddr = sqCtx.contextInfo.ubJfs.tailAddr;
    state->sqDbVa = sqCtx.contextInfo.ubJfs.dbVa;
    state->cqVa = cqCtx.contextInfo.ubJfc.scqVa;
    state->cqHeadAddr = cqCtx.contextInfo.ubJfc.headAddr;
    state->cqTailAddr = cqCtx.contextInfo.ubJfc.tailAddr;
    state->cqDbVa = cqCtx.contextInfo.ubJfc.dbVa;
    state->sqHead = sqHead;
    state->cqHead = cqHead;
    state->pendingBb = 0U;
    state->wqeOffset = wqeOffset;
    state->cqeOffset = cqeOffset;
    state->wqeSize = batchWqeSize;
    state->sqDepth = sqCtx.contextInfo.ubJfs.sqDepth;
    state->cqeSize = cqCtx.contextInfo.ubJfc.cqeSize;
    state->cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    state->ubPosition = position;
    state->capacityBb = (availableLen - wqeOffset) / batchWqeSize;
    if (state->capacityBb > state->sqDepth) {
        state->capacityBb = state->sqDepth;
    }
    if (state->capacityBb > state->cqDepth) {
        state->capacityBb = state->cqDepth;
    }
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA MakeBatchHandle success capacity=%u sqHead=%u cqHead=%u\n", state->capacityBb,
        state->sqHead, state->cqHead);
    return BatchHandle{reinterpret_cast<uint64_t>(state)};
}

__aicore__ inline BatchHandleRef HcommImpl<COMM_PROTOCOL_UBC_CTP>::GetHandleRef(
    BatchHandle handle, uint32_t remoteIndex, GM_ADDR remoteAddr)
{
    BatchHandleRef invalidRef{0U, 0U, 0U, 0U};
    __ubuf__ HcommUrmaBatchState* state = HcommUrmaGetBatchState(handle);
    if (!HcommUrmaIsValidBatchState(state)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA GetHandleRef failed with invalid batch handle\n");
        return invalidRef;
    }

    auto* channel = reinterpret_cast<__gm__ ChannelEntity*>(
        state->multiChannel + static_cast<uint64_t>(remoteIndex) * sizeof(ChannelEntity));
    if (channel->engine != COMM_ENGINE_AIV || channel->protocol != COMM_PROTOCOL_UBC_CTP || channel->sqNum == 0U ||
        channel->cqNum == 0U || channel->sqContextAddr == nullptr || channel->cqContextAddr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA GetHandleRef failed with invalid channel index=%u\n", remoteIndex);
        return invalidRef;
    }
    auto sqCtx = channel->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqCtx = channel->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    if (!HcommUrmaUsesBatchQueue(state, sqCtx, cqCtx)) {
        KERNEL_LOG(
            KERNEL_ERROR, "Hcomm URMA GetHandleRef channel does not use the batch queue index=%u\n", remoteIndex);
        return invalidRef;
    }

    int32_t remoteBufferIndex = HcommFindBufferIdx(channel->remoteBufferAddr, channel->remoteBufferNum, remoteAddr, 1U);
    if (remoteBufferIndex == HCOMM_FAILED) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA GetHandleRef failed to resolve remote memory index=%u\n", remoteIndex);
        return invalidRef;
    }
    return BatchHandleRef{
        handle.stateAddr, reinterpret_cast<ChannelHandle>(channel), static_cast<uint32_t>(remoteBufferIndex), 0U};
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    BatchHandleRef handleRef, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    static_assert(config.inlineEn == 0U, "Batch WriteNbi does not support inline WQEs");
    static_assert(config.cqe == 1U, "Batch WriteNbi requires a CQE for every WQE");
    __ubuf__ HcommUrmaBatchState* state = HcommUrmaGetBatchState(BatchHandle{handleRef.stateAddr});
    if (!HcommUrmaIsValidBatchState(state) || handleRef.channel == 0U || state->pendingBb >= state->capacityBb ||
        len > UINT32_MAX) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch WriteNbi failed with invalid parameter or full buffer\n");
        return HCOMM_FAILED;
    }

    auto* channel = reinterpret_cast<__gm__ ChannelEntity*>(handleRef.channel);
    if (channel->engine != COMM_ENGINE_AIV || channel->protocol != COMM_PROTOCOL_UBC_CTP ||
        handleRef.remoteBufferIndex >= channel->remoteBufferNum || channel->remoteBufferAddr == nullptr ||
        channel->sqContextAddr == nullptr || channel->cqContextAddr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch WriteNbi failed with invalid handle reference\n");
        return HCOMM_FAILED;
    }
    auto sqCtx = channel->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    auto cqCtx = channel->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    if (!HcommUrmaUsesBatchQueue(state, sqCtx, cqCtx)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch WriteNbi channel does not use the batch queue\n");
        return HCOMM_FAILED;
    }

    auto remoteMemInfo = channel->remoteBufferAddr[handleRef.remoteBufferIndex];
    uint64_t targetAddr = reinterpret_cast<uint64_t>(dst);
    uint64_t remoteBase = remoteMemInfo.bufferInfo.rma.addr;
    uint64_t remoteSize = remoteMemInfo.bufferInfo.rma.size;
    if (remoteMemInfo.type != REGED_BUFFER_RMA ||
        remoteMemInfo.bufferInfo.rma.protectionInfo.type != PROTECTION_TYPE_UB || targetAddr < remoteBase ||
        targetAddr - remoteBase > remoteSize || len > remoteSize - (targetAddr - remoteBase)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch WriteNbi failed with invalid remote buffer\n");
        return HCOMM_FAILED;
    }

    __ubuf__ uint8_t* wqeAddress =
        reinterpret_cast<__ubuf__ uint8_t*>(handleRef.stateAddr) + state->wqeOffset + state->pendingBb * state->wqeSize;
    auto* sqeCtx = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(wqeAddress);
    auto* wqeWords = reinterpret_cast<__ubuf__ uint32_t*>(wqeAddress);
    for (uint32_t i = 0U; i < state->wqeSize / sizeof(uint32_t); ++i) {
        wqeWords[i] = 0U;
    }

    uint32_t wqeHead = state->sqHead + state->pendingBb;
    HcommUrmaFillSqeCtx<HcommUrmaOpCode::WRITE, config, uint64_t>(
        sqeCtx, reinterpret_cast<__gm__ uint8_t*>(dst), sqCtx, remoteMemInfo, wqeHead);
    auto* sgeCtx = reinterpret_cast<__ubuf__ HcommUrmaSgeCtx*>(wqeAddress + sizeof(HcommUrmaSqeCtx));
    HcommUrmaFillSgeCtx<HcommUrmaOpCode::WRITE>(
        sgeCtx, len, reinterpret_cast<__gm__ uint8_t*>(src), UdmaParams<uint64_t>{});
    state->pendingBb++;
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchCommit(BatchHandle handle)
{
    (void)pipe;
    __ubuf__ HcommUrmaBatchState* state = HcommUrmaGetBatchState(handle);
    if (!HcommUrmaIsValidBatchState(state)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA BatchCommit failed with invalid batch handle\n");
        return HCOMM_FAILED;
    }
    if (state->pendingBb == 0U) {
        return HCOMM_SUCCESS;
    }

    auto* sqHeadAddr = reinterpret_cast<__gm__ uint32_t*>(state->sqHeadAddr);
    auto* cqHeadAddr = reinterpret_cast<__gm__ uint32_t*>(state->cqHeadAddr);
    uint32_t sharedSqHead = static_cast<uint32_t>(ld_dev(sqHeadAddr, 0));
    uint32_t sharedCqHead = static_cast<uint32_t>(ld_dev(cqHeadAddr, 0));
    uint32_t sqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->sqTailAddr), 0));
    uint32_t cqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->cqTailAddr), 0));
    uint32_t usedSq = sharedSqHead - sqTail;
    uint32_t usedCq = sharedCqHead - cqTail;
    if (sharedSqHead != state->sqHead || sharedCqHead != state->cqHead || sharedSqHead != sharedCqHead ||
        sqTail != cqTail || usedSq > state->sqDepth || usedCq > state->cqDepth ||
        state->pendingBb > state->sqDepth - usedSq || state->pendingBb > state->cqDepth - usedCq) {
        KERNEL_LOG(
            KERNEL_ERROR,
            "Hcomm URMA BatchCommit failed sharedSqHead=%u sqHead=%u sharedCqHead=%u cqHead=%u pending=%u\n",
            sharedSqHead, state->sqHead, sharedCqHead, state->cqHead, state->pendingBb);
        return HCOMM_FAILED;
    }

    uint32_t sqOffset = state->sqHead % state->sqDepth;
    uint32_t firstBb = state->pendingBb;
    if (firstBb > state->sqDepth - sqOffset) {
        firstBb = state->sqDepth - sqOffset;
    }
    uint32_t wordsPerWqe = state->wqeSize / sizeof(uint32_t);
    __ubuf__ uint8_t* wqeAddress = reinterpret_cast<__ubuf__ uint8_t*>(handle.stateAddr) + state->wqeOffset;
    LocalTensor<uint32_t> wqeTensor =
        HcommUrmaMakeUbTensor(wqeAddress, state->capacityBb * state->wqeSize, state->ubPosition);
    GlobalTensor<uint32_t> sqTensor;
    sqTensor.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(state->sqVa));

    SyncAction<HardEvent::S_MTE3>();
    DataCopy(sqTensor[sqOffset * wordsPerWqe], wqeTensor, firstBb * wordsPerWqe);
    uint32_t secondBb = state->pendingBb - firstBb;
    if (secondBb != 0U) {
        DataCopy(sqTensor, wqeTensor[firstBb * wordsPerWqe], secondBb * wordsPerWqe);
    }
    SyncAction<HardEvent::MTE3_S>();

    state->sqHead += state->pendingBb;
    state->cqHead += state->pendingBb;
    st_dev(state->sqHead, sqHeadAddr, 0);
    st_dev(state->cqHead, cqHeadAddr, 0);
    st_dev(state->sqHead, reinterpret_cast<__gm__ uint32_t*>(state->sqDbVa), 0);
    state->pendingBb = 0U;
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA BatchCommit success sqHead=%u cqHead=%u\n", state->sqHead, state->cqHead);
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(BatchHandle handle)
{
    (void)pipe;
    __ubuf__ HcommUrmaBatchState* state = HcommUrmaGetBatchState(handle);
    if (!HcommUrmaIsValidBatchState(state)) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch Drain failed with invalid batch handle\n");
        return HCOMM_FAILED;
    }

    uint32_t sharedSqHead = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->sqHeadAddr), 0));
    uint32_t sharedCqHead = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->cqHeadAddr), 0));
    uint32_t sqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->sqTailAddr), 0));
    uint32_t cqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(state->cqTailAddr), 0));
    uint32_t usedSq = sharedSqHead - sqTail;
    uint32_t usedCq = sharedCqHead - cqTail;
    if (sharedSqHead != state->sqHead || sharedCqHead != state->cqHead || sharedSqHead != sharedCqHead ||
        sqTail != cqTail || usedSq > state->sqDepth || usedCq > state->cqDepth) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch Drain failed with inconsistent shared queue index\n");
        return HCOMM_FAILED;
    }
    if (cqTail == state->cqHead) {
        return HCOMM_SUCCESS;
    }

    uint32_t curTail = cqTail;
    __ubuf__ uint8_t* cqeAddress = reinterpret_cast<__ubuf__ uint8_t*>(handle.stateAddr) + state->cqeOffset;
    LocalTensor<uint32_t> cqeTensor = HcommUrmaMakeUbTensor(cqeAddress, state->cqeSize, state->ubPosition);
    auto* cqe = reinterpret_cast<__ubuf__ HcommUrmaJfcCqeCtx*>(cqeAddress);
    GlobalTensor<uint32_t> cqTensor;
    cqTensor.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(state->cqVa));
#if defined(UT_TEST)
    curTail = state->cqHead;
#else
    while (curTail != state->cqHead) {
        uint32_t cqOffset = curTail % state->cqDepth;
        SyncAction<HardEvent::S_MTE2>();
        DataCopy(cqeTensor, cqTensor[cqOffset * state->cqeSize / sizeof(uint32_t)], state->cqeSize / sizeof(uint32_t));
        SyncAction<HardEvent::MTE2_S>();
        bool validOwner = (curTail / state->cqDepth) & 1U;
        uint32_t times = 0U;
        while ((validOwner ^ cqe->owner) == 0U && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            SyncAction<HardEvent::S_MTE2>();
            DataCopy(
                cqeTensor, cqTensor[cqOffset * state->cqeSize / sizeof(uint32_t)], state->cqeSize / sizeof(uint32_t));
            SyncAction<HardEvent::MTE2_S>();
            times++;
        }
        if ((validOwner ^ cqe->owner) == 0U) {
            if (curTail != cqTail) {
                HcommUrmaUpdateBatchTail(state, curTail);
            }
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch Drain timeout curTail=%u cqHead=%u\n", curTail, state->cqHead);
            return 0xFFU;
        }
        uint8_t status = cqe->status & 0xFFU;
        uint8_t subStatus = cqe->substatus & 0xFFU;
        constexpr uint8_t statusShift = 8U;
        if (status != 0U || subStatus != 0U) {
            curTail++;
            HcommUrmaUpdateBatchTail(state, curTail);
            KERNEL_LOG(
                KERNEL_ERROR, "Hcomm URMA batch Drain failed status=%u subStatus=%u curTail=%u\n", status, subStatus,
                curTail);
            return (status << statusShift) | subStatus;
        }
        curTail++;
    }
#endif

    HcommUrmaUpdateBatchTail(state, curTail);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA batch Drain success tail=%u\n", curTail);
    return HCOMM_SUCCESS;
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCqWhenSqOverflow(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t sqHead, uint32_t cqeCnt)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t cqTail = channelEntity->cqTail;
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    uint32_t sqDepth = sqCtx.contextInfo.ubJfs.sqDepth;
    if ((cqeCnt + POLL_CQ_THRESHOLD) % cqDepth == cqTail % cqDepth ||
        (sqHead % sqDepth) + POLL_CQ_THRESHOLD >= sqDepth) {
        uint32_t idx = (cqTail + NUM_CQE_PER_POLL_CQ) > cqeCnt ? cqeCnt : cqTail + NUM_CQE_PER_POLL_CQ;
        KERNEL_LOG(
            KERNEL_INFO, "Hcomm URMA queue overflow sqHead=%u cqeCnt=%u cqTail=%u idx=%u sqDepth=%u cqDepth=%u \n",
            sqHead, cqeCnt, cqTail, idx, sqDepth, cqDepth);
        (void)PollCq(channel, idx);
    }
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, HcommUrmaOpCode opCode, auto const& config, typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostSend(
    ChannelHandle channel, GM_ADDR remoteAddr, GM_ADDR localAddr, uint64_t len, GM_ADDR notifyAddr,
    const UdmaParams<T>& params)
{
    (void)commitPipe;
    (void)reqPipe;

    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    int32_t remoteIdx =
        HcommFindBufferIdx(channelEntity->remoteBufferAddr, channelEntity->remoteBufferNum, remoteAddr, len);
    if (remoteIdx == HCOMM_FAILED) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA PostSend failed with invalid remote buffer \n");
        return HCOMM_FAILED;
    }

    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t curHead = channelEntity->sqHead;
    uint32_t cqeCnt = channelEntity->cqHead;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA PostSend resolved remoteIdx=%d curHead=%u sqDepth=%u \n", remoteIdx, curHead,
        sqCtx.contextInfo.ubJfs.sqDepth);

    // poll CQ if CQ or SQ is nearly full
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    PollCqWhenSqOverflow(channel, sqCtx, cqCtx, curHead, cqeCnt);

    // write SQE
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeItem_.GetPhyAddr();
    auto remoteMemInfo = channelEntity->remoteBufferAddr[remoteIdx];
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
    __gm__ uint8_t* sqeAddr = (__gm__ uint8_t*)(sqBaseAddr + wqeSize * (curHead % baseBlockCount));
    AscendC::GlobalTensor<uint32_t> sqeGlobal;
    sqeGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqeAddr);

    constexpr uint32_t wqeBbCnt = (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY || opCode == HcommUrmaOpCode::FAA ||
                                   opCode == HcommUrmaOpCode::CAS) ?
                                      2U :
                                      1U;
    SyncAction<HardEvent::S_MTE3>();
    if constexpr (wqeBbCnt == 2) {
        if (unlikely((curHead % baseBlockCount) == baseBlockCount - 1)) {
            AscendC::GlobalTensor<uint32_t> sqeBaseGlobal;
            sqeBaseGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqBaseAddr);

            uint32_t wqeSizePerU32 = wqeSize / sizeof(uint32_t);
            DataCopy(sqeGlobal, wqeItem_, wqeSizePerU32);
            DataCopy(sqeBaseGlobal, wqeItem_[wqeSizePerU32], wqeSizePerU32);
        } else {
            DataCopy(sqeGlobal, wqeItem_, wqeSize * wqeBbCnt / sizeof(uint32_t));
        }
    } else {
        DataCopy(sqeGlobal, wqeItem_, wqeSize * wqeBbCnt / sizeof(uint32_t));
    }
    SyncAction<HardEvent::MTE3_S>();

    if constexpr (config.cqe != 0) {
        cqeCnt++;
        channelEntity->cqHead = cqeCnt;
    }
    curHead += wqeBbCnt;
    channelEntity->sqHead = curHead;

    if constexpr (commit) {
        st_dev(curHead, reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.dbVa), 0);
    }
    HcommUrmaDumpWqeCtx(sqeCtx, sizeof(T));
    return HCOMM_SUCCESS;
}

__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCq(ChannelHandle channel, uint32_t expectIdx)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t curTail = channelEntity->cqTail;

    uint64_t cqBaseAddr = cqCtx.contextInfo.ubJfc.scqVa;
    uint32_t cqeSize = cqCtx.contextInfo.ubJfc.cqeSize;
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    __ubuf__ HcommUrmaJfcCqeCtx* cqeUb = (__ubuf__ HcommUrmaJfcCqeCtx*)cqeItem_.GetPhyAddr();
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm URMA PollCq enter expectIdx=%u curTail=%u cqDepth=%u \n", expectIdx, curTail, cqDepth);
#if defined(UT_TEST)
    curTail = expectIdx;
#else
    while (curTail != expectIdx) {
        __gm__ uint8_t* cqeAddr = (__gm__ uint8_t*)(cqBaseAddr + cqeSize * (curTail & (cqDepth - 1)));
        AscendC::GlobalTensor<uint32_t> cqeGlobal;
        cqeGlobal.SetGlobalBuffer((__gm__ uint32_t*)cqeAddr);
        SyncAction<HardEvent::S_MTE2>();
        DataCopy(cqeItem_, cqeGlobal, cqeSize / sizeof(uint32_t));
        SyncAction<HardEvent::MTE2_S>();
        bool validOwner = (curTail / cqDepth) & 1;
        uint32_t times = 0;
        while ((validOwner ^ cqeUb->owner) == 0 && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            SyncAction<HardEvent::S_MTE2>();
            DataCopy(cqeItem_, cqeGlobal, cqeSize / sizeof(uint32_t));
            SyncAction<HardEvent::MTE2_S>();
            times++;
        }
        if (times >= HCOMM_URMA_MAX_RETRY_TIMES) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Poll CQ timeout curTail=%u expectIdx=%u \n", curTail, expectIdx);
            HcommUrmaDumpCqeCtx(cqeUb);
            return 0xFFU;
        }
        // check CQE status
        uint8_t status = cqeUb->status & 0xFFU;
        uint8_t subStatus = cqeUb->substatus & 0xFFU;
        constexpr uint8_t statusShift = 8;
        if (status != 0 || subStatus != 0) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA CQE failed status=%u subStatus=%u \n", status, subStatus);
            HcommUrmaDumpCqeCtx(cqeUb);
            return (status << statusShift) | subStatus;
        }
        curTail++;
    }
#endif

    // update CQ tail
    channelEntity->cqTail = curTail;

    // ring CQ doorbell
    st_dev(curTail & 0xFFFFFFU, (__gm__ uint32_t*)cqCtx.contextInfo.ubJfc.dbVa, 0);

    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(channel, dst, src, len);
}

template <typename T, HcommUrmaReduceOp reduceOp, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteReduceNbi(
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
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::READ, config>(channel, src, dst, len);
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    static_assert(config.inlineEn == 1, "WriteValueNbi requires inline data in WQE");
    UdmaParams<T> params{value, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(
        channel, dst, nullptr, sizeof(T), nullptr, params);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    UdmaParams<uint64_t> params{notifyVal, 0, 0, 0};
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
        channel, dst, src, len, notifyAddr, params);
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicFAA(
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
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::AtomicCAS(
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
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Commit(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t curHead = channelEntity->sqHead;

    st_dev(curHead, (__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.dbVa, 0);

    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    uint32_t ret = PollCq(channel, channelEntity->cqHead);
    if (ret != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Drain by channel failed channel=%lu pollRet=%u \n", channel, ret);
        return ret;
    }
    return HCOMM_SUCCESS;
}

} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H
#endif
