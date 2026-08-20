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
__aicore__ inline void HcommUrmaUpdateBatchTail(
    uint64_t sqTailAddr, uint64_t cqTailAddr, uint64_t cqDbAddr, uint32_t sqTail, uint32_t cqTail)
{
    st_dev(sqTail, reinterpret_cast<__gm__ uint32_t*>(sqTailAddr), 0);
    st_dev(cqTail, reinterpret_cast<__gm__ uint32_t*>(cqTailAddr), 0);
    st_dev(cqTail & 0xFFFFFFU, reinterpret_cast<__gm__ uint32_t*>(cqDbAddr), 0);
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
__aicore__ inline void HcommUrmaFillSqeCtxByRemote(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr, uint32_t sqDepth, uint32_t tpID, uint32_t tokenId,
    uint32_t tokenValue, uint64_t rmtEidL, uint64_t rmtEidH, uint32_t curHead,
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
    sqeCtx->owner = (curHead & sqDepth) == 0 ? 1 : 0;
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
    sqeCtx->tpId = tpID;
    sqeCtx->rmtJettyOrSegId = tokenId;
    sqeCtx->rmtTokenValue = tokenValue;
    uint64_t remoteAddrValue = reinterpret_cast<uint64_t>(remoteAddr);
    sqeCtx->rmtAddrLOrTokenId = remoteAddrValue & 0xFFFFFFFFU;
    sqeCtx->rmtAddrHOrTokenValue = (remoteAddrValue >> 32) & 0xFFFFFFFFU;
    sqeCtx->rmtEidL = rmtEidL;
    sqeCtx->rmtEidH = rmtEidH;
    sqeCtx->udfType = 0;
    sqeCtx->reduceDataType = 0;
    sqeCtx->reduceOpcode = 0;
    if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_REDUCE) {
        sqeCtx->reduceDataType = params.reduceDataType;
        sqeCtx->reduceOpcode = params.reduceOpcode;
    }
}

template <HcommUrmaOpCode opCode, auto const& config, typename T>
__aicore__ inline void HcommUrmaFillSqeCtx(
    __ubuf__ HcommUrmaSqeCtx* sqeCtx, __gm__ uint8_t* remoteAddr, const SqContext& sqCtx,
    const RegedBufferEntity& remoteMemInfo, uint32_t curHead, GM_ADDR notifyAddr = nullptr,
    const UdmaParams<T>& params = UdmaParams<T>{})
{
    const auto& ubJfs = sqCtx.contextInfo.ubJfs;
    const auto& ubToken = remoteMemInfo.bufferInfo.rma.protectionInfo.memInfo.ub;
    auto rmtEid = reinterpret_cast<const uint64_t*>(ubJfs.remoteEID);
    HcommUrmaFillSqeCtxByRemote<opCode, config, T>(
        sqeCtx, remoteAddr, ubJfs.sqDepth, ubJfs.tpID, ubToken.tokenId, ubToken.tokenValue, rmtEid[0], rmtEid[1],
        curHead, params);
    if constexpr (opCode == HcommUrmaOpCode::WRITE_WITH_NOTIFY) {
        __ubuf__ HcommUrmaNotifyCtx* notifyCtx =
            (__ubuf__ HcommUrmaNotifyCtx*)((__ubuf__ uint8_t*)sqeCtx + sizeof(HcommUrmaSqeCtx));
        HcommUrmaFillNotifyCtx(notifyCtx, remoteMemInfo, notifyAddr, params.value);
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

__aicore__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    MultiChannelHandle multiChannel, __ubuf__ uint8_t* buff, uint32_t buffLen)
{
    __ubuf__ uint8_t* alignedBuff = AlignAddrTo32Bytes(buff);
    uint32_t alignOffset = static_cast<uint32_t>(alignedBuff - buff);
    uint32_t alignedLen = buffLen > alignOffset ? buffLen - alignOffset : 0U;
    TBuffAddr addr{};
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(alignedBuff));
    addr.dataLen = alignedLen;
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t*>(alignedBuff);
#endif
    LocalTensor<uint8_t> tensor;
    tensor.SetAddr(addr);
    return MakeBatchHandle(multiChannel, tensor, alignedLen);
}

