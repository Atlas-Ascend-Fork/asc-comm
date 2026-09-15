/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the License).
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file ccu_res_desc_c_adpt.cc
 * @brief ResDesc 查询链 C ABI：HcommCcuInsResDesc* 生命周期 + HcommCcuKernelQueryResReq。
 *
 * HcommCcuResDesc POD 不对用户暴露，用户只感知 HcommCcuResDescHandle；
 * 本文件维护 handle table，并把 asc_ccu_res_request（block/非 block 分开）换算为
 * ResDesc 统计口径（block 合并计数，见 CcuResDescType）。
 * QueryResReq 前置条件：该设备已完成 asccomm_ccu_kernel_register_start（translators 已 Configure）。
 */

#include "hcomm/hcomm_ccu_res_desc.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "hcomm/common/ccu_device_context.h"
#include "hcomm/common/ccu_log.h"
#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

namespace {

using asc::asc_ccu_res_request;

struct ResDescEntry {
    HcommCcuResDesc desc{};
};

// ResDesc handle table：handle 单调递增不复用，0 为无效句柄
class ResDescRegistry {
public:
    static ResDescRegistry& GetInstance()
    {
        static ResDescRegistry registry;
        return registry;
    }

    CcuResult Create(uint32_t die_id, HcommCcuResDescHandle& handle)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        try {
            entries_.emplace(++next_handle_, ResDescEntry{});
        } catch (...) {
            return CcuResult::CCU_E_INTERNAL;
        }
        auto& desc = entries_[next_handle_].desc;
        desc.header = {HCOMM_CCU_RES_DESC_ABI_VERSION, HCOMM_CCU_RES_DESC_MAGIC_WORD, sizeof(HcommCcuResDesc), 0};
        desc.dieId = die_id;
        desc.resLength = HCOMM_CCU_RES_DESC_TYPE_COUNT;
        handle = next_handle_;
        return CcuResult::CCU_SUCCESS;
    }

    CcuResult Destroy(HcommCcuResDescHandle handle)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return entries_.erase(handle) != 0 ? CcuResult::CCU_SUCCESS : CcuResult::CCU_E_NOT_FOUND;
    }

    CcuResult QueryDieId(HcommCcuResDescHandle handle, uint32_t& die_id) const
    {
        const HcommCcuResDesc* desc = Find(handle);
        if (desc == nullptr) {
            return CcuResult::CCU_E_NOT_FOUND;
        }
        die_id = desc->dieId;
        return CcuResult::CCU_SUCCESS;
    }

    // 值拷贝反查：hcomm 侧据此获取实际需要的 CCU 资源
    CcuResult Get(HcommCcuResDescHandle handle, HcommCcuResDesc& out) const
    {
        const HcommCcuResDesc* desc = Find(handle);
        if (desc == nullptr) {
            return CcuResult::CCU_E_NOT_FOUND;
        }
        out = *desc;
        return CcuResult::CCU_SUCCESS;
    }

    CcuResult SetNum(HcommCcuResDescHandle handle, CcuResDescType res_type, uint32_t num)
    {
        HcommCcuResDesc* desc = Find(handle);
        if (desc == nullptr) {
            return CcuResult::CCU_E_NOT_FOUND;
        }
        if (static_cast<uint32_t>(res_type) >= HCOMM_CCU_RES_DESC_TYPE_COUNT) {
            return CcuResult::CCU_E_PARA;
        }
        desc->resNum[res_type] = num;
        return CcuResult::CCU_SUCCESS;
    }

    CcuResult QueryNum(HcommCcuResDescHandle handle, CcuResDescType res_type, uint32_t& num) const
    {
        const HcommCcuResDesc* desc = Find(handle);
        if (desc == nullptr) {
            return CcuResult::CCU_E_NOT_FOUND;
        }
        if (static_cast<uint32_t>(res_type) >= HCOMM_CCU_RES_DESC_TYPE_COUNT) {
            return CcuResult::CCU_E_PARA;
        }
        num = desc->resNum[res_type];
        return CcuResult::CCU_SUCCESS;
    }

