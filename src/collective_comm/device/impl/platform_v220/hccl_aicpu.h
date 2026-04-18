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
 * \file hccl_aicpu.h
 * \brief
 */
#ifndef IMPL_V220_HCCL_AICPU_H
#define IMPL_V220_HCCL_AICPU_H

#include "hccl_aicpu_utils.h"

namespace AscendC {
template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::AllReduce(
    GM_ADDR sendBuf, GM_ADDR recvBuf, uint64_t count, HcclDataType dataType, HcclReduceOp op, uint8_t repeat)
{
    ASCENDC_HCCL_API_ASSERT(
        op >= HCCL_REDUCE_SUM && op < HCCL_REDUCE_RESERVED, { return INVALID_HANDLE_ID; },
        "Call AllReduce failed, param HcclReduceOp is %d, invalid.", static_cast<int32_t>(op));

    return CommonPrepareImpl<commit>(
        {HcclCMDType::HCCL_CMD_ALLREDUCE, sendBuf, recvBuf, count, dataType, dataType, op, 0, repeat});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::AllGather(
    GM_ADDR sendBuf, GM_ADDR recvBuf, uint64_t sendCount, HcclDataType dataType, uint64_t strideCount, uint8_t repeat)
{
    return CommonPrepareImpl<commit>(
        {HcclCMDType::HCCL_CMD_ALLGATHER, sendBuf, recvBuf, sendCount, dataType, dataType, HCCL_REDUCE_RESERVED,
         strideCount, repeat});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::ReduceScatter(
    GM_ADDR sendBuf, GM_ADDR recvBuf, uint64_t recvCount, HcclDataType dataType, HcclReduceOp op, uint64_t strideCount,
    uint8_t repeat)
{
    ASCENDC_HCCL_API_ASSERT(
        op >= HCCL_REDUCE_SUM && op < HCCL_REDUCE_RESERVED, { return INVALID_HANDLE_ID; },
        "Call ReduceScatter failed, param HcclReduceOp is %d, invalid.", static_cast<int32_t>(op));
    return CommonPrepareImpl<commit>(
        {HcclCMDType::HCCL_CMD_REDUCE_SCATTER, sendBuf, recvBuf, recvCount, dataType, dataType, op, strideCount,
         repeat});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::AlltoAll(
    GM_ADDR sendBuf, GM_ADDR recvBuf, uint64_t dataCount, HcclDataType dataType, uint64_t strideCount, uint8_t repeat)
{
    return CommonPrepareImpl<commit>(
        {HcclCMDType::HCCL_CMD_ALLTOALL, sendBuf, recvBuf, dataCount, dataType, dataType, HCCL_REDUCE_RESERVED,
         strideCount, repeat});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::AlltoAllV(
    GM_ADDR sendBuf, void* sendCounts, void* sdispls, HcclDataType sendType, GM_ADDR recvBuf, void* recvCounts,
    void* rdispls, HcclDataType recvType, uint8_t repeat)
{
    ASCENDC_HCCL_API_ASSERT(
        sendType == recvType, { return INVALID_HANDLE_ID; },
        "Call AlltoAllV failed, param sendType[%d] is not equal to recvType[%d], invalid.",
        static_cast<int32_t>(sendType), static_cast<int32_t>(recvType));
    return CommonPrepareImpl<commit>(
        {HcclCMDType::HCCL_CMD_ALLTOALLV,
         sendBuf,
         recvBuf,
         0U,
         sendType,
         recvType,
         HCCL_REDUCE_RESERVED,
         0U,
         repeat,
         {static_cast<uint64_t*>(sendCounts), static_cast<uint64_t*>(sdispls), static_cast<uint64_t*>(recvCounts),
          static_cast<uint64_t*>(rdispls)}});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::BatchWrite(
    GM_ADDR batchWriteInfo, uint32_t itemNum, uint16_t queueID)
{
    return CommonPrepareImpl<true>(
        {HcclCMDType::HCCL_CMD_BATCH_WRITE, batchWriteInfo, batchWriteInfo, itemNum, static_cast<HcclDataType>(queueID),
         static_cast<HcclDataType>(queueID), static_cast<HcclReduceOp>(queueID + GetBlockIdx() * queueNum_)});
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::AlltoAllvWrite(
    GM_ADDR usrIn, GM_ADDR sendOffsets, GM_ADDR sendSizes, uint64_t remoteWinOffset, uint64_t localDataSize)
{
    CommonPrepareParam commonPrepareParam = {
        HcclCMDType::HCCL_CMD_HALF_ALLTOALLV,
        usrIn,
        usrIn,
        localDataSize,
        HCCL_DATA_TYPE_INT8,
        HCCL_DATA_TYPE_INT8,
        HCCL_REDUCE_RESERVED,
        0,
        1,
        {},
        {reinterpret_cast<uint64_t>(sendOffsets), reinterpret_cast<uint64_t>(sendSizes), remoteWinOffset}};

    return CommonPrepareImpl<commit>(commonPrepareParam);
}

template <const auto& config>
__aicore__ inline int32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Query(HcclHandle handleId)
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION, { return HCCL_FAILED; },
        "Call Query failed, ensure Hccl::Init func has been called successfully!");
    ASCENDC_HCCL_API_ASSERT(
        (handleId > INVALID_HANDLE_ID) && (handleId < HCCL_MAX_HANDLE_ID), { return HCCL_FAILED; },
        "Call Query failed, handleId is[%d], expected in range of [0, %d).", handleId, HCCL_MAX_HANDLE_ID);
    if (queueNum_ != 0U) {
        return 0;
    }
    int8_t curMsgPos = handleIdMsgPosition_[handleId];
    ASCENDC_HCCL_API_ASSERT(
        curMsgPos >= 0, { return HCCL_FAILED; }, "Call Query failed, handleId[%d] was not got by Prepare interface.",
        handleId);
    return WaitFinishCntFromGm(curMsgPos, 0UL);
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::InterHcclGroupSync(
    int8_t srcGroupID, HcclHandle srcHandleID)
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION, { return; },
        "Call InterHcclGroupSync failed, ensure Hccl::Init func has been called successfully!");
    CommonPrepareParam param;
    param.commType.msgType = ControlMsgType::HCCL_CMD_INTER_GROUP_SYNC;
    SendMsgToServer(0U, param, srcGroupID, srcHandleID);
    ++(curMsgPosition_[0U]);
    ASCENDC_HCCL_API_ASSERT(
        curMsgPosition_[0U] < HCCL_MSG_CNT, { return; }, "Message amount exceeds the maximum value when sync group.");
    if (workingFlag_) {
        UpdateControlMsgCount(hcclMsgArea_, ControlMsgType::HCCL_CMD_INTER_GROUP_SYNC);
    }
}

template <const auto& config>
__aicore__ inline GM_ADDR HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetWindowsInAddr(uint32_t rankId)
{
    if (curVersion_ == HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
        return 0UL;
    }
    ASCENDC_HCCL_API_ASSERT(
        rankId < GetRankDim(), { return nullptr; }, "GetWindowsInAddr failed, rankId[%u], expected less than[%u]",
        rankId, GetRankDim());
    if (devType_ != HCCL_ASCEND910B) {
        __gm__ HcclContextDef::HcclOpResParam* hcclContext = (__gm__ HcclContextDef::HcclOpResParam*)hcclContext_;
        if (rankId == hcclContext->rankId) {
            return reinterpret_cast<GM_ADDR>(hcclContext->localWindowsIn);
        } else {
            const auto addr = GetRemoteRankAddrs(hcclContext, rankId);
            return reinterpret_cast<GM_ADDR>(addr != nullptr ? addr->windowsIn : 0UL);
        }
    } else {
        if (hcclContext_->multiFlag == 0U) {
            return (GM_ADDR)hcclContext_->windowsIn[rankId];
        } else {
            if (rankId == hcclContext_->rankId) {
                return (GM_ADDR)(hcclContext_->data[rankId].localInput.addr);
            } else {
                return (GM_ADDR)(hcclContext_->data[rankId].remoteInput.addr);
            }
        }
    }
}

template <const auto& config>
__aicore__ inline GM_ADDR HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetWindowsOutAddr(uint32_t rankId)
{
    if (curVersion_ == HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
        return 0UL;
    }
    ASCENDC_HCCL_API_ASSERT(
        rankId < GetRankDim(), { return nullptr; }, "GetWindowsOutAddr failed, rankId[%u], expected less than[%u]",
        rankId, GetRankDim());
    if (devType_ != HCCL_ASCEND910B) {
        __gm__ HcclContextDef::HcclOpResParam* hcclContext = (__gm__ HcclContextDef::HcclOpResParam*)hcclContext_;
        if (rankId == hcclContext->rankId) {
            return reinterpret_cast<GM_ADDR>(hcclContext->localWindowsOut);
        } else {
            const auto addr = GetRemoteRankAddrs(hcclContext, rankId);
            return reinterpret_cast<GM_ADDR>(addr != nullptr ? addr->windowsOut : 0UL);
        }
    } else {
        if (hcclContext_->multiFlag == 0U) {
            return (GM_ADDR)hcclContext_->windowsOut[rankId];
        } else {
            if (rankId == hcclContext_->rankId) {
                return (GM_ADDR)(hcclContext_->data[rankId].localOutput.addr);
            } else {
                return (GM_ADDR)(hcclContext_->data[rankId].remoteOutput.addr);
            }
        }
    }
}

template <const auto& config>
__aicore__ inline uint32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetRankId()
{
    if (curVersion_ == HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
        return (reinterpret_cast<__gm__ CommKfcContext*>(hcclContext_))->apiCtx.rankId;
    }
    return hcclContext_->rankId;
}

template <const auto& config>
__aicore__ inline uint32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetRankDim()
{
    if (curVersion_ == HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
        return (reinterpret_cast<__gm__ CommKfcContext*>(hcclContext_))->apiCtx.rankNum;
    }
    return hcclContext_->rankNum;
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Init(
    GM_ADDR context, __gm__ void* initTiling)
{
    HcclTilingVersion version;
    if (initTiling != nullptr) {
        version = HcclTilingVersion::NEW_TILING_VERSION;
        auto initTilingPtr = static_cast<__gm__ Mc2InitTilingInner*>(initTiling);
        debugMode_ = initTilingPtr->debugMode;
        queueNum_ = initTilingPtr->queueNum;
        devType_ = initTilingPtr->devType;
    } else {
        version = HcclTilingVersion::DEPRECATED_TILING_VERSION;
        devType_ = HCCL_ASCEND910B;
    }
    InitInner(context, version);
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::InitV2(
    GM_ADDR context, const void* initTiling)
{
    if (initTiling == nullptr) {
        InitInner(context, HcclTilingVersion::CONTEXT_DECOUPLE_VERSION);
        return;
    }
    const Mc2InitTilingInner* initTilingPtr = static_cast<const Mc2InitTilingInner*>(initTiling);
    debugMode_ = initTilingPtr->debugMode;
    queueNum_ = initTilingPtr->queueNum;
    devType_ = initTilingPtr->devType;
    InitInner(context, HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION);
    tilingBaseAddr_ = reinterpret_cast<uint64_t>(initTiling);
}

template <const auto& config>
__aicore__ inline int32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SetCcTiling(
    __gm__ void* ccOpTilingData)
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ == HcclTilingVersion::NEW_TILING_VERSION, { return HCCL_FAILED; },
        "Call SetCcTiling failed, ensure Hccl::InitV1 func has been called successfully!");
    ASCENDC_HCCL_API_ASSERT(
        ccOpTilingData != nullptr, { return HCCL_FAILED; },
        "Call SetCcTiling failed, ensure ccOpTilingData is not nullptr");
    const uint32_t opType = (static_cast<__gm__ Mc2CcTilingInner*>(ccOpTilingData))->opType;
    ASCENDC_HCCL_API_ASSERT(
        opType >= 0 && opType < static_cast<uint32_t>(HcclCMDType::HCCL_CMD_ALL), { return HCCL_FAILED; },
        "Call SetCcTiling failed, ensure cmdType is valid");
    KERNEL_LOG(KERNEL_INFO, "CmdType = %d, ccOpTilingData = %lu ", opType, reinterpret_cast<uint64_t>(ccOpTilingData));
    ccOpTilingDataTable_[opType] = reinterpret_cast<uint64_t>(ccOpTilingData);
    return HCCL_SUCCESS;
}

template <const auto& config>
__aicore__ inline int32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SetCcTilingV2(uint64_t offset)
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ == HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION, { return HCCL_FAILED; },
        "Call SetCcTiling failed, ensure Hccl::InitV2 func has been called successfully!");
    const uint32_t opType = (reinterpret_cast<Mc2CcTilingInner*>(tilingBaseAddr_ + offset))->opType;
    ASCENDC_HCCL_API_ASSERT(
        opType >= 0 && opType < static_cast<uint32_t>(HcclCMDType::HCCL_CMD_ALL), { return HCCL_FAILED; },
        "Call SetCcTiling failed, ensure cmdType is valid");
    ccOpTilingDataTable_[opType] = offset;
    return HCCL_SUCCESS;
}

template <const auto& config>
__aicore__ inline int32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Wait(HcclHandle handleId)
{
    HCCL_CHECK_RESTART(hcclMsgArea_, return HCCL_FAILED);
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION, { return HCCL_FAILED; },
        "Call Wait failed, ensure Hccl::Init func has been called successfully!");
    if (queueNum_ != 0U) {
        return HCCL_SUCCESS;
    }
    if (unlikely(handleId <= INVALID_HANDLE_ID || handleId >= HCCL_MAX_HANDLE_ID)) {
        KERNEL_LOG(
            KERNEL_ERROR, "Failed to wait, handleId is[%d], expected to be in range of [0, %d).", handleId,
            HCCL_MAX_HANDLE_ID);
        return HCCL_FAILED;
    }
    uint16_t& waitCnt = handleIdWaitCallNum_[handleId];
    if (unlikely(waitCnt >= handleIdCommitTurnCnt_[handleId])) {
        KERNEL_LOG(
            KERNEL_ERROR,
            "Failed to wait, call num of Wait for handleId[%d] is[%u], expected to be no larger "
            "than Commit num[%u].",
            handleId, waitCnt + 1, handleIdCommitTurnCnt_[handleId]);
        return HCCL_FAILED;
    }
    if (workingFlag_) {
        __gm__ TurnCnt* apiInfo = &(hcclMsgArea_->apiStats.waitStats[handleId2CmdType_[handleId]]);
        FlushDataCache(apiInfo);
        ++(apiInfo->cnt);
        FlushDataCache(apiInfo);
    }
    int8_t curMsgPos = handleIdMsgPosition_[handleId];
    ASCENDC_HCCL_API_ASSERT(
        curMsgPos >= 0, { return HCCL_FAILED; }, "Call Wait failed, handleId[%d] was not got by Prepare interface.",
        handleId);
    const uint16_t stepSize = GetStepSizeByHandle(handleId);
    waitCnt += (stepSize == 0U ? 1U : stepSize);
    (void)WaitFinishCntFromGm(curMsgPos, waitCnt);
    return HCCL_SUCCESS;
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Commit(HcclHandle handleId)
{
    HCCL_CHECK_RESTART(hcclMsgArea_, return );
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION, { return; },
        "Call Commit failed, ensure Hccl::Init func has been called successfully!");
    if (unlikely(handleId <= INVALID_HANDLE_ID || handleId >= HCCL_MAX_HANDLE_ID)) {
        KERNEL_LOG(
            KERNEL_ERROR, "Call Commit failed, handleId is[%d], expected in range of [0, %d).", handleId,
            HCCL_MAX_HANDLE_ID);
        return;
    }
    const uint16_t commitCnt = handleIdCommitTurnCnt_[handleId];
    if (unlikely(commitCnt >= handleIdRepeat_[handleId] * GetStepCntsPerRepeatByHandle(handleId))) {
        KERNEL_LOG(
            KERNEL_ERROR,
            "Call Commit for handleId[%d] failed, call num is[%u], "
            "expected no larger than task num[%u].",
            handleId, commitCnt + 1, handleIdRepeat_[handleId] * GetStepCntsPerRepeatByHandle(handleId));
        return;
    }
    const uint16_t stepSize = GetStepSizeByHandle(handleId);
    SetCommitTurnCntToGm(handleIdMsgPosition_[handleId], (stepSize == 0U ? 1U : stepSize), handleId);
}

template <const auto& config>
template <ScopeType type>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::QueueBarrier(uint16_t queueID)
{
    CommonPrepareParam param;
    param.commType.msgType = ControlMsgType::HCCL_CMD_BARRIER;
    SendMsgToServer(queueID, param);
    ++(curMsgPosition_[queueID]);
    ASCENDC_HCCL_API_ASSERT(
        curMsgPosition_[queueID] < HCCL_MSG_CNT, { return; }, "Message amount exceeds the maximum value when barrier.");
    if (workingFlag_) {
        UpdateControlMsgCount(hcclMsgArea_, ControlMsgType::HCCL_CMD_BARRIER);
    }
}

template <const auto& config>
template <bool sync>
__aicore__ inline int32_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Iterate(
    HcclHandle handleId, uint16_t* seqSlices, uint16_t seqSliceLen)
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ == HcclTilingVersion::NEW_TILING_VERSION ||
            curVersion_ == HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION,
        { return HCCL_FAILED; }, "Initialization has not been done properly.");
    ASCENDC_HCCL_API_ASSERT(
        seqSlices != nullptr && seqSliceLen != 0U, { return HCCL_FAILED; }, "Invalid param for Iterate.");
    const uint16_t stepSize = GetStepSizeByHandle(handleId);
    const uint16_t stepsPerRepeat = GetStepCntsPerRepeatByHandle(handleId);
    ASCENDC_HCCL_API_ASSERT(
        stepSize > 0U && stepsPerRepeat > 1U, { return HCCL_FAILED; }, "Handle id %d is not for fine-grained.",
        handleId);
    uint16_t& curSlice = handleId2CurrSliceId_[handleId];
    KERNEL_LOG(
        KERNEL_INFO, "The step size for handle %d is %u, current slice and total slices are %u/%u.", handleId, stepSize,
        curSlice, stepsPerRepeat);

    // Only for All2AllV + pairwise
    if (curSlice >= stepsPerRepeat * handleIdRepeat_[handleId]) {
        KERNEL_LOG(KERNEL_INFO, "The step id %u for handle id %d reach the maximum.", handleId, curSlice);
        return 0;
    }
    const uint16_t slicesPerRepeat = stepsPerRepeat;
    const uint32_t rankId = GetRankId();
    const uint32_t rankDim = GetRankDim();
    ASCENDC_HCCL_API_ASSERT(
        rankDim != 0U, { return HCCL_FAILED; }, "Invalid rank-dim.");
    for (uint16_t i = 0U; i < seqSliceLen; ++i) {
        if constexpr (sync) {
            if ((curSlice + 1) % stepSize == 0) {
                (void)Wait(handleId);
            }
            seqSlices[i] = (rankId + rankDim - curSlice % slicesPerRepeat) % rankDim;
        } else {
            seqSlices[i] = (rankId + curSlice % slicesPerRepeat) % rankDim;
        }
        ++curSlice;
    }
    return seqSliceLen;
}