template <typename T>
__aicore__ inline UbcCtpBatchHandle HcommImpl<COMM_PROTOCOL_UBC_CTP>::MakeBatchHandle(
    MultiChannelHandle multiChannel, const LocalTensor<T>& buff, uint32_t buffLen)
{
    UbcCtpBatchHandle handle{};
    LocalTensor<uint32_t> buffer = buff.template ReinterpretCast<uint32_t>();

    if (multiChannel == 0U) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with null multi-channel handle\n");
        return handle;
    }
    auto* multiChannelEntity = reinterpret_cast<__gm__ HcommMultiChannelEntity*>(multiChannel);
    uint32_t channelNum = multiChannelEntity->channelNum;
    uint64_t expectedEntitySize = sizeof(HcommMultiChannelEntity) +
                                  static_cast<uint64_t>(channelNum) * sizeof(UbcCtpBatchRemoteInfo);
    if (multiChannelEntity->abiHeader.version != HCOMM_MULTI_CHANNEL_VERSION ||
        multiChannelEntity->abiHeader.magicWord != HCOMM_MULTI_CHANNEL_MAGIC_WORD || channelNum == 0U ||
        expectedEntitySize > 0xFFFFFFFFULL || multiChannelEntity->abiHeader.size != expectedEntitySize) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with invalid multi-channel ABI\n");
        return handle;
    }
    uint32_t sqDepth = multiChannelEntity->sqContext.contextInfo.ubJfs.sqDepth;
    uint32_t wqeSize = multiChannelEntity->sqContext.contextInfo.ubJfs.wqeSize;
    uint32_t cqeSize = multiChannelEntity->cqContext.contextInfo.ubJfc.cqeSize;
    uint32_t sqHead = static_cast<uint32_t>(
        ld_dev(reinterpret_cast<__gm__ uint32_t*>(multiChannelEntity->sqContext.contextInfo.ubJfs.headAddr), 0));
    uint32_t cqHead = static_cast<uint32_t>(
        ld_dev(reinterpret_cast<__gm__ uint32_t*>(multiChannelEntity->cqContext.contextInfo.ubJfc.headAddr), 0));
    if (wqeSize == 0U || cqeSize == 0U || buffLen < cqeSize + wqeSize) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA MakeBatchHandle failed with insufficient batch buffer\n");
        return handle;
    }
    uint32_t capacityBb = (buffLen - cqeSize) / wqeSize;
    uint32_t wqeBufferSize = capacityBb * wqeSize;

    handle.multiChannel = multiChannel;
    handle.wqeBuffer = buffer;
    handle.cqeBuffer = buffer[wqeBufferSize / sizeof(uint32_t)];
    handle.sqHead = sqHead;
    handle.cqHead = cqHead;
    handle.pendingBb = 0U;
    handle.pendingCqe = 0U;
    handle.pendingCompletionSqHead = sqHead;
    handle.completionSqHead = sqHead;
    handle.capacityBb = capacityBb > sqDepth ? sqDepth : capacityBb;
    handle.sqDepth = sqDepth;
    handle.wqeSize = wqeSize;
    handle.cqeSize = cqeSize;
    return handle;
}

