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
    cqeUB_ = wqeUB_[ROCE_CQE_POS];
    return HCOMM_SUCCESS;
}

template <typename T>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Init(const LocalTensor<T>& buff, uint32_t len)
{
    if (len < HCOMM_UB_BUF_SIZE || buff.GetSize() < HCOMM_UB_BUF_SIZE) {
        return HCOMM_FAILED;
    }
    wqeUB_ = buff.template ReinterpretCast<uint8_t>();
    cqeUB_ = wqeUB_[ROCE_CQE_POS];
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
        KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeWqe: failed with invalid remote buffer addr %llu.\n", dst);
        return HCOMM_FAILED;
    }
    int32_t localIdx = HcommFindBufferIdx(chnlPtr->localBufferAddr, chnlPtr->localBufferNum, src, len);
    if (localIdx < 0) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm MakeWqe: failed with invalid local buffer addr %llu.\n", src);
        return HCOMM_FAILED;
    }
    __gm__ uint8_t* sqBaseAddr = (__gm__ uint8_t*)(chnlPtr->sqContextAddr->contextInfo.roceSq.sqVa);
    uint32_t wqeSize = chnlPtr->sqContextAddr->contextInfo.roceSq.wqeSize;
    KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: Get wqeSize from channelEntiry, wqeSize = %u.\n", wqeSize);
    __gm__ uint8_t* sqAddr = (__gm__ uint8_t*)(sqBaseAddr + (sqHead & (sqDepth - 1)) * wqeSize);
    GlobalTensor<uint8_t> sqGlobal;
    sqGlobal.SetGlobalBuffer(sqAddr);

    __ubuf__ RoceWqeEntry* wqePtr = (__ubuf__ RoceWqeEntry*)(wqeUB_.GetPhyAddr());
    uint32_t rKey = chnlPtr->remoteBufferAddr[remoteIdx].bufferInfo.rma.protectionInfo.memInfo.roce.rkey;
    uint32_t lKey = chnlPtr->localBufferAddr[localIdx].bufferInfo.rma.protectionInfo.memInfo.roce.lkey;
    __gm__ uint8_t* sqAddrNext = (__gm__ uint8_t*)(sqBaseAddr + ((sqHead + 1) & (sqDepth - 1)) * wqeSize);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    FillCtrlSeg(wqePtr, sqHead, sqDepth, enCqe);
    FillTaskSeg(wqePtr, dst, len, opType, rKey, lKey, fence);
    FillDataSeg(wqePtr, src, len, lKey);
    WriteInvalidWqebb(sqAddrNext, (sqHead + 1), sqDepth);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    Mutex::Lock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);
    DataCopy(sqGlobal, wqeUB_, sizeof(RoceWqeEntry));
    Mutex::Unlock<PIPE_MTE3>(HCOMM_ROCE_MUTEX_ID);

    KERNEL_LOG(KERNEL_INFO, "Hcomm MakeWqe: set wqe to qp ok.\n");
    return HCOMM_SUCCESS;
}

