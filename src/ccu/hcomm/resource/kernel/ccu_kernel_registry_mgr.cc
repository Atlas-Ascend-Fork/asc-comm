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
#include "string_util.h"
#include "framework/entrance/hccl_common_v2.h"

namespace hcomm {

CcuKernelRegistryMgr::~CcuKernelRegistryMgr()
{
    if (!initializedFlag_) {
        return;
    }

    (void)Deinit();
}

CcuKernelRegistryMgr& CcuKernelRegistryMgr::GetInstance(const int32_t deviceLogicId)
{
    static CcuKernelRegistryMgr instanceMgrs[MAX_MODULE_DEVICE_NUM + 1];

    int32_t devLogicId = deviceLogicId;
    if (devLogicId < 0 || static_cast<uint32_t>(devLogicId) >= MAX_MODULE_DEVICE_NUM) {
        HCCL_WARNING(
            "[CcuKernelRegistryMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.",
            __func__, devLogicId, MAX_MODULE_DEVICE_NUM);
        devLogicId = MAX_MODULE_DEVICE_NUM; // 使用备份设备
    }

    instanceMgrs[devLogicId].devLogicId_ = devLogicId;
    return instanceMgrs[devLogicId];
}

CcuResult CcuKernelRegistryMgr::Init()
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    if (initializedFlag_) {
        return CcuResult::CCU_SUCCESS;
    }

    initializedFlag_ = true;
    insMap_.clear();
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistryMgr::Deinit()
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    insMap_.clear();
    initializedFlag_ = false;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuKernelRegistryMgr::GetOrCreate(int32_t deviceLogicId, CcuInsHandle insHandle,
    CcuKernelRegistry*& registry)
{
    registry = nullptr;
    if (insHandle == 0) {
        return CcuResult::CCU_E_PARA;
    }

    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    auto it = insMap_.find(insHandle);
    if (it != insMap_.end()) {
        registry = it->second.get();
        return CcuResult::CCU_SUCCESS;
    }

    std::unique_ptr<CcuKernelRegistry> instance{nullptr};
    EXCEPTION_CATCH(instance = std::make_unique<CcuKernelRegistry>(), return CcuResult::CCU_E_PTR);
    CCU_CHK_RET(instance->Init(deviceLogicId));
    instance->SetHandle(insHandle);
    registry = instance.get();
    insMap_.emplace(insHandle, std::move(instance));
    return CcuResult::CCU_SUCCESS;
}

CcuKernelRegistry* CcuKernelRegistryMgr::Get(CcuInsHandle insHandle) const
{
    std::shared_lock<std::shared_timed_mutex> lock(insMapMutex_);
    auto it = insMap_.find(insHandle);
    if (it == insMap_.end()) {
        HCCL_ERROR("[CcuKernelRegistryMgr][%s] handle[%llx] is not existed.", __func__, insHandle);
        return nullptr;
    }

    return it->second.get();
}

} // namespace hcomm