__aicore__ inline UbcCtpBatchHandleRef HcommImpl<COMM_PROTOCOL_UBC_CTP>::GetHandleRef(
    UbcCtpBatchHandle& handle, uint32_t channelIndex)
{
    if (!handle.IsValid()) {
        return UbcCtpBatchHandleRef{};
    }
    auto* multiChannelEntity = reinterpret_cast<__gm__ HcommMultiChannelEntity*>(handle.multiChannel);
    if (channelIndex >= multiChannelEntity->channelNum) {
        return UbcCtpBatchHandleRef{};
    }
    auto* channelInfos = reinterpret_cast<__gm__ UbcCtpBatchRemoteInfo*>(
        handle.multiChannel + sizeof(HcommMultiChannelEntity));
    UbcCtpBatchRemoteInfo remoteInfo{};
    remoteInfo.remoteBufferAddr = channelInfos[channelIndex].remoteBufferAddr;
    remoteInfo.remoteBufferNum = channelInfos[channelIndex].remoteBufferNum;
    remoteInfo.tpID = channelInfos[channelIndex].tpID;
    remoteInfo.rmtEidL = channelInfos[channelIndex].rmtEidL;
    remoteInfo.rmtEidH = channelInfos[channelIndex].rmtEidH;
    return UbcCtpBatchHandleRef{&handle, remoteInfo};
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::WriteNbi(
    UbcCtpBatchHandleRef& handleRef, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)reqPipe;
    static_assert(config.inlineEn == 0U, "Batch WriteNbi does not support inline WQEs");
    static_assert(config.cqe == 0U || config.cqe == 1U, "Batch WriteNbi only supports cqe values 0 and 1");
    UbcCtpBatchHandle& batch = *handleRef.batch;
    if (batch.pendingBb >= batch.capacityBb) {
        return HCOMM_FAILED;
    }

    const UbcCtpBatchRemoteInfo& remoteInfo = handleRef.remoteInfo;
    auto* remoteBuffers = reinterpret_cast<RegedBufferEntity*>(remoteInfo.remoteBufferAddr);
    int32_t remoteBufferIndex = HcommFindBufferIdx(remoteBuffers, remoteInfo.remoteBufferNum, dst, len);
    uint32_t remoteTokenId = remoteBuffers[static_cast<uint32_t>(remoteBufferIndex)]
                                 .bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
    uint32_t remoteTokenValue = remoteBuffers[static_cast<uint32_t>(remoteBufferIndex)]
                                    .bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
    __ubuf__ uint8_t* wqeAddress = reinterpret_cast<__ubuf__ uint8_t*>(batch.wqeBuffer.GetPhyAddr()) +
                                      batch.pendingBb * batch.wqeSize;
    auto* sqeCtx = reinterpret_cast<__ubuf__ HcommUrmaSqeCtx*>(wqeAddress);
    auto* wqeWords = reinterpret_cast<__ubuf__ uint32_t*>(wqeAddress);
    for (uint32_t i = 0U; i < batch.wqeSize / sizeof(uint32_t); ++i) {
        wqeWords[i] = 0U;
    }

    uint32_t wqeHead = batch.sqHead + batch.pendingBb;
    HcommUrmaFillSqeCtxByRemote<HcommUrmaOpCode::WRITE, config, uint64_t>(
        sqeCtx, reinterpret_cast<__gm__ uint8_t*>(dst), batch.sqDepth, remoteInfo.tpID, remoteTokenId,
        remoteTokenValue, remoteInfo.rmtEidL, remoteInfo.rmtEidH, wqeHead);
    auto* sgeCtx = reinterpret_cast<__ubuf__ HcommUrmaSgeCtx*>(wqeAddress + sizeof(HcommUrmaSqeCtx));
    HcommUrmaFillSgeCtx<HcommUrmaOpCode::WRITE>(
        sgeCtx, len, reinterpret_cast<__gm__ uint8_t*>(src), UdmaParams<uint64_t>{});
    batch.pendingBb++;
    if constexpr (config.cqe != 0U) {
        batch.pendingCqe++;
        batch.pendingCompletionSqHead = batch.sqHead + batch.pendingBb;
    }
    if constexpr (commit) {
        return BatchCommit<commitPipe>(batch);
    }
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::BatchCommit(UbcCtpBatchHandle& handle)
{
    (void)pipe;
    if (handle.pendingBb == 0U) {
        return HCOMM_SUCCESS;
    }

    auto* multiChannelEntity = reinterpret_cast<__gm__ HcommMultiChannelEntity*>(handle.multiChannel);
    uint64_t sqHeadAddr = multiChannelEntity->sqContext.contextInfo.ubJfs.headAddr;
    uint64_t sqTailAddr = multiChannelEntity->sqContext.contextInfo.ubJfs.tailAddr;
    uint64_t sqDbAddr = multiChannelEntity->sqContext.contextInfo.ubJfs.dbVa;
    uint64_t sqVa = multiChannelEntity->sqContext.contextInfo.ubJfs.sqVa;
    uint64_t cqHeadAddr = multiChannelEntity->cqContext.contextInfo.ubJfc.headAddr;
    uint64_t cqTailAddr = multiChannelEntity->cqContext.contextInfo.ubJfc.tailAddr;
    uint32_t cqDepth = multiChannelEntity->cqContext.contextInfo.ubJfc.cqDepth;
    uint32_t sqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(sqTailAddr), 0));
    uint32_t cqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(cqTailAddr), 0));
    uint32_t usedSq = handle.sqHead - sqTail;
    uint32_t usedCq = handle.cqHead - cqTail;
    if (usedSq > handle.sqDepth || usedCq > cqDepth || handle.pendingBb > handle.sqDepth - usedSq ||
        handle.pendingCqe > cqDepth - usedCq) {
        return HCOMM_FAILED;
    }

    uint32_t sqOffset = handle.sqHead % handle.sqDepth;
    uint32_t contiguousBb = handle.sqDepth - sqOffset;
    uint32_t firstBb = handle.pendingBb > contiguousBb ? contiguousBb : handle.pendingBb;
    uint32_t wordsPerWqe = handle.wqeSize / sizeof(uint32_t);
    GlobalTensor<uint32_t> sqTensor;
    sqTensor.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(sqVa));

    Mutex::Lock<PIPE_MTE3>(HCOMM_URMA_MUTEX_ID);
    DataCopy(sqTensor[sqOffset * wordsPerWqe], handle.wqeBuffer, firstBb * wordsPerWqe);
    uint32_t secondBb = handle.pendingBb - firstBb;
    if (secondBb != 0U) {
        DataCopy(sqTensor, handle.wqeBuffer[firstBb * wordsPerWqe], secondBb * wordsPerWqe);
    }
    Mutex::Unlock<PIPE_MTE3>(HCOMM_URMA_MUTEX_ID);

    handle.sqHead += handle.pendingBb;
    handle.cqHead += handle.pendingCqe;
    if (handle.pendingCqe != 0U) {
        handle.completionSqHead = handle.pendingCompletionSqHead;
    }
    Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
    st_dev(handle.sqHead, reinterpret_cast<__gm__ uint32_t*>(sqHeadAddr), 0);
    st_dev(handle.cqHead, reinterpret_cast<__gm__ uint32_t*>(cqHeadAddr), 0);
    st_dev(handle.sqHead, reinterpret_cast<__gm__ uint32_t*>(sqDbAddr), 0);
    Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
    handle.pendingBb = 0U;
    handle.pendingCqe = 0U;
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_UBC_CTP>::Drain(UbcCtpBatchHandle& handle)
{
    (void)pipe;
    auto* multiChannelEntity = reinterpret_cast<__gm__ HcommMultiChannelEntity*>(handle.multiChannel);
    uint64_t sqTailAddr = multiChannelEntity->sqContext.contextInfo.ubJfs.tailAddr;
    uint64_t cqVa = multiChannelEntity->cqContext.contextInfo.ubJfc.scqVa;
    uint64_t cqTailAddr = multiChannelEntity->cqContext.contextInfo.ubJfc.tailAddr;
    uint64_t cqDbAddr = multiChannelEntity->cqContext.contextInfo.ubJfc.dbVa;
    uint32_t cqDepth = multiChannelEntity->cqContext.contextInfo.ubJfc.cqDepth;
    uint32_t sqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(sqTailAddr), 0));
    uint32_t cqTail = static_cast<uint32_t>(ld_dev(reinterpret_cast<__gm__ uint32_t*>(cqTailAddr), 0));
    uint32_t curTail = cqTail;
    __ubuf__ uint8_t* cqeAddress = reinterpret_cast<__ubuf__ uint8_t*>(handle.cqeBuffer.GetPhyAddr());
    auto* cqe = reinterpret_cast<__ubuf__ HcommUrmaJfcCqeCtx*>(cqeAddress);
    GlobalTensor<uint32_t> cqTensor;
    cqTensor.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(cqVa));
