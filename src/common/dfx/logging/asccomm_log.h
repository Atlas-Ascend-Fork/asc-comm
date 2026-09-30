/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_POD_LOG_H
#define ASCCOMM_POD_LOG_H

#include <exception>
#include <sys/syscall.h>
#include <unistd.h>
#include <dlog_pub.h>
#include "asccomm_result.h"

#ifndef LIKELY
#define LIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 1)))
#define UNLIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 0)))
#endif

void DlogRecord(int32_t moduleId, int32_t level, const char* fmt, ...) __attribute__((weak));
bool HcclCheckLogLevel(int logType, int moduleId = HCCL);
bool IsErrorToWarn();

#define ASCCOMM_LOG_PRINT(moduleId, logType, format, ...) \
    DlogRecord(moduleId, logType, "[%s:%d] [%ld]" format, __FILE__, __LINE__, syscall(SYS_gettid), ##__VA_ARGS__)

#define ASCCOMM_INFO(format, ...)                                      \
    do {                                                               \
        if (UNLIKELY(HcclCheckLogLevel(DLOG_INFO))) {                  \
            ASCCOMM_LOG_PRINT(HCCL, DLOG_INFO, format, ##__VA_ARGS__); \
        }                                                              \
    } while (0)

#define ASCCOMM_WARNING(format, ...)                                   \
    do {                                                               \
        if (UNLIKELY(HcclCheckLogLevel(DLOG_WARN))) {                  \
            ASCCOMM_LOG_PRINT(HCCL, DLOG_WARN, format, ##__VA_ARGS__); \
        }                                                              \
    } while (0)

#define ASCCOMM_ERROR(format, ...)                                                                      \
    do {                                                                                                \
        if (LIKELY(HcclCheckLogLevel(DLOG_ERROR))) {                                                    \
            const bool errorToWarn = IsErrorToWarn();                                                   \
            const int32_t module =                                                                      \
                errorToWarn ? (static_cast<int32_t>(HCCL) | static_cast<int32_t>(RUN_LOG_MASK)) : HCCL; \
            const int32_t level = errorToWarn ? DLOG_WARN : DLOG_ERROR;                                 \
            ASCCOMM_LOG_PRINT(module, level, format, ##__VA_ARGS__);                                    \
        }                                                                                               \
    } while (0)

#define CHK_PTR_NULL(ptr)                                               \
    do {                                                                \
        if (UNLIKELY((ptr) == nullptr)) {                               \
            ASCCOMM_ERROR("[%s] ptr [%s] is nullptr.", __func__, #ptr); \
            return ASCCOMM_E_PTR;                                       \
        }                                                               \
    } while (0)

#define CHK_PRT_RET(result, exeLog, retCode) \
    do {                                     \
        if (UNLIKELY(result)) {              \
            exeLog;                          \
            return retCode;                  \
        }                                    \
    } while (0)

#define CHK_RET(call)                                                          \
    do {                                                                       \
        AsccommResult asccommRet = (call);                                     \
        if (UNLIKELY(asccommRet != ASCCOMM_SUCCESS)) {                         \
            ASCCOMM_ERROR("[%s] call failed, ret[%d].", __func__, asccommRet); \
            return asccommRet;                                                 \
        }                                                                      \
    } while (0)

#define EXCEPTION_CATCH(expression, retExpression)                                  \
    do {                                                                            \
        try {                                                                       \
            expression;                                                             \
        } catch (std::exception & exception) {                                      \
            ASCCOMM_ERROR("[%s] exception caught: %s", __func__, exception.what()); \
            retExpression;                                                          \
        }                                                                           \
    } while (0)

#endif // ASCCOMM_POD_LOG_H
