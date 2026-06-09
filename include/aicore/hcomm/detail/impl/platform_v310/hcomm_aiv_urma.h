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

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv_urma.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use public interface headers.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H__
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
__aicore__ inline void HcommUrmaFillNotifyCtx(__ubuf__ HcommUrmaNotifyCtx* notifyCtx,
    const RegedBufferEntity& remoteMemInfo, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    uint64_t notifyAddrValue = reinterpret_cast<uint64_t>(notifyAddr);
    notifyCtx->notifyTokenId = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId & 0xFFFFFU;
    notifyCtx->notifyTokenValue = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    notifyCtx->notifyAddrL = notifyAddrValue & 0xFFFFFFFFU;
    notifyCtx->notifyAddrH = (notifyAddrValue >> 32) & 0xFFFFFFFFU;
    notifyCtx->notifyDataL = notifyVal & 0xFFFFFFFFU;
    notifyCtx->notifyDataH = (notifyVal >> 32) & 0xFFFFFFFFU;
}

template <HcommUrmaOpCode opCode, auto const &config>
__aicore__ inline void HcommUrmaFillSqeCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr,
    const SqContext& sqCtx, const RegedBufferEntity& remoteMemInfo, uint32_t curHead, GM_ADDR notifyAddr = nullptr,
    uint64_t notifyVal = 0)
{
    sqeCtx->opcode = static_cast<uint32_t>(opCode);
    sqeCtx->flag = (config.odr & 0x7U) | ((config.fence & 0x1U) << 3U) | ((config.se & 0x1U) << 4U) |
        ((config.cqe & 0x1U) << 5U) | ((config.inlineEn & 0x1U) << 6U) | (0 & 0x1U << 7U);
    sqeCtx->nf = 0;
    sqeCtx->tokenEn = 1;
    sqeCtx->rmtJettyType = 1;
    uint32_t baseBlockCount = sqCtx.contextInfo.ubJfs.sqDepth;
    sqeCtx->owner = (curHead & baseBlockCount) == 0 ? 1 : 0;
    sqeCtx->targetHint = 0;
    sqeCtx->inlineMsgLen = 0;
    sqeCtx->tpId = sqCtx.contextInfo.ubJfs.tpID;
    sqeCtx->sgeNum = 1;
    sqeCtx->rmtJettyOrSegId = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    sqeCtx->rmtTokenValue = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeCtx->rmtAddrLOrTokenId = remoteAddrValue & 0xFFFFFFFFU;
    sqeCtx->rmtAddrHOrTokenValue = (remoteAddrValue >> 32) & 0xFFFFFFFFU;
    auto rmtEid = reinterpret_cast<const uint64_t*>(sqCtx.contextInfo.ubJfs.remoteEID);
    sqeCtx->rmtEidL = rmtEid[0];
    sqeCtx->rmtEidH = rmtEid[1];
    if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx =
            (__ubuf__ HcommUrmaNotifyCtx*)((__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx));
        HcommUrmaFillNotifyCtx(notifyCtx, remoteMemInfo, notifyAddr, notifyVal);
    }
}

__aicore__ inline void HcommUrmaFillSgeCtx(
    __ubuf__ HcommUrmaSgeCtx* sgeCtx, uint64_t messageLen, __gm__ uint8_t* localAddr)
{
    sgeCtx->len = static_cast<uint32_t>(messageLen);
    sgeCtx->va = reinterpret_cast<uint64_t>(localAddr);
}

__aicore__ inline void HcommUrmaDumpSgeCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx, __ubuf__ uint8_t* sgeAddr)
{
    if (sqeCtx == nullptr || sgeAddr == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: nullptr pointer");
        return;
    }
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)sgeAddr;
    for (uint32_t i = 0; i < sqeCtx->sgeNum; i++) {
        auto sgeLen = sgeCtx->len;
        auto sgeRmtAddr = sgeCtx->va;
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SGE: sge idx: %d, va: %p sge_len: %d", i, sgeRmtAddr, sgeLen);
        sgeCtx++;
    }
}

__aicore__ inline void HcommUrmaDumpNotifyCtx(__ubuf__ HcommUrmaNotifyCtx* notifyCtx)
{
    auto notifyTokenId = notifyCtx->notifyTokenId;
    auto notifyTokenValue = notifyCtx->notifyTokenValue;
    auto notifyAddrL = notifyCtx->notifyAddrL;
    auto notifyAddrH = notifyCtx->notifyAddrH;
    auto notifyDataL = notifyCtx->notifyDataL;
    auto notifyDataH = notifyCtx->notifyDataH;
    KERNEL_LOG(KERNEL_INFO,
        "Hcomm URMA WQE: notifyTokenId: %x notifyTokenValue: %x notifyAddrL: %x notifyAddrH: %x notifyDataL: %x "
        "notifyDataH: %x",
        notifyTokenId, notifyTokenValue, notifyAddrL, notifyAddrH, notifyDataL, notifyDataH);
}

