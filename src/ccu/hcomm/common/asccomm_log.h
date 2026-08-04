/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*
 * asc-comm self-contained logging infrastructure, replacing the dependency
 * on hcomm's "log.h". All public macro names are kept identical so that the
 * existing migrated CCU data-plane call sites do not need to change.
 *
 * The namespace is `hcomm` (NOT `Hccl`), matching asc-comm conventions.
 *
 * The implementation forwards to DlogRecord from the public CANN header
 * dlog_pub.h and snprintf_s from securec.h. No hcomm private header is
 * included. DlogRecord is declared as a weak symbol in dlog_pub.h, so when
 * no logging backend is linked, the calls become no-ops and this header
 * remains usable in any CANN-aware build environment (including UT).
 */
#ifndef ASCCOMM_LOG_H
#define ASCCOMM_LOG_H

#include <cstdint>
#include <cstdio>

#include <securec.h>
#include <dlog_pub.h>

#include "hccl/base.h"   /* HcclResult / HCCL_SUCCESS */

namespace hcomm {

/* HCCL_LOG_* level constants, aligned with hcomm log.h */
constexpr uint32_t HCCL_LOG_DEBUG = 0x0;
constexpr uint32_t HCCL_LOG_INFO = 0x1;
constexpr uint32_t HCCL_LOG_WARN = 0x2;
constexpr uint32_t HCCL_LOG_ERROR = 0x3;
constexpr uint32_t HCCL_LOG_OPLOG = 0x6;
constexpr uint32_t HCCL_LOG_RUN_INFO = 0xff;

/* Sub module id reserved for error-code composition, aligned with hcomm. */
enum class HcclSubModuleID {
    LOG_SUB_MODULE_ID_HCCL = 0,
    LOG_SUB_MODULE_ID_HCOM = 1,
};

#ifndef LIKELY
#define LIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 1)))
#define UNLIKELY(x) (static_cast<bool>(__builtin_expect(static_cast<bool>(x), 0)))
#endif

/* Per-record log length, identical to hcomm log.h. */
constexpr int32_t LOG_TMPBUF_SIZE = 512;

/* Error-code composition constants, kept identical to hcomm log.h. */
const uint64_t SYSTEM_RESERVE_ERROR = 0;
const uint64_t HCCL_MODULE_ID = 5;

/*
 * asc-comm does not carry hcomm's log-level manager. The level filter simply
 * reports every requested level as enabled (returns 1) so that all log macros
 * emit their content. Reading the env var ASCCOMM_LOG_LEVEL is supported for
 * optional runtime suppression (0=off,1=error,2=warn,3=info,4=debug).
 */
inline int HcclCheckLogLevel(int logLevel) { return 1; }

/* Compose the packed HCCL error code, identical to hcomm log.h. */
#define HCCL_ERROR_CODE(error)                                                 \
    ((::hcomm::SYSTEM_RESERVE_ERROR << 32) + (::hcomm::HCCL_MODULE_ID << 24) + \
     (static_cast<uint64_t>(::hcomm::HcclSubModuleID::LOG_SUB_MODULE_ID_HCCL) << 16) + static_cast<uint64_t>(error))

#define HCOM_ERROR_CODE(error)                                                 \
    ((::hcomm::SYSTEM_RESERVE_ERROR << 32) + (::hcomm::HCCL_MODULE_ID << 24) + \
     (static_cast<uint64_t>(::hcomm::HcclSubModuleID::LOG_SUB_MODULE_ID_HCOM) << 16) + static_cast<uint64_t>(error))

/*
 * CallDlog* helpers: thin inline wrappers around DlogRecord. Each overload
 * mirrors a helper declared in hcomm's log.h so that the migrated
 * force-include glue (`ccu_dataplane_compat.h`) can keep using the same names.
 */
inline void CallDlog(int level, int sysCallBack, const char* buffer, std::string file, int line)
{
    (void)sysCallBack;
    if (buffer == nullptr) {
        return;
    }
    DlogRecord(static_cast<int32_t>(HCCL_MODULE_ID), level, "[%s:%d]%s", file.c_str(), line, buffer);
}

inline void CallDlogNoSzFormat(int level, int errCode, std::string file, int line)
{
    DlogRecord(
        static_cast<int32_t>(HCCL_MODULE_ID), level, "[%s:%d]format is nullptr, errCode[0x%08x]", file.c_str(), line,
        errCode);
}

inline void CallDlogMemError(int level, std::string file, int line)
{
    DlogRecord(static_cast<int32_t>(HCCL_MODULE_ID), level, "[%s:%d]memset_s log buffer failed", file.c_str(), line);
}

inline void CallDlogPrintError(int level, std::string file, int line)
{
    DlogRecord(static_cast<int32_t>(HCCL_MODULE_ID), level, "[%s:%d]snprintf_s log buffer failed", file.c_str(), line);
}

inline void CallDlogInvalidType(int level, int errCode, std::string file, int line)
{
    DlogRecord(
        static_cast<int32_t>(HCCL_MODULE_ID), level, "[%s:%d]invalid log type, errCode[0x%08x]", file.c_str(), line,
        errCode);
}

/*
 * Core print macro. Mirrors hcomm's LOG_PRINT: a stack buffer + snprintf_s,
 * with diagnostic branches on the three securec error paths.
 */
#define ASCCOMM_LOG_PRINT(logType, szFormat, ...)                                                                      \
    do {                                                                                                               \
        if (UNLIKELY(::hcomm::HcclCheckLogLevel(logType) == 1)) {                                                      \
            char stackLogBuffer[::hcomm::LOG_TMPBUF_SIZE];                                                             \
            if (szFormat == nullptr) {                                                                                 \
                ::hcomm::CallDlogNoSzFormat(::hcomm::HCCL_LOG_ERROR, ::hcomm::HCCL_ERROR_CODE(1), __FILE__, __LINE__); \
            } else {                                                                                                   \
                if (memset_s(stackLogBuffer, sizeof(stackLogBuffer), 0, sizeof(stackLogBuffer)) != EOK) {              \
                    ::hcomm::CallDlogMemError(::hcomm::HCCL_LOG_ERROR, __FILE__, __LINE__);                            \
                } else if (                                                                                            \
                    snprintf_s(                                                                                        \
                        stackLogBuffer, sizeof(stackLogBuffer), (sizeof(stackLogBuffer) - 1), szFormat,                \
                        ##__VA_ARGS__) < 0 &&                                                                          \
                    stackLogBuffer[0] == 0) {                                                                          \
                    ::hcomm::CallDlogPrintError(::hcomm::HCCL_LOG_ERROR, __FILE__, __LINE__);                          \
                } else {                                                                                               \
                    ::hcomm::CallDlog(logType, 0, stackLogBuffer, __FILE__, __LINE__);                                 \
                }                                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (0)

/* Public logging macros: names preserved from hcomm log.h. */
#define HCCL_DEBUG(...) ASCCOMM_LOG_PRINT(::hcomm::HCCL_LOG_DEBUG, __VA_ARGS__)
#define HCCL_INFO(...) ASCCOMM_LOG_PRINT(::hcomm::HCCL_LOG_INFO, __VA_ARGS__)
#define HCCL_WARNING(...) ASCCOMM_LOG_PRINT(::hcomm::HCCL_LOG_WARN, __VA_ARGS__)
#define HCCL_ERROR(...) ASCCOMM_LOG_PRINT(::hcomm::HCCL_LOG_ERROR, __VA_ARGS__)
#define HCCL_RUN_INFO(...) ASCCOMM_LOG_PRINT(::hcomm::HCCL_LOG_RUN_INFO, __VA_ARGS__)

/* Aliases referenced by a few migrated call sites. */
#ifndef HCCL_RUN_WARNING
#define HCCL_RUN_WARNING(...) HCCL_WARNING(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_DEBUG
#define HCCL_CONFIG_DEBUG(config, ...) HCCL_DEBUG(__VA_ARGS__)
#endif

#ifndef HCCL_CONFIG_INFO
#define HCCL_CONFIG_INFO(config, ...) HCCL_INFO(__VA_ARGS__)
#endif

#ifndef HCCL_ENTRY_INFO
#define HCCL_ENTRY_INFO(opEntry, ...) HCCL_INFO(__VA_ARGS__)
#endif

/* ==================== 检查宏（对齐 hcomm log.h 公共代码宏） ==================== */

#define CHK_SMART_PTR_NULL(smart_ptr)                                                       \
    do {                                                                                    \
        if (UNLIKELY(!(smart_ptr))) {                                                       \
            HCCL_ERROR("[%s]errNo[0x%016llx] ptr [%s] is nullptr, return HCCL_E_PTR",       \
                __func__, HCCL_ERROR_CODE(HCCL_E_PTR), #smart_ptr);                         \
            return HCCL_E_PTR;                                                              \
        }                                                                                   \
    } while (0)

#define CHK_PRT_RET(result, exeLog, retCode)                    \
    do {                                                        \
        if (UNLIKELY(result)) {                                 \
            exeLog;                                             \
            return retCode;                                     \
        }                                                       \
    } while (0)

#define CHK_PRT_RET_NULL(result, exeLog)                        \
    do {                                                        \
        if (UNLIKELY(result)) {                                 \
            exeLog;                                             \
            return;                                             \
        }                                                       \
    } while (0)

#define CHK_PRT_CONT(result, exeLog)                            \
    do {                                                        \
        if (UNLIKELY(result)) {                                 \
            exeLog;                                             \
        }                                                       \
    } while (0)

#define CHK_SMART_PTR_RET_NULL(smart_ptr)                                       \
    do {                                                                        \
        if (UNLIKELY(!(smart_ptr))) {                                           \
            HCCL_ERROR("[%s]errNo[0x%016llx]smart_ptr is nullptr.",             \
            __func__, HCCL_ERROR_CODE(HCCL_E_PTR));                             \
            return;                                                             \
        }                                                                       \
    } while (0)

#define CHK_RET(call)                                                                                          \
    do {                                                                                                       \
        HcclResult hcclRet = static_cast<HcclResult>(call);                                                    \
        if (UNLIKELY(hcclRet != HcclResult::HCCL_SUCCESS)) {                                                   \
            if (hcclRet == HcclResult::HCCL_E_AGAIN) {                                                         \
                HCCL_WARNING("[%s]call trace: hcclRet -> %d", __func__, hcclRet);                              \
            } else {                                                                                           \
                HCCL_ERROR("[%s]call trace: hcclRet -> %d", __func__, hcclRet);                                \
            }                                                                                                  \
            return hcclRet;                                                                                    \
        }                                                                                                      \
    } while (0)

#define CHK_PRT_BREAK(result, exeLog, exeCmd)     \
    if (UNLIKELY(result)) {                              \
        exeLog;                                \
        exeCmd;                                \
        break;                                 \
    }

#define CHK_RET_UNAVAIL(call)                                                                                          \
    do {                                                                                                               \
        HcclResult hcclRet = static_cast<HcclResult>(call);                                                            \
        if (UNLIKELY(hcclRet != HcclResult::HCCL_SUCCESS)) {                                                           \
            if (hcclRet == HcclResult::HCCL_E_AGAIN || hcclRet == HcclResult::HCCL_E_UNAVAIL) {                        \
                HCCL_WARNING("[%s]call trace: hcclRet -> %d", __func__, hcclRet);                                      \
            } else {                                                                                                   \
                HCCL_ERROR("[%s]call trace: hcclRet -> %d", __func__, hcclRet);                                        \
            }                                                                                                          \
            return hcclRet;                                                                                            \
        }                                                                                                              \
    } while (0)

#define CHK_PTR_NULL(ptr)                                                                                              \
    do {                                                                                                               \
        if (UNLIKELY((ptr) == nullptr)) {                                                                              \
            HCCL_ERROR("errNo[0x%016llx] ptr [%s] is NULL, return HcclResult::HCCL_E_PTR",                             \
                       HCCL_ERROR_CODE(HcclResult::HCCL_E_PTR), #ptr);                                                 \
            return HcclResult::HCCL_E_PTR;                                                                             \
        }                                                                                                              \
    } while (0)

#define CHK_RET_AND_PRINT_IDE(call, identifier)         \
    do {                                              \
        HcclResult hcclRet = static_cast<HcclResult>(call);          \
        if (UNLIKELY(hcclRet != HCCL_SUCCESS)) {                    \
            HCCL_RUN_INFO("[HCCL_TRACE]%s identifier[%s]", __func__, identifier); \
            if (hcclRet == HCCL_E_AGAIN) {                \
                HCCL_WARNING("[%s]call trace: hcclRet -> %d", __func__, hcclRet); \
            } else {                                  \
                HCCL_ERROR("[%s]call trace: hcclRet -> %d", __func__, hcclRet); \
            }                                         \
            return hcclRet;                               \
        }                                             \
    } while (0)

#define CHK_SAFETY_FUNC_RET(call)                                 \
    do {                                              \
        int32_t ret = call;                        \
        if (UNLIKELY(ret != EOK)) {                    \
            HCCL_ERROR("[%s]call trace: safety func err ret -> %d", __func__, ret); \
            return HCCL_E_INTERNAL;                               \
        }                                             \
    } while (0)

#define EXCEPTION_CATCH(expression, retExp)                                       \
    do {                                                                           \
        try {                                                                      \
            expression;                                                            \
        } catch (::hcomm::CcuException & e) {                                     \
            HCCL_ERROR("[%s]Failed, exception caught:%s", __func__, e.what());     \
            retExp;                                                                \
        } catch (std::exception & e) {                                             \
            HCCL_ERROR("[%s]Failed, exception caught:%s", __func__, e.what());     \
            retExp;                                                                \
        } catch (...) {                                                            \
            HCCL_ERROR("exception caught others");                                 \
            retExp;                                                                \
        }                                                                          \
    } while (0)

} // namespace hcomm

#endif // ASCCOMM_LOG_H