private:
    HcommCcuResDesc* Find(HcommCcuResDescHandle handle)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(handle);
        return it == entries_.end() ? nullptr : &it->second.desc;
    }

    const HcommCcuResDesc* Find(HcommCcuResDescHandle handle) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = entries_.find(handle);
        return it == entries_.end() ? nullptr : &it->second.desc;
    }

    mutable std::mutex mutex_{};
    std::unordered_map<uint64_t, ResDescEntry> entries_{};
    uint64_t next_handle_{0};
};

// 把 kernel 资源需求换算为 ResDesc 统计口径：block/非 block 求和，INS 为指令条数，COUNT_XN 预留为 0
CcuResult ConvertResReqToDesc(
    HcommCcuResDescHandle handle, const asc_ccu_res_request& req, uint32_t instr_count, uint32_t selected_die)
{
    auto& registry = ResDescRegistry::GetInstance();
    const uint32_t loop_num =
        req.count[HCOMM_CCU_BATCH_RES_LOOP][selected_die] + req.count[HCOMM_CCU_BATCH_RES_BLOCK_LOOP][selected_die];
    const uint32_t ms_num =
        req.count[HCOMM_CCU_BATCH_RES_MS][selected_die] + req.count[HCOMM_CCU_BATCH_RES_BLOCK_MS][selected_die];
    const uint32_t xn_num =
        req.count[HCOMM_CCU_BATCH_RES_XN][selected_die] + req.count[HCOMM_CCU_BATCH_RES_BLOCK_XN][selected_die];
    const uint32_t gsa_num =
        req.count[HCOMM_CCU_BATCH_RES_GSA][selected_die] + req.count[HCOMM_CCU_BATCH_RES_BLOCK_GSA][selected_die];
    const uint32_t cke_num =
        req.count[HCOMM_CCU_BATCH_RES_CKE][selected_die] + req.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][selected_die];
    const uint32_t mission_num = req.count[HCOMM_CCU_BATCH_RES_MISSION][selected_die];

    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_LOOP, loop_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_MS, ms_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_XN, xn_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_GSA, gsa_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_CKE, cke_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_MISSION, mission_num));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_INS, instr_count));
    CCU_CHK_RET(registry.SetNum(handle, CCU_RES_DESC_COUNT_XN, 0)); // 预留口径，当前恒为 0

    HCCL_INFO(
        "[HcommCcuKernelQueryResReq] success, aggregated resource request, resDesc[%llu], dieId[%u], "
        "loop[%u], ms[%u], xn[%u], gsa[%u], cke[%u], mission[%u], ins[%u].",
        static_cast<unsigned long long>(handle), selected_die, loop_num, ms_num, xn_num, gsa_num, cke_num, mission_num,
        instr_count);
    return CcuResult::CCU_SUCCESS;
}

} // namespace

