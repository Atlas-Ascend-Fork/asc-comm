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
#include <cstdio>
#include <vector>

#include <hccl/hccl_rank_graph.h>
#include <hccl_ccu_res.h>
#include <ccu_res.h>
#include <ccu/hcomm/ccu_api_types.h>
#include <hcomm/ccu/hcomm_res_ccu_entity_defs.h>

namespace asccomm_examples {

constexpr uint32_t CCU_MAX_DIE_NUM = 2;

// CreateCcuInsWithEnabledDies 为每个使能 die 申请的各类资源数量（默认比例）。
// 取值与 asc-comm 9.2.0 examples/ccu/ccu_direct/01_allgather/main.asc 保持一致。
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_ADDRESS = 400;
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_LOOP = 16;
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_CCU_BUF = 128;
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_VARIABLE = 400;
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_EVENT = 48;
constexpr uint32_t CCU_DEFAULT_RES_FRACTION_CCU_THREAD = 2;

struct CcuDefaultRes {
    HcommCcuResType type;
    uint32_t num;
};

static constexpr CcuDefaultRes CCU_DEFAULT_RES[] = {
    {HCOMM_CCU_RES_TYPE_ADDRESS, CCU_DEFAULT_RES_FRACTION_ADDRESS},
    {HCOMM_CCU_RES_TYPE_LOOP, CCU_DEFAULT_RES_FRACTION_LOOP},
    {HCOMM_CCU_RES_TYPE_CCU_BUF, CCU_DEFAULT_RES_FRACTION_CCU_BUF},
    {HCOMM_CCU_RES_TYPE_VARIABLE, CCU_DEFAULT_RES_FRACTION_VARIABLE},
    {HCOMM_CCU_RES_TYPE_EVENT, CCU_DEFAULT_RES_FRACTION_EVENT},
    {HCOMM_CCU_RES_TYPE_CCU_THREAD, CCU_DEFAULT_RES_FRACTION_CCU_THREAD},
};

inline const char* CcuResTypeName(HcommCcuResType type)
{
    switch (type) {
        case HCOMM_CCU_RES_TYPE_LOOP:
            return "LOOP";
        case HCOMM_CCU_RES_TYPE_CCU_BUF:
            return "CCU_BUF";
        case HCOMM_CCU_RES_TYPE_VARIABLE:
            return "VARIABLE";
        case HCOMM_CCU_RES_TYPE_ADDRESS:
            return "ADDRESS";
        case HCOMM_CCU_RES_TYPE_EVENT:
            return "EVENT";
        case HCOMM_CCU_RES_TYPE_CCU_THREAD:
            return "CCU_THREAD";
        case HCOMM_CCU_RES_TYPE_INSTRUCTION:
            return "INSTRUCTION";
        case HCOMM_CCU_RES_TYPE_CASC_CNT:
            return "CASC_CNT";
        default:
            return "UNKNOWN";
    }
}

/**
 * 查询通信域 CCU 实例并返回原语链句柄。直接调用 hcomm 对外接口；
 * void* 请求地址由用户侧传入，返回 hcomm 持有的 HcommCcuInstanceEntity 稳定地址。
 * CCU 实例资源数量说明：
 * - HcclCommQueryCcuIns 返回的 insNum 恒为 1（一个通信域至多绑定一个 CCU 实例）
 *   按 CCU_MS / CCU_SCHED 使用固定申请量
 *     CCU_MS：    blockLoopEngine=128, blockMs=1024, blockCke=160, blockXn=400, blockGsa=400, mission=2
 *     CCU_SCHED： blockLoopEngine=16,  blockMs=128,  blockCke=48,  blockXn=400, blockGsa=400, mission=2
 * - 若用户显式用 HcommCcuInsCreate + ResDesc 创建实例，资源数量改由资源描述符决定；
 *   HcommCcuInsCreateDefault 则取设备全部可用资源。
 */
inline HcclResult QueryCcuInstance(HcclComm comm, void*& insHandle)
{
    CcuInsHandle insPtr = reinterpret_cast<CcuInsHandle>(&insHandle);
    uint32_t insNum = 0;
    HcclResult ret = HcclCommQueryCcuIns(comm, &insPtr, &insNum);
    if (ret != HCCL_SUCCESS) {
        return ret;
    }
    if (insNum != 1) {
        return HCCL_E_INTERNAL;
    }
    insHandle = reinterpret_cast<void*>(insPtr);
    return HCCL_SUCCESS;
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

inline void DestroyAllDescs(std::vector<HcommCcuResDescHandle>& descs)
{
    for (auto& desc : descs) {
        (void)HcommCcuInsResDescDestroy(desc);
    }
}

/**
 * 用所有已使能 IO Die 的"默认资源比例"显式创建一个 CCU 实例。
 * 流程：逐 die 创建资源描述符 -> HcommCcuQueryRemainResDesc 探测该 die 是否使能
 *       (CCU_E_UNAVAIL 视为未使能，跳过) -> 按 CCU_DEFAULT_RES 设置各类型数量 ->
 *       HcommCcuInsCreate 创建实例。
 * 注意：
 * - 创建出的实例尚未绑定通信域；调用方需再调用 HcclCommAssignCcuIns(comm, insHandle)
 *   将其绑定（失败时应 HcommCcuInsDestroy(insHandle) 销毁实例，避免泄漏）。
 * - 与 QueryCcuInstance 的自动创建（按展开模式 CCU_MS/CCU_SCHED 的固定量）不同，
 *   这里由调用方显式指定各资源类型数量。
 * - 返回 CCU_SUCCESS 时 insHandle 有效；其余返回值下已清理内部描述符，不产生泄漏。
 */
inline CcuResult CreateCcuInsWithCustomNum(CcuInsHandle& insHandle)
{
    std::vector<HcommCcuResDescHandle> resDescs;
    for (uint32_t dieId = 0; dieId < CCU_MAX_DIE_NUM; dieId++) {
        HcommCcuResDescHandle desc = 0;
        CcuResult descCreateRet = HcommCcuInsResDescCreate(dieId, &desc);
        if (descCreateRet != CCU_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyAllDescs(resDescs);
            return descCreateRet;
        }

        // 探测该 die 是否使能：未使能返回 CCU_E_UNAVAIL，跳过该 die。
        CcuResult probeRet = HcommCcuQueryRemainResDesc(desc);
        if (probeRet == CCU_E_UNAVAIL) {
            printf(">> die %u is not enabled, skipped.\n", dieId);
            (void)HcommCcuInsResDescDestroy(desc);
            continue;
        }
        if (probeRet != CCU_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyAllDescs(resDescs);
            return probeRet;
        }

        for (const auto& res : CCU_DEFAULT_RES) {
            CcuResult setRet = HcommCcuInsResDescSetNum(desc, res.type, res.num);
            if (setRet != CCU_SUCCESS) {
                (void)HcommCcuInsResDescDestroy(desc);
                DestroyAllDescs(resDescs);
                return setRet;
            }
        }
        resDescs.push_back(desc);
    }

    if (resDescs.empty()) {
        return CcuResult::CCU_E_INTERNAL;
    }

    CcuResult createRet = HcommCcuInsCreate(resDescs.data(), static_cast<uint32_t>(resDescs.size()), &insHandle);
    DestroyAllDescs(resDescs);
    return createRet;
}

/**
 * 用设备各已使能 IO Die 的"剩余资源"显式创建一个 CCU 实例。
 * 流程：逐 die 创建资源描述符 -> HcommCcuQueryRemainResDesc 探测该 die 是否使能
 *       (CCU_E_UNAVAIL 视为未使能，跳过) -> 收集使能 die 的描述符 ->
 *       HcommCcuInsCreate 创建实例。
 * 用户信息：
 * - 创建成功后经 HcommCcuInsQueryResDesc 回填、HcommCcuInsResDescQueryDieId /
 *   HcommCcuInsResDescQueryNum 读取，打印实例在各 die 上占用的资源数量。
 * 注意：
 * - 创建出的实例尚未绑定通信域；调用方需再调用 HcclCommAssignCcuIns(comm, insHandle)
 *   将其绑定（失败时应 HcommCcuInsDestroy(insHandle) 销毁实例，避免泄漏）。
 * - 返回 CCU_SUCCESS 时 insHandle 有效；其余返回值下已清理内部描述符，不产生泄漏。
 */
inline CcuResult CreateCcuInsWithRemainResDesc(CcuInsHandle& insHandle)
{
    std::vector<HcommCcuResDescHandle> resDescs;
    for (uint32_t dieId = 0; dieId < CCU_MAX_DIE_NUM; dieId++) {
        HcommCcuResDescHandle desc = 0;
        CcuResult descCreateRet = HcommCcuInsResDescCreate(dieId, &desc);
        if (descCreateRet != CCU_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyAllDescs(resDescs);
            return descCreateRet;
        }

        // 探测该 die 是否使能：未使能返回 CCU_E_UNAVAIL，跳过该 die。
        CcuResult probeRet = HcommCcuQueryRemainResDesc(desc);
        if (probeRet == CCU_E_UNAVAIL) {
            printf(">> die %u is not enabled, skipped.\n", dieId);
            (void)HcommCcuInsResDescDestroy(desc);
            continue;
        }
        if (probeRet != CCU_SUCCESS) {
            (void)HcommCcuInsResDescDestroy(desc);
            DestroyAllDescs(resDescs);
            return probeRet;
        }
        printf(">> die %u is enabled, res desc collected.\n", dieId);
        resDescs.push_back(desc);
    }

    if (resDescs.empty()) {
        return CcuResult::CCU_E_INTERNAL;
    }

    CcuResult createRet = HcommCcuInsCreate(resDescs.data(), static_cast<uint32_t>(resDescs.size()), &insHandle);
    if (createRet == CCU_SUCCESS) {
        // 用户信息：逐个打印实例在各 die 上占用的资源
        // resDescs 中每个元素对应一个 die 的描述符，互不覆盖；HcommCcuInsQueryResDesc 回填 + QueryDieId / QueryNum 读取
        for (auto& desc : resDescs) {
            if (HcommCcuInsQueryResDesc(insHandle, desc) != CCU_SUCCESS) {
                continue;
            }
            uint32_t occDieId = 0;
            (void)HcommCcuInsResDescQueryDieId(desc, &occDieId);
            printf(">> ccu ins die %u occupied res:", occDieId);
            for (const auto& res : CCU_DEFAULT_RES) {
                uint32_t resNum = 0;
                if (HcommCcuInsResDescQueryNum(desc, res.type, &resNum) == CCU_SUCCESS) {
                    printf(" %s=%u", CcuResTypeName(res.type), resNum);
                }
            }
            printf("\n");
        }
    }
    DestroyAllDescs(resDescs);
    return createRet;
}

} // namespace asccomm_examples

#endif // ASCCOMM_EXAMPLES_CCU_INSTANCE_H