__aicore__ inline void HcommUrmaDumpWqeCtx(__ubuf__ HcommUrmaSqeCtx* sqeCtx)
{
    if (sqeCtx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: nullptr pointer");
        return;
    }
    auto sqeBbIdx = sqeCtx->sqeBbIdx;
    auto flag = sqeCtx->flag;
    auto rsv0 = sqeCtx->rsv0;
    auto nf = sqeCtx->nf;
    auto tokenEn = sqeCtx->tokenEn;
    auto rmtJettyType = sqeCtx->rmtJettyType;
    KERNEL_LOG(KERNEL_INFO,
        "Hcomm URMA WQE: sqe_bb_idx: %x flag: %x rsv0: %x nf: %x token_en: %x rmt_jetty_type: %x",
        sqeBbIdx, flag, rsv0, nf, tokenEn, rmtJettyType);
    auto owner = sqeCtx->owner;
    auto targetHint = sqeCtx->targetHint;
    auto opcode = sqeCtx->opcode;
    auto rsv1 = sqeCtx->rsv1;
    auto inlineMsgLen = sqeCtx->inlineMsgLen;
    auto tpId = sqeCtx->tpId;
    KERNEL_LOG(KERNEL_INFO,
        "Hcomm URMA WQE: owner: %x target_hint: %x opcode: %x rsv1: %x inline_msg_len: %x tp_id: %x",
        owner, targetHint, opcode, rsv1, inlineMsgLen, tpId);
    auto sgeNum = sqeCtx->sgeNum;
    auto rmtJettyOrSegId = sqeCtx->rmtJettyOrSegId;
    auto rsv2 = sqeCtx->rsv2;
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: sge_num: %x rmt_jetty_or_seg_id: %x rsv2: %x",
        sgeNum, rmtJettyOrSegId, rsv2);
    auto rmtEidL = sqeCtx->rmtEidL;
    auto rmtEidH = sqeCtx->rmtEidH;
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: rmt_eid: %x, %x", rmtEidL, rmtEidH);
    auto rmtTokenValue = sqeCtx->rmtTokenValue;
    auto udfType = sqeCtx->udfType;
    auto reduceDataType = sqeCtx->reduceDataType;
    auto reduceOpcode = sqeCtx->reduceOpcode;
    KERNEL_LOG(KERNEL_INFO,
        "Hcomm URMA WQE: rmt_token_value: %x udf_type: %x reduce_data_type: %x reduce_opcode: %x",
        rmtTokenValue, udfType, reduceDataType, reduceOpcode);
    auto rmtAddrLOrTokenId = sqeCtx->rmtAddrLOrTokenId;
    auto rmtAddrHOrTokenValue = sqeCtx->rmtAddrHOrTokenValue;
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA WQE: rmt_addr_l_or_token_id: %x rmt_addr_h_or_token_value: %x",
        rmtAddrLOrTokenId, rmtAddrHOrTokenValue);
    __ubuf__ uint8_t* sgeAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx);
    if (opcode == static_cast<uint32_t>(HcommUrmaOpCode::WRITE_WITH_NOTIFY)) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx = (__ubuf__ HcommUrmaNotifyCtx*)sgeAddr;
        HcommUrmaDumpNotifyCtx(notifyCtx);
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    HcommUrmaDumpSgeCtx(sqeCtx, sgeAddr);
}

