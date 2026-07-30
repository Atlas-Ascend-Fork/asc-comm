/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * Licensed under the CANN Open Software License Agreement Version 2.0.
 */
#ifndef ASC_COMM_EXAMPLE_LOG_H
#define ASC_COMM_EXAMPLE_LOG_H

#include <cstdio>

#include <ccu/ccu_types.h>
#include <hccl/hccl_types.h>

enum LogLevel {
    LOG_LEVEL_INFO = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_ERROR = 3,
    LOG_LEVEL_NONE = 4
};

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_INFO
#endif

#ifndef LIKELY
#define LIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 1)))
#define UNLIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 0)))
#endif

#define HCCL_INFO(format, ...)                                                  \
    do {                                                                        \
        if (LOG_LEVEL <= LOG_LEVEL_INFO) {                                      \
            std::printf("[INFO][%s][%s:%d]" format "\n",                        \
                __func__, __FILE__, __LINE__, ##__VA_ARGS__);                   \
        }                                                                       \
    } while (0)

#define HCCL_WARNING(format, ...)                                               \
    do {                                                                        \
        if (LOG_LEVEL <= LOG_LEVEL_WARNING) {                                   \
            std::printf("[WARN][%s][%s:%d]" format "\n",                        \
                __func__, __FILE__, __LINE__, ##__VA_ARGS__);                   \
        }                                                                       \
    } while (0)

#define HCCL_ERROR(format, ...)                                                 \
    do {                                                                        \
        if (LOG_LEVEL <= LOG_LEVEL_ERROR) {                                     \
            std::printf("[ERROR][%s][%s:%d]" format "\n",                       \
                __func__, __FILE__, __LINE__, ##__VA_ARGS__);                   \
        }                                                                       \
    } while (0)

#define CHK_PTR_NULL(ptr)                                                       \
    do {                                                                        \
        if (UNLIKELY((ptr) == nullptr)) {                                       \
            HCCL_ERROR("[%s] ptr [%s] is nullptr, return HCCL_E_PTR",           \
                __func__, #ptr);                                                \
            return HCCL_E_PTR;                                                  \
        }                                                                       \
    } while (0)

#define CHK_PRT_RET(result, logStatement, retCode)                              \
    do {                                                                        \
        if (UNLIKELY(result)) {                                                 \
            logStatement;                                                       \
            return retCode;                                                     \
        }                                                                       \
    } while (0)

#define CHK_RET(call)                                                           \
    do {                                                                        \
        const int32_t hcclRet = call;                                           \
        if (UNLIKELY(hcclRet != HCCL_SUCCESS)) {                               \
            if (hcclRet == HCCL_E_AGAIN) {                                     \
                HCCL_WARNING("[%s] call trace: hcclRet -> %d",                  \
                    __func__, hcclRet);                                         \
            } else {                                                            \
                HCCL_ERROR("[%s] call trace: hcclRet -> %d",                    \
                    __func__, hcclRet);                                         \
            }                                                                   \
            return static_cast<HcclResult>(hcclRet);                            \
        }                                                                       \
    } while (0)

#define CCU_CHK_RET(call)                                                       \
    do {                                                                        \
        const CcuResult ccuRet = static_cast<CcuResult>(call);                  \
        if (UNLIKELY(ccuRet != CCU_SUCCESS)) {                                  \
            HCCL_ERROR("[%s] call trace: ccuRet -> %d", __func__,               \
                static_cast<int32_t>(ccuRet));                                  \
            return ccuRet;                                                      \
        }                                                                       \
    } while (0)

#endif  // ASC_COMM_EXAMPLE_LOG_H
