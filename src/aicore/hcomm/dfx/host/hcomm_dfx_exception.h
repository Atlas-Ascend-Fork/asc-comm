/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_EXCEPTION_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_EXCEPTION_H

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include "acl/acl.h"
#include "acl/acl_rt.h"

namespace AscendC {
namespace HcommDfx {

struct ExceptionRecord {
    std::atomic<bool> pending{false};
    std::atomic<bool> callbackBound{false};
    std::atomic<uint64_t> count{0};
    std::atomic<uint32_t> taskId{0};
    std::atomic<uint32_t> streamId{0};
    std::atomic<uint32_t> deviceId{0};
};

inline ExceptionRecord g_exception{};

inline bool IsTraceEnabled()
{
    // Read once at first use: avoids calling the non-thread-safe getenv() on every Trace().
    static const bool enabled = [] {
        const char *value = std::getenv("HCOMM_DFX_DUMP_TRACE");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return enabled;
}

inline void Trace(const char *format, ...)
{
    if (!IsTraceEnabled()) {
        return;
    }
    std::printf("[HcommDfxDump][TRACE] ");
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

inline void RecordException(aclrtExceptionInfo *exceptionInfo)
{
    if (exceptionInfo == nullptr) {
        return;
    }

    g_exception.taskId.store(aclrtGetTaskIdFromExceptionInfo(exceptionInfo), std::memory_order_relaxed);
    g_exception.streamId.store(aclrtGetStreamIdFromExceptionInfo(exceptionInfo), std::memory_order_relaxed);
    g_exception.deviceId.store(aclrtGetDeviceIdFromExceptionInfo(exceptionInfo), std::memory_order_relaxed);
    g_exception.count.fetch_add(1U, std::memory_order_relaxed);
    g_exception.pending.store(true, std::memory_order_release);
    Trace("record exception task=%u stream=%u device=%u count=%llu",
          g_exception.taskId.load(std::memory_order_relaxed),
          g_exception.streamId.load(std::memory_order_relaxed),
          g_exception.deviceId.load(std::memory_order_relaxed),
          static_cast<unsigned long long>(g_exception.count.load(std::memory_order_relaxed)));
}

inline void ExceptionCallback(aclrtExceptionInfo *exceptionInfo, void *userData)
{
    (void)userData;
    RecordException(exceptionInfo);
}

inline bool HasPendingException()
{
    return g_exception.pending.load(std::memory_order_acquire);
}

inline bool IsExceptionCallbackBound()
{
    return g_exception.callbackBound.load(std::memory_order_acquire);
}

inline void MarkExceptionCallbackBound()
{
    g_exception.callbackBound.store(true, std::memory_order_release);
}

inline void ClearPendingException()
{
    g_exception.pending.store(false, std::memory_order_release);
    g_exception.count.store(0U, std::memory_order_release);
}

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_EXCEPTION_H
