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
#include "ins_temp_all_gather_mesh.h"

namespace Hccl {
InsTempAllGatherMesh1D::InsTempAllGatherMesh1D(const RankId virtualRank, const u32 tempRankSize,
                                           const std::vector<std::vector<RankId>> &tempVTopo,
                                           const std::map<RankId, u32>            &tempVirtRankMap)
    : InsAlgTemplateBase(virtualRank, tempRankSize, tempVTopo, tempVirtRankMap)
{
}

InsTempAllGatherMesh1D::~InsTempAllGatherMesh1D()
{
}

HcclResult InsTempAllGatherMesh1D::CalcRes(AlgTempResReq &tempResReq)
{
    HCCL_DEBUG("[InsTempAllGatherMesh1D] Enter CalcRes");
    tempResReq.queNum = tempVTopo_[0].size();
    tempResReq.streamNum = tempResReq.queNum;
    tempResReq.queNotifys = CreateMasterSlaveQueNotifiesRequest(tempResReq.queNum);
    HCCL_DEBUG("[InsTempAllGatherMesh1D] CalcRes queNotifys size[%zu]", tempResReq.queNotifys.size());

    QId centerQ = 0;
    tempResReq.localWaitGroupCntNotify.emplace_back(centerQ, 0);
    tempResReq.localBcastPostCntNotify.emplace_back(centerQ, 0);

    CHK_RET(CalcResLinksMesh(myRank_, tempRankSize_, tempVTopo_, linkNumBtwPeers_, tempResReq));
    HCCL_DEBUG("[InsTempAllGatherMesh1D] CalcRes done");
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherMesh1D::CalcSliceInfo(const AllignInfo &allignInfo, const u64 dataSize,
                                               RankSliceInfo &sliceInfoVec)
{
    std::vector<SliceInfo> tmp(1);
    sliceInfoVec.resize(tempRankSize_, tmp);

    CHK_RET(CalcRsAgSliceInfoMesh(myRank_, tempRankSize_, allignInfo, dataSize, sliceInfoVec));

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherMesh1D::GenExtIns(const TempFuncs &tempFuncs, const TemplateDataParams &tempAlgParams,
                                           const ResLinks &tempLinks,
                                           std::vector<InsQuePtr> &tempInsQues)
{
    HCCL_INFO("[InsTempAllGatherMesh1D] RunAllGather start");

    opMode_ = tempFuncs.opMode;
    tempAlgParams_ = tempAlgParams;
    tempLinks_ = tempLinks;

    CHK_PRT_RET(tempInsQues.size() != tempVTopo_[0].size(),
        HCCL_ERROR("[InsTempAllGatherMesh1D] RunAllGather Rank [%d], requiredQueNum [%u] not equals to "
                   "templateQueNum [%u].",
                   myRank_, tempVTopo_[0].size(), tempInsQues.size()),
        HcclResult::HCCL_E_INTERNAL);

    CHK_RET(LocalCopyToScratch(tempInsQues[0]));

    // semaphore sync
    CHK_RET(PreSyncInterQueues(tempInsQues));

    // Local Copy from Input to Output
    CHK_RET(LocalCopyToUsrOut(tempInsQues[0]));

    // locate myRank in tempVTopo -> algRank
    u32 myAlgRank;
    CHK_RET(GetAlgRank(myRank_, tempVTopo_[0], myAlgRank));

    // run Mesh 使用第1至rankSize条queue
    CHK_PRT_RET(
        RunMesh(myAlgRank, tempVTopo_[0], tempInsQues) != HcclResult::HCCL_SUCCESS,
        HCCL_ERROR("[InsTempAllGatherMesh1D] Rank [%d], unable to run mesh algorithm.", myRank_),
        HcclResult::HCCL_E_INTERNAL);

    // semaphore sync
    CHK_RET(PostSyncInterQueues(tempInsQues));

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherMesh1D::LocalCopyToUsrOut(InsQuePtr tempInsQue)
{
    if (tempAlgParams_.buffInfo.inBuffType == tempAlgParams_.buffInfo.outBuffType) {
        return HcclResult::HCCL_SUCCESS;
    }

    u32 myAlgRank;
    CHK_RET(GetAlgRank(myRank_, tempVTopo_[0], myAlgRank));
    for (u32 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
        const u64 inBaseOff  = tempAlgParams_.buffInfo.inBuffBaseOff  + rpt * tempAlgParams_.inputRepeatStride;
        const u64 outBaseOff = tempAlgParams_.buffInfo.outBuffBaseOff + rpt * tempAlgParams_.outputRepeatStride;

        const u64 inOff = tempAlgParams_.inputSliceStride * myAlgRank + inBaseOff;
        const u64 outOff = tempAlgParams_.outputSliceStride * myAlgRank + outBaseOff;

        DataSlice src(tempAlgParams_.buffInfo.inBuffType, inOff, tempAlgParams_.sliceSize);
        DataSlice dst(tempAlgParams_.buffInfo.outBuffType, outOff, tempAlgParams_.sliceSize);
        HCCL_INFO("[InsTempAllGatherMesh1D] in:%s -> out:%s", src.Describe().c_str(), dst.Describe().c_str());

        auto ins = std::make_unique<InsLocalCopy>(src, dst);
        tempInsQue->Append(std::move(ins));
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherMesh1D::LocalCopyToScratch(InsQuePtr tempInsQue)
{
    u32 myAlgRank;
    CHK_RET(GetAlgRank(myRank_, tempVTopo_[0], myAlgRank));

    if (opMode_ == OpMode::OPBASE) {
        for (u32 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
            const u64 scratchRepeatStride = tempAlgParams_.sliceSize * tempRankSize_;
            const u64 inBaseOff  = tempAlgParams_.buffInfo.inBuffBaseOff + rpt * tempAlgParams_.inputRepeatStride;
            const u64 outBaseOff = tempAlgParams_.buffInfo.scratchBuffBaseOff + rpt * scratchRepeatStride;
            const u64 inOff = tempAlgParams_.inputSliceStride * myAlgRank + inBaseOff;
            const u64 outOff = tempAlgParams_.sliceSize * myAlgRank + outBaseOff;

            DataSlice src(tempAlgParams_.buffInfo.inBuffType, inOff, tempAlgParams_.sliceSize);
            DataSlice dst(tempAlgParams_.buffInfo.scratBuffType, outOff, tempAlgParams_.sliceSize);
            HCCL_INFO("[InsTempAllGatherMesh1D] in:%s -> scratch:%s", src.Describe().c_str(), dst.Describe().c_str());

            auto ins = std::make_unique<InsLocalCopy>(src, dst);
            tempInsQue->Append(std::move(ins));
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempAllGatherMesh1D::RunMesh(const u32 myAlgRank, const std::vector<RankId> &vTopo,
                                           std::vector<InsQuePtr> &tempInsQues)
{
    for (u32 rpt = 0; rpt < tempAlgParams_.repeatNum; ++rpt) {
        const u64 inBaseOff = tempAlgParams_.buffInfo.inBuffBaseOff + rpt * tempAlgParams_.inputRepeatStride;
        const u64 outBaseOff = tempAlgParams_.buffInfo.outBuffBaseOff + rpt * tempAlgParams_.outputRepeatStride;
        const u64 scratchRepeatStride = tempAlgParams_.sliceSize * tempRankSize_;
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff + rpt * scratchRepeatStride;

        for (u32 queIdx = 0; queIdx < vTopo.size() - 1; queIdx++) {
            RankId connectedRank = vTopo[(myAlgRank + 1 + queIdx) % vTopo.size()];
            
            u32 connectedAlgRank = 0;
            CHK_RET(GetAlgRank(connectedRank, tempVTopo_[0], connectedAlgRank));
            auto it = tempLinks_.find(connectedRank);
            if (it == tempLinks_.end()) {
                HCCL_ERROR("[InsTempAllGatherMesh1D] connectedRank does not exist");
                return HcclResult::HCCL_E_PARA;
            }
            CHK_PRT_RET(queIdx + 1 >= tempInsQues.size() || tempLinks_.at(connectedRank).empty(),
                HCCL_ERROR("[InsTempAllGatherMesh1D] queIdx=%u, tempInsQues.size=%u, connectedRank=%d, tempLinks_.size=%u",
                           queIdx, tempInsQues.size(), connectedRank, tempLinks_.size()),
                HcclResult::HCCL_E_INTERNAL);

            InsQuePtr currQue = tempInsQues[queIdx + 1];
            LinkData &neighborLinkData = tempLinks_.at(connectedRank)[0];

            BufferType writeType = (opMode_ == OpMode::OPBASE) ?
                tempAlgParams_.buffInfo.scratBuffType : tempAlgParams_.buffInfo.inBuffType;

            u64 txInOffset = tempAlgParams_.inputSliceStride * myAlgRank + inBaseOff;
            u64 txOutOffset = tempAlgParams_.outputSliceStride * myAlgRank + outBaseOff;
            u64 txScratchOffset = scratchBase + tempAlgParams_.sliceSize * myAlgRank;
            u64 txDstOffset = (opMode_ == OpMode::OPBASE) ? txScratchOffset : txOutOffset;

            u64 rxInOffset = tempAlgParams_.inputSliceStride * connectedAlgRank + inBaseOff;
            u64 rxOutOffset = tempAlgParams_.outputSliceStride * connectedAlgRank + outBaseOff;
            u64 rxScratchOffset = scratchBase + tempAlgParams_.sliceSize * connectedAlgRank;
            u64 rxSrcOffset = (opMode_ == OpMode::OPBASE) ? rxScratchOffset : rxInOffset;

            vector<DataSlice> txSrcSlices{ DataSlice(tempAlgParams_.buffInfo.inBuffType, txInOffset,
                                                     tempAlgParams_.sliceSize) };
            vector<DataSlice> txDstSlices{ DataSlice(writeType,  txDstOffset, tempAlgParams_.sliceSize) };
            vector<DataSlice> rxSrcSlices{ DataSlice(writeType, rxSrcOffset, tempAlgParams_.sliceSize) };
            vector<DataSlice> rxDstSlices{ DataSlice(tempAlgParams_.buffInfo.outBuffType, rxOutOffset,
                                                     tempAlgParams_.sliceSize) };

            TxRxSlicesList sendRecvSlicesList({txSrcSlices, txDstSlices}, {rxSrcSlices, rxDstSlices});
            TxRxLinks sendRecvLinks(neighborLinkData, neighborLinkData);
            SendRecvInfo sendRecvInfo(sendRecvLinks, sendRecvSlicesList);

            CHK_PRT_RET(SendRecv(sendRecvInfo, currQue, 0, true, DmaMode::GET),
                HCCL_ERROR("[InsTempAllGatherMesh1D] sendrecv failed (nbr=%d, queIdx=%u, rpt=%u)", connectedRank,
                queIdx, rpt), HcclResult::HCCL_E_INTERNAL);
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

} // namespace Hccl
