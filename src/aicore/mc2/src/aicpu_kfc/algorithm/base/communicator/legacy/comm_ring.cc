/**
* Copyright (c) 2025 Huawei Technologies Co., Ltd.
* This program is free software, you can redistribute it and/or modify it under the terms and conditions of
* CANN Open Software License Agreement Version 2.0 (the "License").
* Please refer to the License for details. You may not use this file except in compliance with the License.
* THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
* INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
* See LICENSE in the root of the software repository for the full text of the License.
*/
#include "comm_ring.h"

namespace hccl {
CommRing::CommRing(const std::string &collectiveId, const u32 userRank,
                   const u32 userRankSize, const u32 rank, const u32 rankSize, const TopoType topoFlag,
                   const HcclDispatcher dispatcher, const std::unique_ptr<NotifyPool> &notifyPool,
                   std::map<HcclIpAddress, HcclNetDevCtx> &netDevCtxMap,
                   const IntraExchanger &exchanger, const std::vector<RankInfo> paraVector,
                   const DeviceMem& inputMem, const DeviceMem& outputMem, const bool isUsedRdmaLevel0,
                   const void* transportResourceInfoAddr, size_t transportResourceInfoSize,
                   const std::string &tag,
                   const NICDeployment nicDeployInner, const bool useOneDoorbell,
                   const bool isAicpuModeEn, const bool isHaveCpuRank, const bool useSuperPodMode)
    : CommBase(collectiveId, userRank, userRankSize, rank, rankSize, paraVector, topoFlag, dispatcher, notifyPool,
               netDevCtxMap, exchanger, inputMem, outputMem, isUsedRdmaLevel0, transportResourceInfoAddr,
               transportResourceInfoSize, tag, nicDeployInner, 0, useOneDoorbell, isAicpuModeEn, INVALID_UINT,
               isHaveCpuRank, useSuperPodMode)
{
}

CommRing::~CommRing()
{
}

HcclResult CommRing::CalcLink()
{
    u32 dstClientRank = INVALID_VALUE_RANKID;
    u32 dstServerRank = INVALID_VALUE_RANKID;
    HcclResult ret = HCCL_SUCCESS;
    if (rank_ == HCCL_RANK_ZERO) {  // 当前rank为rank0
        // rank 作为server
        dstClientRank = rank_ + HCCL_RANK_OFFSET;
        ret = CalcLinksNum(MachineType::MACHINE_SERVER_TYPE, dstClientRank);
        CHK_PRT_RET(ret != HCCL_SUCCESS,
            HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstClientRank[%u]",
                static_cast<int32_t>(MachineType::MACHINE_SERVER_TYPE), dstClientRank), ret);

        if (rankSize_ > HCCL_RANK_SIZE_EQ_TWO) {
            // rank 作为client
            dstServerRank = rankSize_ - HCCL_RANK_OFFSET;
            ret = CalcLinksNum(MachineType::MACHINE_CLIENT_TYPE, dstServerRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstServerRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_CLIENT_TYPE), dstServerRank), ret);
        }
    } else if ((rankSize_ - HCCL_RANK_OFFSET) == rank_) {  // 当前rank为ring环尾，rankx(x = (rankSize_ - 1))
        if (rankSize_ > HCCL_RANK_SIZE_EQ_TWO) {
            // rank 作为server
            dstClientRank = HCCL_RANK_ZERO;
            ret = CalcLinksNum(MachineType::MACHINE_SERVER_TYPE, dstClientRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstClientRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_SERVER_TYPE), dstClientRank), ret);
        }

        // rank 作为client
        dstServerRank = rank_ - HCCL_RANK_OFFSET;
        ret = CalcLinksNum(MachineType::MACHINE_CLIENT_TYPE, dstServerRank);
        CHK_PRT_RET(ret != HCCL_SUCCESS,
            HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstServerRank[%u]",
                static_cast<int32_t>(MachineType::MACHINE_CLIENT_TYPE), dstServerRank), ret);
    } else {                     // 奇数先创建client，偶数先创建server
        if ((rank_ % 2) != 0) {  // 模2判断奇偶性，rank为奇数
            // rank 作为client
            dstServerRank = rank_ - HCCL_RANK_OFFSET;
            ret = CalcLinksNum(MachineType::MACHINE_CLIENT_TYPE, dstServerRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstServerRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_CLIENT_TYPE), dstServerRank), ret);

            // rank 作为server
            dstClientRank = rank_ + HCCL_RANK_OFFSET;
            ret = CalcLinksNum(MachineType::MACHINE_SERVER_TYPE, dstClientRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstClientRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_SERVER_TYPE), dstClientRank), ret);
        } else {  // rank为偶数
            // rank 作为server
            dstClientRank = rank_ + HCCL_RANK_OFFSET;
            ret = CalcLinksNum(MachineType::MACHINE_SERVER_TYPE, dstClientRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstClientRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_SERVER_TYPE), dstClientRank), ret);

            // rank 作为client
            dstServerRank = rank_ - HCCL_RANK_OFFSET;
            ret = CalcLinksNum(MachineType::MACHINE_CLIENT_TYPE, dstServerRank);
            CHK_PRT_RET(ret != HCCL_SUCCESS,
                HCCL_ERROR("[Calc][Link]comm ring calc links num failed, type[%d], dstServerRank[%u]",
                    static_cast<int32_t>(MachineType::MACHINE_CLIENT_TYPE), dstServerRank), ret);
        }
    }

    return HCCL_SUCCESS;
}

// 获取每个 link 需要的 socket 数量
u32 CommRing::GetSocketsPerLink()
{
    const u32 rdmaTaskNumRatio = 4; // server间ring算法每个link上rdma task数为 4*rank size
    HcclWorkflowMode workFlowMode = GetWorkflowMode();
    if (workFlowMode != HcclWorkflowMode::HCCL_WORKFLOW_MODE_OP_BASE) {
        return (rankSize_ * rdmaTaskNumRatio + (HCCP_SQ_TEMPLATE_CAPACITY - 1)) / HCCP_SQ_TEMPLATE_CAPACITY;
    } else {
        // op base 场景每个link使用 1 个QP，只需要建立1个socket链接
        return 1;
    }
}

void CommRing::SetMachineLinkMode(MachinePara &machinePara)
{
    machinePara.linkMode = LinkMode::LINK_DUPLEX_MODE;
}
}  // namespace hccl

