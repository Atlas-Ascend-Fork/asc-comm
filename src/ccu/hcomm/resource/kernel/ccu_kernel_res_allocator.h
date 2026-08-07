/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_KERNEL_RES_ALLOCATOR_H
#define CCU_KERNEL_RES_ALLOCATOR_H

#include "hcomm/resource/common/ccu_res_repo.h"
#include "ccu/hcomm/ccu_types.h"

namespace asccomm {

CcuResult PlanKernelResources(
    const CcuResRepository& available, const CcuResReq& request, CcuResRepository& remaining,
    CcuResRepository& allocated);

} // namespace asccomm

#endif // CCU_KERNEL_RES_ALLOCATOR_H
