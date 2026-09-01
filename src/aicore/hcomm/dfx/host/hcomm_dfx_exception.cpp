/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm_dfx_impl.h"

#include "acl/acl.h"
#include "acl/acl_rt.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace AscendC {
namespace HcommDfx {
namespace {
struct ExceptionRecord {
    std::atomic<bool> pending{false};
    std::atomic<bool> callbackBound{false};
    std::atomic<uint64_t> count{0};
    std::atomic<uint32_t> taskId{0};
    std::atomic<uint32_t> streamId{0};
    std::atomic<uint32_t> deviceId{0};
};

ExceptionRecord g_exception;

bool IsTraceEnabled()
{
    const char *enabled = std::getenv("HCOMM_DFX_DUMP_TRACE");
    return enabled != nullptr && enabled[0] != '\0' && enabled[0] != '0';
}

void RecordException(aclrtExceptionInfo *exceptionInfo)
{
    if (exceptionInfo == nullptr) {
        return;
    }

    uint32_t taskId = aclrtGetTaskIdFromExceptionInfo(exceptionInfo);
    uint32_t streamId = aclrtGetStreamIdFromExceptionInfo(exceptionInfo);
    uint32_t deviceId = aclrtGetDeviceIdFromExceptionInfo(exceptionInfo);

    g_exception.taskId.store(taskId, std::memory_order_relaxed);
    g_exception.streamId.store(streamId, std::memory_order_relaxed);
    g_exception.deviceId.store(deviceId, std::memory_order_relaxed);
    uint64_t count = g_exception.count.fetch_add(1U) + 1U;
    g_exception.pending.store(true, std::memory_order_release);
    std::printf("[HcommDfxDump] exception callback entered count=%llu task=%u stream=%u device=%u\n",
        static_cast<unsigned long long>(count), taskId, streamId, deviceId);
    std::fflush(stdout);
}
} // namespace

void Trace(const char *format, ...)
{
    if (!IsTraceEnabled()) {
        return;
    }
    std::fprintf(stderr, "[HcommDfxDump][TRACE] ");
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);
}

void ExceptionCallback(aclrtExceptionInfo *exceptionInfo, void *userData)
{
    (void)userData;
    RecordException(exceptionInfo);
}

bool HasPendingException()
{
    return g_exception.pending.load(std::memory_order_acquire);
}

bool IsExceptionCallbackBound()
{
    return g_exception.callbackBound.load(std::memory_order_acquire);
}

void MarkExceptionCallbackBound()
{
    g_exception.callbackBound.store(true, std::memory_order_release);
}

void ClearPendingException()
{
    g_exception.pending.store(false, std::memory_order_release);
    g_exception.count.store(0U, std::memory_order_release);
}

} // namespace HcommDfx
} // namespace AscendC
