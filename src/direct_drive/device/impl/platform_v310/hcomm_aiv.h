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
 * \file hcomm_roce.h
 * \brief Hcomm AIV implementation for V310
 */

#if !defined(__ASCENDC_INCLUDE_INTERNAL_HEADERS__)
#pragma message( \
    "impl/adv_api/detail/hcomm/impl/platform_v310/hcomm_aiv.h is an internal header file and must not be used directly. Functions or variables defined in this file may be removed in the future. Please use \"#include \"adv_api/activation/simplesoftmax.h\"\" and use public functions or variables defined in interface headers files.")
#define __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#define __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H__
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_H
#define IMPL_ADV_API_DETAIL_HCOMM_IMPL_PLATFORM_V310_HCOMM_AIV_H

#include "hcomm_aiv_def.h"
#include "../../common/hcomm_utils.h"
#include "../../common/hcomm_inner_def.h"

namespace AscendC {

__aicore__ inline uint64_t GetDbValue(uint32_t qpn)
{
    RoceDbEntry dbEntry = {0};
    dbEntry.dw0.bs.type = ROCE_SQ_DOORBELL_TYPE;
    dbEntry.dw0.bs.c = 0;
    dbEntry.dw0.bs.n = 0;
    dbEntry.dw0.bs.cntxSize = 1;
    dbEntry.dw0.bs.qpn = qpn;
    dbEntry.dw0.bs.mtuShift = 0;
    dbEntry.dw0.bs.resv = 0;
    dbEntry.dw0.bs.pi = 0;
    dbEntry.dw0.bs.sgidIndex = ROCE_INIT_SQ_DB_SGIT_IDX;
    dbEntry.dw0.bs.cos = 0x7;
    return dbEntry.dw0.value;
}

__aicore__ inline uint32_t HtoNL(uint32_t x)
{
    constexpr uint32_t byte0Mask = 0x000000ffU;
    constexpr uint32_t byte1Mask = 0x0000ff00U;
    constexpr uint32_t byte2Mask = 0x00ff0000U;
    constexpr uint32_t byte3Mask = 0xff000000U;

    constexpr uint32_t byteShift = 8;
    constexpr uint32_t wordShift = 24;

    return (
        ((x & byte3Mask) >> wordShift) | ((x & byte2Mask) >> byteShift) | ((x & byte1Mask) << byteShift) |
        ((x & byte0Mask) << wordShift));
}

template <typename T>
__aicore__ inline void Gm2Ub(
    const AscendC::LocalTensor<T>& dstLocal, const AscendC::GlobalTensor<T>& srcGlobal, int32_t length)
{
    AscendC::DataCopyExtParams copyParams{1, (uint32_t)(length * sizeof(T)), 0, 0, 0};
    AscendC::DataCopyPadExtParams<T> padParams{true, 0, (ONE_BLOCK_SIZE - sizeof(T)) / sizeof(T), 0};

    PipeBarrier<PIPE_ALL>();
    AscendC::DataCopyPad(dstLocal, srcGlobal, copyParams, padParams);
    PipeBarrier<PIPE_ALL>();
}

template <typename T>
__aicore__ inline void Ub2Gm(
    const AscendC::GlobalTensor<T>& dstGlobal, const AscendC::LocalTensor<T>& srcLocal, int32_t length)
{
    AscendC::DataCopyExtParams copyParams{1, (uint32_t)(length * sizeof(T)), 0, 0, 0};
    PipeBarrier<PIPE_ALL>();
    AscendC::DataCopyPad(dstGlobal, srcLocal, copyParams);
    PipeBarrier<PIPE_ALL>();
}

__aicore__ inline HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::HcommImpl()
{
    pipe_.InitBuffer(hcommBuf_, HCOMM_TMP_BUF_SIZE);
    wqeItem_ = hcommBuf_.Get<uint32_t>();
    cqeItem_ = wqeItem_[16];
    sqPI_ = cqeItem_[8];
    sqCI_ = sqPI_[8];
    cqCI_ = sqCI_[8];
    doorBell_ = cqCI_[8];
}

__aicore__ inline HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::~HcommImpl() {}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe>
__aicore__ inline HcommHandle HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::Operate(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len, uint32_t opType)
{
    __gm__ Channel* channelPtr = (__gm__ Channel*)(channel);
    auto sqPIAddr = (__gm__ uint32_t*)(channelPtr->sqContextAddr->ctx.rdmaSqContext.headAddr);
    GlobalTensor<uint32_t> sqPIGlobal;
    sqPIGlobal.SetGlobalBuffer(sqPIAddr);
    Gm2Ub(sqPI_, sqPIGlobal, 1);
    uint32_t sqHead = sqPI_.GetValue(0);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: opType = %u, sqHead = %u", opType, sqHead);

    auto sqCIAddr = (__gm__ uint32_t*)(channelPtr->sqContextAddr->ctx.rdmaSqContext.tailAddr);
    GlobalTensor<uint32_t> sqCIGlobal;
    sqCIGlobal.SetGlobalBuffer(sqCIAddr);
    uint32_t sqDepth = channelPtr->sqContextAddr->ctx.rdmaSqContext.depth;
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: sqDepth = %u", sqDepth);

#if !defined(UT_TEST)
    while (1) {
        Gm2Ub(sqCI_, sqCIGlobal, 1);
        uint32_t sqTail = sqCI_.GetValue(0);
        KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: sqTail = %u", sqTail);
        if (sqHead - sqTail < sqDepth - 1) {
            break;
        }
    }
#endif

    __ubuf__ RoceWqeEntry* wqePtr = (__ubuf__ RoceWqeEntry*)(wqeItem_.GetPhyAddr());
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
    wqePtr->vaHigh32 = HtoNL(((uint64_t)dst + len) >> 32);
    wqePtr->vaLow32 = HtoNL(((uint64_t)dst + len) & 0xffffffff);
    uint32_t rKey = channelPtr->remoteBufferAddr->pti.rdmaMemProtectionInfo.rkey;
    wqePtr->rKey = HtoNL(rKey);
    wqePtr->rsvd1 = 0;
    wqePtr->data.bufAddrHigh32 = HtoNL((uint64_t)src >> 32);
    wqePtr->data.bufAddrLow32 = HtoNL((uint64_t)src & 0xffffffff);
    wqePtr->data.rLen = (uint32_t)len;
    uint32_t lKey = channelPtr->localBufferAddr->pti.rdmaMemProtectionInfo.lkey;
    wqePtr->data.leKey = HtoNL(1 << 31 | lKey);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: make wqe ok");

    __gm__ uint8_t* sqBaseAddr = (__gm__ uint8_t*)(channelPtr->sqContextAddr->ctx.rdmaSqContext.sqVa);
    uint32_t sqPIMask = sqDepth - 1;
    uint32_t wqeSize = channelPtr->sqContextAddr->ctx.rdmaSqContext.wqeSize;
    GlobalTensor<uint32_t> sqGlobal;
    auto sqAddr = sqBaseAddr + (sqHead & sqPIMask) * wqeSize;
    sqGlobal.SetGlobalBuffer((__gm__ uint32_t*)(sqAddr));
    PipeBarrier<PIPE_ALL>();
    DataCopy(sqGlobal, wqeItem_, 16);
    PipeBarrier<PIPE_ALL>();
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: set wqe to qp ok");

    HcommHandle handleId = ++curHandleId_;
    channelList_[handleId] = channel;
    sqHead++;
    sqPI_.SetValue(0, sqHead);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: update PI in stack, sqHead = %u handleId = %d", sqHead, handleId);

    if constexpr (commit) {
        Commit(handleId);
        KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: Commit ok, handleId = %d", handleId);
    }

    Ub2Gm<uint32_t>(sqPIGlobal, sqPI_, 1);
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: update PI to GM sqHead = %u handleId = %d", sqHead, handleId);
    return handleId;
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe>
__aicore__ inline HcommHandle HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::Write(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return Operate<commit, commitPipe, reqPipe>(channel, dst, src, len, static_cast<uint32_t>(HCOMM_OP_TYPE::WRITE));
}

template <bool commit, pipe_t commitPipe, pipe_t reqPipe>
__aicore__ inline HcommHandle HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::Read(
    ChannelHandle channel, GM_ADDR dst, GM_ADDR src, uint64_t len)
{
    return Operate<commit, commitPipe, reqPipe>(channel, dst, src, len, static_cast<uint32_t>(HCOMM_OP_TYPE::READ));
}

template <bool isWait>
__aicore__ inline bool HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::JudgeHandleId(HcommHandle handleId)
{
    // 判断handleId是否合法
    if (handleId < 0 || handleId >= 1024) {
        KERNEL_LOG(KERNEL_ERROR, "Judge HandleId failed without invalid handleId");
        return false;
    }
    if (channelList_[handleId] == 0) {
        KERNEL_LOG(KERNEL_ERROR, "Judge HandleId failed without valid channel");
        return false;
    }
    if constexpr (isWait) {
        // Wait操作，拒绝对未进行Commit或者已进行Wait的handleId进行操作
        if (!handleCommitList_[handleId]) {
            KERNEL_LOG(KERNEL_ERROR, "Wait Judge HandleId failed without Commit");
            return false;
        }
        if (handleWaitList_[handleId]) {
            KERNEL_LOG(KERNEL_ERROR, "Wait Judge HandleId failed with already Wait");
            return false;
        }
    } else {
        // Commit操作，拒绝对已进行Commit的handleId进行操作
        if (handleCommitList_[handleId]) {
            KERNEL_LOG(KERNEL_ERROR, "Commit Judge HandleId failed with already Commit");
            return false;
        }
    }
    return true;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::Commit(HcommHandle handleId)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: opType = Commit, handleId = %u", handleId);
    if (!JudgeHandleId<false>(handleId)) {
        return HCOMM_FAILED;
    }
    ChannelHandle channel = channelList_[handleId];
    __gm__ Channel* channelPtr = (__gm__ Channel*)(channel); // 获取channel
    AscendC::GlobalTensor<uint32_t> sqPIGlobalTensor;
    sqPIGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)channelPtr->sqContextAddr->ctx.rdmaSqContext.headAddr);
    Gm2Ub(sqPI_, sqPIGlobalTensor, 1);
    uint32_t sqHead = sqPI_.GetValue(0); // 获取当前channel对应的sq pi

