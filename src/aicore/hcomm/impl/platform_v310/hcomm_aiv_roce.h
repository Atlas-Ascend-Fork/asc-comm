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

#if !defined(HCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define HCOMM_INCLUDE_INTERNAL_HEADERS
#define HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_ROCE_H

#include "hcomm_aiv_roce_def.h"
#include "../../common/hcomm_log.h"
#include "../../common/hcomm_utils.h"
#include "../../common/hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Init(__ubuf__ uint8_t* buff, uint32_t len)
{
    if (len < HCOMM_UB_BUF_SIZE) {
        return HCOMM_FAILED;
    }
    __ubuf__ uint8_t* alignedAddr = AlignAddrTo32Bytes(buff);
    TBuffAddr addr;
    addr.logicPos = static_cast<uint8_t>(TPosition::VECOUT);
    addr.dataLen = len;
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(alignedAddr));
#if defined(UT_TEST)
    addr.absAddr = reinterpret_cast<uint8_t*>(alignedAddr);
#endif

    wqeUB_.SetAddr(addr);
    cqeUB_ = wqeUB_[ROCE_CQE_POS / sizeof(uint32_t)];
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    if (len < HCOMM_UB_BUF_SIZE || buff.GetSize() * sizeof(T) < HCOMM_UB_BUF_SIZE) {
        return HCOMM_FAILED;
    }
    wqeUB_ = buff.template ReinterpretCast<uint32_t>();
    cqeUB_ = wqeUB_[ROCE_CQE_POS / sizeof(uint32_t)];
    return HCOMM_SUCCESS;
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::FillCtrlSeg(
    __ubuf__ RoceWqeEntry* wqePtr, uint32_t sqHead, uint32_t sqDepth, uint32_t enCqe)
{
    uint8_t owner = (sqHead & sqDepth) == 0 ? 0 : 1;
    wqePtr->ctrl.ownerSl = (owner << ROCE_1825_WQE_OWNER_SHIFT) | ROCE_1825_WQE_CTRL_VALUE;

    wqePtr->ctrl.dfTsl = ((enCqe == 1) ? (1U << ROCE_1825_WQE_SQ_SIGNAL_SHIFT) : 0) | ROCE_1825_WQE_SQ_VA_VALUE;
    wqePtr->ctrl.dfTsl |= sizeof(RoceWqeTaskSeg) / ROCE_1825_WQE_TASK_SEG_ALIGN;

    wqePtr->ctrl.wfBdsl = HtoNS(static_cast<uint16_t>(0 << ROCE_1825_WQE_FAST_DMA_SHIFT));
    wqePtr->ctrl.wfBdsl |= HtoNS((sqHead & ROCE_1825_WQE_SSN_MASK) << ROCE_1825_WQE_SSN_SHIFT);
    wqePtr->ctrl.wfBdsl |= HtoNS(static_cast<uint16_t>(
        static_cast<uint32_t>(1) << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));

    wqePtr->ctrl.clPi = HtoNL(ROCE_1825_WQE_CMP_TASK_LEN1 << ROCE_1825_WQE_CMP_TASK_LEN_SHIFT);
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::FillTaskSeg(
    __ubuf__ RoceWqeEntry* wqePtr, GM_ADDR dst, uint64_t len, uint32_t opType, uint32_t rKey, uint32_t lKey,
    uint32_t fence)
{
    wqePtr->task.comTask.value = 0;
    wqePtr->task.comTask.dw0.signal = !!((wqePtr->ctrl.dfTsl & (1U << ROCE_1825_WQE_CQE_SIGNAL_SHIFT)) > 0);
    wqePtr->task.comTask.dw0.fence = fence;
    wqePtr->task.comTask.dw0.opType = opType;
    wqePtr->task.comTask.dw0.se = 0;
    wqePtr->task.comTask.value = HtoNL(wqePtr->task.comTask.value);

    wqePtr->task.dataLen = HtoNL((uint32_t)len);
    wqePtr->task.immData = 0;
    wqePtr->task.dw3.value =
        (opType == (uint32_t)HCOMM_ROCE_OP_TYPE::READ) ? HtoNL(ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN) : 0;
    wqePtr->task.vaRemote = HtoNLL((uint64_t)dst);
    wqePtr->task.rKey = HtoNL(rKey);
    wqePtr->task.ulp = HtoNL(lKey & 0xffffU);
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::FillDataSeg(
    __ubuf__ RoceWqeEntry* wqePtr, GM_ADDR src, uint64_t len, uint32_t lKey)
{
    wqePtr->data.vaLocal = HtoNLL((uint64_t)src);
    wqePtr->data.rLen = HtoNL((uint32_t)len);
    wqePtr->data.leKey = HtoNL((lKey & (~ROCE_1825_WQE_NEXT_SGE_INVALID)) | ROCE_1825_WQE_NEXT_SGE_INVALID);
}

// Pre-mark the next WQEBB owner byte as invalid so the hardware stops there until the following WQE is posted.
__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::WriteInvalidWqebb(
    __gm__ uint8_t* sqAddr, uint32_t sqHead, uint32_t sqDepth)
{
    __gm__ RoceWqeCtrlSeg* ctrl = (__gm__ RoceWqeCtrlSeg*)sqAddr;
    ctrl->ownerSl = ((sqHead & sqDepth) == 0) ? 0xff : 0x7f;
    CacheWriteThrough<uint8_t>(sqAddr, 1);
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::MakeWqe(
    __gm__ ChannelEntity* chnlPtr, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType, uint32_t sqHead,
    uint32_t sqDepth, uint32_t enCqe, uint32_t fence)
{
    int32_t remoteIdx = HcommFindBufferIdx(chnlPtr->remoteBufferAddr, chnlPtr->remoteBufferNum, dst, len);
    if (remoteIdx < 0) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeWqe: failed with invalid remote buffer addr %llu.\n", dst);
        return HCOMM_FAILED;
    }
    int32_t localIdx = HcommFindBufferIdx(chnlPtr->localBufferAddr, chnlPtr->localBufferNum, src, len);
    if (localIdx < 0) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeWqe: failed with invalid local buffer addr %llu.\n", src);
        return HCOMM_FAILED;
    }
    __gm__ uint8_t* sqBaseAddr = (__gm__ uint8_t*)(chnlPtr->sqContextAddr->contextInfo.roceSq.sqVa);
    __gm__ uint8_t* sqAddr = (__gm__ uint8_t*)(sqBaseAddr + (sqHead & (sqDepth - 1)) * HCOMM_ROCE_WQEBB_SIZE);
    GlobalTensor<uint32_t> sqGlobal;
    sqGlobal.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(sqAddr));

    __ubuf__ RoceWqeEntry* wqePtr = (__ubuf__ RoceWqeEntry*)(wqeUB_.GetPhyAddr());
    uint32_t rKey = chnlPtr->remoteBufferAddr[remoteIdx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    uint32_t lKey = chnlPtr->localBufferAddr[localIdx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    __gm__ uint8_t* sqAddrNext = (__gm__ uint8_t*)(sqBaseAddr + ((sqHead + 1) & (sqDepth - 1)) * HCOMM_ROCE_WQEBB_SIZE);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    FillCtrlSeg(wqePtr, sqHead, sqDepth, enCqe);
    FillTaskSeg(wqePtr, dst, len, opType, rKey, lKey, fence);
    FillDataSeg(wqePtr, src, len, lKey);
    WriteInvalidWqebb(sqAddrNext, (sqHead + 1), sqDepth);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    Mutex::Lock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);
    DataCopy(sqGlobal, wqeUB_, sizeof(RoceWqeEntry) / sizeof(uint32_t));
    Mutex::Unlock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);

    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: set wqe to qp ok.\n");
    return HCOMM_SUCCESS;
}

