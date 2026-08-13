/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_API_TYPES_H
#define CCU_API_TYPES_H

#include <stdint.h>

#include "hcomm/hcomm_ccu_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCU_CONDITION_EQ = 0,
    CCU_CONDITION_NE = 1,
} CcuConditionType;

typedef uint64_t CcuLoop;
typedef uint64_t CcuLoopGroup;
typedef uint64_t CcuLoopExecutors;

typedef struct {
    uint64_t addrOffset;
    uint64_t iterNum;
} CcuLoopConfig;

typedef struct {
    uint32_t cloneNum;
    uint32_t cloneLoopOffset;
    uint32_t addrOffset;
    uint32_t ccuBufferOffset;
    uint32_t eventOffset;
} CcuLoopGroupConfig;

typedef uint64_t CcuKernelHandle;
typedef uint64_t CcuVariableHandle;
typedef uint64_t CcuAddressHandle;
typedef uint64_t CcuEventHandle;
typedef uint64_t CcuBufferHandle;
typedef uint64_t CcuLocalAddrHandle;
typedef uint64_t CcuRemoteAddrHandle;
typedef void *CcuKernelArg;

#ifdef __cplusplus
}
#endif

#endif // CCU_API_TYPES_H
