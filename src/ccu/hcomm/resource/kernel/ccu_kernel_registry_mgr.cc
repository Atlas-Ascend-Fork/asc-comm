/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_registry_mgr.h"

#include <algorithm>

#include "hcomm/common/ccu_log.h"
#include "hcomm/common/ccu_device_context.h"

namespace asc {

ccu_kernel_registry_mgr::~ccu_kernel_registry_mgr()
{
    if (!initialized_flag_) {
        return;
    }

    (void)deinit();
}

ccu_kernel_registry_mgr& ccu_kernel_registry_mgr::get_instance(const int32_t device_logic_id)
{
    static ccu_kernel_registry_mgr instance_mgrs[ccu_max_device_num + 1];

    int32_t dev_logic_id = device_logic_id;
    if (dev_logic_id < 0 || static_cast<uint32_t>(dev_logic_id) >= ccu_max_device_num) {
        HCCL_WARNING(
            "[CcuKernelRegistryMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.",
            __func__, dev_logic_id, ccu_max_device_num);
        dev_logic_id = ccu_max_device_num; // 使用备份设备
    }

    instance_mgrs[dev_logic_id].dev_logic_id_ = dev_logic_id;
    return instance_mgrs[dev_logic_id];
}

CcuResult ccu_kernel_registry_mgr::init()
{
    std::unique_lock<std::shared_timed_mutex> lock(ins_map_mutex_);
    if (initialized_flag_) {
        return CcuResult::CCU_SUCCESS;
    }

    initialized_flag_ = true;
    ins_map_.clear();
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry_mgr::deinit()
{
    std::unique_lock<std::shared_timed_mutex> lock(ins_map_mutex_);
    ins_map_.clear();
    initialized_flag_ = false;
    return CcuResult::CCU_SUCCESS;
}

CcuResult ccu_kernel_registry_mgr::get_or_create(
    int32_t device_logic_id, CcuInsHandle ins_handle, ccu_kernel_registry*& registry)
{
    registry = nullptr;
    if (ins_handle == 0) {
        return CcuResult::CCU_E_PARA;
    }

    std::unique_lock<std::shared_timed_mutex> lock(ins_map_mutex_);
    auto it = ins_map_.find(ins_handle);
    if (it != ins_map_.end()) {
        registry = it->second.get();
        return CcuResult::CCU_SUCCESS;
    }

    std::unique_ptr<ccu_kernel_registry> instance{nullptr};
    EXCEPTION_CATCH(instance = std::make_unique<ccu_kernel_registry>(), return CcuResult::CCU_E_PTR);
    CCU_CHK_RET(instance->init(device_logic_id));
    instance->set_handle(ins_handle);
    registry = instance.get();
    ins_map_.emplace(ins_handle, std::move(instance));
    return CcuResult::CCU_SUCCESS;
}

ccu_kernel_registry* ccu_kernel_registry_mgr::get(CcuInsHandle ins_handle) const
{
    std::shared_lock<std::shared_timed_mutex> lock(ins_map_mutex_);
    auto it = ins_map_.find(ins_handle);
    if (it == ins_map_.end()) {
        HCCL_ERROR("[CcuKernelRegistryMgr][%s] handle[%llx] is not existed.", __func__, ins_handle);
        return nullptr;
    }

    return it->second.get();
}

} // namespace asc