    AscendC::GlobalTensor<uint32_t> dbGlobalTensor;
    __gm__ uint32_t* doorBellAddr =
        (__gm__ uint32_t*)(channelPtr->sqContextAddr->ctx.rdmaSqContext.dbVa + (sqHead >> 8) & 0xff);
    dbGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)doorBellAddr);

    uint64_t dbValue = GetDbValue(channelPtr->sqContextAddr->ctx.rdmaSqContext.qpn);
    __ubuf__ uint32_t* dbUB = (__ubuf__ uint32_t*)(doorBell_.GetPhyAddr());
    *(dbUB + 0) = (dbValue & 0xffffffff);
    *(dbUB + 1) = (dbValue >> 32) | ((sqHead >> 8) & 0xff);

    // 此处想保证db写入为原子操作，穿刺中未验证操作正确性，是否需要换成SetAtomicAdd
    set_atomic_add();
    Ub2Gm(dbGlobalTensor, doorBell_, 2);
    SetAtomicNone();

    // 从当前handleId向前遍历更新handleCommitList_
    for (HcommHandle i = handleId; i >= 0; i--) {
        if (channelList_[i] == channel) {
            if (!handleCommitList_[i]) {
                handleCommitList_[i] = true;
            } else {
                break;
            }
        }
    }
    return HCOMM_SUCCESS;
}

