/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_HCOMM_PRIMITIVES_H
#define ASCCOMM_CCU_UT_HCOMM_PRIMITIVES_H

#include <stdint.h>

#include "hcomm/hcomm_types.h"

using s32 = int32_t;

typedef uint64_t ChannelHandle;
typedef uint64_t ThreadHandle;

typedef enum {
    HCOMM_REDUCE_SUM = 0,
    HCOMM_REDUCE_PROD = 1,
    HCOMM_REDUCE_MAX = 2,
    HCOMM_REDUCE_MIN = 3,
    HCOMM_REDUCE_RESERVED = 255,
} HcommReduceOp;

typedef enum {
    HCOMM_DATA_TYPE_INT8 = 0,
    HCOMM_DATA_TYPE_INT16 = 1,
    HCOMM_DATA_TYPE_INT32 = 2,
    HCOMM_DATA_TYPE_FP16 = 3,
    HCOMM_DATA_TYPE_FP32 = 4,
    HCOMM_DATA_TYPE_INT64 = 5,
    HCOMM_DATA_TYPE_UINT64 = 6,
    HCOMM_DATA_TYPE_UINT8 = 7,
    HCOMM_DATA_TYPE_UINT16 = 8,
    HCOMM_DATA_TYPE_UINT32 = 9,
    HCOMM_DATA_TYPE_FP64 = 10,
    HCOMM_DATA_TYPE_BFP16 = 11,
    HCOMM_DATA_TYPE_INT128 = 12,
    HCOMM_DATA_TYPE_HIF8 = 14,
    HCOMM_DATA_TYPE_FP8E4M3 = 15,
    HCOMM_DATA_TYPE_FP8E5M2 = 16,
    HCOMM_DATA_TYPE_FP8E8M0 = 17,
    HCOMM_DATA_TYPE_RESERVED = 255,
} HcommDataType;

#endif
