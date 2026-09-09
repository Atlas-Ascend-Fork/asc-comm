/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#pragma once
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
using aclError = int;
using aclrtStream = void*;
struct aclrtExceptionInfo {};
constexpr int ACL_SUCCESS = 0;
constexpr int ACL_MEM_MALLOC_HUGE_FIRST = 0;
constexpr int ACL_MEMCPY_DEVICE_TO_HOST = 0;
inline std::set<uint64_t> failedAddresses;
inline int syncError = 0;
inline int aclrtMemcpy(void* dst, size_t capacity, const void* src, size_t size, int)
{
    if (!src || size > capacity || failedAddresses.count(reinterpret_cast<uint64_t>(src))) {
        return 123;
    }
    std::memcpy(dst, src, size);
    return 0;
}
inline int aclrtMalloc(void** p, size_t size, int)
{
    *p = std::malloc(size);
    return *p ? 0 : 1;
}
inline int aclrtFree(void* p)
{
    std::free(p);
    return 0;
}
inline int aclrtCreateStream(void** p)
{
    *p = reinterpret_cast<void*>(1);
    return 0;
}
inline int aclrtDestroyStream(void*) { return 0; }
inline int aclrtSynchronizeStream(void*) { return syncError; }
inline uint32_t aclrtGetTaskIdFromExceptionInfo(aclrtExceptionInfo*) { return 1; }
inline uint32_t aclrtGetStreamIdFromExceptionInfo(aclrtExceptionInfo*) { return 1; }
inline uint32_t aclrtGetDeviceIdFromExceptionInfo(aclrtExceptionInfo*) { return 0; }