template <pipe_t pipe>
__aicore__ inline int32_t HcommImpl<CommEngine::AIV, CommProtocol::ROCE>::Wait(HcommHandle handleId)
{
    KERNEL_LOG(KERNEL_INFO, "Hcomm Operate: opType = Wait, handleId = %u", handleId);
    if (!JudgeHandleId<true>(handleId)) {
        return HCOMM_FAILED;
    }
    ChannelHandle channel = channelList_[handleId];
    __gm__ Channel* channelPtr = (__gm__ Channel*)(channel); // 获取channel
    auto sqCIAddr = channelPtr->sqContextAddr->ctx.rdmaSqContext.tailAddr;
    AscendC::GlobalTensor<uint32_t> sqCIGlobalTensor;
    sqCIGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)sqCIAddr); // 获取Sq CI对应的global tensor

    auto cqCIAddr = channelPtr->cqContextAddr->ctx.rdmaCqContext.tailAddr;
    AscendC::GlobalTensor<uint32_t> cqCIGlobalTensor;
    cqCIGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)cqCIAddr); // 获取Cq CI对应的global tensor

    uint8_t owner;
    uint32_t cqTail;
    uint32_t cqMask;
    uint32_t cqeSize = channelPtr->cqContextAddr->ctx.rdmaCqContext.cqeSize;
    uint32_t cqDepth = channelPtr->cqContextAddr->ctx.rdmaCqContext.cqDepth;
    uint32_t cqn = channelPtr->cqContextAddr->ctx.rdmaCqContext.cqn;
    __gm__ uint8_t* cqBaseBuf = (__gm__ uint8_t*)(channelPtr->cqContextAddr->ctx.rdmaCqContext.dbVa);

    AscendC::GlobalTensor<uint32_t> cqeGlobalTensor;
    __ubuf__ RoceCqeEntry* cqeUB = (__ubuf__ RoceCqeEntry*)(cqeItem_.GetPhyAddr());

    do {
        Gm2Ub(cqCI_, cqCIGlobalTensor, 1);
        cqTail = cqCI_.GetValue(static_cast<uint32_t>(0));

        owner = (HtoNL(cqTail) & (cqDepth + 1)) == 0 ? 0 : 1;
        auto cqeAddr = cqBaseBuf + ((HtoNL(cqTail) & cqDepth) * cqeSize);
        cqeGlobalTensor.SetGlobalBuffer((__gm__ uint32_t*)cqeAddr);

        PipeBarrier<PIPE_ALL>();
        DataCopy(cqeItem_, cqeGlobalTensor, 8);
        PipeBarrier<PIPE_ALL>();

#if defined(UT_TEST)
        break;
#else
        if (((((cqeUB->cqe0) >> 31) & 0x1) == owner) && ((cqeUB->cqe0 & 0xfffff) == cqn)) {
            break;
        }
#endif
    } while (1);

    cqTail = HtoNL(HtoNL(cqTail) + 1);
    cqTail += 1;
    cqCI_.SetValue(0, cqTail);

    Ub2Gm(cqCIGlobalTensor, cqCI_, 1); // 更新 Cq CI 到 gm

    Gm2Ub(sqCI_, sqCIGlobalTensor, 1);

    uint32_t sqTail = sqCI_.GetValue(static_cast<uint32_t>(0));
    sqTail += 1;
    sqCI_.SetValue(0, sqTail);

    Ub2Gm(sqCIGlobalTensor, sqCI_, 1); // 更新 Sq CI 到 gm

    // 从当前handleId向前遍历处理handleWaitList_
    for (HcommHandle i = handleId; i >= 0; i--) {
        if (channelList_[i] == channel) {
            if (!handleWaitList_[i]) {
                handleWaitList_[i] = true;
            } else {
                break;
            }
        }
    }
    return HCOMM_SUCCESS;
}
} // namespace AscendC

#endif
#if defined(__UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H__)
#undef __ASCENDC_INCLUDE_INTERNAL_HEADERS__
#undef __UNDEF_ASCENDC_INCLUDE_INTERNAL_HEADERS_HCOMM_AIV_H__
#endif
