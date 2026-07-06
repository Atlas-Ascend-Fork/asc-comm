/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_pfe_mgr.h"

#include "hccp_ctx.h"

#include "../../../ccu_res_specs.h"
#include "ccu_pfe_cfg_mgr.h"

namespace hcomm {

inline PfeCtx BuildPfeCtx(const uint8_t dieId, const PfeJettyCtxCfg &cfg)
{
    struct PfeCtx ctx = {0};
    ctx.startJettyId = cfg.startTaJettyId;
    ctx.jettyNum = cfg.size - 1; // PFE给CCU分配的Jetty个数，配置硬件时需减1
    ctx.startLocalJettyCtxId = cfg.startJettyCtxId;

    HCCL_RUN_INFO("[CcuPfeMgr][%s]: dieId[%u] feId[%u], startJettyId[%u], jettyNum(-1)[%u], "
        "startLocalJettyCtxId[%u], pfe ctx size[%u]", __func__, dieId, cfg.feId,
        ctx.startJettyId, ctx.jettyNum, ctx.startLocalJettyCtxId, sizeof(PfeCtx));
    return ctx;
}

inline PfeJettyStrategy BuildStrategy(const PfeJettyCtxCfg &cfg)
{
    struct PfeJettyStrategy pfeJettyStrategy = {0};
    pfeJettyStrategy.feId  = cfg.feId;
    pfeJettyStrategy.pfeId = cfg.feId;
    pfeJettyStrategy.size  = cfg.size;
    pfeJettyStrategy.startTaJettyId = cfg.startTaJettyId;
    pfeJettyStrategy.startLocalJettyCtxId = cfg.startJettyCtxId;
    return pfeJettyStrategy;
}

static HcclResult ConfigPfeTable(const uint32_t devPhyId, const uint8_t dieId, const uint32_t feId,
    const uint32_t pfeReservedNum, const PfeCtx &pfeCtx)
{
    if (UNLIKELY(feId > UINT32_MAX - static_cast<uint32_t>(dieId) * pfeReservedNum)) {
        HCCL_ERROR("[CcuPfeMgr][%s] failed, feId[%u] is greater than expected, "
            "pfeReservedNum[%u], will exceeds the range of uint32_t, devPhyId[%u], "
            "dieId[%u].", __func__, feId, pfeReservedNum, devPhyId, dieId);
        return HcclResult::HCCL_E_INTERNAL;
    }
    // die1 使用后半部分pfe表项，故根据pfe预留数量偏移
    const uint32_t pfeTableOffset = static_cast<uint32_t>(dieId) * pfeReservedNum + feId;

    const RaInfo info{NetworkMode::NETWORK_OFFLINE, devPhyId};
    CustomChannelInfoIn  inBuff{};
    CustomChannelInfoOut outBuff{};
    inBuff.op                          = CcuOpcodeType::CCU_U_OP_SET_PFE;
    inBuff.data.dataInfo.udieIdx       = static_cast<uint32_t>(dieId);
    inBuff.data.dataInfo.dataArraySize = 1;
    inBuff.data.dataInfo.dataLen       = sizeof(struct PfeCtx); // 单个8B
    inBuff.offsetStartIdx              = pfeTableOffset;

    (void)memcpy_s(inBuff.data.dataInfo.dataArray, inBuff.data.dataInfo.dataLen, &pfeCtx,
        inBuff.data.dataInfo.dataLen);

    auto ret = RaCustomChannel(info,
        reinterpret_cast<CustomChanInfoIn *>(&inBuff),
        reinterpret_cast<CustomChanInfoOut *>(&outBuff));
    if (ret != 0) {
        HCCL_ERROR("[CcuResSpecifications][%s] failed to call ccu driver, "
            "devPhyId[%u] dieId[%d] op[%s].", __func__, devPhyId, dieId,
            "SET_PFE");
        return HcclResult::HCCL_E_NETWORK;
    }
    
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuPfeMgr::Init()
{
    std::vector<PfeJettyCtxCfg> cfgs =
        CcuPfeCfgMgr::GetInstance(devLogicId_).GetPfeJettyCtxCfg(dieId_);
    if (UNLIKELY(cfgs.empty())) { // 此处不中断流程，后续jettyCtx分配时会因无pfe配置报错停止
        HCCL_WARNING("[CcuJettyCtxMgr] config pfe table passed, pfe cfgs size is 0, "
            "devLogicId[%d], dieId[%u].", devLogicId_, dieId_);
        return HcclResult::HCCL_SUCCESS;
    }

    uint32_t pfeReservedNum = 0;
    (void)CcuResSpecifications::GetInstance(devLogicId_).GetPfeReservedNum(dieId_, pfeReservedNum);
    if (UNLIKELY(pfeReservedNum == 0)) { // 此处不中断流程，后续jettyCtx分配时会因无pfe配置报错停止
        HCCL_WARNING("[CcuPfeMgr] config pfe table passed, pfe reserved num is 0, "
            "devLogicId[%d], dieId[%u].", devLogicId_, dieId_);
        return HcclResult::HCCL_SUCCESS;
    }

    for (const auto &cfg : cfgs) {
        // 分配策略保证cfg中feId均为ccu可用设备
        const uint32_t feId = cfg.feId;
        const auto &iter = pfeJettyMap_.find(feId);
        if (iter != pfeJettyMap_.end()) {
            continue; // 跳过已配置pfe
        }
        pfeJettyMap_[feId] = BuildStrategy(cfg);

        const auto &pfeCtx = BuildPfeCtx(dieId_, cfg);
        CHK_RET(ConfigPfeTable(devPhyId_, dieId_, feId, pfeReservedNum, pfeCtx));
    }

    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuPfeMgr::GetPfeStrategy(uint32_t feId, PfeJettyStrategy &pfeJettyStrategy) const
{
    const auto &iter = pfeJettyMap_.find(feId);
    if (iter == pfeJettyMap_.end()) {
        HCCL_ERROR("[CcuPfeMgr][%s] failed, feId[%u] is not found, "
            "devLogicId[%d], dieId[%u].", __func__, feId, devLogicId_, dieId_);
        return HCCL_E_NOT_FOUND;
    }

    pfeJettyStrategy = iter->second;
    HCCL_RUN_INFO("[CcuPfeMgr][%s] dieId[%u] feId[%u] pfeId[%u] size[%u] "
 	    "startTaJettyId[%u] startLocalJettyCtxId[%u]", __func__, dieId_,
 	    pfeJettyStrategy.feId, pfeJettyStrategy.pfeId, pfeJettyStrategy.size,
 	    pfeJettyStrategy.startTaJettyId, pfeJettyStrategy.startLocalJettyCtxId);  

    return HCCL_SUCCESS;
}

}; // Hccl