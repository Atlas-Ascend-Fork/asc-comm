/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_DEVICE_CONTEXT_H
#define ASCCOMM_CCU_DEVICE_CONTEXT_H

#include <cstdint>

#include "hcomm/hcomm_ccu_control.h"

namespace asc {

constexpr uint32_t ccu_max_device_num = 64;

void set_current_ccu_device_logic_id(int32_t device_logic_id);
int32_t get_current_ccu_device_logic_id();
void set_current_ccu_control_ops(const HcommCcuControlOpsPod& control_ops);
const HcommCcuControlOpsPod& get_current_ccu_control_ops();

} // namespace asc

#endif // ASCCOMM_CCU_DEVICE_CONTEXT_H
