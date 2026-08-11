/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef __AICPU_HCCL_SQCQ_H__
#define __AICPU_HCCL_SQCQ_H__

#include "hccl/base.h"
#include "driver/ascend_hal_define.h"

// NOTE: QuerySqStatusByType 被 AicpuTsThread 依赖。
extern HcclResult QuerySqStatusByType(
    uint32_t devId, uint32_t sqId, drvSqCqPropType_t type, uint32_t &outVal);

#endif  // __AICPU_HCCL_SQCQ_H__
