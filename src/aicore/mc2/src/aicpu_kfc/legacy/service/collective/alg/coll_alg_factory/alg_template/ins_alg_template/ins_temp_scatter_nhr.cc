/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "ins_temp_scatter_nhr.h"
#include "ins_temp_all_gather_nhr.h"
#include "alg_data_trans_wrapper.h"
#include "dev_mode.h"
#include "log.h"


namespace Hccl {
InsTempScatterNHR::InsTempScatterNHR(const RankId virtualRank, const u32 tempRankSize,
                                             const std::vector<std::vector<RankId>> &tempVTopo,
                                             const std::map<RankId, u32>            &tempVirtRankMap)
    : InsAlgTemplateBase(virtualRank, tempRankSize, tempVTopo, tempVirtRankMap)
{
}

InsTempScatterNHR::~InsTempScatterNHR()
{
}

HcclResult InsTempScatterNHR::CalcRes(AlgTempResReq &tempResReq)
{
    // NHR 需要的 que Num 为 1
    tempResReq.queNum = 1;
    tempResReq.streamNum = tempResReq.queNum;
    tempResReq.queNotifys = CreateMasterSlaveQueNotifiesRequest(tempResReq.queNum);
    CHK_PRT_RET(CalcResLinksNHR(myRank_, tempRankSize_, tempVTopo_, tempResReq) != HcclResult::HCCL_SUCCESS,
                HCCL_ERROR("[CollAlgFactory] [InsTempScatterNHR] Rank [%d], resLinks calculation error!", myRank_),
                HcclResult::HCCL_E_INTERNAL);

    return HcclResult::HCCL_SUCCESS;
}

uint64_t InsTempScatterNHR::GetExpandedMode() const
{
    return DeviceMode::AICPU;
}

HcclResult InsTempScatterNHR::PreCopy(TemplateDataParams &templateDataParams, std::vector<InsQuePtr> &tempInsQues)
{       
    if (u32(myRank_) != root_ || buffInfo_.inBuffType == BufferType::SCRATCH) {
        return HCCL_SUCCESS;
    }
    for (u32 r = 0; r < templateDataParams.repeatNum; r++) {
        for (u32 algRank = 0; algRank < tempRankSize_; algRank++) {
            DataSlice srcSlice(BufferType::INPUT, r * templateDataParams.inputRepeatStride + templateDataParams.inputSliceStride * algRank + buffInfo_.inBuffBaseOff,
                                templateDataParams.sliceSize);
            DataSlice dstSlice(BufferType::SCRATCH, r * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + algRank * templateDataParams.sliceSize,
                               templateDataParams.sliceSize);
            LocalCopy(tempInsQues[0], srcSlice, dstSlice);
        }
    }
   
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterNHR::PostCopy(TemplateDataParams &templateDataParams, std::vector<InsQuePtr> &tempInsQues)
{   
    u32 myAlgRank;
    GetAlgRank(myRank_, tempVTopo_[0], myAlgRank);
    for (u32 r = 0; r < templateDataParams.repeatNum; r++) {
        u64 dstOffset = buffInfo_.outBuffBaseOff + r * templateDataParams.sliceSize;
        u64 srcOffset = r * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + myAlgRank * templateDataParams.sliceSize;
        DataSlice dstSlice(buffInfo_.outBuffType, dstOffset, templateDataParams.sliceSize);
        DataSlice srcSlice(BufferType::SCRATCH, srcOffset, templateDataParams.sliceSize);
        if (buffInfo_.outBuffType == BufferType::SCRATCH && srcOffset == dstOffset) {
            continue;
        }
        LocalCopy(tempInsQues[0], srcSlice, dstSlice);
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterNHR::RunNHR(TemplateDataParams &templateDataParams, ResLinks &tempLinks, std::vector<InsQuePtr> &tempInsQues) const
{   
    // nhr主体部分
    u32 nSteps = GetNHRStepNum(tempRankSize_);
    for (u32 r = 0; r < templateDataParams.repeatNum; r++) {
        for (u32 step = 0; step < nSteps; step++) {
            AicpuNHRStepInfo stepInfo;
            GetStepInfo(step, nSteps, stepInfo);
            // 只有Tx,使用send指令
            if (stepInfo.txSliceIdxs.size() > 0 && stepInfo.rxSliceIdxs.size() == 0) {
                CHK_RET(BatchSend(stepInfo, tempLinks, tempInsQues[0], templateDataParams, r));
            }
            // 只有Rx，使用recv指令
            else if (stepInfo.txSliceIdxs.size() == 0 && stepInfo.rxSliceIdxs.size() > 0) {
                CHK_RET(BatchRecv(stepInfo, tempLinks, tempInsQues[0],  templateDataParams, r));
            }
            // 既有Tx又有Rx，使用SendRecv指令
            else if (stepInfo.txSliceIdxs.size() > 0 && stepInfo.rxSliceIdxs.size() > 0) {
                CHK_RET(BatchSR(stepInfo, tempLinks, tempInsQues[0], templateDataParams, r));
            }
        }
    }
    return HCCL_SUCCESS;
}

// 需要支持input->scratch, scratch->output, input->output
HcclResult InsTempScatterNHR::GenExtIns(TempFuncs &tempFuncs,TemplateDataParams &templateDataParams,
                                        ResLinks &tempLinks, std::vector<InsQuePtr> &tempInsQues)
{
    if (IsPcieLink(tempLinks)) {
        dmaMode_ = DmaMode::GET;
    } else {
        dmaMode_ = DmaMode::PUT;
    }
    opMode_              = tempFuncs.opMode;
    enableCounterNotify_ = tempFuncs.enableCounterNotify;
    buffInfo_            = templateDataParams.buffInfo;

    HCCL_INFO("[InsTempScatterNHR] Run start");

    queNum_ = tempVTopo_.size();
    CHK_PRT_RET(queNum_ != tempInsQues.size(),
                HCCL_ERROR("[CollAlgFactory] [InsTempScatterNHR] Rank [%d], requiredQue Error.", myRank_),
                HcclResult::HCCL_E_INTERNAL);

    HCCL_INFO("[InsTempScatterNHR Run]RankID:[%d], root:[%u], isForepart:[%d], isBottom:[%d]", myRank_, root_, tempFuncs.isForepart, tempFuncs.isBottom);
    CHK_RET(PreCopy(templateDataParams, tempInsQues));
    CHK_RET(RunNHR(templateDataParams, tempLinks, tempInsQues));
    CHK_RET(PostCopy(templateDataParams, tempInsQues));
    return HCCL_SUCCESS;
}

u32 InsTempScatterNHR::CalcScratchMultiple(BufferType inBuffType, BufferType outBuffType) const
{   
    (void) inBuffType;
    (void) outBuffType;
    return tempRankSize_;
}

HcclResult InsTempScatterNHR::BatchSend(AicpuNHRStepInfo &stepInfo, const ResLinks &tempLinks, InsQuePtr &queue,
                                        TemplateDataParams &templateDataParams, u32 repeat) const
{
    const LinkData &linkSend = tempLinks.at(stepInfo.toRank)[0];
    std::vector<DataSlice> srcDstSlices;
    for (u32 i = 0; i < stepInfo.txSliceIdxs.size(); i++) {
        u32 txId = stepInfo.txSliceIdxs[i];
        u64 srcDstOffset = repeat * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + txId * templateDataParams.sliceSize;
        DataSlice srcDstSlice(BufferType::SCRATCH, srcDstOffset, templateDataParams.sliceSize);
        srcDstSlices.push_back(srcDstSlice);
    }
    SlicesList txSlicesList(srcDstSlices, srcDstSlices);
    DataInfo sendData(linkSend, txSlicesList);
    CHK_PRT_RET(Send(sendData, queue, 0, true, dmaMode_), HCCL_ERROR("[InsTempScatterNHR] BatchSend failed"),
        HcclResult::HCCL_E_INTERNAL);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterNHR::BatchRecv(AicpuNHRStepInfo &stepInfo, const ResLinks &tempLinks, InsQuePtr &queue,
                                        TemplateDataParams &templateDataParams, u32 repeat) const
{
    const LinkData &linkRecv = tempLinks.at(stepInfo.fromRank)[0];
    std::vector<DataSlice> srcDstSlices;
    for (u32 i = 0; i < stepInfo.rxSliceIdxs.size(); i++) {
        u32 rxId = stepInfo.rxSliceIdxs[i];
        u64 srcDstOffset = repeat * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + rxId * templateDataParams.sliceSize;
        DataSlice srcDstSlice(BufferType::SCRATCH, srcDstOffset, templateDataParams.sliceSize);
        srcDstSlices.push_back(srcDstSlice);
    }
    SlicesList rxSlicesList(srcDstSlices, srcDstSlices);
    DataInfo recvData(linkRecv, rxSlicesList);
    CHK_PRT_RET(Recv(recvData, queue, 0, true, dmaMode_), HCCL_ERROR("[InsTempScatterNHR] BatchTxRx Recv failed"),
        HcclResult::HCCL_E_INTERNAL);
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterNHR::BatchSR(AicpuNHRStepInfo &stepInfo, const ResLinks &tempLinks, InsQuePtr &queue,
                                      TemplateDataParams &templateDataParams, u32 repeat)const
{
    const LinkData &linkSend = tempLinks.at(stepInfo.toRank)[0];
    const LinkData &linkRecv = tempLinks.at(stepInfo.fromRank)[0];
    TxRxLinks linkSendRecv = {linkSend, linkRecv};

    std::vector<DataSlice> txSrcDstSlices;
    for (u32 i = 0; i < stepInfo.txSliceIdxs.size(); i++) {
        u32 txId = stepInfo.txSliceIdxs[i];
        u64 srcDstOffset = repeat * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + txId * templateDataParams.sliceSize;
        DataSlice srcDstSlice(BufferType::SCRATCH, srcDstOffset, templateDataParams.sliceSize);
        txSrcDstSlices.push_back(srcDstSlice);
    }
    SlicesList txSlicesList(txSrcDstSlices, txSrcDstSlices);
    std::vector<DataSlice> rxSrcDstSlices;
    for (u32 i = 0; i < stepInfo.rxSliceIdxs.size(); i++) {
        u32 rxId = stepInfo.rxSliceIdxs[i];
        u64 srcDstOffset = repeat * tempRankSize_ * templateDataParams.sliceSize + buffInfo_.scratchBuffBaseOff + rxId * templateDataParams.sliceSize;
        DataSlice srcDstSlice(BufferType::SCRATCH, srcDstOffset, templateDataParams.sliceSize);
        rxSrcDstSlices.push_back(srcDstSlice);
    }
    SlicesList rxSlicesList(rxSrcDstSlices, rxSrcDstSlices);
    TxRxSlicesList txRxSlicesList(txSlicesList, rxSlicesList);
    SendRecvInfo sendRecvInfo(linkSendRecv, txRxSlicesList);
    CHK_PRT_RET(SendRecv(sendRecvInfo, queue, 0, true, dmaMode_), HCCL_ERROR("[InsTempScatterNHR] BatchTxRx SendRecv failed"),
        HcclResult::HCCL_E_INTERNAL);
    return HcclResult::HCCL_SUCCESS;
}

// NHR每步的算法描述原理函数
HcclResult InsTempScatterNHR::GetStepInfo(u32 step, u32 nSteps, AicpuNHRStepInfo &stepInfo) const
{
    u32 rankSize = tempRankSize_;
    u32 myAlgRank;
    u32 rootAlgRank;
    GetAlgRank(myRank_, tempVTopo_[0], myAlgRank);
    GetAlgRank(root_, tempVTopo_[0], rootAlgRank);
    stepInfo.txSliceIdxs.clear();
    stepInfo.rxSliceIdxs.clear();
    stepInfo.nSlices = 0;
    stepInfo.toRank = rankSize;
    stepInfo.fromRank = rankSize;
    stepInfo.step = step;
    stepInfo.myRank = myRank_;

    u32 deltaRoot = (rootAlgRank + rankSize - myAlgRank) % rankSize;
    u32 deltaRankPair = 1 << step;

    // 数据份数和数据编号增量
    u32 nSlices = (rankSize - 1 + (1 << step)) / (1 << (step + 1));
    u32 deltaSliceIndex = 1 << (step + 1);

    // 判断是否是2的幂
    u32 nRanks = 0; // 本步需要进行收/发的rank数
    bool isPerfect = (rankSize & (rankSize - 1)) == 0;
    if (!isPerfect && step == nSteps - 1) {
        nRanks = rankSize - deltaRankPair;
    } else {
        nRanks = deltaRankPair;
    }

    if (deltaRoot < nRanks) { // 需要发
        u32 sendTo = (myAlgRank + rankSize - deltaRankPair) % rankSize;
        u32 txSliceIdx = sendTo;
        for (u32 i = 0; i < nSlices; i++) {
            u32 targetTxSliceIdx = txSliceIdx;
            stepInfo.txSliceIdxs.push_back(targetTxSliceIdx);
            txSliceIdx = (txSliceIdx + rankSize - deltaSliceIndex) % rankSize;
        }

        stepInfo.toRank = tempVTopo_[0][sendTo];
        stepInfo.nSlices = nSlices;
    } else if (deltaRoot >= deltaRankPair && deltaRoot < nRanks + deltaRankPair) { // 需要收
        u32 recvFrom = (myAlgRank + deltaRankPair) % rankSize;
        u32 rxSliceIdx = myAlgRank;
        for (u32 i = 0; i < nSlices; i++) {
            u32 targetRxSliceIdx = rxSliceIdx;
            stepInfo.rxSliceIdxs.push_back(targetRxSliceIdx);
            rxSliceIdx = (rxSliceIdx + rankSize - deltaSliceIndex) % rankSize;
        }

        stepInfo.fromRank = tempVTopo_[0][recvFrom];
        stepInfo.nSlices = nSlices;
    }
    return HcclResult::HCCL_SUCCESS;
}


} // namespace Hccl
