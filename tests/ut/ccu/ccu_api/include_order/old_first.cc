/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <hcomm/ccu/ccu_types.h>
#include <ccu/ccu_api/ccu_types.h>

static_assert(static_cast<int>(ccu_result::CCU_SUCCESS) == 0, "ccu_result::CCU_SUCCESS value changed");
static_assert(static_cast<int>(ccu_result::CCU_E_RESERVED) == 9216, "ccu_result::CCU_E_RESERVED value changed");

int ccu_types_old_first_check()
{
    ccu_result result = ccu_result::CCU_SUCCESS;
    return static_cast<int>(result);
}
