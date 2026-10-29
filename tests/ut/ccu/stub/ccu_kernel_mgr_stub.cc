/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/kernel/ccu_kernel_mgr.h"

namespace asc {

ccu_kernel_mgr& ccu_kernel_mgr::get_instance(int32_t)
{
    static ccu_kernel_mgr instance;
    return instance;
}

ccu_kernel_mgr::~ccu_kernel_mgr() = default;

ccu_kernel* ccu_kernel_mgr::get_kernel(ccu_kernel_handle) { return nullptr; }

} // namespace asc
