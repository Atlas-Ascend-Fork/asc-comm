/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file ccu_log.h
 * @brief 提供 asc-comm CCU 数据面的本地日志、返回值检查及 C 接口异常转换兼容层。
 */

#ifndef CCU_LOG_H
#define CCU_LOG_H

// asc-comm 数据面使用本地日志实现；仍共享该兼容头的 hcomm 控制面文件继续使用原有日志实现。
// 该条件分支仅隔离两个动态库的日志依赖，不改变既有日志宏的错误码和返回语义。
#ifdef ASCCOMM_CCU_LOCAL_LOGGING
#ifndef LOG_H
#define LOG_H
#endif
#else
#include "log.h"
#endif

#include <new>
#include <exception>
#include <sstream>
#include <stdexcept>

#include "base/dlog_pub.h"
#include "securec.h"
#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_utils.hpp"

enum {
    asccomm_ccu_log_module_id = 5,
};

#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif
#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif

#define ASCCOMM_CCU_LOG(level, format, ...)                                                                            \
    do {                                                                                                               \
        if (DlogRecord != nullptr &&                                                                                   \
            ((level) == DLOG_ERROR || CheckLogLevel(asccomm_ccu_log_module_id, (level)) == 1)) {                       \
            DlogRecord(asccomm_ccu_log_module_id, (level), "[%s:%d]" format, DLOG_FILE_NAME, __LINE__, ##__VA_ARGS__); \
        }                                                                                                              \
    } while (0)

#ifdef ASCCOMM_CCU_LOCAL_LOGGING
#define HCCL_ERROR(...) ASCCOMM_CCU_LOG(DLOG_ERROR, __VA_ARGS__)
#define HCCL_WARNING(...) ASCCOMM_CCU_LOG(DLOG_WARN, __VA_ARGS__)
#define HCCL_INFO(...) ASCCOMM_CCU_LOG(DLOG_INFO, __VA_ARGS__)
#define HCCL_DEBUG(...) ASCCOMM_CCU_LOG(DLOG_DEBUG, __VA_ARGS__)
#define HCCL_RUN_INFO(...) HCCL_INFO(__VA_ARGS__)
#define HCCL_RUN_WARNING(...) HCCL_WARNING(__VA_ARGS__)
#define HCCL_CONFIG_DEBUG(config, ...) HCCL_DEBUG(__VA_ARGS__)
#define HCCL_CONFIG_INFO(config, ...) HCCL_INFO(__VA_ARGS__)
#define HCCL_ENTRY_INFO(opEntry, ...) HCCL_INFO(__VA_ARGS__)
#endif

#ifndef HCOM_ERROR_CODE
#define HCOM_ERROR_CODE(error) static_cast<uint64_t>(error)
#endif

#ifndef HCCL_ERROR_CODE
#define HCCL_ERROR_CODE(error) static_cast<uint64_t>(error)
#endif

#ifndef CHK_PTR_NULL
#define CHK_PTR_NULL(ptr)                                          \
    do {                                                           \
        if (UNLIKELY((ptr) == nullptr)) {                          \
            HCCL_ERROR("[%s] ptr[%s] is nullptr", __func__, #ptr); \
            return HcclResult::HCCL_E_PTR;                         \
        }                                                          \
    } while (0)
#endif

#ifndef CHK_RET
#define CHK_RET(call)                                                    \
    do {                                                                 \
        const auto hcommRet = (call);                                    \
        if (UNLIKELY(hcommRet != 0)) {                                   \
            HCCL_ERROR("[%s] call failed, ret[%d]", __func__, hcommRet); \
            return hcommRet;                                             \
        }                                                                \
    } while (0)
#endif

#ifndef CHK_PRT_RET
#define CHK_PRT_RET(condition, logStatement, returnValue) \
    do {                                                  \
        if (UNLIKELY(condition)) {                        \
            logStatement;                                 \
            return (returnValue);                         \
        }                                                 \
    } while (0)
#endif

#ifndef CHK_SMART_PTR_NULL
#define CHK_SMART_PTR_NULL(ptr) CHK_PTR_NULL(ptr)
#endif

#ifndef CHK_SAFETY_FUNC_RET
#define CHK_SAFETY_FUNC_RET(call)                                                    \
    do {                                                                             \
        const int32_t safetyRet = (call);                                            \
        if (UNLIKELY(safetyRet != 0)) {                                              \
            HCCL_ERROR("[%s] safety function failed, ret[%d]", __func__, safetyRet); \
            return HCCL_E_INTERNAL;                                                  \
        }                                                                            \
    } while (0)
#endif

#ifndef EXCEPTION_CATCH
#define EXCEPTION_CATCH(statement, action) \
    do {                                   \
        try {                              \
            statement;                     \
        } catch (...) {                    \
            action;                        \
        }                                  \
    } while (0)
#endif

#define HCCL_TO_CCU_RET(ret) static_cast<CcuResult>(ret)

#define CCU_CHK_RES_UNAVAIL(ccuRet) (ccuRet == CCU_E_UNAVAIL)

/* 检查函数返回值, 并返回指定错误码 */
#define CCU_CHK_RET(call)                                                                                 \
    do {                                                                                                  \
        CcuResult ccuRet = HCCL_TO_CCU_RET(call);                                                         \
        if (UNLIKELY(ccuRet != CCU_SUCCESS)) {                                                            \
            if (ccuRet == CCU_E_DRV_BUSY) {                                                               \
                HCCL_WARNING("[%s]call trace: ccuRet -> %d", __func__, ccuRet);                           \
            } else if (ccuRet == CCU_E_UNAVAIL) {                                                         \
                HCCL_WARNING("[%s]call trace: ccuRet resources are unavailable -> %d", __func__, ccuRet); \
            } else {                                                                                      \
                HCCL_ERROR("[%s]call trace: ccuRet -> %d", __func__, ccuRet);                             \
            }                                                                                             \
            return ccuRet;                                                                                \
        }                                                                                                 \
    } while (0)

#define CCU_CHK_PTR_NULL(ptr)                                                        \
    do {                                                                             \
        if (UNLIKELY((ptr) == nullptr)) {                                            \
            HCCL_ERROR("[%s] ptr[%s] is nullptr, return CCU_E_PTR", __func__, #ptr); \
            return CCU_E_PTR;                                                        \
        }                                                                            \
    } while (0)

// 宏定义，用于包装 C 接口函数的异常处理
#define CCU_EXCEPTION_HANDLE_BEGIN try {
#define CCU_EXCEPTION_HANDLE_END_INFO(func_name)                             \
    }                                                                        \
    catch (const ::AscendC::ccu::detail::ccu_exception& ccu_exception)       \
    {                                                                        \
        HCCL_ERROR("[%s] exception: %s", (func_name), ccu_exception.what()); \
        return ccu_exception.code();                                         \
    }                                                                        \
    catch (const std::out_of_range& exception)                               \
    {                                                                        \
        HCCL_ERROR("[%s] out of range: %s", (func_name), exception.what());  \
        return CcuResult::CCU_E_NOT_FOUND;                                   \
    }                                                                        \
    catch (const std::runtime_error& exception)                              \
    {                                                                        \
        HCCL_ERROR("[%s] runtime error: %s", (func_name), exception.what()); \
        return CcuResult::CCU_E_RUNTIME;                                     \
    }                                                                        \
    catch (const std::logic_error& exception)                                \
    {                                                                        \
        HCCL_ERROR("[%s] logic error: %s", (func_name), exception.what());   \
        return CcuResult::CCU_E_INTERNAL;                                    \
    }                                                                        \
    catch (const std::exception& exception)                                  \
    {                                                                        \
        HCCL_ERROR("[%s] exception: %s", (func_name), exception.what());     \
        return CcuResult::CCU_E_INTERNAL;                                    \
    }                                                                        \
    catch (...)                                                              \
    {                                                                        \
        HCCL_ERROR("[%s] unknown exception", (func_name));                   \
        return CcuResult::CCU_E_INTERNAL;                                    \
    }

#define CCU_EXCEPTION_HANDLE_END CCU_EXCEPTION_HANDLE_END_INFO(__func__)

#endif // CCU_LOG_H