__aicore__ inline bool HcommImpl<COMM_PROTOCOL_ROCE>::CheckChannelParam(__gm__ ChannelEntity* chnlPtr)
{
    paramValid_ = false;
    if (chnlPtr == nullptr) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm CheckChannelParam failed: chnlPtr is nullptr.\n");
        return false;
    }
    if (chnlPtr->sqNum == 0 || chnlPtr->cqNum == 0 || chnlPtr->sqContextAddr == 0 || chnlPtr->cqContextAddr == 0) {
        KERNEL_LOG(
            KERNEL_ERROR,
            "Hcomm CheckChannelParam failed: sqNum = %u cqNum = %u sqContextAddr = %llu  cqContextAddr = %llu\n",
            chnlPtr->sqNum, chnlPtr->cqNum, chnlPtr->sqContextAddr, chnlPtr->cqContextAddr);
        return false;
    }
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    if (sqDepth == 0 || cqDepth == 0 || chnlPtr->sqContextAddr[0].contextInfo.roceSq.sqVa == 0 ||
        chnlPtr->sqContextAddr[0].contextInfo.roceSq.wqeSize == 0 ||
        chnlPtr->sqContextAddr[0].contextInfo.roceSq.dbSwVa == 0 ||
        chnlPtr->sqContextAddr[0].contextInfo.roceSq.dbHwVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.dbSwVa == 0 ||
        chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqeSize == 0) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm CheckChannelParam failed: invalid SQ/CQ context.\n");
        return false;
    }
    paramValid_ = true;
    return true;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::PostSend(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType)
{
    if (dst == 0 || src == 0 || len == 0) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: dst = %llu, src = %llu len = %llu.\n", dst, src, len);
        return HCOMM_FAILED;
    }
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    if (!paramValid_ && !CheckChannelParam(chnlPtr)) {
        return HCOMM_FAILED;
    }
    uint32_t sqHead = chnlPtr->sqHead;
    uint32_t sqTail = chnlPtr->sqTail;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm PostSend: opType = %u, sqHead = %u, sqTail = %u, sqDepth = %u.\n", opType, sqHead, sqTail,
        sqDepth);
    uint32_t outstanding = sqHead - sqTail;
    if (outstanding >= sqDepth || sqDepth - outstanding <= HCOMM_POLL_CQ_THRESHOLD) {
        KERNEL_LOG(
            KERNEL_INFO, "Hcomm PostSend: RoCE SQ overflow sqHead=%u sqTail=%u sqDepth=%u\n", sqHead, sqTail, sqDepth);
        if (PollCq<true>(chnlPtr, chnlPtr->cqHead, HCOMM_POLL_CQ_THRESHOLD) != HCOMM_SUCCESS) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: RoCE SQ overflow, PollCq failed.\n");
            return HCOMM_FAILED;
        }
    }
    if (sqHead - chnlPtr->sqTail >= sqDepth) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: RoCE SQ has no free WQE.\n");
        return HCOMM_FAILED;
    }

    if (MakeWqe(chnlPtr, dst, src, len, opType, sqHead, sqDepth, config.cqe, config.fence) != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: MakeWqe failed.\n");
        return HCOMM_FAILED;
    }

    uint32_t cqHead = chnlPtr->cqHead;
    if constexpr (config.cqe != 0) {
        cqHead++;
        chnlPtr->cqHead = cqHead;
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: update CQ PI cqHead = %u\n", cqHead);
    }
    sqHead++;
    chnlPtr->sqHead = sqHead;
    KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: update SQ PI sqHead = %u\n", sqHead);

    if constexpr (commit) {
        if (KnockDoorBell(chnlPtr, sqHead) != HCOMM_SUCCESS) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm PostSend: KnockDoorBell failed.\n");
            return HCOMM_FAILED;
        }
        KERNEL_LOG(KERNEL_INFO, "Hcomm PostSend: KnockDoorBell ok.\n");
    }
    return HCOMM_SUCCESS;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::WriteNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return PostSend<commit, commitPipe, reqPipe>(
        channel, dst, src, len, static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::WRITE));
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe, auto const& config>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::ReadNbi(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    (void)config;
    return PostSend<commit, commitPipe, reqPipe>(
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
    KERNEL_LOG(KERNEL_ERROR, "Hcomm ROCE WriteValueNbi is not supported.");
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
    KERNEL_LOG(KERNEL_ERROR, "Hcomm ROCE WriteWithNotifyNbi is not supported.");
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
    dbEntry.dw0.bs.pi = ((sqHead >> ROCE_1825_SQ_DB_PI_HIGH_SHIFT) & 0xffU);
    dbEntry.dw0.bs.sgidIdx = ROCE_1825_SQ_DB_SGIT_IDX;
    dbEntry.dw0.bs.type = ROCE_1825_SQ_DB_TYPE;
    dbEntry.dw0.bs.mtuShift =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_MTUSHIFT_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    dbEntry.dw0.bs.cos =
        static_cast<uint32_t>((vendor >> ROCE_1825_SQ_DB_VENDOR_COS_SHIFT) & ROCE_1825_SQ_DB_VENDOR_FIELD_MASK);
    dbEntry.dw0.bs.xrcVld = 0;
    return dbEntry.dw0.value;
}

__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::KnockDoorBell(__gm__ ChannelEntity* chnlPtr, uint32_t sqHead)
{
    __gm__ uint32_t* dbSwAddr = reinterpret_cast<__gm__ uint32_t*>(chnlPtr->sqContextAddr->contextInfo.roceSq.dbSwVa);
    __gm__ uint64_t* dbHwAddr = reinterpret_cast<__gm__ uint64_t*>(chnlPtr->sqContextAddr->contextInfo.roceSq.dbHwVa);

    uint64_t dbValue = GetDbValue(
        sqHead, chnlPtr->sqContextAddr->contextInfo.roceSq.qpn,
        chnlPtr->sqContextAddr->contextInfo.roceSq.dbVendorSpecified);
    uint64_t dbFinalVal =
        dbValue | ((((uint64_t)(sqHead) >> ROCE_1825_SQ_DB_PI_HIGH_SHIFT) & 0xffULL) << ROCE_1825_SQ_DB_PI_FIELD_SHIFT);
    KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: dbValue = %llu, dbFinalVal = %llu\n", dbValue, dbFinalVal);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    st_dev(HtoNL(sqHead), dbSwAddr, 0);
    KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: write sw db ok, swDbVal = %u\n", sqHead);
    st_dev(dbFinalVal, dbHwAddr, 0);
    KERNEL_LOG(KERNEL_INFO, "Hcomm KnockDoorBell: write hw db ok, hwDbVal = %llu\n", dbFinalVal);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);

    uint32_t cqHead = chnlPtr->cqHead;
    uint32_t cqTail = chnlPtr->cqTail;
    uint32_t cqDepth = chnlPtr->cqContextAddr[0].contextInfo.roceCq.cqDepth;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    uint32_t cqOutstanding = cqHead - cqTail;
    if (cqOutstanding >= cqDepth || (cqOutstanding != 0 && cqDepth - cqOutstanding <= HCOMM_POLL_CQ_THRESHOLD)) {
        uint32_t pollCount = cqOutstanding < HCOMM_NUM_CQE_PER_POLL_CQ ? cqOutstanding : HCOMM_NUM_CQE_PER_POLL_CQ;
        uint32_t idx = cqTail + pollCount;
        KERNEL_LOG(
            KERNEL_INFO,
            "Hcomm KnockDoorBell: cq overflow sqHead=%u cqHead=%u cqTail=%u idx=%u sqDepth=%u cqDepth=%u\n", sqHead,
            cqHead, cqTail, idx, sqDepth, cqDepth);
        if (PollCq(chnlPtr, idx) != HCOMM_SUCCESS) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm KnockDoorBell: PollCq failed.\n");
            return HCOMM_FAILED;
        }
    }
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Commit(ChannelHandle channel)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    // PostSend updates the software PI; the hardware head is not a staging location for deferred posts.
    uint32_t sqHead = chnlPtr->sqHead;
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: sqHead = %u\n", sqHead);

    if (KnockDoorBell(chnlPtr, sqHead) != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm Commit: KnockDoorBell failed.\n");
        return HCOMM_FAILED;
    }
    KERNEL_LOG(KERNEL_INFO, "Hcomm Commit: Exit ok.\n");
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
    __gm__ ChannelEntity* chnlPtr, uint32_t expectIdx, uint32_t threshold)
{
    if (expectIdx == 0) {
        return HCOMM_SUCCESS;
    }
    auto cqContextInfo = chnlPtr->cqContextAddr[0].contextInfo;
    uint32_t cqeSize = cqContextInfo.roceCq.cqeSize;
    uint32_t cqDepth = cqContextInfo.roceCq.cqDepth;
    uint32_t cqTail = chnlPtr->cqTail;
    uint32_t sqTail = chnlPtr->sqTail;
    uint32_t sqDepth = chnlPtr->sqContextAddr[0].contextInfo.roceSq.depth;
    KERNEL_LOG(
        KERNEL_INFO, "Hcomm PollCq: cqeSize = %u cqDepth = %u cqTail= %u expectIdx = %u\n", cqeSize, cqDepth, cqTail,
        expectIdx);
    __ubuf__ RoceCqeEntry* cqePtr = (__ubuf__ RoceCqeEntry*)(cqeUB_.GetPhyAddr());
    __gm__ uint8_t* cqBaseBuf = (__gm__ uint8_t*)(cqContextInfo.roceCq.cqVa);
    AscendC::GlobalTensor<uint8_t> cqeGlobalTensor;

    while (true) {
        if (!EnContinue<sqSafeMode>(expectIdx, cqTail, chnlPtr->sqHead, sqTail, sqDepth, threshold)) {
            break;
        }
        __gm__ uint8_t* cqeAddr = (__gm__ uint8_t*)(cqBaseBuf + cqeSize * (cqTail & (cqDepth - 1)));
        cqeGlobalTensor.SetGlobalBuffer(cqeAddr);
        uint32_t loop = 0;
        uint32_t cqeType = ROCE_1825_CQE_OPTYPE_INVALID;
        for (; loop < HCOMM_POLLCQ_MAX_RETRY_TIMES; loop++) {
            Mutex::Lock<PIPE_MTE2>(HCOMM_ROCE_MUTEX_ID);
            DataCopy(cqeUB_, cqeGlobalTensor, sizeof(RoceCqeEntry));
            Mutex::Unlock<PIPE_MTE2>(HCOMM_ROCE_MUTEX_ID);
#if defined(UT_TEST)
            cqTail = expectIdx - 1;
            sqTail = expectIdx;
            break;
#else
            Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
            cqeType = (cqePtr->opSrWqebb >> ROCE_1825_CQE_OPCODE_SHIFT) & ROCE_1825_CQE_OPCODE_MASK;
            uint32_t cqeQpn = cqePtr->ownerIdQpn & 0xfffffU;
            uint32_t expectedQpn = chnlPtr->sqContextAddr[0].contextInfo.roceSq.qpn & 0xfffffU;
            bool cqeReady = cqeType != ROCE_1825_CQE_OPTYPE_INVALID && CheckCqeOwner(cqePtr, cqTail, cqDepth) &&
                            cqeQpn == expectedQpn;
            uint32_t advance = cqeReady ? static_cast<uint16_t>(cqePtr->wqeCounter - static_cast<uint16_t>(sqTail)) : 0;
            Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
            if (cqeReady) {
                if (advance > chnlPtr->sqHead - sqTail) {
                    KERNEL_LOG(KERNEL_ERROR, "Hcomm PollCq: invalid SQ WQE counter = %u.\n", cqePtr->wqeCounter);
                    return HCOMM_FAILED;
                }
                sqTail += (advance + 1);
                break;
            }
#endif
        }
        if (loop >= HCOMM_POLLCQ_MAX_RETRY_TIMES) {
            KERNEL_LOG(KERNEL_ERROR, "Hcomm PollCq: failed, overtime and exit.\n");
            return HCOMM_FAILED;
        }
        if (cqeType == ROCE_1825_CQE_OPTYPE_ERROR) {
            KERNEL_LOG(
                KERNEL_ERROR, "Hcomm PollCq: failed, syndrome = 0x%x, qpn = %u, cqTail = %u\n", cqePtr->syndrome,
                cqePtr->ownerIdQpn & 0xfffffU, cqTail);
            return HCOMM_FAILED;
        }
        cqTail += 1;
        KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: cqTail = %u\n", cqTail);
    }
    chnlPtr->cqTail = cqTail;
    chnlPtr->sqTail = sqTail;
    __gm__ uint32_t* dbSwAddr = reinterpret_cast<__gm__ uint32_t*>(cqContextInfo.roceCq.dbSwVa);
    Mutex::Lock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    st_dev(cqTail & ROCE_1825_CQE_UPDATE_CI_MASK, dbSwAddr, 0);
    Mutex::Unlock<PIPE_S>(HCOMM_ROCE_MUTEX_ID);
    KERNEL_LOG(KERNEL_INFO, "Hcomm PollCq: knock cq doorbell ok, cqTail = %u, sqTail = %u\n", cqTail, sqTail);
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<COMM_PROTOCOL_ROCE>::Drain(ChannelHandle channel)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Enter\n");
    __gm__ ChannelEntity* chnlPtr = (__gm__ ChannelEntity*)(channel);
    uint32_t cqHead = chnlPtr->cqHead;
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: cqHead = %u\n", cqHead);
    if (PollCq(chnlPtr, cqHead) != HCOMM_SUCCESS) {
        KERNEL_LOG(KERNEL_ERROR, "Hcomm Drain: PollCq failed.\n");
        return HCOMM_FAILED;
    }
    KERNEL_LOG(KERNEL_INFO, "Hcomm Drain: Exit ok.\n");
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
} // namespace AscendC

#endif
#if defined(HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H)
#undef HCOMM_INCLUDE_INTERNAL_HEADERS
#undef HCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_ROCE_H
#endif
