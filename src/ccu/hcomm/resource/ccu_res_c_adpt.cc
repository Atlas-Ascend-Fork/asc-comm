/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu/hcomm/ccu_res.h"

#include <array>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "hcomm/resource/instance/ccu_instance_mgr.h"
#include "hcomm/common/ccu_log.h"
#include "hcom_common.h"
#include "ccu_device_pub.h"

namespace {

constexpr uint32_t CCU_MAX_DIE_NUM = 2;
constexpr uint32_t CCU_RES_TYPE_NUM = 7;

enum InternalResIndex : size_t {
    INTERNAL_LOOP = 0,
    INTERNAL_MS = 1,
    INTERNAL_CKE = 2,
    INTERNAL_XN = 3,
    INTERNAL_GSA = 5,
    INTERNAL_INS = 6,
    INTERNAL_MISSION = 7,
};

struct CcuResourceDesc {
    uint32_t dieId;
    std::array<uint32_t, CCU_RES_TYPE_NUM> nums{};
};

std::mutex g_descMutex;
std::unordered_map<HcommCcuResDescHandle, CcuResourceDesc> g_descs;
HcommCcuResDescHandle g_nextDesc = 0;

bool IsValidResType(HcommCcuResType resType)
{
    return resType >= HCOMM_CCU_RES_TYPE_LOOP && resType <= HCOMM_CCU_RES_TYPE_INSTRUCTION;
}

CcuResult IsDieEnabled(uint32_t dieId)
{
    if (dieId >= CCU_MAX_DIE_NUM) {
        return CcuResult::CCU_E_PARA;
    }
    bool enabled = false;
    const int32_t devLogicId = HcclGetThreadDeviceId();
    CcuResult ret =
        static_cast<CcuResult>(hcomm::CcuGetDieEnableInfo(devLogicId, static_cast<uint8_t>(dieId), enabled));
    if (ret != CcuResult::CCU_SUCCESS) {
        return ret;
    }
    return enabled ? CcuResult::CCU_SUCCESS : CcuResult::CCU_E_UNAVAIL;
}

} // namespace

CcuResult hcomm_ccu_ins_create_legacy(const CcuInstanceType insType, CcuInsHandle *ccuInsHandle)
{
    CCU_CHK_PTR_NULL(ccuInsHandle);

    const int32_t devLogicId = HcclGetThreadDeviceId();
    CCU_CHK_RET(asccomm::CcuInstanceMgr::GetInstance(devLogicId).Create(insType, *ccuInsHandle));
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_ins_create(
    const HcommCcuResDescHandle* resDescs, uint32_t resDescNum, CcuInsHandle* ccuInsHandle)
{
    CCU_CHK_PTR_NULL(resDescs);
    CCU_CHK_PTR_NULL(ccuInsHandle);
    if (resDescNum == 0 || resDescNum > CCU_MAX_DIE_NUM) {
        return CcuResult::CCU_E_PARA;
    }

    std::vector<CcuResourceDesc> snapshots;
    snapshots.reserve(resDescNum);
    {
        std::unordered_set<uint32_t> dieIds;
        std::lock_guard<std::mutex> lock(g_descMutex);
        for (uint32_t i = 0; i < resDescNum; ++i) {
            auto it = g_descs.find(resDescs[i]);
            if (it == g_descs.end() || !dieIds.insert(it->second.dieId).second) {
                return CcuResult::CCU_E_PARA;
            }
            snapshots.push_back(it->second);
        }
    }

    std::vector<asccomm::CcuResDescSnapshot> internalDescs;
    internalDescs.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        CCU_CHK_RET(IsDieEnabled(snapshot.dieId));
        asccomm::CcuResDescSnapshot desc;
        desc.dieId = snapshot.dieId;
        desc.resNum[INTERNAL_LOOP] = snapshot.nums[HCOMM_CCU_RES_TYPE_LOOP];
        desc.resNum[INTERNAL_MS] = snapshot.nums[HCOMM_CCU_RES_TYPE_CCU_BUF];
        desc.resNum[INTERNAL_CKE] = snapshot.nums[HCOMM_CCU_RES_TYPE_VARIABLE];
        desc.resNum[INTERNAL_XN] = snapshot.nums[HCOMM_CCU_RES_TYPE_ADDRESS];
        desc.resNum[INTERNAL_GSA] = snapshot.nums[HCOMM_CCU_RES_TYPE_EVENT];
        desc.resNum[INTERNAL_MISSION] = snapshot.nums[HCOMM_CCU_RES_TYPE_CCU_THREAD];
        desc.resNum[INTERNAL_INS] = snapshot.nums[HCOMM_CCU_RES_TYPE_INSTRUCTION];
        internalDescs.push_back(desc);
    }

    const int32_t devLogicId = HcclGetThreadDeviceId();
    return asccomm::CcuInstanceMgr::GetInstance(devLogicId).Create(internalDescs, *ccuInsHandle);
}

