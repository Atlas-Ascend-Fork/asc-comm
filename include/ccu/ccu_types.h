/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_TYPES_H
#define CCU_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CCU_SUCCESS = 0,
    CCU_E_PARA = 1,
    CCU_E_PTR = 2,
    CCU_E_NOT_INITIALIZED = 3,
    CCU_E_INTERNAL = 4,
    CCU_E_NOT_SUPPORT = 5,
    CCU_E_NOT_FOUND = 6,
    CCU_E_UNAVAIL = 7,
    CCU_E_RUNTIME = 15,
    CCU_E_DRV_START = 4096,
    CCU_E_DRV_INIT_FAILED = 4097,
    CCU_E_DRV_BUSY = 4098,
    CCU_E_DRV_END = 4224,
    CCU_E_RESERVED = 9216
} CcuResult;

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

typedef uint64_t CcuInsHandle;

typedef enum {
    CCU_DEFAULT = 0,
    CCU_SCHED = 1,
    CCU_MS = 2,
    CCU_UNUSED = 254,
    CCU_RESERVED = 255
} CcuInstanceType;

typedef struct {
    uint32_t dieId;
    CcuInstanceType insType;
} CcuResDesc;

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

static_assert(sizeof(CcuResult) == sizeof(uint32_t), "CcuResult ABI changed");
static_assert(sizeof(CcuInsHandle) == sizeof(uint64_t), "CcuInsHandle ABI changed");
static_assert(sizeof(CcuKernelHandle) == sizeof(uint64_t), "CcuKernelHandle ABI changed");
static_assert(sizeof(CcuVariableHandle) == sizeof(uint64_t), "CcuVariableHandle ABI changed");
static_assert(sizeof(CcuAddressHandle) == sizeof(uint64_t), "CcuAddressHandle ABI changed");
static_assert(sizeof(CcuEventHandle) == sizeof(uint64_t), "CcuEventHandle ABI changed");
static_assert(sizeof(CcuBufferHandle) == sizeof(uint64_t), "CcuBufferHandle ABI changed");
static_assert(sizeof(CcuLocalAddrHandle) == sizeof(uint64_t), "CcuLocalAddrHandle ABI changed");
static_assert(sizeof(CcuRemoteAddrHandle) == sizeof(uint64_t), "CcuRemoteAddrHandle ABI changed");
static_assert(sizeof(CcuInstanceType) == sizeof(uint32_t), "CcuInstanceType ABI changed");
static_assert(sizeof(CcuResDesc) == 8, "CcuResDesc ABI changed");
static_assert(alignof(CcuResDesc) == alignof(uint32_t), "CcuResDesc alignment changed");
static_assert(offsetof(CcuResDesc, dieId) == 0 && offsetof(CcuResDesc, insType) == 4,
    "CcuResDesc layout changed");
static_assert(CCU_SUCCESS == 0 && CCU_E_NOT_INITIALIZED == 3 && CCU_E_NOT_SUPPORT == 5,
    "CcuResult values changed");
static_assert(CCU_DEFAULT == 0 && CCU_SCHED == 1 && CCU_MS == 2 && CCU_UNUSED == 254 && CCU_RESERVED == 255,
    "CcuInstanceType values changed");
#endif

#endif
