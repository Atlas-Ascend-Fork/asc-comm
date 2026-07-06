/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_res_specs.h"

#include "hccp.h"
#include "hccp_ctx.h"
#include "string_util.h"

#include "rt_external.h"
#include "driver/ascend_hal.h"
#include "adapter_rts.h"

namespace hcomm {

CcuResSpecifications &CcuResSpecifications::GetInstance(const int32_t deviceLogicId)
{
    static CcuResSpecifications ccuResSpecifications[MAX_MODULE_DEVICE_NUM + 1];
    int32_t devLogicId = deviceLogicId;
    if (devLogicId < 0 || static_cast<uint32_t>(devLogicId) >= MAX_MODULE_DEVICE_NUM) {
        HCCL_WARNING("[CcuResSpecifications][%s] use the backup device, devLogicId[%d] "
            "should be less than %u.", __func__, devLogicId, MAX_MODULE_DEVICE_NUM);
        devLogicId = MAX_MODULE_DEVICE_NUM; // 使用备份设备
    }
    ccuResSpecifications[devLogicId].devLogicId_ = devLogicId;
    return ccuResSpecifications[devLogicId];
}

static CcuVersion CheckCcuVersion()
{
    return CcuVersion::CCU_V1; // 当前仅有CCU V1
}

static bool CheckDieEnable(const uint32_t devPhyId, const uint8_t dieId)
{
    const RaInfo info{NetworkMode::NETWORK_OFFLINE, devPhyId};
    CustomChannelInfoIn  inBuff{};
    CustomChannelInfoOut outBuff{};
    inBuff.op                    = CcuOpcodeType::CCU_U_OP_GET_DIE_WORKING;
    inBuff.offsetStartIdx        = 0;
    inBuff.data.dataInfo.udieIdx = dieId;

    auto ret = RaCustomChannel(info,
        reinterpret_cast<CustomChanInfoIn *>(&inBuff),
        reinterpret_cast<CustomChanInfoOut *>(&outBuff));
    if (ret != 0) {
        HCCL_WARNING("[CcuResSpecifications][%s] failed to call ccu driver, "
            "devPhyId[%u] dieId[%d] op[%s].", __func__, devPhyId, dieId,
            "GET_DIE_WORKING");
        return false;
    }

    const uint32_t enableFlag = outBuff.data.dataInfo.dataArray[0].dieinfo.enableFlag;
    return enableFlag == Hccl::CCU_ENABLE_FLAG;
}

static CcuBaseInfoData ParseOutBuffToBaseInfoData(const CustomChannelInfoOut &outBuff)
{
    CcuBaseInfoData baseInfoData{};
    baseInfoData.resourceAddr = outBuff.data.dataInfo.dataArray[0].baseinfo.resourceAddr;
    baseInfoData.missionKey   = outBuff.data.dataInfo.dataArray[0].baseinfo.missionKey;
    baseInfoData.msId         = outBuff.data.dataInfo.dataArray[0].baseinfo.msId;
    baseInfoData.caps.cap0    = outBuff.data.dataInfo.dataArray[0].baseinfo.caps.cap0;
    baseInfoData.caps.cap1    = outBuff.data.dataInfo.dataArray[0].baseinfo.caps.cap1;
    baseInfoData.caps.cap2    = outBuff.data.dataInfo.dataArray[0].baseinfo.caps.cap2;
    baseInfoData.caps.cap3    = outBuff.data.dataInfo.dataArray[0].baseinfo.caps.cap3;
    baseInfoData.caps.cap4    = outBuff.data.dataInfo.dataArray[0].baseinfo.caps.cap4;
    return baseInfoData;
}

static CcuResSpecInfo ParseOutBuffToResSpecInfo(const CcuVersion ccuVersion, const CustomChannelInfoOut &outBuff)
{
    if (ccuVersion != CcuVersion::CCU_V1) {
        HCCL_WARNING("[CcuResSpecifications][%s] failed to parse out buff, ccu driver "
            "version[%s] is not expected.", __func__, ccuVersion.Describe().c_str());
        return {};
    }

    const auto &baseInfoData = ParseOutBuffToBaseInfoData(outBuff);

    CcuResSpecInfo ccuResSpecInfo{};
    ccuResSpecInfo.msId         = baseInfoData.msId;
    ccuResSpecInfo.resourceAddr = baseInfoData.resourceAddr;
    ccuResSpecInfo.missionKey   = baseInfoData.missionKey;

    ccuResSpecInfo.instructionNum = (baseInfoData.caps.cap0 & 0x0000FFFF) + 1;
    ccuResSpecInfo.xnNum          = ((baseInfoData.caps.cap1 >> MOVE_16_BITS) & 0x0000FFFF) + 1;
    ccuResSpecInfo.msNum          = ((baseInfoData.caps.cap2 >> MOVE_16_BITS) & 0x0000FFFF) + 1;
    ccuResSpecInfo.ckeNum         = (baseInfoData.caps.cap2 & 0x0000FFFF) + 1;
    ccuResSpecInfo.jettyNum       = ((baseInfoData.caps.cap3 >> MOVE_16_BITS) & 0x0000FFFF) + 1;
    ccuResSpecInfo.channelNum     = (baseInfoData.caps.cap3 & 0x0000FFFF) + 1;
    ccuResSpecInfo.pfeNum         = (baseInfoData.caps.cap4 & 0x000000FF) + 1;

    ccuResSpecInfo.missionNum     = ((baseInfoData.caps.cap0 >> MOVE_16_BITS) & 0x000000FF) + 1;
    ccuResSpecInfo.loopEngineNum  = ((baseInfoData.caps.cap0 >> MOVE_24_BITS) & 0x000000FF) + 1;
    ccuResSpecInfo.gsaNum         = (baseInfoData.caps.cap1 & 0x0000FFFF) + 1;
    return ccuResSpecInfo;
}

static HcclResult CheckResSpecifications(const uint32_t devPhyId, const uint8_t dieId,
    const CcuVersion ccuVersion, CcuResSpecInfo &resSpecs)
{
    const RaInfo info{NetworkMode::NETWORK_OFFLINE, devPhyId};
    CustomChannelInfoIn  inBuff{};
    CustomChannelInfoOut outBuff{};
    inBuff.op                    = CcuOpcodeType::CCU_U_OP_GET_BASIC_INFO;
    inBuff.offsetStartIdx        = 0;
    inBuff.data.dataInfo.udieIdx = dieId;

    auto ret = RaCustomChannel(info,
        reinterpret_cast<CustomChanInfoIn *>(&inBuff),
        reinterpret_cast<CustomChanInfoOut *>(&outBuff));
    if (ret != 0) {
        HCCL_ERROR("[CcuResSpecifications][%s] failed to call ccu driver, "
            "devPhyId[%u] dieId[%d] op[%s].", __func__, devPhyId, dieId,
            "GET_BASIC_INFO");
        return HcclResult::HCCL_E_NETWORK;
    }

    resSpecs = ParseOutBuffToResSpecInfo(ccuVersion, outBuff);
    return HcclResult::HCCL_SUCCESS;
}

/*
 * 获取Mainboard ID 5-7位，输出整机形态枚举值
 * Mainboard ID描述说明
 * Mainboard ID采用了16bit，区分形态，主从，以及端口配置
 * bit[7:5] 区分整机形态(当前POD和EVB没有区分A+X或A+K)
 * {
 *  000: 天成 POD
 *  001: A+K Server
 *  010: A+X Server
 *  011: PCIE标卡
 *  100-101: RSV
 *  110: 装备
 *  111: EVB
 * }
 * bit[4:1] 整机形态细分
 * {
 *  0000-1111
 * }
 * bit[0] 主从或池化
 * {
 *  0: 主从（NPU作为某个Host的从设备，Host主控）
 *  1: 池化（NPU作为资源池，其它Host对等访问）
 * }
 */
constexpr uint64_t POD_MAINBOARD = 0x0;
constexpr uint64_t A_K_SERVER_MAINBOARD = 0x1;
constexpr uint64_t A_X_SERVER_MAINBOARD = 0x2;
constexpr uint64_t PCIE_STD_MAINBOARD = 0x3;
constexpr uint64_t RSV1_MAINBOARD = 0x4;
constexpr uint64_t RSV2_MAINBOARD = 0x5;
constexpr uint64_t EQUIP_MAINBOARD = 0x6;
constexpr uint64_t EVB_MAINBOARD = 0x7;
const std::unordered_map<uint64_t, HcclMainboardId> rtMainboardIdToHcclMainboardId = {
    {POD_MAINBOARD, HcclMainboardId::MAINBOARD_POD},
    {A_K_SERVER_MAINBOARD, HcclMainboardId::MAINBOARD_A_K_SERVER},
    {A_X_SERVER_MAINBOARD, HcclMainboardId::MAINBOARD_A_X_SERVER},
    {PCIE_STD_MAINBOARD, HcclMainboardId::MAINBOARD_PCIE_STD},
    {RSV1_MAINBOARD, HcclMainboardId::MAINBOARD_RSV},
    {RSV2_MAINBOARD, HcclMainboardId::MAINBOARD_RSV},
    {EQUIP_MAINBOARD, HcclMainboardId::MAINBOARD_EQUIPMENT},
    {EVB_MAINBOARD, HcclMainboardId::MAINBOARD_EVB}
};

HcclResult CcuGetMainboardId(uint32_t deviceLogicId, HcclMainboardId &hcclMainboardId)
{
    constexpr aclrtDevAttr devAttr = aclrtDevAttr::ACL_DEV_ATTR_MAINBOARD_ID;
    constexpr uint64_t BITS_5 = 5;
    constexpr uint64_t MASK_7 = 0x7;
    int64_t val = 0;
    auto ret = aclrtGetDeviceInfo(deviceLogicId, devAttr, &val);
    if (ret != RT_ERROR_NONE) {
        HCCL_ERROR("[%s]errNo[0x%016llx] rt get device info failed, "
                   "deviceLogicId=%u, devAttr=%d", __func__,
                   HCCL_ERROR_CODE(HcclResult::HCCL_E_RUNTIME),
                   deviceLogicId, devAttr);
        return HcclResult::HCCL_E_RUNTIME;
    }

    HCCL_INFO("[%s] deviceLogicId[%d] val[%ld].", __func__, deviceLogicId, val);
    uint64_t mainboardId = (static_cast<uint64_t>(val) >> BITS_5) & MASK_7; // 提取val的5-7位，判断整机形态
    hcclMainboardId = HcclMainboardId::MAINBOARD_OTHERS;
    auto it = rtMainboardIdToHcclMainboardId.find(mainboardId);
    if (it != rtMainboardIdToHcclMainboardId.end()) {
        hcclMainboardId = it->second;
    }
    HCCL_INFO("[%s] deviceLogicId[%d] mainboardId[%llu] hcclMainboardId[%s].",
              __func__, deviceLogicId, mainboardId, hcclMainboardId.Describe().c_str());
    return HcclResult::HCCL_SUCCESS;
}

static HcclResult CheckArmX86Flag(int32_t devLogicId, bool &armX86Flag)
{
    HcclMainboardId hcclMainboardId{HcclMainboardId::MAINBOARD_RSV};
    CHK_RET(CcuGetMainboardId(devLogicId, hcclMainboardId));

    armX86Flag = hcclMainboardId == HcclMainboardId::MAINBOARD_A_X_SERVER
        || hcclMainboardId == HcclMainboardId::MAINBOARD_PCIE_STD;
    HCCL_INFO("[CcuResSpecifications][%s] devLogicId[%d] "
        "hcclMainboardId[%s] armX86Flag_[%d].", __func__, devLogicId,
        hcclMainboardId.Describe().c_str(), static_cast<int>(armX86Flag));
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::Init()
{
    if (initFlag_) {
        return HcclResult::HCCL_SUCCESS;
    }

    CHK_RET(hrtGetDevicePhyIdByIndex(static_cast<uint32_t>(devLogicId_), devPhyId_));
    CHK_RET(CheckArmX86Flag(devLogicId_, armX86Flag_));
    ccuVersion_ = CheckCcuVersion();
    for (uint8_t dieId = 0; dieId < CCU_MAX_IODIE_NUM; dieId++) {
        dieEnableFlags_[dieId] = CheckDieEnable(devPhyId_, dieId);
        if (!dieEnableFlags_[dieId]) {
            resSpecs_[dieId] = CcuResSpecInfo{};
            continue;
        }

        CHK_RET(CheckResSpecifications(devPhyId_, dieId, ccuVersion_, resSpecs_[dieId]));
    }

    initFlag_ = true;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::Deinit()
{
    for (uint32_t i = 0; i < CCU_MAX_IODIE_NUM; i++) {
        dieEnableFlags_[i] = false;
        resSpecs_[i] = CcuResSpecInfo{};
    }

    armX86Flag_ = true;
    initFlag_ = false;
    return HcclResult::HCCL_SUCCESS;
}

CcuVersion CcuResSpecifications::GetCcuVersion() const
{
    return ccuVersion_;
}

HcclResult CcuResSpecifications::GetDieEnableFlag(const uint8_t dieId, bool &dieEnableFlag) const
{
    // 只校验dieId合法性，不校验die是否使能
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, {true, true}));
    dieEnableFlag = dieEnableFlags_[dieId];
    return HcclResult::HCCL_SUCCESS;
}

bool CcuResSpecifications::GetArmX86Flag() const
{
    return armX86Flag_;
}

HcclResult CcuResSpecifications::GetResourceAddr(const uint8_t dieId, uint64_t &resourceAddr) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    resourceAddr = resSpecs_[dieId].resourceAddr;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetXnBaseAddr(const uint8_t dieId, uint64_t &xnBaseAddr) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));

    const uint64_t ccuResAddr = resSpecs_[dieId].resourceAddr;
    if (ccuResAddr == 0) {
        HCCL_WARNING("[CcuResSpecifications][%s] failed, CCU resource base address is 0, "
            "devLogicId[%d] dieId[%u].", __func__, devLogicId_, dieId);
        return HcclResult::HCCL_E_INTERNAL;
    }

    // xn位于ins与gsa之后，xn偏移 = CCUM偏移 + 指令空间大小 + GSA大小，常量计算不会溢出
    constexpr uint64_t instrRevserveSize = CCU_RESOURCE_INS_RESERVE_SIZE;
    constexpr uint64_t gsaReserveSize = CCU_V1_RESOURCE_GSA_RESERVE_SIZE;
    constexpr uint64_t ccum_offset = CCU_V1_CCUM_OFFSET;
    constexpr uint32_t ccuXnOffset = ccum_offset + instrRevserveSize + gsaReserveSize;
    if (ccuResAddr > UINT64_MAX - ccuXnOffset) {
        HCCL_ERROR("[CcuResSpecifications][%s] failed, CCU resource base address[%llu] is "
            "greater then expected, ccu xn offset[%llu], their sum will exceeds the range "
            "of uint64_t.", __func__, ccuResAddr, ccuXnOffset);
    }

    xnBaseAddr = ccuResAddr + ccuXnOffset;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetMsId(const uint8_t dieId, uint32_t &msId) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    msId = resSpecs_[dieId].msId;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetMissionKey(const uint8_t dieId, uint32_t &missionKey) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    missionKey = resSpecs_[dieId].missionKey;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetInstructionNum(const uint8_t dieId, uint32_t &instrNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    instrNum = resSpecs_[dieId].instructionNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetMissionNum(const uint8_t dieId, uint32_t &missionNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    missionNum = resSpecs_[dieId].missionNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetLoopEngineNum(const uint8_t dieId, uint32_t &loopNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    loopNum = resSpecs_[dieId].loopEngineNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetGsaNum(const uint8_t dieId, uint32_t &gsaNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    gsaNum = resSpecs_[dieId].gsaNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetXnNum(const uint8_t dieId, uint32_t &xnNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    xnNum = resSpecs_[dieId].xnNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetCkeNum(const uint8_t dieId, uint32_t &ckeNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    ckeNum = resSpecs_[dieId].ckeNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetMsNum(const uint8_t dieId, uint32_t &msNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    msNum = resSpecs_[dieId].msNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetChannelNum(const uint8_t dieId, uint32_t &channelNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    channelNum = resSpecs_[dieId].channelNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetJettyNum(const uint8_t dieId, uint32_t &jettyNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    jettyNum = resSpecs_[dieId].jettyNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetPfeReservedNum(const uint8_t dieId, uint32_t &pfeNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    pfeNum = CCU_V1_PER_DIE_PFE_RESERVED_NUM;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetPfeNum(const uint8_t dieId, uint32_t &pfeNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    pfeNum = resSpecs_[dieId].pfeNum;
    return HcclResult::HCCL_SUCCESS;
}

HcclResult CcuResSpecifications::GetWqeBBNum(const uint8_t dieId, uint32_t &wqeBBNum) const
{
    CHK_RET(CheckDieValid(__func__, devLogicId_, dieId, dieEnableFlags_));
    wqeBBNum = resSpecs_[dieId].wqeBBNum;
    return HcclResult::HCCL_SUCCESS;
}

} // namespace hcomm