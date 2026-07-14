/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_INSTANCE_MGR_H
#define HCOMM_CCU_INSTANCE_MGR_H

#include <mutex>
#include <shared_mutex>
#include <unordered_map>

#include "ccu_types.h"
#include "ccu_instance.h"

namespace hcomm {

class CcuInstanceMgr {
public:
    static CcuInstanceMgr &GetInstance(const int32_t deviceLogicId);

    CcuResult Init();
    CcuResult Deinit();

    CcuResult Create(CcuInstanceType insType, CcuInsHandle &insHandle);
    CcuInstance *Get(CcuInsHandle insHandle) const;
    CcuResult Destroy(CcuInsHandle insHandle);

private:
    explicit CcuInstanceMgr() = default;
    ~CcuInstanceMgr();

    CcuInstanceMgr(const CcuInstanceMgr &that) = delete;
    CcuInstanceMgr &operator=(const CcuInstanceMgr &that) = delete;

private:
    bool initializedFlag_{false};
    int32_t devLogicId_{-1};
    CcuInsHandle instanceId_{0};
    mutable std::shared_timed_mutex insMapMutex_;
    std::unordered_map<CcuInsHandle, std::unique_ptr<CcuInstance>> insMap_{};
};
}; // namespace hcomm

#endif // HCOMM_CCU_INSTANCE_MGR_H