extern "C" {

CcuResult HcommCcuInsResDescCreate(uint32_t dieId, HcommCcuResDescHandle* resDesc)
{
    if (resDesc == nullptr) {
        return CcuResult::CCU_E_PTR;
    }
    *resDesc = 0;
    return ResDescRegistry::GetInstance().Create(dieId, *resDesc);
}

CcuResult HcommCcuInsResDescDestroy(HcommCcuResDescHandle resDesc)
{
    if (resDesc == 0) {
        return CcuResult::CCU_E_PARA;
    }
    return ResDescRegistry::GetInstance().Destroy(resDesc);
}

CcuResult HcommCcuInsResDescQueryDieId(HcommCcuResDescHandle resDesc, uint32_t* dieId)
{
    if (resDesc == 0 || dieId == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    return ResDescRegistry::GetInstance().QueryDieId(resDesc, *dieId);
}

CcuResult HcommCcuInsResDescSetNum(HcommCcuResDescHandle resDesc, uint32_t resType, uint32_t num)
{
    if (resDesc == 0 || resType >= HCOMM_CCU_RES_DESC_TYPE_COUNT) {
        return CcuResult::CCU_E_PARA;
    }
    return ResDescRegistry::GetInstance().SetNum(resDesc, static_cast<CcuResDescType>(resType), num);
}

CcuResult HcommCcuInsResDescQueryNum(HcommCcuResDescHandle resDesc, uint32_t resType, uint32_t* num)
{
    if (resDesc == 0 || num == nullptr || resType >= HCOMM_CCU_RES_DESC_TYPE_COUNT) {
        return CcuResult::CCU_E_PARA;
    }
    return ResDescRegistry::GetInstance().QueryNum(resDesc, static_cast<CcuResDescType>(resType), *num);
}

// 反查通道：hcomm 按需建实例等控制面路径用 handle 取回描述符内容（值拷贝）
CcuResult HcommCcuInsResDescQuery(HcommCcuResDescHandle resDesc, HcommCcuResDesc* out)
{
    if (resDesc == 0 || out == nullptr) {
        return CcuResult::CCU_E_PARA;
    }
    *out = HcommCcuResDesc{};
    return ResDescRegistry::GetInstance().Get(resDesc, *out);
}

CcuResult HcommCcuKernelQueryResReq(
    const void* kernelFunc, const void** kernelArgs, uint32_t argNum, uint32_t dieId, HcommCcuResDescHandle resDesc)
{
    HCCL_INFO(
        "[%s] begin, argNum[%u], dieId[%u], resDesc[%llu], kernelFunc[%p].", __func__, argNum, dieId,
        static_cast<unsigned long long>(resDesc), kernelFunc);
    if (kernelFunc == nullptr) {
        return CcuResult::CCU_E_PTR;
    }
    if (resDesc == 0 || argNum > 1) {
        HCCL_ERROR(
            "[%s] failed, resDesc[%llu], argNum[%u].", __func__, static_cast<unsigned long long>(resDesc), argNum);
        return CcuResult::CCU_E_PARA;
    }
    if (argNum == 1) {
        if (kernelArgs == nullptr || kernelArgs[0] == nullptr) {
            return CcuResult::CCU_E_PTR;
        }
    }

    uint32_t descriptor_die_id = 0;
    CCU_CHK_RET(ResDescRegistry::GetInstance().QueryDieId(resDesc, descriptor_die_id));
    if (descriptor_die_id != dieId) {
        HCCL_ERROR("[%s] failed, descriptor dieId[%u] mismatches input dieId[%u].", __func__, descriptor_die_id, dieId);
        return CcuResult::CCU_E_PARA;
    }

    // 前置：该线程已完成 register_start（Configure 实例化 translators/insGenePtr 并绑定线程设备）
    const int32_t dev_logic_id = asc::get_current_ccu_device_logic_id();
    if (dev_logic_id < 0) {
        HCCL_ERROR("[%s] failed, register context not initialized on this thread.", __func__);
        return CcuResult::CCU_E_UNAVAIL;
    }

    CCU_EXCEPTION_HANDLE_BEGIN
    asc_ccu_res_request res_req{};
    uint32_t instr_count = 0;
    auto& kernel_mgr = asc::ccu_kernel_mgr::get_instance(dev_logic_id);
    CCU_CHK_RET(kernel_mgr.get_kernel_resource_request(
        dieId, "KernelForHcommCcuKernelQueryResReq", kernelFunc, kernelArgs, argNum, res_req, instr_count));
    CCU_CHK_RET(ConvertResReqToDesc(resDesc, res_req, instr_count, dieId));
    CCU_EXCEPTION_HANDLE_END

    return CcuResult::CCU_SUCCESS;
}

} // extern "C"
