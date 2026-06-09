/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "log.h"

#include "alg_data_trans_wrapper.h"
#include "ins_alg_template/ins_temp_all_gather_nhr.h"

namespace Hccl {
InsTempAllGatherNHR::InsTempAllGatherNHR(const RankId virtualRank, const u32 tempRankSize,
                                   const std::vector<std::vector<RankId>> &tempVTopo,
                                   const std::map<RankId, u32>            &tempVirtRankMap)
    : InsAlgTemplateBase(virtualRank, tempRankSize, tempVTopo, tempVirtRankMap)
{
}

InsTempAllGatherNHR::~InsTempAllGatherNHR()
{
}

HcclResult InsTempAllGatherNHR::CalcRes(AlgTempResReq &tempResReq)
{
    // NHR 需要的 que Num 为 1 
    tempResReq.queNum = 1;
    tempResReq.streamNum = tempResReq.queNum;
    tempResReq.queNotifys = CreateMasterSlaveQueNotifiesRequest(tempResReq.queNum);
    CHK_PRT_RET(CalcResLinksNHR(myRank_, tempRankSize_, tempVTopo_, tempResReq) != HcclResult::HCCL_SUCCESS,
                HCCL_ERROR("[CollAlgFactory] [InsTempAllGatherNHR] Rank [%d], resLinks calculation error!", myRank_),
                HcclResult::HCCL_E_INTERNAL);

    return HcclResult::HCCL_SUCCESS;
}

/*
dataSize / (rankSize) --> chunkSize
dataSize / (rankSize * queNum) --> sliceSize

SliceInfoVecforNHR: [1st chunk: [1st Slice, 2nd Slice, ...], 2nd chunk: [1st Slice, 2nd Slice, ...], ...]
*/
HcclResult InsTempAllGatherNHR::CalcSliceInfo(const AllignInfo &allignInfo, const u64 dataSize,
                                            RankSliceInfo &sliceInfoVec)
{
    std::vector<SliceInfo> tmp(tempVTopo_.size());
    sliceInfoVec.resize(tempRankSize_, tmp);

    CHK_RET(CalcRsAgSliceInfoNHR(myRank_, tempRankSize_, allignInfo, dataSize, sliceInfoVec));

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherNHR::GenExtIns(const TempFuncs &tempFuncs,
    const TemplateDataParams &tempAlgParams,
    const ResLinks &tempLinks,
    std::vector<InsQuePtr> &tempInsQues)
{
    if (IsPcieLink(tempLinks)) {
        dmaMode_ = DmaMode::GET;
    }
    opMode_ = tempFuncs.opMode;
    tempAlgParams_ = tempAlgParams;
    tempLinks_ = tempLinks;

    queNum_ = tempVTopo_.size();
    CHK_PRT_RET(queNum_ != tempInsQues.size(),
        HCCL_ERROR("[InsTempAllGatherNHR] Rank [%d], requiredQueNum [%u] not equals templateQueNum [%zu].",
                   myRank_, queNum_, tempInsQues.size()),
        HcclResult::HCCL_E_INTERNAL);

    CHK_RET(LocalDataCopy(tempInsQues));
    CHK_RET(RunNHR(tempInsQues));
    CHK_RET(PostLocalCopy(tempInsQues));

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherNHR::GetStepInfo(u32 step, u32 nSteps, AicpuNHRStepInfo &stepInfo)
{
    u32 rankIdx = 0;
    CHK_RET(GetAlgRank(myRank_, tempVTopo_[0], rankIdx));
    stepInfo.txSliceIdxs.clear();
    stepInfo.rxSliceIdxs.clear();
    stepInfo.step = step;
    stepInfo.myRank = rankIdx;

    // 计算通信对象
    u32 deltaRank = 1 << (nSteps - 1 - step);
    u32 recvFrom = (rankIdx + tempRankSize_ - deltaRank) % tempRankSize_;
    u32 sendTo = (rankIdx + deltaRank) % tempRankSize_;

    // 数据份数和数据编号增量
    u32 nSlices = (tempRankSize_ - 1 + (1 << (nSteps - 1 - step))) / (1 << (nSteps - step));
    u32 deltaSliceIndex = 1 << (nSteps - step);
    u32 txSliceIdx = rankIdx;
    u32 rxSliceIdx = (rankIdx - (1 << (nSteps - 1 - step)) + tempRankSize_) % tempRankSize_;

    stepInfo.nSlices = nSlices;
    stepInfo.toRank = sendTo;
    stepInfo.fromRank = recvFrom;

    for (u32 i = 0; i < nSlices; i++) {
        stepInfo.txSliceIdxs.push_back(txSliceIdx);
        stepInfo.rxSliceIdxs.push_back(rxSliceIdx);

        HCCL_DEBUG("[AllGatherNHR][GetStepInfo] i[%u] txSliceIdx[%u] rxSliceIdx[%u]", i, txSliceIdx, rxSliceIdx);

        txSliceIdx = (txSliceIdx + tempRankSize_ - deltaSliceIndex) % tempRankSize_;
        rxSliceIdx = (rxSliceIdx + tempRankSize_ - deltaSliceIndex) % tempRankSize_;
    }
    return HcclResult::HCCL_SUCCESS;
}

RankId InsTempAllGatherNHR::GetRankFromMap(const u32 rankIdx)
{
    return tempVTopo_[0].at(rankIdx);
}

HcclResult InsTempAllGatherNHR::LocalDataCopy(std::vector<InsQuePtr> &tempInsQues)
{
    u32 algRankIdx = 0;
    CHK_RET(GetAlgRank(myRank_, tempVTopo_[0], algRankIdx));

    for (u64 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
        const u64 inBaseOff  = tempAlgParams_.buffInfo.inBuffBaseOff  + rpt * tempAlgParams_.inputRepeatStride;
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff +
            rpt * (tempAlgParams_.sliceSize * tempRankSize_);

        const u64 inOff = tempAlgParams_.inputSliceStride  * algRankIdx + inBaseOff;
        const u64 scOff = scratchBase + tempAlgParams_.sliceSize * algRankIdx;

        DataSlice src(tempAlgParams_.buffInfo.inBuffType,   inOff, tempAlgParams_.sliceSize);
        DataSlice dst(tempAlgParams_.buffInfo.scratBuffType, scOff, tempAlgParams_.sliceSize);

        auto ins = std::make_unique<InsLocalCopy>(src, dst);
         tempInsQues[0]->Append(std::move(ins));
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherNHR::PostLocalCopy(std::vector<InsQuePtr> &tempInsQues)
{
    CHK_PRT_RET(tempInsQues.empty(),
        HCCL_ERROR("[AG-NHR][PostLocalCopy] empty queue"), HcclResult::HCCL_E_INTERNAL);
    CHK_PTR_NULL(tempInsQues[0]);
    for (u64 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
        const u64 outBaseOff = tempAlgParams_.buffInfo.outBuffBaseOff + rpt * tempAlgParams_.outputRepeatStride;
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff +
            rpt * (tempAlgParams_.sliceSize * tempRankSize_);

        for (u32 algIdx = 0; algIdx < tempRankSize_; ++algIdx) {
            const u64 scratchOffset = scratchBase + tempAlgParams_.sliceSize * algIdx;
            const u64 outOffset = tempAlgParams_.outputSliceStride * algIdx + outBaseOff;

            DataSlice src(tempAlgParams_.buffInfo.scratBuffType, scratchOffset, tempAlgParams_.sliceSize);
            DataSlice dst(tempAlgParams_.buffInfo.outBuffType,   outOffset,     tempAlgParams_.sliceSize);

            auto ins = std::make_unique<InsLocalCopy>(src, dst);
            tempInsQues[0]->Append(std::move(ins));
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherNHR::RunNHR(std::vector<InsQuePtr> &tempInsQues)
{
    const u32 nSteps = GetNHRStepNum(tempRankSize_);

    for (u32 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
        const u64 scratchRepeatStride = tempAlgParams_.sliceSize * tempRankSize_;
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff + rpt * scratchRepeatStride;

        for (u32 step = 0; step < nSteps; ++step) {
            AicpuNHRStepInfo stepInfo;
            CHK_RET(GetStepInfo(step, nSteps, stepInfo));

            const std::vector<LinkData> &linkRecv = tempLinks_.at(GetRankFromMap(stepInfo.fromRank));
            const std::vector<LinkData> &linkSend = tempLinks_.at(GetRankFromMap(stepInfo.toRank));

            std::vector<DataSlice> txSrcSlices;
            std::vector<DataSlice> txDstSlices;
            std::vector<DataSlice> rxSrcSlices;
            std::vector<DataSlice> rxDstSlices;

            HCCL_DEBUG("[InsTempAllGatherNHR] rank[%d] rankSize[%u] recvFrom[%u] sendTo[%u] step[%u] nSteps[%u] nSlices[%u]",
                myRank_, tempRankSize_, stepInfo.fromRank, stepInfo.toRank, step, nSteps, stepInfo.nSlices);

            for (u32 i = 0; i < stepInfo.nSlices; ++i) {
                const u32 txIdx = stepInfo.txSliceIdxs[i];
                const u32 rxIdx = stepInfo.rxSliceIdxs[i];
                
                const u64 txScratchOff = scratchBase + tempAlgParams_.sliceSize * txIdx;

                const u64 rxScratchOff = scratchBase + tempAlgParams_.sliceSize * rxIdx;

                txSrcSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, txScratchOff, tempAlgParams_.sliceSize);
                txDstSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, txScratchOff, tempAlgParams_.sliceSize);
                rxSrcSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, rxScratchOff, tempAlgParams_.sliceSize);
                rxDstSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, rxScratchOff, tempAlgParams_.sliceSize);
            }

            TxRxSlicesList sendRecvSlicesList({txSrcSlices, txDstSlices}, {rxSrcSlices, rxDstSlices});
            TxRxLinks sendRecvLinks(linkSend[0], linkRecv[0]);
            SendRecvInfo sendRecvInfo(sendRecvLinks, sendRecvSlicesList);

            CHK_PRT_RET(SendRecv(sendRecvInfo, tempInsQues[0], 0, true, dmaMode_),
                HCCL_ERROR("[InsTempAllGatherNHR] sendrecv failed (step=%u, rpt=%u)", step, rpt),
                HcclResult::HCCL_E_INTERNAL);
        }
    }
    return HcclResult::HCCL_SUCCESS;
}
} // namespace Hccl
