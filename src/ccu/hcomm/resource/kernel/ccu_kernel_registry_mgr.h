/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_KERNEL_REGISTRY_MGR_H
#define CCU_KERNEL_REGISTRY_MGR_H

#include <mutex>
#include <memory>
#include <shared_mutex>
#include <unordered_map>

#include "ccu/hcomm/ccu_api_types.h"
#include "hcomm/resource/kernel/ccu_kernel_registry.h"

namespace hcomm {

class CcuKernelRegistryMgr {
public:
    static CcuKernelRegistryMgr& GetInstance(const int32_t deviceLogicId);

    CcuResult Init();
    CcuResult Deinit();

    // insHandle is hcomm-owned. asc-comm only uses it as an opaque key.
    CcuResult GetOrCreate(int32_t deviceLogicId, CcuInsHandle insHandle, CcuKernelRegistry*& registry);
    CcuKernelRegistry* Get(CcuInsHandle insHandle) const;

private:
    explicit CcuKernelRegistryMgr() = default;
    ~CcuKernelRegistryMgr();

    CcuKernelRegistryMgr(const CcuKernelRegistryMgr& that) = delete;
    CcuKernelRegistryMgr& operator=(const CcuKernelRegistryMgr& that) = delete;

private:
    bool initializedFlag_{false};
    int32_t devLogicId_{-1};
    mutable std::shared_timed_mutex insMapMutex_;
    std::unordered_map<CcuInsHandle, std::unique_ptr<CcuKernelRegistry>> insMap_{};
};

} // namespace hcomm

#endif // CCU_KERNEL_REGISTRY_MGR_H