template <const auto& config>
template <bool sync>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::Finalize()
{
    ASCENDC_HCCL_API_ASSERT(
        curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION, { return; },
        "Call Finalize failed, ensure Hccl::Init func has been called successfully!");
    HCCL_CHECK_RESTART(hcclMsgArea_, return );

    if (!workingFlag_ && queueNum_ == 0U) {
        ++(curMsgPosition_[0U]);
        ASCENDC_HCCL_API_ASSERT(
            curMsgPosition_[0U] < HCCL_MSG_CNT, { return; }, "Message amount exceeds the maximum value when finalize.");
        return;
    }

    // 1. wait until last hccl task finished(the commitTurnCnt will be reset by aicpu-server before task finished),
    //    then commitTurnCnt can be used by next op.
    if constexpr (sync) {
        if (curHandleId_ > INVALID_HANDLE_ID) {
            KERNEL_LOG(KERNEL_INFO, "Wait hccl task finished for last HandleId[%d] when Finalize.", curHandleId_);
            while ((debugMode_ != HCCL_ONLY_COMPUTE) && (Query(curHandleId_) < handleIdRepeat_[curHandleId_])) {
                HCCL_CHECK_RESTART(hcclMsgArea_, return );
            }
        }
    }

    // 2. send Finalize msg
    SendFinalizeMsg<sync>();

    if constexpr (sync) {
        // 3. wait for server sqe task finished, and client can ResetFinishedTurnCnt
        // 4. reset finishedTurnCnt, then the finishedTurnCnt can be used by next op.
        __gm__ TurnCnt* finishGM = hcclMsgArea_->commMsg.singleMsg.finishedTurnCnt + curMsgPosition_[0U];
        KERNEL_LOG(KERNEL_INFO, "Only block idx[%ld] wait until Finalize msg has been read.", GetBlockIdx());
        do {
            HCCL_CHECK_RESTART(hcclMsgArea_, return );
            FlushDataCache(finishGM);
        } while ((debugMode_ != HCCL_ONLY_COMPUTE) && (finishGM->cnt != FINALIZE_FINISH_CNT));
        KERNEL_LOG(KERNEL_INFO, "Only block idx[%ld] will ResetFinishedTurnCnt.", GetBlockIdx());
        ResetFinishedTurnCnt();
        ++(curMsgPosition_[0U]);
        ASCENDC_HCCL_API_ASSERT(
            curMsgPosition_[0U] < HCCL_MSG_CNT, { return; }, "Message amount exceeds the maximum value when finalize.");
    }
    if (workingFlag_) {
        UpdateControlMsgCount(hcclMsgArea_, ControlMsgType::HCCL_CMD_FINALIZE);
        __gm__ TurnCnt* snapshots = hcclMsgArea_->apiStats.snapshots;
        FlushDataCache(snapshots);
        for (auto handleId = 0; handleId <= curHandleId_; ++handleId) {
            auto& apiSnapshot = snapshots[snapshots->cnt % HCCL_API_SNAPSHOTS_CNT + 1UL];
            apiSnapshot.cnt = handleId2CmdType_[handleId];
            FlushDataCache(&apiSnapshot);
            ++(snapshots->cnt);
        }
        FlushDataCache(snapshots);
    }
}
} // namespace AscendC

#endif