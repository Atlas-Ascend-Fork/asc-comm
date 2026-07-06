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
 * \file hcomm_aiv_roce.h
 * \brief Hcomm AIV implementation for V310
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv_roce.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H

#include "hcomm_aiv_roce_def.h"
#include "../../common/hcomm_utils.h"
#include "../../common/hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Init(__ubuf__ uint8_t *buff, uint32_t len)
{
    if (len < HCOMM_UB_BUF_SIZE) {
        return HCOMM_FAILED;
    }
    __ubuf__ uint8_t *alignedAddr = AlignAddrTo32Bytes(buff);
    TBuffAddr addr;
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.dataLen = len;
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(alignedAddr));
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t *>(alignedAddr);
#endif

    wqeUB_.SetAddr(addr);
    cqeUB_ = wqeUB_[ROCE_CQE_POS];
    dbUB_ = wqeUB_[ROCE_DB_POS];
    wqeAddr_ = alignedAddr;
    cqeAddr_ = alignedAddr + ROCE_CQE_POS;
    dbAddr_ = alignedAddr + ROCE_DB_POS;
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    if (len < HCOMM_UB_BUF_SIZE || buff.GetSize() < HCOMM_UB_BUF_SIZE) {
        return HCOMM_FAILED;
    }
    LocalTensor<uint8_t> buffTensor = buff.template ReinterpretCast<uint8_t>();
    wqeUB_ = buffTensor[0];
    cqeUB_ = buffTensor[ROCE_CQE_POS];
    dbUB_ = buffTensor[ROCE_DB_POS];
    wqeAddr_ = (__ubuf__ uint8_t *)wqeUB_.GetPhyAddr();
    cqeAddr_ = (__ubuf__ uint8_t *)cqeUB_.GetPhyAddr();
    dbAddr_ = (__ubuf__ uint8_t *)dbUB_.GetPhyAddr();
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::MakeWqe(__gm__ ChannelEntity* chnlPtr,
    GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType, uint32_t sqHead, uint32_t sqDepth)
{
    int32_t remoteIdx = HcommFindBufferIdx(chnlPtr->remoteBufferAddr, chnlPtr->remoteBufferNum, dst, len);
    if (remoteIdx < 0) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: failed with invalid remote buffer addr %llu\n", dst);
        return HCOMM_FAILED;
    }
    int32_t localIdx = HcommFindBufferIdx(chnlPtr->localBufferAddr, chnlPtr->localBufferNum, src, len);
    if (localIdx < 0) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: failed with invalid local buffer addr %llu\n", src);
        return HCOMM_FAILED;
    }
    __gm__ uint8_t* sqBaseAddr = (__gm__ uint8_t*)(chnlPtr->sqContextAddr->contextInfo.roceSq.sqVa);
    uint32_t wqeSize = chnlPtr->sqContextAddr->contextInfo.roceSq.wqeSize;
    __gm__ uint8_t* sqAddr = (__gm__ uint8_t*)(sqBaseAddr + (sqHead % sqDepth) * wqeSize);
    GlobalTensor<uint8_t> sqGlobal;
    sqGlobal.SetGlobalBuffer(sqAddr);
    __ubuf__ RoceWqeEntry *wqePtr = (__ubuf__ RoceWqeEntry*)(wqeAddr_);

    uint8_t owner = (sqHead & sqDepth) == 0 ? 0 : 1;
    wqePtr->ctrl.dw0.value = HtoNL(
        owner << HCOMM_WQE_OWNER_OFFSET | 2U << HCOMM_WQE_CTRLSL_OFFSET | 1U << HCOMM_WQE_CR_OFFSET |
        1U << HCOMM_WQE_VA_OFFSET | 4U << HCOMM_WQE_TSL_OFFSET | 2U << HCOMM_WQE_BDSL_OFFSET);
    wqePtr->ctrl.dw1.value = HtoNL(1U << HCOMM_WQE_CL_OFFSET);
    wqePtr->doorbell = 0;
    wqePtr->task.dw0.value = HtoNL(opType << HCOMM_WQE_OP_TYPE_OFFSET | 1U << HCOMM_WQE_C_OFFSET);
    wqePtr->dataLen = HtoNL(len);
    wqePtr->immeData = 0;
    wqePtr->firstLast = 0;
    wqePtr->nxtEthHdr = 0;
    wqePtr->cmdLen = 0;
    wqePtr->rsvd0 = 0;
    wqePtr->lastExtLen = 0;
    wqePtr->vaRemote = HtoNLL((uint64_t)dst);
    uint32_t rKey = chnlPtr->remoteBufferAddr[remoteIdx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    wqePtr->rKey = HtoNL(rKey);
    wqePtr->rsvd1 = 0;
    wqePtr->data.vaLocal = HtoNLL((uint64_t)src);
    wqePtr->data.rLen = HtoNL((uint32_t)len);
    uint32_t lKey = chnlPtr->localBufferAddr[localIdx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    wqePtr->data.leKey = HtoNL(lKey & (~(1U << 31)));
    KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: make wqe ok\n");

    SyncAction<HardEvent::S_MTE3>();
    DataCopy(sqGlobal, wqeUB_, sizeof(RoceWqeEntry));
    SyncAction<HardEvent::MTE3_S>();

    KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: set wqe to qp ok\n");
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::PostSend(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType)
{
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    if (chnlPtr == nullptr || chnlPtr->sqNum == 0 || chnlPtr->cqNum == 0 ||
        chnlPtr->sqContextAddr == nullptr || chnlPtr->cqContextAddr == nullptr) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: channel sqNum = %u, cqNum = %u.\n", chnlPtr->sqNum, chnlPtr->cqNum);
        return HCOMM_FAILED;
    }
    auto sqPIAddr = chnlPtr->sqContextAddr[0].contextInfo.roceSq.headAddr;
    CacheWriteThrough((__gm__ uint8_t*)(sqPIAddr), sizeof(uint32_t));
    uint32_t sqHead = *(__gm__ uint32_t*)(sqPIAddr);
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    auto sqCIAddr = chnlPtr->sqContextAddr[0].contextInfo.roceSq.tailAddr;
    CacheWriteThrough((__gm__ uint8_t*)(sqCIAddr), sizeof(uint32_t));
    uint32_t sqTail = *(__gm__ uint32_t*)(sqCIAddr);
    KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: opType = %u, sqHead = %u, sqTail = %u, sqDepth = %u, cqDepth = %u.\n",
        opType, sqHead, sqTail, sqDepth, cqDepth);

    constexpr uint32_t POLL_CQ_THRESHOLD = 10;
    constexpr uint32_t NUM_CQE_PER_POLL_CQ = 100;
    if ((sqHead + POLL_CQ_THRESHOLD) % cqDepth == sqTail % cqDepth) {
        uint32_t idx = (sqTail + NUM_CQE_PER_POLL_CQ) > sqHead ? sqHead : (sqTail + NUM_CQE_PER_POLL_CQ);
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: RoCE SQ overflow sqHead=%u sqTail=%u idx=%u cqDepth=%u\n", sqHead,
            sqTail, idx, cqDepth);
        if (PollCq(chnlPtr, idx) != HCOMM_SUCCESS) {
            KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: RoCE SQ overflow, PollCq failed.\n");
            return HCOMM_FAILED;
        }
    }

    if (MakeWqe(chnlPtr, dst, src, len, opType, sqHead, sqDepth) != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: MakeWqe failed.\n");
        return HCOMM_FAILED;
    }

    sqHead++;
    *(__gm__ uint32_t*)(sqPIAddr) = sqHead;
    CacheWriteThrough((__gm__ uint8_t*)(sqPIAddr), sizeof(uint32_t));
    KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: update PI to GM sqHead = %u\n", sqHead);

    if constexpr (commit) {
        KnockDoorBell<commitPipe>(chnlPtr, sqHead);
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: Commit ok.\n");
    }
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return PostSend<commit, commitPipe, reqPipe>(channel, dst, src, len,
        static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::WRITE));
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return PostSend<commit, commitPipe, reqPipe>(channel, src, dst, len,
        static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::READ));
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const &config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteWithNotifyNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, GM_ADDR notifyAddr, uint64_t notifyVal)
{
    (void)commit;
    (void)commitPipe;
    (void)reqPipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)src;
    (void)len;
    (void)notifyAddr;
    (void)notifyVal;
    KERNEL_LOG(KERNEL_ERROR, "Hcomm ROCE WriteWithNotifyNbi is not supported");
    return HCOMM_FAILED;
}

