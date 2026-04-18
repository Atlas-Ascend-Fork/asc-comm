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
 * \file hccl_aicpu_utils.h
 * \brief
 */
#ifndef IMPL_V220_HCCL_AICPU_UTILS_H
#define IMPL_V220_HCCL_AICPU_UTILS_H

#include "../../common/hccl_utils.h"
#include "../../common/hccl_impl_dfx.h"
#include "../../common/hccl_control.h"

namespace AscendC {
__aicore__ inline void CopyHcclMsg(const uint8_t* src, __gm__ HcclMsg* dst)
{
    constexpr uint32_t HCCL_VALID_POS = 12U;
    __gm__ DataBlock* tmpDst = reinterpret_cast<__gm__ DataBlock*>(dst);
    volatile uint32_t xorCheck = 0U;
    for (uint32_t i = 0; i < HCCL_MSG_DATA_CNT - 1U; ++i) {
        if (i == HCCL_VALID_POS) {
            xorCheck ^= HCCL_MSG_VALID_MASK;
        } else {
            xorCheck ^= tmpDst->data[i] = *(reinterpret_cast<const uint32_t*>(src));
        }
        src += sizeof(tmpDst->data[i]);
    }
    tmpDst->data[HCCL_MSG_DATA_CNT - 1U] = xorCheck;
    tmpDst->data[HCCL_VALID_POS] = HCCL_MSG_VALID_MASK;
}

__aicore__ inline void AssembleHcclMsgExt(const AlltoAllVParamExt& param, uint32_t rankDim, __gm__ HcclMsgExt* dst)
{
    uint64_t xorCheck = 0U;
    for (uint32_t i = 0U; i < rankDim; ++i) {
        xorCheck ^= dst->sendCounts[i] = param.sendCounts[i];
        xorCheck ^= dst->sendOffset[i] = param.sdispls[i];
        xorCheck ^= dst->recvCounts[i] = param.recvCounts[i];
        xorCheck ^= dst->recvOffset[i] = param.rdispls[i];
    }
    dst->xorCheck = (xorCheck ^ HCCL_MSG_VALID_MASK);
    dst->valid = HCCL_MSG_VALID_MASK;
}

__aicore__ inline void AssembleHcclMsg(
    const CommonPrepareParam& param, HcclTilingVersion ver, HcclHandle handle, uint64_t tiling, __gm__ HcclMsg* dst,
    __gm__ ControlHcclMsg* controlMsgGM)
{
    HcclMsg tmp{};
    static uint8_t primitiveId = 0U;
    static bool isResetPrimitiveId = false;
    FlushDataCache(controlMsgGM);
    if (controlMsgGM->resetSeq > 0) {
        controlMsgGM->resetSeq = 0;
        if (!isResetPrimitiveId) {
            primitiveId = 0U;
            isResetPrimitiveId = true;
        }
    }
    tmp.commType.msgType = param.commType.msgType;
    if (param.commType.msgType == ControlMsgType::HCCL_CMD_FINALIZE) {
        primitiveId = 0U;
        isResetPrimitiveId = false;
    } else {
        tmp.opType = param.op;
        tmp.sendBuffer = reinterpret_cast<uint64_t>(param.sendBuf);
        tmp.recvBuffer = reinterpret_cast<uint64_t>(param.recvBuf);
        tmp.dataCnt = param.count;
        tmp.strideCount = param.strideCount;
        if (ver == HcclTilingVersion::DEPRECATED_TILING_VERSION) {
            tmp.addMsg.v0Msg.hcclDataType = param.dataType;
            tmp.addMsg.v0Msg.repeatCnt = param.repeat;
            tmp.addMsg.v0Msg.selfHandleID = handle;
            tmp.addMsg.v0Msg.seqNum = primitiveId++;
            tmp.addMsg.v0Msg.version = ver;
        } else {
            tmp.addMsg.v1Msg.ccOpTilingData = tiling;
            tmp.addMsg.v1Msg.hcclDataType = param.dataType;
            tmp.addMsg.v1Msg.repeatCnt = param.repeat;
            tmp.addMsg.v1Msg.selfHandleID = handle;
            tmp.addMsg.v1Msg.seqNum = primitiveId++;
            tmp.addMsg.v1Msg.version = ver;
        }
    }
    tmp.addMsg.v0Msg.valid = HCCL_MSG_VALID_MASK;
    CopyHcclMsg(reinterpret_cast<const uint8_t*>(&tmp), dst);
}

__aicore__ inline void AssembleHcclMsg(
    const CommonPrepareParam& param, int8_t srcGroupID, HcclHandle srcHandleID, __gm__ HcclMsg* dst)
{
    HcclMsg tmp{};
    tmp.commType.msgType = param.commType.msgType;
    tmp.addMsg.v0Msg.commDepGroupID = srcGroupID;
    tmp.addMsg.v0Msg.commDepHandleID = srcHandleID;
    tmp.addMsg.v0Msg.valid = HCCL_MSG_VALID_MASK;
    CopyHcclMsg(reinterpret_cast<const uint8_t*>(&tmp), dst);
}

__aicore__ inline HcclContextDef::HcclRankRelationResV2* GetRemoteRankAddrs(
    __gm__ HcclContextDef::HcclOpResParam* ctx, uint32_t rankId)
{
    const HcclContextDef::RemoteResPtr* remoteRes =
        reinterpret_cast<const HcclContextDef::RemoteResPtr*>(reinterpret_cast<uintptr_t>(ctx) + ctx->rWinStart);
    return remoteRes[rankId].nextDevicePtr;
}

__aicore__ inline void UpdateControlMsgCount(__gm__ HcclMsgArea* hcclMsgArea, ControlMsgType msg)
{
    ASCENDC_HCCL_API_ASSERT(
        msg < ControlMsgType::HCCL_CMD_MAX, { return; }, "Invalid msg type %u.", static_cast<uint32_t>(msg));
    __gm__ TurnCnt* apiInfo =
        &(hcclMsgArea->apiStats
              .msgStats[static_cast<uint32_t>(msg) - static_cast<uint32_t>(ControlMsgType::HCCL_CMD_FINALIZE)]);
    FlushDataCache(apiInfo);
    ++(apiInfo->cnt);
    FlushDataCache(apiInfo);
}

template <const auto& config>
__aicore__ inline bool HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::CheckCommonPrepareParamValid(
    const CommonPrepareParam& param)
{
    const HcclCMDType commType = param.commType.prepareType;
    uint64_t tiling = 0UL;
    if (commType < HcclCMDType::HCCL_CMD_ALL) {
        tiling = ccOpTilingDataTable_[static_cast<uint32_t>(commType)];
    }
    if (curVersion_ == HcclTilingVersion::NEW_TILING_VERSION ||
        curVersion_ == HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION) {
        ASCENDC_HCCL_API_ASSERT(
            tiling != 0UL, { return false; }, "Failed to prepare for type %u, ensure SetCcTiling has been called.",
            static_cast<uint32_t>(commType));
    } else {
        ASCENDC_HCCL_API_ASSERT(
            curVersion_ != HcclTilingVersion::INVALID_TILING_VERSION && tiling == 0UL, { return false; },
            "Failed to prepare for type %u, ensure Init has been called", static_cast<uint32_t>(commType));
    }
    ASCENDC_HCCL_API_ASSERT(
        param.sendBuf != nullptr && param.recvBuf != nullptr, { return false; },
        "Call Prepare[%d] failed, the param sendBuf/recvBuf is nullptr, "
        "which is an invalid parameter.",
        static_cast<int32_t>(commType));
    ASCENDC_HCCL_API_ASSERT(
        commType == HcclCMDType::HCCL_CMD_BATCH_WRITE ||
            (param.dataType >= HCCL_DATA_TYPE_INT8 && param.dataType < HCCL_DATA_TYPE_RESERVED),
        { return false; }, "Call Prepare[%d] failed, param HcclDataType is %d, invalid.",
        static_cast<int32_t>(commType), static_cast<int32_t>(param.dataType));
    if (commType == HcclCMDType::HCCL_CMD_ALLTOALLV) {
        ASCENDC_HCCL_API_ASSERT(
            param.paramExt.sendCounts != nullptr && param.paramExt.sdispls != nullptr &&
                param.paramExt.recvCounts != nullptr && param.paramExt.rdispls != nullptr,
            { return false; },
            "Call AlltoAllV failed, "
            "param sendCounts/recvCounts/sdispls/rdispls is nullptr, invalid.");
    } else {
        ASCENDC_HCCL_API_ASSERT(
            param.count != 0, { return false; }, "Call Prepare[%d] failed, param sendCount/recvCount is 0, invalid.",
            static_cast<int32_t>(commType));
    }
    return true;
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::InitWorkingFlag()
{
    using T = decltype(config);
    static_assert(std::is_same<T, const HcclServerConfig&>::value);
    KERNEL_LOG(KERNEL_INFO, "Working core type %u id %u.", static_cast<uint8_t>(config.type), config.blockId);
    if constexpr (config.type == CoreType::ON_AIV) {
        workingFlag_ = (g_coreType == AscendC::AIV && GetBlockIdx() == config.blockId);
    } else if constexpr (config.type == CoreType::ON_AIC) {
        workingFlag_ = (g_coreType == AscendC::AIC && GetBlockIdx() == config.blockId);
    } else {
        workingFlag_ = (GetBlockIdx() == config.blockId);
    }
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::InitInner(
    GM_ADDR context, HcclTilingVersion version)
{
    hcclContext_ = (__gm__ HcclCombineOpParam*)context;
    ASCENDC_HCCL_API_ASSERT(
        hcclContext_ != nullptr, { return; }, "Init Hccl failed, context addr is nullptr.");
    uint64_t msgAddr;
    if (version != HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
        ASCENDC_HCCL_API_ASSERT(
            curVersion_ == HcclTilingVersion::INVALID_TILING_VERSION, { return; }, "Init repeatedly is not allowed.");
        if (unlikely(hcclContext_->workSpace == 0UL)) {
            return;
        }
        // ensure hcclMsgArea 512B aligned
        msgAddr = hcclContext_->workSpace;
        if (msgAddr & 0x1ff) {
            msgAddr = (msgAddr & (~((uint64_t)0x1ff))) + 0x200;
        }
    } else {
        msgAddr = (reinterpret_cast<__gm__ CommKfcContext*>(context))->apiCtx.workSpace;
    }
    hcclMsgArea_ = reinterpret_cast<__gm__ HcclMsgArea*>(msgAddr);
    for (uint32_t i = 0U; i < HCCL_MAX_HANDLE_ID; ++i) {
        handleIdMsgPosition_[i] = -1;
    }
    InitWorkingFlag();
    curVersion_ = version;
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SendMsgToServer(
    uint16_t queId, const CommonPrepareParam& param, int8_t srcGroupID, HcclHandle srcHandleID)
{
    if (!workingFlag_ && queueNum_ == 0U) {
        return;
    }
    __gm__ HcclMsg* hcclSendMsg;
    if (queueNum_ == 0U) {
        hcclSendMsg = hcclMsgArea_->commMsg.singleMsg.sendMsgs + curMsgPosition_[0U];
    } else {
        hcclSendMsg =
            hcclMsgArea_->commMsg.multiMsg.sendMsgs[queId + GetBlockIdx() * queueNum_] + curMsgPosition_[queId];
    }

    do {
        HCCL_CHECK_RESTART(hcclMsgArea_, return );
        FlushDataCache(hcclSendMsg);
    } while (debugMode_ != HCCL_ONLY_COMPUTE && hcclSendMsg->addMsg.v0Msg.valid == HCCL_MSG_VALID_MASK);
    KERNEL_LOG(KERNEL_INFO, "Hccl send msg[%u] is available now.", curMsgPosition_[queId]);
    if (srcGroupID < 0) {
        uint64_t tiling = 0UL;
        if (param.commType.prepareType < HcclCMDType::HCCL_CMD_ALL) {
            if (curVersion_ != HcclTilingVersion::CONTEXT_DECOUPLE_VERSION) {
                tiling = ccOpTilingDataTable_[static_cast<uint32_t>(param.commType.prepareType)];
            } else {
                tiling = (reinterpret_cast<__gm__ CommKfcContext*>(hcclContext_))->hcclContext;
            }
        }
        AssembleHcclMsg(param, curVersion_, curHandleId_, tiling, hcclSendMsg, &hcclMsgArea_->controlMsg);
    } else {
        AssembleHcclMsg(param, srcGroupID, srcHandleID, hcclSendMsg);
    }
    FlushDataCache(reinterpret_cast<__gm__ void*>(hcclSendMsg));
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SendMsgToServer(
    const AlltoAllVParamExt& param)
{
    if (!workingFlag_) {
        return;
    }
    __gm__ HcclMsgExt* hcclSendMsg = &(hcclMsgArea_->commMsg.singleMsg.paramExtMsgList[curMsgPosition_[0U]]);
    do {
        HCCL_CHECK_RESTART(hcclMsgArea_, return );
        FlushDataCache(hcclSendMsg);
    } while ((debugMode_ != HCCL_ONLY_COMPUTE) && (hcclSendMsg->valid == HCCL_MSG_VALID_MASK));
    KERNEL_LOG(KERNEL_INFO, "Hccl send extMsg[%u] is available now.", curMsgPosition_[0U]);
    uint32_t rankNum = GetRankDim();
    AssembleHcclMsgExt(param, rankNum, hcclSendMsg);
    GlobalTensor<int64_t> globalHcclMsgArea;
    for (uint32_t i = 0U; i < rankNum; i += MAX_DCCI_CNT / sizeof(uint64_t)) {
        FlushDataCache(globalHcclMsgArea, (hcclSendMsg->sendCounts + i));
        FlushDataCache(globalHcclMsgArea, (hcclSendMsg->sendOffset + i));
        FlushDataCache(globalHcclMsgArea, (hcclSendMsg->recvCounts + i));
        FlushDataCache(globalHcclMsgArea, (hcclSendMsg->recvOffset + i));
    }
    FlushDataCache(globalHcclMsgArea, hcclSendMsg->reserved);
}

template <const auto& config>
__aicore__ inline uint16_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetStepSizeByHandle(
    HcclHandle handle)
{
    if (curVersion_ != HcclTilingVersion::NEW_TILING_VERSION &&
        curVersion_ != HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION) {
        return 0U;
    }
    const uint8_t commType = handleId2CmdType_[handle];
    if (commType != static_cast<uint8_t>(HcclCMDType::HCCL_CMD_ALLTOALLV)) {
        return 0U;
    }
    __gm__ Mc2CcTilingInner* tilingPtr;
    if (curVersion_ == HcclTilingVersion::ONLINE_COMPILATION_TILING_VERSION) {
        tilingPtr = reinterpret_cast<__gm__ Mc2CcTilingInner*>(ccOpTilingDataTable_[commType] + tilingBaseAddr_);
    } else {
        tilingPtr = reinterpret_cast<__gm__ Mc2CcTilingInner*>(ccOpTilingDataTable_[commType]);
    }
    if (tilingPtr == nullptr) {
        return 0U;
    }
    return tilingPtr->stepSize;
}

template <const auto& config>
__aicore__ inline uint16_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::GetStepCntsPerRepeatByHandle(
    HcclHandle handle)
{
    return (GetStepSizeByHandle(handle) == 0U ? 1U : GetRankDim());
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SetCommitTurnCntToGm(
    uint8_t msgPos, uint64_t turnCnt, HcclHandle handleId)
{
    handleIdCommitTurnCnt_[handleId] += turnCnt;
    if (queueNum_ != 0U || !workingFlag_) {
        return;
    }

    __gm__ TurnCnt* commitGM = hcclMsgArea_->commMsg.singleMsg.commitTurnCnt + msgPos;
    do {
        HCCL_CHECK_RESTART(hcclMsgArea_, return );
        FlushDataCache(commitGM);
    } while ((debugMode_ != HCCL_ONLY_COMPUTE) && (commitGM->cnt >= handleIdCommitTurnCnt_[handleId]));
    KERNEL_LOG(
        KERNEL_INFO, "Block idx[%d] write commit turn cnt[%lu].", DEFAULT_CFG.blockId,
        handleIdCommitTurnCnt_[handleId]);
    commitGM->cnt = handleIdCommitTurnCnt_[handleId];
    commitGM->valid = COMMIT_VALID_MASK;
    FlushDataCache(commitGM);
    if (workingFlag_) {
        __gm__ TurnCnt* apiInfo = &(hcclMsgArea_->apiStats.commitStats[handleId2CmdType_[handleId]]);
        FlushDataCache(apiInfo);
        apiInfo->cnt += turnCnt;
        FlushDataCache(apiInfo);
    }
}

template <const auto& config>
__aicore__ inline uint64_t HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::WaitFinishCntFromGm(
    uint8_t msgPos, uint64_t expectedCnt)
{
    __gm__ TurnCnt* finishGM = hcclMsgArea_->commMsg.singleMsg.finishedTurnCnt + msgPos;
    GlobalTensor<int64_t> globalHcclMsgArea;
    while (true) {
        HCCL_CHECK_RESTART(hcclMsgArea_, return finishGM->cnt);
        FlushDataCache(globalHcclMsgArea, finishGM);
        if ((debugMode_ == HCCL_ONLY_COMPUTE) || (finishGM->cnt >= expectedCnt)) {
            break;
        }
    }
    return finishGM->cnt;
}

template <const auto& config>
template <bool commit>
__aicore__ inline HcclHandle HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::CommonPrepareImpl(
    const CommonPrepareParam& param)
{
    HCCL_CHECK_RESTART(hcclMsgArea_, return INVALID_HANDLE_ID);
    if (unlikely(param.repeat == 0U)) {
        return INVALID_HANDLE_ID;
    }
    ASCENDC_HCCL_API_ASSERT(
        CheckCommonPrepareParamValid(param), { return INVALID_HANDLE_ID; }, "Call Prepare[%d] failed, param invalid.",
        static_cast<int32_t>(param.commType.prepareType));

    HcclHandle handleId = ++curHandleId_;
    ASCENDC_HCCL_API_ASSERT(
        handleId < HCCL_MAX_HANDLE_ID, { return INVALID_HANDLE_ID; },
        "Call Prepare[%d] failed, Prepare interface call num is[%d], expected no more than[%d].",
        static_cast<int32_t>(param.commType.prepareType), handleId + 1, HCCL_MAX_HANDLE_ID);
    if (param.commType.prepareType == HcclCMDType::HCCL_CMD_ALLTOALLV) {
        SendMsgToServer(param.paramExt);
    }
    const uint16_t queId = (queueNum_ == 0U ? 0U : static_cast<uint16_t>(param.dataType));
    SendMsgToServer(queId, param);
    handleIdMsgPosition_[handleId] = curMsgPosition_[queId];
    handleIdRepeat_[handleId] = param.repeat;
    handleId2CmdType_[handleId] = static_cast<uint8_t>(param.commType.prepareType);
    if constexpr (commit) {
        SetCommitTurnCntToGm(curMsgPosition_[queId], param.repeat * GetStepCntsPerRepeatByHandle(handleId), handleId);
    }
    ++(curMsgPosition_[queId]);
    ASCENDC_HCCL_API_ASSERT(
        curMsgPosition_[queId] < HCCL_MSG_CNT, { return INVALID_HANDLE_ID; },
        "Message amount exceeds the maximum value when prepare.");
    return handleId;
}

template <const auto& config>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::ResetFinishedTurnCnt()
{
    __gm__ TurnCnt* finishArea = hcclMsgArea_->commMsg.singleMsg.finishedTurnCnt;
    GlobalTensor<int64_t> globalHcclMsgArea;
    for (uint32_t i = 0U; i <= curMsgPosition_[0U]; ++i) {
        __gm__ TurnCnt* finishGM = finishArea + i;
        finishGM->cnt = 0;
        FlushDataCache(globalHcclMsgArea, finishGM);
    }
}

template <const auto& config>
template <bool sync>
__aicore__ inline void HcclImpl<HcclServerType::HCCL_SERVER_TYPE_AICPU, config>::SendFinalizeMsg()
{
    const uint16_t totalQueNum = (queueNum_ == 0U ? 1U : queueNum_);
    CommonPrepareParam param;
    param.commType.msgType = ControlMsgType::HCCL_CMD_FINALIZE;
    for (uint16_t idx = 0U; idx < totalQueNum; ++idx) {
        KERNEL_LOG(
            KERNEL_INFO, "Only block idx[%ld] write sendMsgList[%u] when Finalize.", GetBlockIdx(),
            curMsgPosition_[idx]);
        SendMsgToServer(idx, param);
        if constexpr (!sync) {
            ++(curMsgPosition_[idx]);
            ASCENDC_HCCL_API_ASSERT(
                curMsgPosition_[idx] < HCCL_MSG_CNT, { return; },
                "Message amount exceeds the maximum value when finalize.");
        }
    }
}
} // namespace AscendC

#endif