__aicore__ inline bool HcommImpl<COMM_PROTOCOL_ROCE>::CheckChannelParam(__gm__ ChannelEntity* chnlPtr)
{
#if defined(ASCENDC_DEBUG)
    if (chnlPtr == nullptr) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm CheckChannelParam failed: chnlPtr is nullptr.\n");
        return false;
    }
    if (chnlPtr->sqNum == 0 || chnlPtr->cqNum == 0 || chnlPtr->sqContextAddr == 0 || chnlPtr->cqContextAddr == 0) {
        HCOMM_KERNEL_LOG(
            KERNEL_ERROR,
            "Hcomm CheckChannelParam failed: sqNum = %u cqNum = %u sqContextAddr = %llu  cqContextAddr = %llu\n",
            chnlPtr->sqNum, chnlPtr->cqNum, chnlPtr->sqContextAddr, chnlPtr->cqContextAddr);
        return false;
    }
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t wqeSize = chnlPtr->sqContextAddr[0].contextInfo.roceSq.wqeSize;
    uint32_t cqeSize = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqeSize;
    if (chnlPtr->protocol != COMM_PROTOCOL_ROCE || sqDepth == 0 || cqDepth == 0 ||
        chnlPtr->sqContextAddr[0].contextInfo.roceSq.sqVa == 0 || wqeSize != HCOMM_ROCE_WQEBB_SIZE || sqDepth <= 1U ||
        (sqDepth & (sqDepth - 1U)) != 0 || chnlPtr->sqContextAddr[0].contextInfo.roceSq.dbSwVa == 0 ||
        chnlPtr->sqContextAddr[0].contextInfo.roceSq.dbHwVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.dbSwVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.dbHwVa == 0 || cqeSize < sizeof(RoceCqeEntry) ||
        (cqDepth & (cqDepth - 1U)) != 0) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm CheckChannelParam failed: invalid SQ/CQ context.\n");
        return false;
    }
