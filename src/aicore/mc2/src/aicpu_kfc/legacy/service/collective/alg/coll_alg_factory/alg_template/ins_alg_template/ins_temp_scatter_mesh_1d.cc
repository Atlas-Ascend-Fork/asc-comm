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
#include "ins_temp_scatter_mesh_1d.h"

namespace Hccl {
InsTempScatterMesh1D::InsTempScatterMesh1D(const RankId virtualRank, const u32 tempRankSize,
                                        const std::vector<std::vector<RankId>> &tempVTopo,
                                        const std::map<RankId, u32>            &tempVirtRankMap)
    : InsAlgTemplateBase(virtualRank, tempRankSize, tempVTopo, tempVirtRankMap)
{
}

InsTempScatterMesh1D::~InsTempScatterMesh1D()
{
}

HcclResult InsTempScatterMesh1D::CalcRes(AlgTempResReq &tempResReq)
{
    HCCL_DEBUG("Enter InsTempScatterMesh1D::CalcRes");
    tempResReq.queNum = tempVTopo_[0].size() - 1;
    tempResReq.streamNum = tempResReq.queNum;
    tempResReq.queNotifys = CreateMasterSlaveQueNotifiesRequest(tempResReq.queNum);

    QId centerQ = 0;
    tempResReq.localWaitGroupCntNotify.emplace_back(centerQ, 0);
    tempResReq.localBcastPostCntNotify.emplace_back(centerQ, 0);

    CHK_RET(CalcResLinksMesh(myRank_, tempRankSize_, tempVTopo_, linkNumBtwPeers_, tempResReq));
    return HcclResult::HCCL_SUCCESS;
}

u32 InsTempScatterMesh1D::CalcScratchMultiple(BufferType inBuffType, BufferType outBuffType)
{
    (void) inBuffType;
    (void) outBuffType;
    if (op_.opMode == OpMode::OPBASE) {
        return 1;
    } else {
        return 0;
    }
}

// 需要支持 input->output, input->scratch, scratch->output
HcclResult InsTempScatterMesh1D::GenExtIns(TempFuncs &tempFuncs, TemplateDataParams &tempAlgParams,
                    ResLinks &tempResLinks, std::vector<InsQuePtr> &tempInsQues)
{
    HCCL_INFO("[InsTempScatterMesh1D][Run] start: Rank [%d]", myRank_);

    opMode_              = tempFuncs.opMode;
    buffInfo_            = tempAlgParams.buffInfo;
    majorQueNum_ = tempVTopo_[0].size() - 1;
    isZeroCopy_ = opMode_ == OpMode::OFFLOAD && buffInfo_.inBuffType == BufferType::INPUT &&
                  buffInfo_.outBuffType == BufferType::OUTPUT;

    // queNumPerNeighbor_初始化是1
    CHK_PRT_RET(majorQueNum_ * queNumPerNeighbor_ != tempInsQues.size(),
                HCCL_ERROR("[InsCollAlgFactory] [InsTempScatterMesh1D] Rank [%d], requiredQueNum [%u] not equals to "
                            "templateQueNum [%u].",
                            myRank_, majorQueNum_ * queNumPerNeighbor_, tempInsQues.size()),
                HcclResult::HCCL_E_INTERNAL);

    PreCopy(tempAlgParams, tempInsQues);
    // semaphore sync
    if (majorQueNum_ > 1) { // more than one rank
        CHK_RET(PreSyncInterQueues(tempInsQues));
    }

    // run Mesh
    CHK_RET(RunMesh(tempAlgParams, tempResLinks, tempInsQues));

    // semaphore sync
    if (majorQueNum_ > 1) { // more than one rank
        CHK_RET(PostSyncInterQueues(tempInsQues));
    }
    PostCopy(tempAlgParams, tempInsQues);
    return HcclResult::HCCL_SUCCESS;
}

uint64_t InsTempScatterMesh1D::GetExpandedMode() const
{
    return 1;
}

HcclResult InsTempScatterMesh1D::RunMesh(TemplateDataParams &tempAlgParams,
                    ResLinks &tempResLinks, std::vector<InsQuePtr> &tempInsQues)
{
    u32 myAlgRank;
    GetAlgRank(myRank_, tempVTopo_[0], myAlgRank);
    for (u32 r = 0; r < tempAlgParams.repeatNum; r++) {
        if (root_ == u32(myRank_)) {
            u32 count = 0;
            for (u32 algRank = 0; algRank < tempVTopo_[0].size(); algRank++) {
                if (myAlgRank == algRank) {
                    continue;
                }
                if (tempInsQues.size() < tempVTopo_[0].size() - 1) {
                    HCCL_ERROR("tempInsQues size [%zu] is smaller than tempVTopo_[0].size() -1 [%zu]", tempInsQues.size(), tempVTopo_[0].size() - 1);
                    return HcclResult::HCCL_E_INTERNAL;
                }
                u32 peerRank = tempVTopo_[0][algRank];
                const LinkData &linkSend = tempResLinks.at(peerRank)[0];
                u64 srcOffset = buffInfo_.inBuffType == BufferType::SCRATCH ?
                                buffInfo_.scratchBuffBaseOff + r * tempAlgParams.inputRepeatStride + algRank * tempAlgParams.inputSliceStride :
                                r * tempAlgParams.inputRepeatStride + algRank * tempAlgParams.inputSliceStride + buffInfo_.inBuffBaseOff;
                u64 dstOffset = isZeroCopy_ ?
                                buffInfo_.outBuffBaseOff + r * tempAlgParams.outputRepeatStride :
                                buffInfo_.scratchBuffBaseOff + r * tempAlgParams.outputRepeatStride;
                BufferType dstBuffType = isZeroCopy_ ? BufferType::OUTPUT : BufferType::SCRATCH;
                DataSlice srcSlice(buffInfo_.inBuffType, srcOffset, tempAlgParams.sliceSize);
                DataSlice dstSlice(dstBuffType, dstOffset, tempAlgParams.sliceSize);
                SlicesList txSlicesList({srcSlice}, {dstSlice});
                DataInfo sendData(linkSend, txSlicesList);
                CHK_PRT_RET(Send(sendData, tempInsQues[count], 0, true, DmaMode::PUT), HCCL_ERROR("[InsTempScatterMesh1D] BatchSend failed"),
                        HcclResult::HCCL_E_INTERNAL);
                count++;
            }
        } else {
            const LinkData &linkRecv = tempResLinks.at(root_)[0];
            u64 srcOffset = buffInfo_.inBuffType == BufferType::SCRATCH ?
                            buffInfo_.scratchBuffBaseOff + r * tempAlgParams.inputRepeatStride + myAlgRank * tempAlgParams.inputSliceStride :
                            r * tempAlgParams.inputRepeatStride + myAlgRank * tempAlgParams.inputSliceStride + buffInfo_.inBuffBaseOff;
            u64 dstOffset = isZeroCopy_ ?
                            buffInfo_.outBuffBaseOff + r * tempAlgParams.outputRepeatStride :
                            buffInfo_.scratchBuffBaseOff + r * tempAlgParams.outputRepeatStride;
            BufferType dstBuffType = isZeroCopy_ ? BufferType::OUTPUT : BufferType::SCRATCH;
            DataSlice srcSlice(buffInfo_.inBuffType, srcOffset, tempAlgParams.sliceSize);
            DataSlice dstSlice(dstBuffType, dstOffset, tempAlgParams.sliceSize);
            SlicesList rxSlicesList({srcSlice}, {dstSlice});
            DataInfo recvData(linkRecv, rxSlicesList);
            CHK_PRT_RET(Recv(recvData, tempInsQues[0], 0, true, DmaMode::PUT), HCCL_ERROR("[InsTempScatterMesh1D] BatchRecv failed"),
                    HcclResult::HCCL_E_INTERNAL);
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterMesh1D::PreCopy(TemplateDataParams &tempAlgParams, std::vector<InsQuePtr> &tempInsQues)
{
    if (u32(myRank_) != root_) {
        return HCCL_SUCCESS;
    }
    u32 myAlgRank;
    GetAlgRank(myRank_, tempVTopo_[0], myAlgRank);
    for (u32 r = 0; r < tempAlgParams.repeatNum; r++) {
        u64 srcOffset = buffInfo_.inBuffType == BufferType::SCRATCH ?
                        r * tempAlgParams.inputRepeatStride + tempAlgParams.inputSliceStride * myAlgRank + buffInfo_.scratchBuffBaseOff :
                        r * tempAlgParams.inputRepeatStride + tempAlgParams.inputSliceStride * myAlgRank + buffInfo_.inBuffBaseOff;
        u64 dstOffset = buffInfo_.outBuffType == BufferType::SCRATCH ?
                        r * tempAlgParams.outputRepeatStride + buffInfo_.scratchBuffBaseOff :
                        r * tempAlgParams.outputRepeatStride + buffInfo_.outBuffBaseOff;

        DataSlice srcSlice(buffInfo_.inBuffType, srcOffset, tempAlgParams.sliceSize);
        DataSlice dstSlice(buffInfo_.outBuffType, dstOffset, tempAlgParams.sliceSize);
        LocalCopy(tempInsQues[0], srcSlice, dstSlice);
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempScatterMesh1D::PostCopy(const TemplateDataParams &tempAlgParams, std::vector<InsQuePtr> &tempInsQues)
{
    if (u32(myRank_) == root_ || buffInfo_.outBuffType == BufferType::SCRATCH || isZeroCopy_) {
        return HCCL_SUCCESS;
    }
    u32 myAlgRank;
    GetAlgRank(myRank_, tempVTopo_[0], myAlgRank);
    DataSlice dstSlice(BufferType::OUTPUT, buffInfo_.outBuffBaseOff, tempAlgParams.sliceSize * tempAlgParams.repeatNum);
    DataSlice srcSlice(BufferType::SCRATCH, buffInfo_.scratchBuffBaseOff, tempAlgParams.sliceSize * tempAlgParams.repeatNum);
    LocalCopy(tempInsQues[0], srcSlice, dstSlice);

    return HcclResult::HCCL_SUCCESS;
}

} // namespace Hccl
