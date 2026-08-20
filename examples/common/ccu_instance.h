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

#include <cstdint>

#include <hccl/hccl_rank_graph.h>
#include <ccu/hcomm/ccu_api_types.h>

extern "C" HcclResult HcclCommQueryCcuIns(HcclComm comm, CcuInsHandle* insHandles, uint32_t* insNum);

namespace asccomm_examples {

constexpr uint32_t CCU_MAX_DIE_NUM = 2;

inline HcclResult QueryCcuInstance(HcclComm comm, CcuInsHandle &ins_handle)
{
    uint32_t insNum = 0;
    HcclResult ret = HcclCommQueryCcuIns(comm, &ins_handle, &insNum);
    return ret == HCCL_SUCCESS && insNum != 1 ? HCCL_E_INTERNAL : ret;
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

inline HcclResult phy_die_mask_to_die_id(uint64_t phy_die_mask, uint32_t &dieId)
{
    if (phy_die_mask == 0 || (phy_die_mask & (phy_die_mask - 1)) != 0) {
        return HCCL_E_PARA;
    }
    uint32_t id_ = 0;
    while ((phy_die_mask >>= 1) != 0) {
        ++id_;
    }
    if (id_ >= CCU_MAX_DIE_NUM) {
        return HCCL_E_PARA;
    }
    dieId = id_;
    return HCCL_SUCCESS;
}

} // namespace asccomm_examples

#endif // ASCCOMM_EXAMPLES_CCU_INSTANCE_H
