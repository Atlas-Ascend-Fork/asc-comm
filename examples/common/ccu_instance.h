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
#include <ccu/ccu_types.h>          // 对外 token 句柄（CcuInsHandle，安装包 hcomm 提供）
#include <ccu/hcomm/ccu_api_types.h>
#include <hcomm/hcomm_ccu_resource.h>

extern "C" HcclResult HcclCommQueryCcuIns(HcclComm comm, CcuInsHandle* insHandles, uint32_t* insNum);
extern "C" HcommResult HcommCcuInsQueryInstance(uint64_t ccuInsKey, HcommCcuInstanceHandle* instanceHandle);

namespace asccomm_examples {

constexpr uint32_t CCU_MAX_DIE_NUM = 2;

/**
 * 查询通信域 CCU 实例并兑换为原语链句柄（两步）：
 * 1) HcclCommQueryCcuIns 取不透明 token（对外 API 句柄，仅保存/回传语义）；
 * 2) HcommCcuInsQueryInstance 兑换为 HcommCcuInstanceHandle（含数据面 POD 借用，
 *    供 asccomm_ccu_kernel_register* 原语与 ASC launch 使用，随通信域生命周期有效）。
 */
inline HcclResult QueryCcuInstance(HcclComm comm, HcommCcuInstanceHandle& insHandle)
{
    CcuInsHandle insKey = 0;
    uint32_t insNum = 0;
    HcclResult ret = HcclCommQueryCcuIns(comm, &insKey, &insNum);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (insNum != 1) {
        return HCCL_E_INTERNAL;
    }
    return static_cast<HcclResult>(HcommCcuInsQueryInstance(insKey, &insHandle));
}

inline HcclResult QueryEndpointDie(HcclComm comm, uint32_t rankId, const EndpointDesc& endpoint, uint32_t& dieId)
{
    EndpointAttrDieId endpointDie = 0;
    HcclResult ret =
        HcclRankGraphGetEndpointInfo(comm, rankId, &endpoint, ENDPOINT_ATTR_DIE_ID, sizeof(endpointDie), &endpointDie);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (endpointDie >= CCU_MAX_DIE_NUM) {
        return HCCL_E_PARA;
    }
    dieId = endpointDie;
    return HCCL_SUCCESS;
}

inline HcclResult MergeKernelDie(uint32_t channelDie, bool& hasKernelDie, uint32_t& kernelDie)
{
    if (!hasKernelDie) {
        kernelDie = channelDie;
        hasKernelDie = true;
        return HCCL_SUCCESS;
    }
    return kernelDie == channelDie ? HCCL_SUCCESS : HCCL_E_PARA;
}

inline HcclResult PhyDieMaskToDieId(uint64_t phyDieMask, uint32_t& dieId)
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
