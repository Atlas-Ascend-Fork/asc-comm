/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_instance_mgr.h"

#include "ccu_log.h"
#include "string_util.h"
#include "framework/entrance/hccl_common_v2.h"

namespace hcomm {

CcuInstanceMgr::~CcuInstanceMgr()
{
    if (!initializedFlag_) {
        return;
    }

    (void)Deinit();
}

CcuInstanceMgr &CcuInstanceMgr::GetInstance(const int32_t deviceLogicId)
{
    static CcuInstanceMgr instanceMgrs[MAX_MODULE_DEVICE_NUM + 1];

    int32_t devLogicId = deviceLogicId;
    if (devLogicId < 0 || static_cast<uint32_t>(devLogicId) >= MAX_MODULE_DEVICE_NUM) {
        HCCL_WARNING("[CcuInstanceMgr][%s] use the backup device, devLogicId[%d] should be "
            "less than %u.", __func__, devLogicId, MAX_MODULE_DEVICE_NUM);
        devLogicId = MAX_MODULE_DEVICE_NUM; // 使用备份设备
    }

    instanceMgrs[devLogicId].devLogicId_ = devLogicId;
    return instanceMgrs[devLogicId];
}

CcuResult CcuInstanceMgr::Init()
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    if (initializedFlag_) {
        return CcuResult::CCU_SUCCESS;
    }

    initializedFlag_ = true;
    insMap_.clear();
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstanceMgr::Deinit()
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    insMap_.clear();
    initializedFlag_ = false;
    return CcuResult::CCU_SUCCESS;
}

CcuResult CcuInstanceMgr::Create(
    CcuInstanceType insType, CcuInsHandle &insHandle)
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);

    std::unique_ptr<CcuInstance> instance{nullptr};
    EXCEPTION_CATCH(
        instance = std::make_unique<CcuInstance>(insType),
        return CcuResult::CCU_E_PTR);

    CCU_CHK_RET(instance->Init());

    instanceId_ += 1;
    insMap_.emplace(instanceId_, std::move(instance));
    insHandle = instanceId_;
    return CcuResult::CCU_SUCCESS;
}

CcuInstance *CcuInstanceMgr::Get(CcuInsHandle insHandle) const
{
    std::shared_lock<std::shared_timed_mutex> lock(insMapMutex_);
    auto it = insMap_.find(insHandle);
    if (it == insMap_.end()) {
        HCCL_ERROR("[CcuInstanceMgr][%s] handle[%llx] is not existed.",
            __func__, insHandle);
        return nullptr;
    }

    return it->second.get();
}

CcuResult CcuInstanceMgr::Destroy(CcuInsHandle insHandle)
{
    std::unique_lock<std::shared_timed_mutex> lock(insMapMutex_);
    auto it = insMap_.find(insHandle);
    if (it == insMap_.end()) {
        HCCL_ERROR("[CcuInstanceMgr][%s] handle[%llx] is not existed.",
            __func__, insHandle);
        return CcuResult::CCU_E_NOT_FOUND;
    }

    insMap_.erase(it);
    return CcuResult::CCU_SUCCESS;
}

} // namespace hcomm