#endif
    return true;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::PostSend(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType)
{
    (void)commitPipe;
    (void)reqPipe;
    if (dst == 0 || src == 0 || len >= ROCE_1825_WQE_MAX_DATA_LEN) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend failed: dst = %llu, src = %llu, len = %llu.\n", dst, src, len);
        return HCOMM_FAILED;
    }
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    if (!CheckChannelParam(chnlPtr)) {
        return HCOMM_FAILED;
    }
    uint32_t sqHead = chnlPtr->sqHead;
    uint32_t sqTail = chnlPtr->sqTail;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm PostSend: opType = %u, sqHead = %u, sqTail = %u, sqDepth = %u.\n", opType, sqHead, sqTail,
        sqDepth);
    uint32_t outstanding = sqHead - sqTail;
    if (outstanding >= sqDepth || sqDepth - outstanding <= HCOMM_POLL_CQ_THRESHOLD) {
        HCOMM_KERNEL_LOG(
            KERNEL_INFO, "Hcomm PostSend: RoCE SQ overflow sqHead=%u sqTail=%u sqDepth=%u\n", sqHead, sqTail, sqDepth);
        if (PollCq<true>(chnlPtr, chnlPtr->cqHead, cqeUB_, HCOMM_POLL_CQ_THRESHOLD) != HCOMM_SUCCESS) {
            HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: RoCE SQ overflow, PollCq failed.\n");
            return HCOMM_FAILED;
        }
    }
    // Keep one extra slot free so the next-slot invalid marker cannot overwrite the oldest in-flight WQE.
    if (sqHead - chnlPtr->sqTail >= sqDepth - 1U) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: RoCE SQ has no free WQE.\n");
        return HCOMM_FAILED;
    }

    if (MakeWqe(chnlPtr, dst, src, len, opType, sqHead, sqDepth, config.cqe, config.fence) != HCOMM_SUCCESS) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: MakeWqe failed.\n");
        return HCOMM_FAILED;
    }

    uint32_t cqHead = chnlPtr->cqHead;
    if constexpr (config.cqe != 0) {
        cqHead++;
        chnlPtr->cqHead = cqHead;
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: update CQ PI cqHead = %u\n", cqHead);
    }
    sqHead++;
    chnlPtr->sqHead = sqHead;
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: update SQ PI sqHead = %u\n", sqHead);

    if constexpr (commit) {
        KnockDoorBell(chnlPtr, sqHead, cqeUB_);
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: KnockDoorBell ok.\n");
    }
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, config>(
        channel, dst, src, len, static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::WRITE));
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return PostSend<commit, commitPipe, reqPipe, config>(
        channel, src, dst, len, static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::READ));
}

template <typename T, bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteValueNbi(ChannelHandle channel, GM_ADDR dst, T value)
{
    (void)commit;
    (void)commitPipe;
    (void)reqPipe;
    (void)config;
    (void)channel;
    (void)dst;
    (void)value;
    HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm ROCE WriteValueNbi is not supported.");
    return HCOMM_FAILED;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
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
    HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm ROCE WriteWithNotifyNbi is not supported.");
    return HCOMM_FAILED;
}

__aicore__ inline uint64_t HcommImpl<COMM_PROTOCOL_ROCE>::GetDbValue(uint32_t sqHead, uint32_t qpn, uint64_t vendor)
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
    dbEntry.dw0.bs.sgidIdx = ROCE_1825_SQ_DB_SGIT_IDX;
    dbEntry.dw0.bs.type = ROCE_1825_SQ_DB_TYPE;
    dbEntry.dw0.bs.mtuShift =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_MTUSHIFT_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    dbEntry.dw0.bs.cos =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_COS_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    dbEntry.dw0.bs.xrcVld = 0;
    return dbEntry.dw0.value;
}

__aicore__ inline void HcommImpl<COMM_PROTOCOL_ROCE>::KnockDoorBell(
    __gm__ ChannelEntity* chnlPtr, uint32_t sqHead, LocalTensor<uint32_t>& cqeItem)
{
    __gm__ uint32_t* dbSwAddr = reinterpret_cast<__gm__ uint32_t*>(chnlPtr->sqContextAddr->contextInfo.roceSq.dbSwVa);
    __gm__ uint64_t* dbHwAddr = reinterpret_cast<__gm__ uint64_t*>(chnlPtr->sqContextAddr->contextInfo.roceSq.dbHwVa);

    uint64_t dbValue = GetDbValue(
        sqHead, chnlPtr->sqContextAddr->contextInfo.roceSq.qpn,
        chnlPtr->sqContextAddr->contextInfo.roceSq.dbVendorSpecified);
    uint64_t dbFinalVal =
        dbValue | ((((uint64_t)(sqHead) >> ROCE_1825_SQ_DB_PI_HIGH_SHIFT) & 0xffULL) << ROCE_1825_SQ_DB_PI_FIELD_SHIFT);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: dbValue = %llu, dbFinalVal = %llu\n", dbValue, dbFinalVal);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    st_dev(HtoNL(sqHead), dbSwAddr, 0);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: write sw db ok, swDbVal = %u\n", sqHead);
    st_dev(dbFinalVal, dbHwAddr, 0);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: write hw db ok, hwDbVal = %llu\n", dbFinalVal);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    uint32_t cqHead = chnlPtr->cqHead;
    uint32_t cqTail = chnlPtr->cqTail;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cqOutstanding = cqHead - cqTail;
    if (cqOutstanding >= cqDepth || (cqOutstanding != 0 && cqDepth - cqOutstanding <= HCOMM_POLL_CQ_THRESHOLD)) {
        uint32_t pollCount = cqOutstanding < HCOMM_NUM_CQE_PER_POLL_CQ ? cqOutstanding : HCOMM_NUM_CQE_PER_POLL_CQ;
        uint32_t idx = cqTail + pollCount;
        HCOMM_KERNEL_LOG(
            KERNEL_INFO,
            "Hcomm KnockDoorBell: cq overflow sqHead=%u cqHead=%u cqTail=%u idx=%u sqDepth=%u cqDepth=%u\n", sqHead,
            cqHead, cqTail, idx, sqDepth, cqDepth);
        if (PollCq(chnlPtr, idx, cqeItem) != HCOMM_SUCCESS) {
            HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm KnockDoorBell: PollCq failed.\n");
            // The doorbell has already been rung. Keep the submission successful and let Drain report
            // completion errors; treating this as a submission failure could cause callers to retry
            // and duplicate an in-flight request.
        }
    }
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Commit(ChannelHandle channel)
{
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    // PostSend updates the software PI; the hardware head is not a staging location for deferred posts.
    uint32_t sqHead = chnlPtr->sqHead;
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: sqHead = %u\n", sqHead);

    KnockDoorBell(chnlPtr, sqHead, cqeUB_);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Exit ok.\n");
    return HCOMM_SUCCESS;
}

