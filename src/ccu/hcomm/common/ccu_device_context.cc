/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/common/ccu_device_context.h"

#include "acl/acl_rt.h"

namespace asc {
namespace {
thread_local uint64_t current_ccu_control_ops[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT]{};
}

int32_t get_current_ccu_device_logic_id()
{
    int32_t device_logic_id = -1;
    if (aclrtGetDevice(&device_logic_id) != ACL_SUCCESS) {
        return -1;
    }
    return device_logic_id;
}

void set_current_ccu_control_ops(const uint64_t (&asc_custom)[HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT])
{
    for (uint32_t i = 0; i < HCOMM_CCU_ASC_CUSTOM_SLOT_COUNT; ++i) {
        current_ccu_control_ops[i] = asc_custom[i];
    }
}

const uint64_t* get_current_ccu_control_ops() { return current_ccu_control_ops; }

} // namespace asc
