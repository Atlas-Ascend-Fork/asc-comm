/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * 本地化兼容胶水：asc-comm CCU 数据面自持日志/异常/字符串/类型基础设施。
 * 不再 include hcomm 的 log.h/hccl_exception.h；全部指向 asc-comm 自有实现。
 * force-include 注入本头后，所有 .cc 可直接使用 HCCL_* 宏、hcomm::THROW、
 * hcomm::StringFormat、hcomm::DataType/ReduceOp 等。
 */
#ifndef ASCCOMM_CCU_DATAPLANE_COMPAT_H
#define ASCCOMM_CCU_DATAPLANE_COMPAT_H

#include "asccomm_log.h"
#include "asccomm_string_util.h"
#include "asccomm_exception.h"
#include "asccomm_types.h"
#include "hcomm/common/ccu_device_context.h"

using std::string;

#endif // ASCCOMM_CCU_DATAPLANE_COMPAT_H
