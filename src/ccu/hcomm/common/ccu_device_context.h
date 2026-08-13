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

namespace hcomm {

constexpr uint32_t CCU_MAX_DEVICE_NUM = 64;

void SetCurrentCcuDeviceLogicId(int32_t deviceLogicId);
int32_t GetCurrentCcuDeviceLogicId();
void SetCurrentCcuControlOps(const HcommCcuControlOpsPod &controlOps);
const HcommCcuControlOpsPod &GetCurrentCcuControlOps();

} // namespace hcomm

#endif // ASCCOMM_CCU_DEVICE_CONTEXT_H