#if defined(UT_TEST)
    curTail = handle.cqHead;
#else
    while (curTail != handle.cqHead) {
        uint32_t cqOffset = curTail % cqDepth;
        Mutex::Lock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
        DataCopy(
            handle.cqeBuffer, cqTensor[cqOffset * handle.cqeSize / sizeof(uint32_t)],
            handle.cqeSize / sizeof(uint32_t));
        Mutex::Unlock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
        Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        bool validOwner = (curTail / cqDepth) & 1U;
        uint32_t times = 0U;
        while ((validOwner ^ cqe->owner) == 0U && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            Mutex::Lock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
            DataCopy(
                handle.cqeBuffer, cqTensor[cqOffset * handle.cqeSize / sizeof(uint32_t)],
                handle.cqeSize / sizeof(uint32_t));
            Mutex::Unlock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
            Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            times++;
        }
        if ((validOwner ^ cqe->owner) == 0U) {
            Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            HcommUrmaUpdateBatchTail(sqTailAddr, cqTailAddr, cqDbAddr, sqTail, curTail);
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA batch Drain timeout curTail=%u cqHead=%u\n", curTail, handle.cqHead);
            HcommUrmaDumpCqeCtx(cqe);
            return 0xFFU;
        }
        uint8_t status = cqe->status & 0xFFU;
        uint8_t subStatus = cqe->substatus & 0xFFU;
        constexpr uint8_t statusShift = 8U;
        if (status != 0U || subStatus != 0U) {
            Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            curTail++;
            HcommUrmaUpdateBatchTail(sqTailAddr, cqTailAddr, cqDbAddr, sqTail, curTail);
            KERNEL_LOG(
                KERNEL_ERROR, "Hcomm URMA batch Drain failed status=%u subStatus=%u curTail=%u\n", status, subStatus,
                curTail);
            HcommUrmaDumpCqeCtx(cqe);
            return (status << statusShift) | subStatus;
        }
        Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        curTail++;
    }
