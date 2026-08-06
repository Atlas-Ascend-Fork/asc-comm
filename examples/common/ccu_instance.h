/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXAMPLES_CCU_INSTANCE_H
#define ASCCOMM_EXAMPLES_CCU_INSTANCE_H

#include <array>
#include <cstdint>
#include <vector>

#include <hccl/hccl_rank_graph.h>
#include <ccu/hcomm/ccu_res.h>

extern "C" HcclResult HcclCommAssignCcuIns(HcclComm comm, CcuInsHandle insHandle);
extern "C" HcclResult HcclCommQueryCcuIns(HcclComm comm, CcuInsHandle* insHandles, uint32_t* insNum);

namespace asccomm_examples {

constexpr uint32_t CCU_MAX_DIE_NUM = 2;

inline HcclResult ConvertCcuResult(CcuResult ret)
{
    switch (ret) {
        case CCU_SUCCESS:
            return HCCL_SUCCESS;
        case CCU_E_PARA:
            return HCCL_E_PARA;
        case CCU_E_PTR:
            return HCCL_E_PTR;
        case CCU_E_NOT_SUPPORT:
            return HCCL_E_NOT_SUPPORT;
        case CCU_E_NOT_FOUND:
            return HCCL_E_NOT_FOUND;
        case CCU_E_UNAVAIL:
            return HCCL_E_UNAVAIL;
        default:
            return HCCL_E_INTERNAL;
    }
}

inline HcclResult SetCcuSchedResourceNums(HcommCcuResDescHandle desc)
{
    struct ResourceNum {
        HcommCcuResType type;
        uint32_t num;
    };
    constexpr std::array<ResourceNum, 7> resourceNums = {{
        {HCOMM_CCU_RES_TYPE_LOOP, 16},
        {HCOMM_CCU_RES_TYPE_CCU_BUF, 128},
        {HCOMM_CCU_RES_TYPE_VARIABLE, 400},
        {HCOMM_CCU_RES_TYPE_ADDRESS, 400},
        {HCOMM_CCU_RES_TYPE_EVENT, 48},
        {HCOMM_CCU_RES_TYPE_CCU_THREAD, 2},
        {HCOMM_CCU_RES_TYPE_INSTRUCTION, 0},
    }};
    for (const auto &resource : resourceNums) {
        CcuResult ret = HcommCcuInsResDescSetNum(desc, resource.type, resource.num);
        if (ret != CCU_SUCCESS) {
            return ConvertCcuResult(ret);
        }
    }
    return HCCL_SUCCESS;
}

inline void DestroyCcuResourceDescs(const std::vector<HcommCcuResDescHandle> &descs)
{
    for (auto desc : descs) {
        (void)HcommCcuInsResDescDestroy(desc);
    }
}

inline HcclResult GetOrCreateCcuInstance(HcclComm comm, CcuInsHandle &insHandle)
{
    uint32_t insNum = 0;
    HcclResult queryRet = HcclCommQueryCcuIns(comm, &insHandle, &insNum);
    if (queryRet == HCCL_SUCCESS) {
        return insNum == 1 ? HCCL_SUCCESS : HCCL_E_INTERNAL;
    }
    if (queryRet != HCCL_E_UNAVAIL) {
        return queryRet;
    }

    std::vector<HcommCcuResDescHandle> descs;
    for (uint32_t dieId = 0; dieId < CCU_MAX_DIE_NUM; ++dieId) {
        HcommCcuResDescHandle desc = 0;
        CcuResult ret = HcommCcuInsResDescCreate(dieId, &desc);
        if (ret != CCU_SUCCESS) {
            DestroyCcuResourceDescs(descs);
            return ConvertCcuResult(ret);
        }
        ret = HcommCcuQueryRemainResDesc(desc);
        if (ret == CCU_E_UNAVAIL) {
            (void)HcommCcuInsResDescDestroy(desc);
            continue;
        }
        if (ret != CCU_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyCcuResourceDescs(descs);
            return ConvertCcuResult(ret);
        }
        HcclResult setRet = SetCcuSchedResourceNums(desc);
        if (setRet != HCCL_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyCcuResourceDescs(descs);
            return setRet;
        }
        descs.push_back(desc);
    }
    if (descs.empty()) {
        return HCCL_E_UNAVAIL;
    }

    CcuResult createRet = HcommCcuInsCreate(descs.data(), static_cast<uint32_t>(descs.size()), &insHandle);
    DestroyCcuResourceDescs(descs);
    if (createRet != CCU_SUCCESS) {
        return ConvertCcuResult(createRet);
    }

    HcclResult assignRet = HcclCommAssignCcuIns(comm, insHandle);
    if (assignRet != HCCL_SUCCESS) {
        (void)HcommCcuInsDestroy(insHandle);
        insHandle = 0;
        return assignRet;
    }
    return HCCL_SUCCESS;
}

inline HcclResult QueryEndpointDie(
    HcclComm comm, uint32_t rankId, const EndpointDesc &endpoint, uint32_t &dieId)
{
    EndpointAttrDieId endpointDie = 0;
    HcclResult ret = HcclRankGraphGetEndpointInfo(
        comm, rankId, &endpoint, ENDPOINT_ATTR_DIE_ID, sizeof(endpointDie), &endpointDie);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (endpointDie >= CCU_MAX_DIE_NUM) {
        return HCCL_E_PARA;
    }
    dieId = endpointDie;
    return HCCL_SUCCESS;
}

inline HcclResult MergeKernelDie(uint32_t channelDie, bool &hasKernelDie, uint32_t &kernelDie)
{
    if (!hasKernelDie) {
        kernelDie = channelDie;
        hasKernelDie = true;
        return HCCL_SUCCESS;
    }
    return kernelDie == channelDie ? HCCL_SUCCESS : HCCL_E_PARA;
}

inline HcclResult PhyDieMaskToDieId(uint64_t phyDieMask, uint32_t &dieId)
{
    if (phyDieMask == 0 || (phyDieMask & (phyDieMask - 1)) != 0) {
        return HCCL_E_PARA;
    }
    uint32_t id = 0;
    while ((phyDieMask >>= 1) != 0) {
        ++id;
    }
    if (id >= CCU_MAX_DIE_NUM) {
        return HCCL_E_PARA;
    }
    dieId = id;
    return HCCL_SUCCESS;
}

} // namespace asccomm_examples

#endif // ASCCOMM_EXAMPLES_CCU_INSTANCE_H
