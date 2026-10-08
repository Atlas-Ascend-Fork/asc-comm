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

#include "hcomm/ccu/hcomm_res_ccu_entity_defs.h"

namespace asc {

constexpr uint32_t ccu_max_device_num = 64;

/**
 * 当前线程设备逻辑 id：直接取 runtime 的 aclrtGetDevice。
 * 要求调用线程已设置设备（HCCL 数据面调用线程由上层保证）；未设置时返回 -1，调用方按错误处理。
 */
int32_t get_current_ccu_device_logic_id();
// 控制操作槽位数组的线程级快照（值按槽位含义强转对应 Fn 类型使用）
void set_current_ccu_control_ops(const uint64_t (&control_ops)[8]);
const uint64_t* get_current_ccu_control_ops();
} // namespace asc

#endif // ASCCOMM_CCU_DEVICE_CONTEXT_H