#endif

    if (curTail != cqTail) {
        sqTail = handle.completionSqHead;
    }
    HcommUrmaUpdateBatchTail(sqTailAddr, cqTailAddr, cqDbAddr, sqTail, curTail);
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

__aicore__ inline void HcommImpl<COMM_PROTOCOL_UBC_CTP>::CommitImpl(
    ChannelHandle channel, const SqContext& sqCtx, uint32_t sqHead, uint32_t cqeCnt)
{
    __gm__ ChannelEntity* channelEntity = (__gm__ ChannelEntity*)channel;
    auto cqCtx = channelEntity->cqContextAddr[HCOMM_URMA_DEFAULT_QP_IDX];
    uint32_t cqDepth = cqCtx.contextInfo.ubJfc.cqDepth;
    uint32_t cqTail = channelEntity->cqTail;
    uint32_t cqLeft = (cqeCnt - cqTail) % cqDepth;
    if (cqLeft >= 0) {
        Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        st_dev(sqHead, reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.dbVa), 0);
        Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
    } else {
        auto commitCnt = sqHead - cqLeft;
        Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        st_dev(commitCnt, reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.dbVa), 0);
        Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        PollCq(channel, commitCnt);
        Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        st_dev(sqHead, reinterpret_cast<__gm__ uint32_t*>(sqCtx.contextInfo.ubJfs.dbVa), 0);
        Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
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

    // write SQE
    __ubuf__ HcommUrmaSqeCtx* sqeCtx = (__ubuf__ HcommUrmaSqeCtx*)wqeItem_.GetPhyAddr();
    auto remoteMemInfo = channelEntity->remoteBufferAddr[remoteIdx];
    Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
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
    Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);

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
    Mutex::Lock<PIPE_MTE3>(HCOMM_URMA_MUTEX_ID);
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
    Mutex::Unlock<PIPE_MTE3>(HCOMM_URMA_MUTEX_ID);

    if constexpr (config.cqe != 0) {
        cqeCnt++;
        channelEntity->cqHead = cqeCnt;
    }
    curHead += wqeBbCnt;
    channelEntity->sqHead = curHead;

    if constexpr (commit) {
        CommitImpl(channel, sqCtx, curHead, cqeCnt);
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
        Mutex::Lock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
        DataCopy(cqeItem_, cqeGlobal, cqeSize / sizeof(uint32_t));
        Mutex::Unlock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
        Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        bool validOwner = (curTail / cqDepth) & 1;
        uint32_t times = 0;
        uint32_t ret = HCOMM_SUCCESS;
        while ((validOwner ^ cqeUb->owner) == 0 && times < HCOMM_URMA_MAX_RETRY_TIMES) {
            Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            Mutex::Lock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
            DataCopy(cqeItem_, cqeGlobal, cqeSize / sizeof(uint32_t));
            Mutex::Unlock<PIPE_MTE2>(HCOMM_URMA_MUTEX_ID);
            Mutex::Lock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
            times++;
        }
        if (times >= HCOMM_URMA_MAX_RETRY_TIMES) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA Poll CQ timeout curTail=%u expectIdx=%u \n", curTail, expectIdx);
            HcommUrmaDumpCqeCtx(cqeUb);
            ret = 0xFFU;
        } else {
            // check CQE status
            uint8_t status = cqeUb->status & 0xFFU;
            uint8_t subStatus = cqeUb->substatus & 0xFFU;
            constexpr uint8_t statusShift = 8;
            if (status != 0 || subStatus != 0) {
                KERNEL_LOG(KERNEL_ERROR, "Hcomm URMA CQE failed status=%u subStatus=%u \n", status, subStatus);
                HcommUrmaDumpCqeCtx(cqeUb);
                ret = (status << statusShift) | subStatus;
            }
        }
        Mutex::Unlock<PIPE_S>(HCOMM_URMA_MUTEX_ID);
        if (ret != HCOMM_SUCCESS) {
            return ret;
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
    CommitImpl(channel, sqCtx, channelEntity->sqHead, channelEntity->cqHead);
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