__aicore__ inline bool HcommImpl<COMM_PROTOCOL_ROCE>::CheckCqeOwner(
    __ubuf__ RoceCqeEntry* cqePtr, uint32_t cqTail, uint32_t depth)
{
    uint32_t curOwner = ((cqePtr->ownerIdQpn & (1U << ROCE_1825_CQE_OWNER_SHIFT)) != 0);
    uint32_t expectOwner = (uint32_t)(((cqTail & depth) == 0));
    return (expectOwner ^ curOwner) != 0;
}

template <bool sqSafeMode>
__aicore__ inline bool HcommImpl<COMM_PROTOCOL_ROCE>::EnContinue(
    uint32_t expectIdx, uint32_t cqTail, uint32_t sqHead, uint32_t sqTail, uint32_t sqDepth, uint32_t threshold)
{
    bool isContinue = false;
    if constexpr (sqSafeMode) {
        uint32_t outstanding = sqHead - sqTail;
        isContinue = (outstanding >= sqDepth || sqDepth - outstanding <= threshold) && (cqTail != expectIdx);
    } else {
        isContinue = (cqTail != expectIdx);
    }
    return isContinue;
}

template <bool sqSafeMode>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::PollCq(
    __gm__ ChannelEntity* chnlPtr, uint32_t expectIdx, LocalTensor<uint32_t>& cqeItem, uint32_t threshold)
{
    if (expectIdx == chnlPtr->cqTail) {
        return HCOMM_SUCCESS;
    }
    auto cqContextInfo = chnlPtr->cqContextAddr[0].contextInfo;
    uint32_t cqeSize = cqContextInfo.roceCq.cqeSize;
    uint32_t cqDepth = cqContextInfo.roceCq.cqDepth;
    uint32_t cqTail = chnlPtr->cqTail;
    uint32_t sqTail = chnlPtr->sqTail;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    HCOMM_KERNEL_LOG(
        KERNEL_INFO, "Hcomm PollCq: cqeSize = %u cqDepth = %u cqTail= %u expectIdx = %u\n", cqeSize, cqDepth, cqTail,
        expectIdx);
    __ubuf__ RoceCqeEntry* cqePtr = (__ubuf__ RoceCqeEntry*)(cqeItem.GetPhyAddr());
    __gm__ uint8_t* cqBaseBuf = (__gm__ uint8_t*)(cqContextInfo.roceCq.cqVa);
    AscendC::GlobalTensor<uint32_t> cqeGlobalTensor;

    while (true) {
        if (!EnContinue<sqSafeMode>(expectIdx, cqTail, chnlPtr->sqHead, sqTail, sqDepth, threshold)) {
            break;
        }
        __gm__ uint8_t* cqeAddr = (__gm__ uint8_t*)(cqBaseBuf + cqeSize * (cqTail & (cqDepth - 1)));
        cqeGlobalTensor.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(cqeAddr));
        uint32_t loop = 0;
        uint32_t cqeType = ROCE_1825_CQE_OPTYPE_INVALID;
        for (; loop < HCOMM_POLLCQ_MAX_RETRY_TIMES; loop++) {
            Mutex::Lock<PIPE_MTE2>(HCOMM_ROCE_MUTEX_ID);
            DataCopy(cqeItem, cqeGlobalTensor, sizeof(RoceCqeEntry) / sizeof(uint32_t));
            Mutex::Unlock<PIPE_MTE2>(HCOMM_ROCE_MUTEX_ID);
            Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
            cqeType = (cqePtr->opSrWqebb >> ROCE_1825_CQE_OPCODE_SHIFT) & ROCE_1825_CQE_OPCODE_MASK;
            uint32_t cqeQpn = cqePtr->ownerIdQpn & 0xfffffU;
            uint32_t expectedQpn = chnlPtr->sqContextAddr[0].contextInfo.roceSq.qpn & 0xfffffU;
            bool cqeReady = cqeType != ROCE_1825_CQE_OPTYPE_INVALID && CheckCqeOwner(cqePtr, cqTail, cqDepth) &&
                            cqeQpn == expectedQpn;
            uint32_t advance = cqeReady ? static_cast<uint16_t>(cqePtr->wqeCounter - static_cast<uint16_t>(sqTail)) : 0;
            Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
            if (cqeReady) {
                if (advance >= chnlPtr->sqHead - sqTail) {
                    HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PollCq: invalid SQ WQE counter = %u.\n", cqePtr->wqeCounter);
                    return HCOMM_FAILED;
                }
                sqTail += (advance + 1);
                break;
            }
        }
        if (loop >= HCOMM_POLLCQ_MAX_RETRY_TIMES) {
            HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm PollCq: failed, overtime and exit.\n");
            return HCOMM_FAILED;
        }
        if (cqeType == ROCE_1825_CQE_OPTYPE_ERROR) {
            HCOMM_KERNEL_LOG(
                KERNEL_ERROR, "Hcomm PollCq: failed, syndrome = 0x%x, qpn = %u, cqTail = %u\n", cqePtr->syndrome,
                cqePtr->ownerIdQpn & 0xfffffU, cqTail);
            return HCOMM_FAILED;
        }
        cqTail += 1;
        HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: cqTail = %u\n", cqTail);
    }
    chnlPtr->cqTail = cqTail;
    chnlPtr->sqTail = sqTail;
    __gm__ uint32_t* dbSwAddr = reinterpret_cast<__gm__ uint32_t*>(cqContextInfo.roceCq.dbSwVa);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    st_dev(cqTail & ROCE_1825_CQE_UPDATE_CI_MASK, dbSwAddr, 0);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: knock cq doorbell ok, cqTail = %u, sqTail = %u\n", cqTail, sqTail);
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Drain(ChannelHandle channel)
{
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    if (!CheckChannelParam(chnlPtr)) {
        return HCOMM_FAILED;
    }
    uint32_t cqHead = chnlPtr->cqHead;
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: cqHead = %u\n", cqHead);
    if (PollCq(chnlPtr, cqHead, cqeUB_) != HCOMM_SUCCESS) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm Drain: PollCq failed.\n");
        return HCOMM_FAILED;
    }
    HCOMM_KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Exit ok.\n");
    return HCOMM_SUCCESS;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Lock(ChannelHandle channel)
{
    return HcommChannelLock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Unlock(ChannelHandle channel)
{
    return HcommChannelUnlock<COMM_PROTOCOL_ROCE>(channel);
}

__aicore__ inline UbcBatchHandle& HcommImpl<COMM_PROTOCOL_ROCE>::GetHandleRef(
    UbcBatchHandle& batchHandle, uint32_t channelIndex, GM_ADDR remoteAddr)
{
    (void)channelIndex;
    (void)remoteAddr;
    return batchHandle;
}

template <typename U>
__aicore__ inline UbcBatchHandle HcommImpl<COMM_PROTOCOL_ROCE>::MakeBatchHandle(
    ChannelHandle channel, const LocalTensor<U>& buff, uint32_t buffLen, GM_ADDR remoteAddr, GM_ADDR localAddr)
{
    UbcBatchHandle batchHandle{};
    __gm__ ChannelEntity* chnlPtr = reinterpret_cast<__gm__ ChannelEntity*>(channel);
    if (!CheckChannelParam(chnlPtr) || buffLen < HCOMM_ROCE_WQEBB_SIZE || buff.GetSize() * sizeof(U) < buffLen) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeBatchHandle: failed with invalid channel or buffer.\n");
        return batchHandle;
    }
    int32_t remoteIdx = HcommFindBufferIdx(chnlPtr->remoteBufferAddr, chnlPtr->remoteBufferNum, remoteAddr, 1);
    if (remoteIdx < 0) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeBatchHandle: failed with invalid remote addr %llu.\n", remoteAddr);
        return batchHandle;
    }
    int32_t localIdx = HcommFindBufferIdx(chnlPtr->localBufferAddr, chnlPtr->localBufferNum, localAddr, 1);
    if (localIdx < 0) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeBatchHandle: failed with invalid local addr %llu.\n", localAddr);
        return batchHandle;
    }
    auto sqCtx = chnlPtr->sqContextAddr[HCOMM_DEFAULT_QP_IDX];
    auto cqCtx = chnlPtr->cqContextAddr[HCOMM_DEFAULT_QP_IDX];
    LocalTensor<uint32_t> wqeBuffer = buff.template ReinterpretCast<uint32_t>();
    batchHandle.channelHandle = channel;
    batchHandle.sqContext = sqCtx;
    batchHandle.cqContext = cqCtx;
    batchHandle.cursor.sqHead = chnlPtr->sqHead;
    batchHandle.cursor.sqTail = chnlPtr->sqTail;
    batchHandle.cursor.cqHead = chnlPtr->cqHead;
    batchHandle.cursor.cqTail = chnlPtr->cqTail;
    batchHandle.cursor.preSqCnt = 0U;
    batchHandle.buffer.buffer = wqeBuffer;
    batchHandle.buffer.bufferCapacity = buffLen / HCOMM_ROCE_WQEBB_SIZE;
    batchHandle.mrKey.lKey = chnlPtr->localBufferAddr[localIdx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    batchHandle.mrKey.rKey = chnlPtr->remoteBufferAddr[remoteIdx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    const uint32_t remoteKeyBe = HtoNL(batchHandle.mrKey.rKey);
    const uint32_t localKeyBe = HtoNL(batchHandle.mrKey.lKey & 0xffffU);
    const uint32_t dataKeyBe =
        HtoNL((batchHandle.mrKey.lKey & (~ROCE_1825_WQE_NEXT_SGE_INVALID)) | ROCE_1825_WQE_NEXT_SGE_INVALID);
    batchHandle.mrKey.keyWord =
        static_cast<uint64_t>(remoteKeyBe) | (static_cast<uint64_t>(localKeyBe) << ROCE_1825_WQE_WORD_BITS);
    batchHandle.mrKey.dataKeyHighWord = static_cast<uint64_t>(dataKeyBe) << ROCE_1825_WQE_WORD_BITS;

    return batchHandle;
}

template <HCOMM_ROCE_OP_TYPE opType, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::BatchPostSend(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "BatchPostSend only supports cqe values 0 and 1.");
    auto* chnlPtr = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    if (batchHandle.channelHandle == 0U || dst == 0 || src == 0 || len >= ROCE_1825_WQE_MAX_DATA_LEN ||
        !CheckChannelParam(chnlPtr)) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchPostSend: failed with invalid handle or request.\n");
        return HCOMM_FAILED;
    }

    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    if (preSqCnt >= batchHandle.buffer.bufferCapacity) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchPostSend: failed with insufficient buffer\n");
        return HCOMM_FAILED;
    }

    constexpr uint32_t taskCtrlBe = HtoNL(
        ((config.cqe & 1U) << ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT) |
        ((config.fence & 1U) << ROCE_1825_WQE_TASK_FENCE_SHIFT) |
        ((static_cast<uint32_t>(opType) & 0x1fU) << ROCE_1825_WQE_TASK_OPTYPE_SHIFT));
    constexpr uint32_t dw3Be = opType == HCOMM_ROCE_OP_TYPE::READ ? HtoNL(ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN) : 0U;
    constexpr uint32_t clPiBe = HtoNL(ROCE_1825_WQE_CMP_TASK_LEN1 << ROCE_1825_WQE_CMP_TASK_LEN_SHIFT);
    constexpr uint16_t singleSgeBdslBe = HtoNS(static_cast<uint16_t>(
        static_cast<uint32_t>(1) << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    constexpr uint8_t dfTsl = static_cast<uint8_t>(
        (config.cqe == 1 ? (1U << ROCE_1825_WQE_SQ_SIGNAL_SHIFT) : 0U) | ROCE_1825_WQE_SQ_VA_VALUE |
        (sizeof(RoceWqeTaskSeg) / ROCE_1825_WQE_TASK_SEG_ALIGN));
    constexpr uint64_t dw3Word = static_cast<uint64_t>(dw3Be) << ROCE_1825_WQE_WORD_BITS;
    constexpr uint64_t ctrlBaseWord = (static_cast<uint64_t>(dfTsl) << ROCE_1825_WQE_BYTE_BITS) |
                                      (static_cast<uint64_t>(clPiBe) << ROCE_1825_WQE_WORD_BITS) |
                                      ROCE_1825_WQE_CTRL_VALUE |
                                      (static_cast<uint64_t>(singleSgeBdslBe) << ROCE_1825_WQE_HALFWORD_BITS);
    LocalTensor<uint32_t> currentWqe = batchHandle.buffer.buffer[preSqCnt * HCOMM_ROCE_WQEBB_U32_NUM];
    __ubuf__ uint64_t* wqePtr = reinterpret_cast<__ubuf__ uint64_t*>(currentWqe.GetPhyAddr());
    const uint64_t dstAddrBe = HtoNLL((uint64_t)dst);
    const uint64_t srcAddrBe = HtoNLL((uint64_t)src);
    const uint32_t lenBe = HtoNL(len);
    uint32_t sqDepth = batchHandle.sqContext.contextInfo.roceSq.depth;
    uint32_t baseHead = batchHandle.cursor.preSqCnt + batchHandle.cursor.sqHead;
    uint64_t ownerWord = static_cast<uint64_t>((baseHead & sqDepth) != 0U) << ROCE_1825_WQE_OWNER_SHIFT;
    uint64_t ssnWord = static_cast<uint64_t>(baseHead & ROCE_1825_WQE_SSN_MASK)
                       << (ROCE_1825_WQE_HALFWORD_BITS + ROCE_1825_WQE_SSN_BE_SHIFT);

    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    // RoceWqeEntry is laid out as eight 64-bit words. Values that are sent to the NIC
    // remain in network byte order, matching FillCtrlSeg/FillTaskSeg/FillDataSeg.
    wqePtr[0] = ctrlBaseWord | ownerWord | ssnWord;
    wqePtr[1] = 0;
    wqePtr[2] = static_cast<uint64_t>(taskCtrlBe) | (static_cast<uint64_t>(lenBe) << ROCE_1825_WQE_WORD_BITS);
    wqePtr[3] = dw3Word;
    wqePtr[4] = dstAddrBe;
    wqePtr[5] = batchHandle.mrKey.keyWord;
    wqePtr[6] = srcAddrBe;
    wqePtr[7] = static_cast<uint64_t>(lenBe) | batchHandle.mrKey.dataKeyHighWord;
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    batchHandle.cursor.preSqCnt = preSqCnt + 1U;
    if constexpr (config.cqe == 1) {
        batchHandle.cursor.cqHead += 1U;
    }
    return HCOMM_SUCCESS;
}

template <HCOMM_ROCE_OP_TYPE opType, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::BatchPostSend(
    UbcBatchHandle& batchHandle, GM_ADDR remoteAddr, const BufDesc* localDescs, uint32_t segNum)
{
    static_assert(config.cqe == 0 || config.cqe == 1, "BatchPostSend only supports cqe values 0 and 1.");
    auto* chnlPtr = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    if (batchHandle.channelHandle == 0U || remoteAddr == 0 || localDescs == nullptr || segNum == 0 ||
        !CheckChannelParam(chnlPtr)) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchPostSend: failed with invalid handle or request.\n");
        return HCOMM_FAILED;
    }

    uint64_t totalLen = 0U;
    for (uint32_t i = 0U; i < segNum; i++) {
        totalLen += localDescs[i].len;
    }
    if (totalLen >= ROCE_1825_WQE_MAX_DATA_LEN) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchPostSend: failed with invalid message length\n");
        return HCOMM_FAILED;
    }

    constexpr uint32_t publicSegSize = sizeof(RoceWqeCtrlSeg) + sizeof(RoceWqeTaskSeg);
    const uint32_t wqeBytes = publicSegSize + segNum * sizeof(RoceWqeDataSeg);
    uint32_t wqebbNum = (wqeBytes + HCOMM_ROCE_WQEBB_SIZE - 1U) / HCOMM_ROCE_WQEBB_SIZE;
    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    if (preSqCnt >= batchHandle.buffer.bufferCapacity || wqebbNum > batchHandle.buffer.bufferCapacity - preSqCnt) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchPostSend: failed with insufficient buffer\n");
        return HCOMM_FAILED;
    }
    constexpr uint32_t taskCtrlBe = HtoNL(
        ((config.cqe & 1U) << ROCE_1825_WQE_TASK_CQE_SIGNAL_SHIFT) |
        ((config.fence & 1U) << ROCE_1825_WQE_TASK_FENCE_SHIFT) |
        ((static_cast<uint32_t>(opType) & 0x1fU) << ROCE_1825_WQE_TASK_OPTYPE_SHIFT));
    constexpr uint32_t dw3Be = opType == HCOMM_ROCE_OP_TYPE::READ ? HtoNL(ROCE_1825_WQE_RDMA_READ_LAST_EXT_LEN) : 0U;
    constexpr uint32_t clPiBe = HtoNL(ROCE_1825_WQE_CMP_TASK_LEN1 << ROCE_1825_WQE_CMP_TASK_LEN_SHIFT);
    constexpr uint8_t dfTsl = static_cast<uint8_t>(
        (config.cqe == 1 ? (1U << ROCE_1825_WQE_SQ_SIGNAL_SHIFT) : 0U) | ROCE_1825_WQE_SQ_VA_VALUE |
        (sizeof(RoceWqeTaskSeg) / ROCE_1825_WQE_TASK_SEG_ALIGN));
    constexpr uint64_t dw3Word = static_cast<uint64_t>(dw3Be) << ROCE_1825_WQE_WORD_BITS;
    constexpr uint64_t ctrlBaseWord = (static_cast<uint64_t>(dfTsl) << ROCE_1825_WQE_BYTE_BITS) |
                                      (static_cast<uint64_t>(clPiBe) << ROCE_1825_WQE_WORD_BITS) |
                                      ROCE_1825_WQE_CTRL_VALUE;

    LocalTensor<uint32_t> currentWqe = batchHandle.buffer.buffer[preSqCnt * HCOMM_ROCE_WQEBB_U32_NUM];
    __ubuf__ uint64_t* wqe = reinterpret_cast<__ubuf__ uint64_t*>(currentWqe.GetPhyAddr());
    uint32_t sqDepth = batchHandle.sqContext.contextInfo.roceSq.depth;
    uint32_t baseHead = batchHandle.cursor.preSqCnt + batchHandle.cursor.sqHead;

    const uint64_t remoteAddrBe = HtoNLL((uint64_t)remoteAddr);
    const uint16_t wfBdslBe =
        HtoNS(static_cast<uint16_t>(segNum << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    const uint32_t totalLenBe = HtoNL(static_cast<uint32_t>(totalLen));
    const uint32_t dataKeyBe = static_cast<uint32_t>(batchHandle.mrKey.dataKeyHighWord >> ROCE_1825_WQE_WORD_BITS);

    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    wqe[0] = ctrlBaseWord | (static_cast<uint64_t>(wfBdslBe) << ROCE_1825_WQE_HALFWORD_BITS) |
             static_cast<uint64_t>((baseHead & sqDepth) != 0U) << ROCE_1825_WQE_OWNER_SHIFT |
             static_cast<uint64_t>(baseHead & ROCE_1825_WQE_SSN_MASK)
                 << (ROCE_1825_WQE_HALFWORD_BITS + ROCE_1825_WQE_SSN_BE_SHIFT);
    wqe[1] = 0;
    wqe[2] = static_cast<uint64_t>(taskCtrlBe) | (static_cast<uint64_t>(totalLenBe) << ROCE_1825_WQE_WORD_BITS);
    wqe[3] = dw3Word;
    wqe[4] = remoteAddrBe;
    wqe[5] = batchHandle.mrKey.keyWord;
    wqe += publicSegSize / HCOMM_ROCE_WQEBB_U64_SIZE;
    for (uint32_t i = 0U; i < segNum; i++) {
        wqe[0] = HtoNLL((uint64_t)localDescs[i].addr);
        constexpr uint32_t lastSgeBeMask = ROCE_1825_WQE_NEXT_SGE_INVALID >> 24;
        const uint32_t segKeyBe = i + 1U == segNum ? dataKeyBe : dataKeyBe & ~lastSgeBeMask;
        wqe[1] = static_cast<uint64_t>(HtoNL(localDescs[i].len)) |
                 (static_cast<uint64_t>(segKeyBe) << ROCE_1825_WQE_WORD_BITS);
        wqe += sizeof(RoceWqeDataSeg) / HCOMM_ROCE_WQEBB_U64_SIZE;
    }
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    batchHandle.cursor.preSqCnt = preSqCnt + wqebbNum;
    if constexpr (config.cqe == 1) {
        batchHandle.cursor.cqHead += 1U;
    }
    return HCOMM_SUCCESS;
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return BatchPostSend<HCOMM_ROCE_OP_TYPE::WRITE, config>(batchHandle, dst, src, len);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::ReadNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, GM_ADDR src, uint32_t len)
{
    return BatchPostSend<HCOMM_ROCE_OP_TYPE::READ, config>(batchHandle, src, dst, len);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteNbi(
    UbcBatchHandle& batchHandle, GM_ADDR dst, const BufDesc* srcDescs, uint32_t srcNum)
{
    return BatchPostSend<HCOMM_ROCE_OP_TYPE::WRITE, config>(batchHandle, dst, srcDescs, srcNum);
}

template <auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::ReadNbi(
    UbcBatchHandle& batchHandle, const BufDesc* dstDescs, uint32_t dstNum, GM_ADDR src)
{
    return BatchPostSend<HCOMM_ROCE_OP_TYPE::READ, config>(batchHandle, src, dstDescs, dstNum);
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::BatchCommit(UbcBatchHandle& batchHandle)
{
    uint32_t preSqCnt = batchHandle.cursor.preSqCnt;
    if (preSqCnt == 0 || preSqCnt > batchHandle.buffer.bufferCapacity) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchCommit: failed with invalid WQEBB count\n");
        return HCOMM_FAILED;
    }
    auto* chnlPtr = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    if (!CheckChannelParam(chnlPtr)) {
        return HCOMM_FAILED;
    }
    uint32_t sqDepth = batchHandle.sqContext.contextInfo.roceSq.depth;
    uint32_t sqTail = chnlPtr->sqTail;
    uint32_t newSqHead = batchHandle.cursor.sqHead + preSqCnt;
    // Keep one extra slot free for the next-slot invalid marker.
    if (newSqHead - sqTail > sqDepth - 1U) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm BatchCommit: failed because batch num larger than SQ capacity\n");
        return HCOMM_FAILED;
    }
    uint32_t sqHead = batchHandle.cursor.sqHead;
    uint64_t sqBaseAddr = batchHandle.sqContext.contextInfo.roceSq.sqVa;
    __gm__ uint8_t* sqAddrNext = (__gm__ uint8_t*)(sqBaseAddr + ((newSqHead) & (sqDepth - 1)) * HCOMM_ROCE_WQEBB_SIZE);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    WriteInvalidWqebb(sqAddrNext, newSqHead, sqDepth);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    uint32_t startSlot = sqHead % sqDepth;
    uint32_t tailWqebbCount = sqDepth - startSlot;
    uint32_t firstWqebbCount = preSqCnt < tailWqebbCount ? preSqCnt : tailWqebbCount;
    uint32_t secondWqebbCount = preSqCnt - firstWqebbCount;
    __gm__ uint8_t* firstWqeAddr =
        reinterpret_cast<__gm__ uint8_t*>(sqBaseAddr + static_cast<uint64_t>(startSlot) * HCOMM_ROCE_WQEBB_SIZE);
    GlobalTensor<uint32_t> firstDst;
    firstDst.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(firstWqeAddr));

    Mutex::Lock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);
    // Copy the first segment from startSlot to the end of the circular SQ, or the whole batch if it does not wrap.
    DataCopy(firstDst, batchHandle.buffer.buffer, firstWqebbCount * HCOMM_ROCE_WQEBB_U32_NUM);
    if (secondWqebbCount != 0) {
        GlobalTensor<uint32_t> secondDst;
        secondDst.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(sqBaseAddr));
        LocalTensor<uint32_t> secondSrc = batchHandle.buffer.buffer[firstWqebbCount * HCOMM_ROCE_WQEBB_U32_NUM];
        // The batch crosses the SQ boundary; copy the remaining WQEBBs to the beginning of the circular SQ.
        DataCopy(secondDst, secondSrc, secondWqebbCount * HCOMM_ROCE_WQEBB_U32_NUM);
    }
    Mutex::Unlock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);

    chnlPtr->sqHead = newSqHead;
    chnlPtr->cqHead = batchHandle.cursor.cqHead;
    // The WQEs have been copied to SQ; their UB storage can now be reused for CQ polling.
    KnockDoorBell(chnlPtr, newSqHead, batchHandle.buffer.buffer);
    batchHandle.cursor.sqHead = newSqHead;
    batchHandle.cursor.preSqCnt = 0;
    return HCOMM_SUCCESS;
}