__aicore__ inline void HcommUrmaDumpCqeCtx(__ubuf__ HcommUrmaJfcCqeCtx* cqeCtx)
{
    if (cqeCtx == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: nullptr pointer");
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
    KERNEL_LOG(KERNEL_INFO,
        "Hcomm URMA CQE: DW0 - sR: %d, isJetty: %d, owner: %d, inlineEn: %d, "
        "opcode: %d, fd: %d, substatus: %d, status: %d",
        sR, isJetty, owner, inlineEn, opcode, fd, substatus, status);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW1 - entryIdx: %d, localNumL: %d", entryIdx, localNumL);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW2 - localNumH: %d, rmtIdx: %d", localNumH, rmtIdx);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW3 - tpn: %d", tpn);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW4 - byteCnt: %d", cqeCtx->byteCnt);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW5-DW6 - userData: 0x%08x%08x", cqeCtx->userDataH, cqeCtx->userDataL);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW7-DW10 - rmtEid: [0x%08x, 0x%08x, 0x%08x, 0x%08x]",
        cqeCtx->rmtEid[0], cqeCtx->rmtEid[1], cqeCtx->rmtEid[2], cqeCtx->rmtEid[3]);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW11-DW12 - data: 0x%08x%08x", cqeCtx->dataH, cqeCtx->dataL);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA CQE: DW13-DW15 - inlineData: [0x%08x, 0x%08x, 0x%08x]",
        cqeCtx->inlineData[0], cqeCtx->inlineData[1], cqeCtx->inlineData[2]);
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
    sqPI_ = cqeItem_[HCOMM_URMA_CQE_U32_NUM];
    sqCI_ = sqPI_[8];
    cqCI_ = sqCI_[8];
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
    sqPI_ = cqeItem_[HCOMM_URMA_CQE_U32_NUM];
    sqCI_ = sqPI_[8];
    cqCI_ = sqCI_[8];
    return HCOMM_SUCCESS;
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCqWhenSqOverflow(
    ChannelHandle channel, const SqContext& sqCtx, const CqContext& cqCtx, uint32_t curHead)
{
    AscendC::GlobalTensor<uint32_t> sqCIGlobal;
    sqCIGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.tailAddr);
    Gm2Ub(sqCI_, sqCIGlobal, 1);
    uint32_t curTail = sqCI_.GetValue(0);
    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    if ((curHead + POLL_CQ_THRESHOLD) % cqDepth == curTail % cqDepth) {
        uint32_t idx = (curTail + NUM_CQE_PER_POLL_CQ) > curHead ? curHead : curTail + NUM_CQE_PER_POLL_CQ;
        KERNEL_LOG(KERNEL_INFO, "Hcomm URMA SQ overflow curHead=%u curTail=%u idx=%u cqDepth=%u", curHead,
            curTail, idx, cqDepth);
        (void)PollCq(channel, idx);
    }
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, HcommUrmaOpCode opCode, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PostSend(
    ChannelHandle channel, GM_ADDR remoteAddr, GM_ADDR localAddr, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    (void)commitPipe;
    (void)reqPipe;

    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    int32_t remoteIdx =
        HcommFindBufferIdx(channelEntity->remoteBufferAddr, channelEntity->remoteBufferNum, remoteAddr, len);
    if (remoteIdx == HCOMM_FAILED) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA PostSend failed with invalid remote buffer");
        return HCOMM_FAILED;
    }

    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    AscendC::GlobalTensor<uint32_t> sqPIGlobal;
    sqPIGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.headAddr);
    Gm2Ub(sqPI_, sqPIGlobal, 1);
    uint32_t curHead = sqPI_.GetValue(0);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA PostSend resolved remoteIdx=%d curHead=%u sqDepth=%u", remoteIdx,
        curHead, sqCtx.contextInfo.ubJfs.sqDepth);

    // poll cq if send queue is full
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    PollCqWhenSqOverflow(channel, sqCtx, cqCtx, curHead);

    // write SQE
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeItem_.GetPhyAddr();
    auto remoteMemInfo = channelEntity->remoteBufferAddr[remoteIdx];
    HcommUrmaFillSqeCtx<opCode, config>(
        sqeCtx, (__gm__ uint8_t*)remoteAddr, sqCtx, remoteMemInfo, curHead, notifyAddr, notifyVal);

    // write SGE
    __ubuf__ uint8_t* sgeAddr = (__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx);
    if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
        sgeAddr += sizeof(HcommUrmaNotifyCtx);
    }
    __ubuf__ HcommUrmaSgeCtx* sgeCtx = (__ubuf__ HcommUrmaSgeCtx*)sgeAddr;
    HcommUrmaFillSgeCtx(sgeCtx, len, (__gm__ uint8_t*)localAddr);

    // SQE & SGE cache flush
    uint64_t sqBaseAddr = sqCtx.contextInfo.ubJfs.sqVa;
    uint32_t wqeSize = sqCtx.contextInfo.ubJfs.wqeSize;
    uint32_t baseBlockCount = sqCtx.contextInfo.ubJfs.sqDepth;
    __gm__ uint8_t* sqeAddr = (__gm__ uint8_t*)(sqBaseAddr + wqeSize * (curHead % baseBlockCount));
    AscendC::GlobalTensor<uint32_t> sqeGlobal;
    sqeGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqeAddr);
    PipeBarrier<PIPE_ALL>();
    constexpr uint32_t wqeBbCnt = opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY ? 2U : 1U;
    DataCopy(sqeGlobal, wqeItem_, wqeSize * wqeBbCnt / sizeof(uint32_t));
    PipeBarrier<PIPE_ALL>();

    curHead += wqeBbCnt;
    sqPI_.SetValue(0, curHead);
    Ub2Gm<uint32_t>(sqPIGlobal, sqPI_, 1);
    if constexpr (commit) {
        Commit(channel);
    }
    HcommUrmaDumpWqeCtx(sqeCtx);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA PostSend finish new curHead=%u wqeBbCnt=%u", curHead, wqeBbCnt);
    return HCOMM_SUCCESS;
}