__aicore__ inline uint64_t HcommImpl<COMM_PROTOCOL_ROCE>::GetDbValue(uint32_t qpn)
{
    RoceDbEntry dbEntry;
    dbEntry.dw0.value = 0;
    dbEntry.dw0.bs.c = 0;
    dbEntry.dw0.bs.r = 0;
    dbEntry.dw0.bs.ctxSize = 1;
    dbEntry.dw0.bs.qpn = qpn;
    dbEntry.dw0.bs.subType = 0;
    dbEntry.dw0.bs.resv = 0;
    dbEntry.dw0.bs.pi = 0;
    dbEntry.dw0.bs.sgidIdx = ROCE_INIT_SQ_DB_SGIT_IDX;
    dbEntry.dw0.bs.type = ROCE_SQ_DOORBELL_TYPE;
    dbEntry.dw0.bs.mtuShift = 0;
    dbEntry.dw0.bs.cos = 0x7;
    dbEntry.dw0.bs.xrcVld = 0;
    return dbEntry.dw0.value;
}

template <pipe_t pipe>
__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::KnockDoorBell(__gm__ ChannelEntity* chnlPtr,
    uint32_t sqHead)
{
    uint64_t dbValue = GetDbValue(chnlPtr->sqContextAddr->contextInfo.roceSq.qpn);
    KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: dbValue = %llu\n", dbValue);
    if constexpr (pipe == PIPE_MTE3) {
        GlobalTensor<uint8_t> dbGlobalTensor;
        __gm__ uint8_t* doorBellAddr = (__gm__ uint8_t*)(chnlPtr->sqContextAddr->contextInfo.roceSq.dbVa);
        dbGlobalTensor.SetGlobalBuffer(doorBellAddr);
        __ubuf__ uint32_t* dbUBPtr = (__ubuf__ uint32_t*)(dbAddr_);
        *(dbUBPtr + 0) = (dbValue & 0xffffffff);
        *(dbUBPtr + 1) = (dbValue >> 32) | ((sqHead >> 8) & 0xff);
        Ub2Gm<uint8_t>(dbGlobalTensor, dbUB_, sizeof(uint64_t));
    } else {
        __gm__ uint64_t* dbAddr = (__gm__ uint64_t*)(chnlPtr->sqContextAddr->contextInfo.roceSq.dbVa);
        uint64_t dbFinalVal = dbValue | ((((uint64_t)(sqHead) >> 8) & 0xff) << 32);
        st_dev(dbFinalVal, dbAddr, 0);
    }
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Commit(ChannelHandle channel)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    auto sqPIAddr = chnlPtr->sqContextAddr->contextInfo.roceSq.headAddr;
    CacheWriteThrough((__gm__ uint8_t*)(sqPIAddr), sizeof(uint32_t));
    uint32_t sqHead = *(__gm__ uint32_t*)(sqPIAddr);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: sqHead = %u\n", sqHead);

    KnockDoorBell<pipe>(chnlPtr, sqHead);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Exit ok\n");
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::PollCq(__gm__ ChannelEntity* chnlPtr, uint32_t expectIdx)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }
    uint32_t cqeSize = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqeSize;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t qpn = chnlPtr->sqContextAddr[0].contextInfo.roceSq.qpn;
    auto sqCIAddr = chnlPtr->sqContextAddr[0].contextInfo.roceSq.tailAddr;
    auto cqCIAddr = chnlPtr->cqContextAddr[0].contextInfo.roceCq.tailAddr;
    CacheWriteThrough((__gm__ uint8_t*)(cqCIAddr), sizeof(uint32_t));
    uint32_t cqTail = *(__gm__ uint32_t*)(cqCIAddr);
    CacheWriteThrough((__gm__ uint8_t*)(sqCIAddr), sizeof(uint32_t));
    uint32_t sqTail = *(__gm__ uint32_t*)(sqCIAddr);
    KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: cqeSize = %u cqDepth = %u qpn = %u cqTail= %u sqTail = %u expectIdx = %u\n",
        cqeSize, cqDepth, qpn, cqTail, sqTail, expectIdx);

    __ubuf__ RoceCqeEntry* cqePtr = (__ubuf__ RoceCqeEntry*)(cqeAddr_);
    __gm__ uint8_t* cqBaseBuf = (__gm__ uint8_t*)(chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqVa);
    AscendC::GlobalTensor<uint8_t> cqeGlobalTensor;
    while (cqTail < expectIdx) {
        __gm__ uint8_t* cqeAddr = (__gm__ uint8_t*)(cqBaseBuf + cqeSize * (cqTail % cqDepth));
        cqeGlobalTensor.SetGlobalBuffer(cqeAddr);
        uint32_t loop = 0;
        for (; loop < HCOMM_POLLCQ_MAX_RETRY_TIMES; loop ++) {
            SyncAction<HardEvent::S_MTE2>();
            DataCopy(cqeUB_, cqeGlobalTensor, sizeof(RoceCqeEntry));
            SyncAction<HardEvent::MTE2_S>();
    #if defined(UT_TEST)
            cqTail = expectIdx;
            break;
    #else
            uint8_t owner = (HtoNL(cqTail) & (cqDepth + 1)) == 0 ? 0 : 1;
            if (((((cqePtr->cqe0) >> 31) & 0x1) == owner) && ((cqePtr->cqe0 & 0xfffff) == qpn)) {
                break;
            }
    #endif
        }
        if (loop >= HCOMM_POLLCQ_MAX_RETRY_TIMES) {
            KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: Overtime exit\n");
            return HCOMM_FAILED;
        }
        cqTail += 1;
        sqTail += 1;
        KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: cqTail = %u, sqTail = %u.\n", cqTail, sqTail);
    }
    *(__gm__ uint32_t*)(cqCIAddr) = cqTail;
    CacheWriteThrough((__gm__ uint8_t*)(cqCIAddr), sizeof(uint32_t));
    *(__gm__ uint32_t*)(sqCIAddr) = sqTail;
    CacheWriteThrough((__gm__ uint8_t*)(sqCIAddr), sizeof(uint32_t));
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Drain(ChannelHandle channel)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    auto sqPIAddr = chnlPtr->sqContextAddr[0].contextInfo.roceSq.headAddr;
    CacheWriteThrough((__gm__ uint8_t*)(sqPIAddr), sizeof(uint32_t));
    uint32_t sqHead = *(__gm__ uint32_t*)(sqPIAddr);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: sqHead = %u\n", sqHead);
    if (PollCq(chnlPtr, sqHead) != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: PollCq failed.\n");
        return HCOMM_FAILED;
    }
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Exit ok\n");
    return HCOMM_SUCCESS;
}
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H__
#endif
