/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_DEVICE_CAPACITY_H
#define HCCL_DEVICE_CAPACITY_H

#include "hccl/base.h"

namespace hccl {
// NOTE: SDMA_SEND_MAX_SIZE 被 AicpuTsThread 依赖。
// 节点内单个SDMA任务发送数据支持的最大数据量
const u64 SDMA_SEND_MAX_SIZE = 0x100000000;
}  // namespace hccl

#endif  // HCCL_DEVICE_CAPACITY_H