__aicore__ inline uint32_t HcommImpl<COMM_PROTOCOL_ROCE>::PollBatchCq(UbcBatchHandle& batchHandle, uint32_t expectIdx)
{
    auto* chnlPtr = reinterpret_cast<__gm__ ChannelEntity*>(batchHandle.channelHandle);
    uint32_t ret = PollCq(chnlPtr, expectIdx, batchHandle.buffer.buffer);
    if (ret != HCOMM_SUCCESS) {
        return ret;
    }
    batchHandle.cursor.cqTail = chnlPtr->cqTail;
    batchHandle.cursor.sqTail = chnlPtr->sqTail;
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Drain(UbcBatchHandle& batchHandle)
{
    (void)pipe;
    static_assert(sizeof(RoceCqeEntry) <= HCOMM_ROCE_WQEBB_SIZE, "Batch Drain CQE scratch space must fit in one WQEBB");
    if (batchHandle.channelHandle == 0U || batchHandle.buffer.bufferCapacity == 0U ||
        batchHandle.cursor.preSqCnt != 0U) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm Batch Drain failed with invalid batch handle\n");
        return HCOMM_FAILED;
    }

    uint32_t ret = PollBatchCq(batchHandle, batchHandle.cursor.cqHead);
    if (ret != HCOMM_SUCCESS) {
        HCOMM_KERNEL_LOG(KERNEL_ERROR, "Hcomm RoCE Drain by batch handle failed pollRet=%u \n", ret);
        return ret;
    }
    return HCOMM_SUCCESS;
}
} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H
#endif
