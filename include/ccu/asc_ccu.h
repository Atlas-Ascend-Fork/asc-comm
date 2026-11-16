/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_CCU_H
#define ASC_COMM_CCU_H

// 算子开发唯一入口：聚合 CCU 设备侧 DSL 与 host 侧下发接口（含 ccu_host_launch.h）。
// 只列模块级头；ccu_primitives.h 已聚合 variable/address/event/buffer/local_addr/
// remote_addr/array/func/loop/control_flow_macro，ccu_print.h 额外提供 printf。
#include "ccu/ccu_api/ccu_types.h"
#include "ccu/ccu_api/ccu_primitives.h"
#include "ccu/ccu_api/ccu_print.h"
#include "ccu/experimental/ccu_resource_api.h"
#include "ccu/ccu_host_launch.h"

#endif // ASC_COMM_CCU_H
