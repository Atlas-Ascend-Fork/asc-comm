/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "ins_temp_reduce_scatter_nhr.h"
#include "buffer.h"
#include "aicpu_ins.h"
#include "log.h"
#include "alg_data_trans_wrapper.h"

namespace Hccl {
InsTempReduceScatterNHR::InsTempReduceScatterNHR(const RankId virtualRank, const u32 tempRankSize,
                                             const std::vector<std::vector<RankId>> &tempVTopo,
                                             const std::map<RankId, u32>            &tempVirtRankMap)
    : InsAlgTemplateBase(virtualRank, tempRankSize, tempVTopo, tempVirtRankMap)
{
}

InsTempReduceScatterNHR::~InsTempReduceScatterNHR()
{
}

HcclResult InsTempReduceScatterNHR::CalcRes(AlgTempResReq &tempResReq)
{
    // NHR 需要的 que Num 为 1
    tempResReq.queNum = 1;
    tempResReq.streamNum = tempResReq.queNum;
    tempResReq.queNotifys = CreateMasterSlaveQueNotifiesRequest(tempResReq.queNum);
    CHK_PRT_RET(CalcResLinksNHR(myRank_, tempRankSize_, tempVTopo_, tempResReq) != HcclResult::HCCL_SUCCESS,
                HCCL_ERROR("[CollAlgFactory] [InsTempReduceScatterNHR] Rank [%d], resLinks calculation error!", myRank_),
                HcclResult::HCCL_E_INTERNAL);

    return HcclResult::HCCL_SUCCESS;
}

/*
dataSize / (rankSize) --> chunkSize
dataSize / (rankSize * queNum) --> sliceSize

SliceInfoVecforNHR: [1st chunk: [1st Slice, 2nd Slice, ...], 2nd chunk: [1st Slice, 2nd Slice, ...], ...]
*/
HcclResult InsTempReduceScatterNHR::CalcSliceInfo(const AllignInfo &allignInfo, const u64 dataSize, RankSliceInfo &sliceInfoVec)
{
    std::vector<SliceInfo> tmp(tempVTopo_.size());
    sliceInfoVec.resize(tempRankSize_, tmp);

    CHK_RET(CalcRsAgSliceInfoNHR(myRank_, tempRankSize_, allignInfo, dataSize, sliceInfoVec));

    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::Run(const TempFuncs &tempFuncs, const RankSliceInfo &sliceInfoVec,
    const BuffInfo &buffInfo, const ResLinks &tempLinks, std::vector<InsQuePtr> &tempInsQues)
{
    if (IsPcieLink(tempLinks)) {
        dmaMode_ = DmaMode::GET;
    }
    opMode_              = tempFuncs.opMode;
    enableCounterNotify_ = tempFuncs.enableCounterNotify;
    buffInfo_            = buffInfo;
    queNum_ = tempVTopo_.size();
    CHK_PRT_RET(queNum_ != tempInsQues.size(),
                HCCL_ERROR("[CollAlgFactory] [InsTempReduceScatterNHR] Rank [%d], requiredQue Error.", myRank_),
                HcclResult::HCCL_E_INTERNAL);
    CHK_RET(PreCopy(tempFuncs, sliceInfoVec, tempInsQues));
    CHK_RET(RunReduceScatter(sliceInfoVec, tempLinks, tempInsQues));
    CHK_RET(PostCopy(tempFuncs, sliceInfoVec, tempInsQues));
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::PreCopy(const TempFuncs &tempFuncs, const RankSliceInfo &sliceInfoVec,
    std::vector<InsQuePtr> &tempInsQues)
{
    (void) sliceInfoVec;
    // 通信前需要将所有的数据统一拷贝到 inBuff 上的对应位置。
    if (tempFuncs.isForepart && opMode_ == OpMode::OPBASE) {
        // 单算子模式下，第一个算子，需要将数据从 userIn 拷贝到 inBuff
        HCCL_INFO("[InsTempReduceScatterNHR][PreCopy] Opbase Forepart, copy from userIn to outBuff");
        CHK_RET(MultiSliceLocalCopy(tempInsQues[0], tempFuncs.usrData.usrInSlices,
            tempFuncs.usrData.scratchInSlices));
    } else {
        // 图模式或者单算子模式下非第一个算子，数据已经在 inbuff 上了，不需要拷贝
        HCCL_INFO("[InsTempReduceScatterNHR][PreCopy] not forpat and opbse, skip precopy");
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::PostCopy(const TempFuncs &tempFuncs, const RankSliceInfo &sliceInfoVec,
                                             std::vector<InsQuePtr> &tempInsQues)
{
    // 通信结束之后，数据都在 inbuff 上，需要搬运到对应的输出位置。
    if (tempFuncs.isBottom && opMode_ == OpMode::OPBASE) {
        // 如果是单算子模式, 并且是最后一步算子，需要将数据从 inBuff 拷贝到 userOut
        // 是否需要将数据搬运到 OutBuff 上再搬运到 UserOut 上？？
        HCCL_INFO("[InsTempReduceScatterNHR][PostCopy] Opbase Bottom, copy from outBuff to userOut");
        CHK_RET(
            MultiSliceLocalCopy(tempInsQues[0], tempFuncs.usrData.scratchOutSlices, tempFuncs.usrData.usrOutSlices));
    } else if (tempFuncs.forAllReduce) {
        // 如果是 forAllReduce 算子的前半部分需要将数据从 inBuff 拷贝到 outBuff 并且加上本rank的偏移
        if (buffInfo_.inBuffType != buffInfo_.outBuffType || buffInfo_.inBuffBaseOff != buffInfo_.outBuffBaseOff) {
            HCCL_INFO("[InsTempReduceScatterNHR][PostCopy] forAllReduce, copy from inBuff to outBuff");
            u64 size = sliceInfoVec[tempVirtRankMap_[myRank_]][0].size;
            u64 srcOffset = sliceInfoVec[tempVirtRankMap_[myRank_]][0].offset;
            u64 dstOffset = sliceInfoVec[tempVirtRankMap_[myRank_]][0].offset;
            DataSlice srcSlice = DataSlice(buffInfo_.inBuffType, srcOffset + buffInfo_.inBuffBaseOff, size);
            DataSlice dstSlice = DataSlice(buffInfo_.outBuffType, dstOffset + buffInfo_.outBuffBaseOff, size);
            CHK_RET(LocalCopy(tempInsQues[0], srcSlice, dstSlice));
        } else {
            HCCL_INFO("[InsTempReduceScatterNHR][PostCopy] forAllReduce, inBuff same as outBuff, skip copy");
        }
    } else {
        // 如果是图模式，或者单算子模式但不是最后一步算子需要将数据从 inBuff 拷贝到 outBuff 顶头放
        u64 size      = sliceInfoVec[tempVirtRankMap_[myRank_]][0].size;
        u64 srcOffset = sliceInfoVec[tempVirtRankMap_[myRank_]][0].offset + buffInfo_.inBuffBaseOff;
        u64 dstOffset = buffInfo_.outBuffBaseOff;
        if (buffInfo_.inBuffType == buffInfo_.outBuffType && srcOffset == dstOffset) {
            HCCL_INFO(
                "[InsTempReduceScatterNHR][PostCopy] not forpat and opbse, inBuffType same as outBuffType, skip copy");
        } else {
            HCCL_INFO("[InsTempReduceScatterNHR][PostCopy] not forpat and opbse, copy from outBuff to userOut");
            DataSlice srcSlice = DataSlice(buffInfo_.inBuffType, srcOffset, size);
            DataSlice dstSlice = DataSlice(buffInfo_.outBuffType, dstOffset, size);
            CHK_RET(LocalCopy(tempInsQues[0], srcSlice, dstSlice));
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::MultiSliceLocalCopy(InsQuePtr &insQue, const std::vector<DataSlice> &srcList,
                                              const std::vector<DataSlice> &dstList) const
{
    CHK_PRT_RET(srcList.size() != dstList.size(),
                HCCL_ERROR("[InsTempReduceScatterNHR] [LocalCopy] Rank [%d], srcList size[%llu] and "
                           "dstList size[%llu] not same.",
                           myRank_, srcList.size(), dstList.size()),
                HcclResult::HCCL_E_INTERNAL);
    CHK_RET(LocalCopySlices(insQue, srcList, dstList));
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::RunReduceScatter(const RankSliceInfo &sliceInfoVec, const ResLinks &tempLinks,
        std::vector<InsQuePtr> &tempInsQues)
{
    std::vector<AicpuNHRStepInfo> stepInfoList;
    GetStepInfoList(stepInfoList);
    for(auto& stepInfo : stepInfoList) {
        HCCL_DEBUG("[InsTempReduceScatterNHR][RunReduceScatter] step[%u], myRank[%u], toRank[%u], fromRank[%u], nSlices[%u].",
            stepInfo.step, stepInfo.myRank, stepInfo.toRank, stepInfo.fromRank, stepInfo.nSlices);

        const std::vector<LinkData> &linkRecv = tempLinks.at(GetRankFromMap(stepInfo.fromRank));
        const std::vector<LinkData> &linkSend = tempLinks.at(GetRankFromMap(stepInfo.toRank));
        std::vector<DataSlice> txSlices;
        std::vector<DataSlice> rxSlices;

        // 在 inBuff 上进行 ReduceScatter 操作
        for (u32 i = 0; i < stepInfo.nSlices; i++) {
            u64 txOffset   = sliceInfoVec[stepInfo.txSliceIdxs[i]][0].offset  + buffInfo_.inBuffBaseOff;
            u64 txSize     = sliceInfoVec[stepInfo.txSliceIdxs[i]][0].size;
            u64 rxOffset   = sliceInfoVec[stepInfo.rxSliceIdxs[i]][0].offset  + buffInfo_.inBuffBaseOff;
            u64 rxSize     = sliceInfoVec[stepInfo.rxSliceIdxs[i]][0].size;
            DataSlice txSlice = DataSlice(buffInfo_.inBuffType, txOffset, txSize);
            DataSlice rxSlice = DataSlice(buffInfo_.inBuffType, rxOffset, rxSize);
            txSlices.push_back(txSlice);
            rxSlices.push_back(rxSlice);
        }
        SendRecvReduceInfo sendRecvReduceInfo{
            {linkSend[0],linkRecv[0]},
            {{txSlices, txSlices},{rxSlices, rxSlices}}, dataType_, redOp_
        };
        CHK_PRT_RET(SendRecvReduce(sendRecvReduceInfo, tempInsQues[0], 0, true, dmaMode_),
            HCCL_ERROR("[InsTempReduceScatterNHR] RunReduceScatter SendRecvReduce failed"),
            HcclResult::HCCL_E_INTERNAL);
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::GenExtIns(const TempFuncs &tempFuncs,
    const TemplateDataParams &tempAlgParams,
    const ResLinks &tempLinks,
    std::vector<InsQuePtr> &tempInsQues)
{
    HCCL_INFO("[InsTempReduceScatterNHR] GenExtIns start");
    if (IsPcieLink(tempLinks)) {
        dmaMode_ = DmaMode::GET;
    }
    opMode_              = tempFuncs.opMode;
    enableCounterNotify_ = tempFuncs.enableCounterNotify;
    tempAlgParams_       = tempAlgParams;
    tempLinks_           = tempLinks;
    buffInfo_            = tempAlgParams_.buffInfo;

    queNum_ = tempVTopo_.size();
    CHK_PRT_RET(tempInsQues.size() != queNum_,
        HCCL_ERROR("[RS-NHR][GenExtIns] Rank[%d] queNum mismatch: need %u, got %zu",
            myRank_, queNum_, tempInsQues.size()),
        HcclResult::HCCL_E_INTERNAL);

    CHK_RET(LocalDataCopy(tempInsQues, tempFuncs));

    if (tempRankSize_ <= 1) {
        CHK_RET(PostLocalCopy(tempInsQues));
        return HcclResult::HCCL_SUCCESS;
    }

    CHK_RET(RunNHR(tempInsQues));
    CHK_RET(PostLocalCopy(tempInsQues));
    return HcclResult::HCCL_SUCCESS;
}


HcclResult InsTempReduceScatterNHR::LocalDataCopy(std::vector<InsQuePtr> &tempInsQues, const TempFuncs &tempFuncs)
{
    CHK_PRT_RET(tempInsQues.empty(),
        HCCL_ERROR("[RS-NHR][LocalDataCopy] empty queue"), HcclResult::HCCL_E_INTERNAL);
    InsQuePtr q = tempInsQues[0];
    u64 inBaseOff;
    u64 inOff;
    const u64 rptNum = std::max<u64>(1, tempAlgParams_.repeatNum);
    for (u32 localRandId = 0; localRandId < tempRankSize_; ++localRandId) { 
        for (u64 rpt = 0; rpt < rptNum; ++rpt) { 
            if (tempFuncs.isBottom) { // 后nhr 前一半数据
                inBaseOff = tempAlgParams_.buffInfo.inBuffBaseOff +
                                  rpt * tempAlgParams_.inputRepeatStride;
                inOff = inBaseOff + localRandId * tempAlgParams_.inputSliceStride;                  
            } else { // 前nhr，后一半数据
                inBaseOff = tempAlgParams_.buffInfo.inBuffBaseOff +
                                  localRandId * tempAlgParams_.inputRepeatStride;
                inOff = inBaseOff + rpt * tempAlgParams_.inputSliceStride;
            }
            const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff +
                                    rpt * tempAlgParams_.outputRepeatStride;                     
            const u64 scOff = scratchBase + localRandId * tempAlgParams_.sliceSize;
            // 如果源地址和目标地址相同，则不需要做拷贝
            if (tempAlgParams_.buffInfo.inBuffType != tempAlgParams_.buffInfo.scratBuffType || inOff != scOff) { 
                DataSlice src(tempAlgParams_.buffInfo.inBuffType, inOff, tempAlgParams_.sliceSize);
                DataSlice dst(tempAlgParams_.buffInfo.scratBuffType, scOff, tempAlgParams_.sliceSize);
                auto ins = std::make_unique<InsLocalCopy>(src, dst);
                q->Append(std::move(ins));
            }
        }
    }
    return HcclResult::HCCL_SUCCESS;
}


HcclResult InsTempReduceScatterNHR::PostLocalCopy(std::vector<InsQuePtr> &tempInsQues)
{
    CHK_PRT_RET(tempInsQues.empty(),
        HCCL_ERROR("[RS-NHR][PostLocalCopy] empty queue"), HcclResult::HCCL_E_INTERNAL);

    const u32 myAlgIdx = tempVirtRankMap_.at(myRank_);
    InsQuePtr q = tempInsQues[0];

    const u64 rptNum = std::max<u64>(1, tempAlgParams_.repeatNum);
    for (u64 rpt = 0; rpt < rptNum; ++rpt) {
        const u64 outBaseOff  = tempAlgParams_.buffInfo.outBuffBaseOff
                              + rpt * tempAlgParams_.outputRepeatStride;
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff
                              + rpt * tempAlgParams_.outputRepeatStride;

        const u64 scOff  = scratchBase + tempAlgParams_.sliceSize * myAlgIdx;
        const u64 outOff = outBaseOff;

        DataSlice src(tempAlgParams_.buffInfo.scratBuffType, scOff,  tempAlgParams_.sliceSize);
        DataSlice dst(tempAlgParams_.buffInfo.outBuffType,   outOff, tempAlgParams_.sliceSize);
        if (tempAlgParams_.buffInfo.scratBuffType != tempAlgParams_.buffInfo.outBuffType || scOff != outOff) {
            auto ins = std::make_unique<InsLocalCopy>(src, dst);
            q->Append(std::move(ins));
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

HcclResult InsTempReduceScatterNHR::RunNHR(std::vector<InsQuePtr> &tempInsQues)
{
    CHK_PRT_RET(tempInsQues.empty(),
        HCCL_ERROR("[RS-NHR][RunNHR] empty queue"), HcclResult::HCCL_E_INTERNAL);

    if (tempRankSize_ <= 1) return HcclResult::HCCL_SUCCESS;

    InsQuePtr q = tempInsQues[0];

    // 步进参数
    const u64 rptNum = std::max<u64>(1, tempAlgParams_.repeatNum);

    // 预计算步骤列表（算法序）
    std::vector<AicpuNHRStepInfo> steps;
    CHK_RET(GetStepInfoList(steps));
    for (u64 rpt = 0; rpt < rptNum; ++rpt) {
        const u64 scratchBase = tempAlgParams_.buffInfo.scratchBuffBaseOff
                              + rpt * tempAlgParams_.outputRepeatStride;
        for (u32 s = 0; s < steps.size(); ++s) {
            const auto &st = steps[s];

            const RankId recvFromRank = GetRankFromMap(st.fromRank);
            const RankId sendToRank   = GetRankFromMap(st.toRank);
            CHK_PRT_RET(recvFromRank == static_cast<RankId>(-1) || sendToRank == static_cast<RankId>(-1),
                HCCL_ERROR("[RS-NHR][RunNHR] rank map failed: from[%u] to[%u]", st.fromRank, st.toRank),
                HcclResult::HCCL_E_INTERNAL);

            auto itRecv = tempLinks_.find(recvFromRank);
            auto itSend = tempLinks_.find(sendToRank);
            CHK_PRT_RET(itRecv == tempLinks_.end() || itRecv->second.empty() ||
                        itSend == tempLinks_.end() || itSend->second.empty(),
                HCCL_ERROR("[RS-NHR][RunNHR] link missing: recvFrom=%d sendTo=%d", recvFromRank, sendToRank),
                HcclResult::HCCL_E_INTERNAL);

            const LinkData &linkRecv = itRecv->second[0];
            const LinkData &linkSend = itSend->second[0];

            std::vector<DataSlice> txSlices;
            std::vector<DataSlice> rxSlices;
            txSlices.reserve(st.nSlices);
            rxSlices.reserve(st.nSlices);

            // RS：在 SCRATCH 上进行规约交换
            for (u32 i = 0; i < st.nSlices; ++i) {
                const u32 txIdx = st.txSliceIdxs[i]; // 算法序
                const u32 rxIdx = st.rxSliceIdxs[i];

                const u64 txScOff = scratchBase + tempAlgParams_.sliceSize * txIdx;
                const u64 rxScOff = scratchBase + tempAlgParams_.sliceSize * rxIdx;

                txSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, txScOff, tempAlgParams_.sliceSize);
                rxSlices.emplace_back(tempAlgParams_.buffInfo.scratBuffType, rxScOff, tempAlgParams_.sliceSize);
            }

            SendRecvReduceInfo info{
                { linkSend, linkRecv }, { { txSlices, txSlices }, { rxSlices, rxSlices } }, dataType_, redOp_
            };

            CHK_PRT_RET(SendRecvReduce(info, tempInsQues[0], 0, true, dmaMode_),
                HCCL_ERROR("[RS-NHR][RunNHR] SendRecvReduce failed (step=%u, rpt=%llu)",
                    st.step, static_cast<unsigned long long>(rpt)),
                HcclResult::HCCL_E_INTERNAL);
        }
    }

    return HcclResult::HCCL_SUCCESS;
}


//  计算每轮收发的对端以及slice编号
HcclResult InsTempReduceScatterNHR::GetStepInfoList(std::vector<AicpuNHRStepInfo> &stepInfoList)
{
    // 将本 rank 号转换成算法使用的索引号
    u32 rankIdx = tempVirtRankMap_[myRank_];
    stepInfoList.clear();

    u32 nSteps = GetNHRStepNum(tempRankSize_);
    stepInfoList.resize(nSteps);
    for (u32 step = 0; step < nSteps; step++) {
        // 计算通信对象
        u32 deltaRank = 1 << step;
        u32 sendTo = (rankIdx + tempRankSize_ - deltaRank) % tempRankSize_;
        u32 recvFrom = (rankIdx + deltaRank) % tempRankSize_;

        // 数据份数和数据编号增量
        u32 nSlices = (tempRankSize_ - 1 + (1 << step)) / (1 << (step + 1));
        u32 deltaSliceIndex = 1 << (step + 1);
        u32 txSliceIdx = sendTo;
        u32 rxSliceIdx = rankIdx;

        AicpuNHRStepInfo &currStepInfo = stepInfoList[step];
        currStepInfo.step = step;
        currStepInfo.myRank = rankIdx;
        currStepInfo.nSlices = nSlices;
        currStepInfo.toRank = sendTo;
        currStepInfo.fromRank = recvFrom;

        // 计算本rank在每轮收/发中的slice编号
        currStepInfo.txSliceIdxs.reserve(nSlices);
        currStepInfo.rxSliceIdxs.reserve(nSlices);
        for (u32 i = 0; i < nSlices; i++) {
            currStepInfo.txSliceIdxs.push_back(txSliceIdx);
            currStepInfo.rxSliceIdxs.push_back(rxSliceIdx);
            HCCL_DEBUG("[InsTempReduceScatterNHR][GetStepInfoList] i[%u] txSliceIdx[%u] rxSliceIdx[%u]", i, txSliceIdx, rxSliceIdx);
            txSliceIdx = (txSliceIdx + tempRankSize_ - deltaSliceIndex) % tempRankSize_;
            rxSliceIdx = (rxSliceIdx + tempRankSize_ - deltaSliceIndex) % tempRankSize_;
        }
    }
    return HcclResult::HCCL_SUCCESS;
}

RankId InsTempReduceScatterNHR::GetRankFromMap(const u32 rankIdx)
{
    RankId rank = -1;
    for (auto &pair : tempVirtRankMap_) {
        if (pair.second == rankIdx) {
            rank = pair.first;
            break;
        }
    }
    return rank;
}

HcclResult InsTempReduceScatterNHR::GetScratchBufferInfo(const uint64_t scratchBufferSize, DataType dataType) const
{
    (void)scratchBufferSize;
    (void)dataType;
    return HcclResult::HCCL_SUCCESS;
}

} // namespace Hccl
