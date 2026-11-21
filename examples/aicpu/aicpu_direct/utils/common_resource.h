/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef AICPU_COMMON_RESOURCE_H
#define AICPU_COMMON_RESOURCE_H

#include <vector>

#include <hccl/hccl_types.h>

#ifndef AICPU_COMPILE
#include <hccl/hccl_res.h>
#include <hcomm_primitives.h>
#include <acl/acl_rt.h>
#endif

#ifdef AICPU_COMPILE
#include <asccomm/asccomm_primitives.h>
#endif

#include "binary_stream.h"

using aicpu_common::BinaryStream;

// 设备类型
enum DeviceType {
    DEVICE_TYPE_A2 = 0,
    DEVICE_TYPE_A3 = 1,
    DEVICE_TYPE_A5 = 2,
};

constexpr uint32_t COMM_INDENTIFIER_MAX_LENGTH = 128;
constexpr uint32_t OP_NAME_LENGTH = 32;
constexpr uint32_t TAG_LENGTH = OP_NAME_LENGTH + COMM_INDENTIFIER_MAX_LENGTH;
constexpr uint32_t INVALID_VALUE_RANKID = 0xFFFFFFFF;
constexpr uint32_t AICPU_CONTROL_NOTIFY_NUM = 2;
#endif // AICPU_COMMON_RESOURCE_H
