/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file ccu_res_desc_c_adpt.cc
 * @brief ResDesc 查询链 C ABI：HcommCcuInsResDesc* 生命周期。
 *
 * HcommCcuResDesc POD 不对用户暴露，用户只感知 HcommCcuResDescHandle；
 * 本文件维护 handle table（handle 单调递增不复用，0 为无效句柄）。
 */

#include "hcomm/hcomm_ccu_res_desc.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "hcomm/common/ccu_log.h"

namespace {

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

} // extern "C"