CcuResult hcomm_ccu_ins_create_default(const uint32_t* dieIds, uint32_t dieNum, CcuInsHandle* ccuInsHandle)
{
    (void)dieIds;
    (void)dieNum;
    CCU_CHK_PTR_NULL(ccuInsHandle);
    const int32_t devLogicId = HcclGetThreadDeviceId();
    return asccomm::CcuInstanceMgr::GetInstance(devLogicId).Create(CcuInstanceType::CCU_SCHED, *ccuInsHandle);
}

CcuResult hcomm_ccu_ins_destroy(CcuInsHandle insHandle)
{
    const int32_t devLogicId = HcclGetThreadDeviceId();
    return asccomm::CcuInstanceMgr::GetInstance(devLogicId).Destroy(insHandle);
}

CcuResult hcomm_ccu_ins_query_res_desc(CcuInsHandle, HcommCcuResDescHandle)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}

CcuResult hcomm_ccu_ins_res_desc_create(uint32_t dieId, HcommCcuResDescHandle* resDesc)
{
    CCU_CHK_PTR_NULL(resDesc);
    if (dieId >= CCU_MAX_DIE_NUM) {
        return CcuResult::CCU_E_PARA;
    }
    std::lock_guard<std::mutex> lock(g_descMutex);
    *resDesc = ++g_nextDesc;
    g_descs.emplace(*resDesc, CcuResourceDesc{dieId, {}});
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_ins_res_desc_destroy(HcommCcuResDescHandle resDesc)
{
    std::lock_guard<std::mutex> lock(g_descMutex);
    return g_descs.erase(resDesc) == 1 ? CcuResult::CCU_SUCCESS : CcuResult::CCU_E_NOT_FOUND;
}

CcuResult hcomm_ccu_ins_res_desc_query_die_id(HcommCcuResDescHandle resDesc, uint32_t* dieId)
{
    CCU_CHK_PTR_NULL(dieId);
    std::lock_guard<std::mutex> lock(g_descMutex);
    auto it = g_descs.find(resDesc);
    if (it == g_descs.end()) {
        return CcuResult::CCU_E_NOT_FOUND;
    }
    *dieId = it->second.dieId;
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_ins_res_desc_set_num(HcommCcuResDescHandle resDesc, HcommCcuResType resType, uint32_t resNum)
{
    if (!IsValidResType(resType)) {
        return CcuResult::CCU_E_PARA;
    }
    std::lock_guard<std::mutex> lock(g_descMutex);
    auto it = g_descs.find(resDesc);
    if (it == g_descs.end()) {
        return CcuResult::CCU_E_NOT_FOUND;
    }
    it->second.nums[static_cast<uint32_t>(resType)] = resNum;
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_ins_res_desc_query_num(HcommCcuResDescHandle resDesc, HcommCcuResType resType, uint32_t* resNum)
{
    CCU_CHK_PTR_NULL(resNum);
    if (!IsValidResType(resType)) {
        return CcuResult::CCU_E_PARA;
    }
    std::lock_guard<std::mutex> lock(g_descMutex);
    auto it = g_descs.find(resDesc);
    if (it == g_descs.end()) {
        return CcuResult::CCU_E_NOT_FOUND;
    }
    *resNum = it->second.nums[static_cast<uint32_t>(resType)];
    return CcuResult::CCU_SUCCESS;
}

CcuResult hcomm_ccu_query_remain_res_desc(HcommCcuResDescHandle resDesc)
{
    uint32_t dieId = 0;
    CCU_CHK_RET(hcomm_ccu_ins_res_desc_query_die_id(resDesc, &dieId));
    return IsDieEnabled(dieId);
}

CcuResult hcomm_ccu_kernel_query_res_req(const void*, const void**, uint32_t, HcommCcuResDescHandle)
{
    return CcuResult::CCU_E_NOT_SUPPORT;
}
