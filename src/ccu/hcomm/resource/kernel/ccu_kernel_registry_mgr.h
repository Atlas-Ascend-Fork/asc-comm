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

namespace asc {

class ccu_kernel_registry_mgr {
public:
    static ccu_kernel_registry_mgr& get_instance(const int32_t device_logic_id);

    CcuResult init();
    CcuResult deinit();

    // insHandle is hcomm-owned. asc-comm only uses it as an opaque key.
    CcuResult get_or_create(int32_t device_logic_id, CcuInsHandle ins_handle, ccu_kernel_registry*& registry);
    ccu_kernel_registry* get(CcuInsHandle ins_handle) const;

private:
    explicit ccu_kernel_registry_mgr() = default;
    ~ccu_kernel_registry_mgr();

    ccu_kernel_registry_mgr(const ccu_kernel_registry_mgr& that) = delete;
    ccu_kernel_registry_mgr& operator=(const ccu_kernel_registry_mgr& that) = delete;

private:
    bool initialized_flag_{false};
    int32_t dev_logic_id_{-1};
    mutable std::shared_timed_mutex ins_map_mutex_;
    std::unordered_map<CcuInsHandle, std::unique_ptr<ccu_kernel_registry>> ins_map_{};
};

} // namespace asc

#endif // CCU_KERNEL_REGISTRY_MGR_H
