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
#include <string>

#include "acl/acl.h"
#include "acl/acl_rt.h"
#include "impl/host_log.h"

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

inline void LogError(const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    const int ret = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (ret > 0) {
        TILING_LOG_ERROR("[HcommDfxDump] %s", message);
    }
}

inline void LogInfo(const char *format, ...)
{
    char message[512];
    va_list args;
    va_start(args, format);
    const int ret = std::vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    if (ret > 0) {
        TILING_LOG_INFO("[HcommDfxDump] %s", message);
    }
}

inline void LogDumpText(const std::string &text)
{
    size_t pos = 0U;
    while (pos < text.size()) {
        const size_t lineEnd = text.find('\n', pos);
        const size_t lineLen = (lineEnd == std::string::npos) ? text.size() - pos : lineEnd - pos;
        if (lineLen != 0U) {
            TILING_LOG_INFO("%.*s", static_cast<int>(lineLen), text.data() + pos);
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        pos = lineEnd + 1U;
    }
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

class HcommDfxException {
public:
    static void Callback(aclrtExceptionInfo *exceptionInfo, void *userData)
    {
        ExceptionCallback(exceptionInfo, userData);
    }
    static bool HasPending() { return HasPendingException(); }
    static bool IsBound() { return IsExceptionCallbackBound(); }
    static void MarkBound() { MarkExceptionCallbackBound(); }
    static void ClearPending() { ClearPendingException(); }
};

} // namespace HcommDfx
} // namespace AscendC

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_HOST_HCOMM_DFX_EXCEPTION_H