__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::PollCq(
    ChannelHandle channel, uint32_t expectIdx)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    AscendC::GlobalTensor<uint32_t> cqCIGlobal;
    cqCIGlobal.SetGlobalBuffer((__gm__ uint32_t*)cqCtx.contextInfo.ubJfc.tailAddr);
    Gm2Ub(cqCI_, cqCIGlobal, 1);
    uint32_t curTail = cqCI_.GetValue(0);

    uint64_t cqBaseAddr = cqCtx.contextInfo.ubJfc.scqVa;
    uint32_t cqeSize = cqCtx.contextInfo.ubJfc.cqeSize;
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    __ubuf__ HcommUrmaJfcCqeCtx* cqeUb = (__ubuf__ HcommUrmaJfcCqeCtx*)cqeItem_.GetPhyAddr();
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA PollCq enter expectIdx=%u curTail=%u cqDepth=%u", expectIdx, curTail, cqDepth);
#if defined(UT_TEST)
    curTail = expectIdx;
#else
    while (curTail != expectIdx) {
        __gm__ uint8_t* cqeAddr = (__gm__ uint8_t*)(cqBaseAddr + cqeSize * (curTail & (cqDepth - 1)));
        AscendC::GlobalTensor<uint32_t> cqeGlobal;
        cqeGlobal.SetGlobalBuffer((__gm__ uint32_t*)cqeAddr);
        bool validOwner = (curTail / cqDepth) & 1;
        uint32_t times = 0;
        while ((validOwner ^ cqeUb->owner) == 0 && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            PipeBarrier<PIPE_ALL>();
            DataCopy(cqeItem_, cqeGlobal, cqeSize / sizeof(uint32_t));
            PipeBarrier<PIPE_ALL>();
            times++;
        }
        if (times >= HCOMM_URMA_MAX_RETRY_TIMES) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Poll CQ timeout curTail=%u expectIdx=%u", curTail, expectIdx);
            HcommUrmaDumpCqeCtx(cqeUb);
            return 0xFFU;
        }
        // check CQE status
        uint8_t status = cqeUb->status & 0xFFU;
        uint8_t subStatus = cqeUb->substatus & 0xFFU;
        constexpr uint8_t statusShift = 8;
        if (status != 0 || subStatus != 0) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA CQE failed status=%u subStatus=%u", status, subStatus);
            HcommUrmaDumpCqeCtx(cqeUb);
            return (status << statusShift) | subStatus;
        }
        curTail++;
    }
#endif

    // update CQ tail
    cqCI_.SetValue(0, curTail);
    Ub2Gm<uint32_t>(cqCIGlobal, cqCI_, 1);

    // ring CQ doorbell
    st_dev(curTail & 0xFFFFFFU, (__gm__ uint32_t*)cqCtx.contextInfo.ubJfc.dbVa, 0);

    // update WQ tail
    AscendC::GlobalTensor<uint32_t> sqCIGlobal;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    sqCIGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.tailAddr);
    sqCI_.SetValue(0, curTail);
    Ub2Gm<uint32_t>(sqCIGlobal, sqCI_, 1);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA PollCq finish expectIdx=%u curTail=%u", expectIdx, curTail);
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE, config>(channel, dst, src, len);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::READ, config>(channel, src, dst, len);
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteWithNotifyNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    return PostSend<commit, commitPipe, reqPipe, HcommUrmaOpCode::WRITE_WITH_NOTIFY, config>(
        channel, dst, src, len, notifyAddr, notifyVal);
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Commit(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    AscendC::GlobalTensor<uint32_t> sqPIGlobal;
    sqPIGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.headAddr);
    Gm2Ub(sqPI_, sqPIGlobal, 1);
    uint32_t curHead = sqPI_.GetValue(0);

    st_dev(curHead, (__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.dbVa, 0);

    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA Commit by channel finish channel=%lu curHead=%u", channel, curHead);
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(ChannelHandle channel)
{
    (void)pipe;
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto sqCtx = channelEntity->sqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    AscendC::GlobalTensor<uint32_t> sqPIGlobal;
    sqPIGlobal.SetGlobalBuffer((__gm__ uint32_t*)sqCtx.contextInfo.ubJfs.headAddr);
    Gm2Ub(sqPI_, sqPIGlobal, 1);
    uint32_t curHead = sqPI_.GetValue(0);
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA Drain by channel enter channel=%lu curHead=%u", channel, curHead);
    uint32_t ret = PollCq(channel, curHead);
    if (ret != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Drain by channel failed channel=%lu pollRet=%u", channel, ret);
        return HCOMM_FAILED;
    }
    KERNEL_LOG(KERNEL_INFO, "Hcomm URMA Drain by channel finish channel=%lu curHead=%u", channel, curHead);
    return HCOMM_SUCCESS;
}

} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_URMA_H__
#endif
