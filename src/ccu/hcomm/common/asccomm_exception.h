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
 * asc-comm self-contained exception infrastructure, replacing dependencies on
 * hcomm's "exception_util.h", "hccl_exception.h", "null_ptr_exception.h",
 * "ccu_api_exception.h" and the InternalException/NotSupportException entries
 * of "const_val.h".
 *
 * - Base class is `CcuException` (formerly `HcclException`).
 * - Subclasses: CcuApiException, InternalException, NotSupportException,
 *   NullPtrException.
 * - THROW/CHECK_NULLPTR templates and all CHK_*_THROW / TRY_CATCH_* macros are
 *   preserved so migrated call sites compile unchanged.
 * - ErrorCode mapping translates each exception class to the corresponding
 *   `CcuResult` (defined in ccu/hcomm/ccu_types.h).
 *
 * Namespace is `hcomm`.
 */
#ifndef ASCCOMM_EXCEPTION_H
#define ASCCOMM_EXCEPTION_H

#include <cstdint>
#include <exception>
#include <execinfo.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include "asccomm_log.h"
#include "asccomm_string_util.h"

/* Pull in CcuResult values (CCU_SUCCESS / CCU_E_PARA / CCU_E_PTR / ...). */
#include "ccu/hcomm/ccu_types.h"

namespace hcomm {

/*
 * Base exception class. Carries a user message, optional backtrace (captured
 * via execinfo.h backtrace/backtrace_symbols) and an integral error code that
 * maps to CcuResult. `what()` returns the composed error message.
 */
class CcuException : public std::exception {
public:
    explicit CcuException(const std::string& userDefinedMsg, int32_t errorCode)
        : userDefinedMsg_(userDefinedMsg), errorCode_(errorCode)
    {
        StoreBackTrace();
        errorMsg_ = userDefinedMsg_;
    }

    const char* what() const noexcept override { return errorMsg_.c_str(); }

    int32_t GetErrorCode() const { return errorCode_; }

    std::vector<std::string> GetBackTraceStrings() const { return backtraceStrings_; }

protected:
    /* Subclasses pass their canonical CcuResult code. */
    explicit CcuException(const std::string& userDefinedMsg) : CcuException(userDefinedMsg, CCU_E_INTERNAL) {}

private:
    void StoreBackTrace()
    {
        constexpr int BACKTRACE_DEPTH = 15;
        void* array[BACKTRACE_DEPTH];
        int size = backtrace(array, BACKTRACE_DEPTH);

        char** callBackStrings = backtrace_symbols(array, size);
        if (callBackStrings == nullptr) {
            backtraceStrings_.emplace_back("Failed to get backtrace symbols");
            return;
        }
        for (int i = 0; i < size; ++i) {
            backtraceStrings_.emplace_back(callBackStrings[i]);
        }
        free(callBackStrings);
    }

    std::vector<std::string> backtraceStrings_;
    std::string userDefinedMsg_;
    std::string errorMsg_;
    int32_t errorCode_;
};

/* API-level error from a CCU public entry point. */
class CcuApiException : public CcuException {
public:
    explicit CcuApiException(const std::string& userDefinedMsg) : CcuException(userDefinedMsg, CCU_E_PARA) {}
};

/* Internal invariant violation. */
class InternalException : public CcuException {
public:
    explicit InternalException(const std::string& userDefinedMsg) : CcuException(userDefinedMsg, CCU_E_INTERNAL) {}
};

/* Feature not supported. */
class NotSupportException : public CcuException {
public:
    explicit NotSupportException(const std::string& userDefinedMsg) : CcuException(userDefinedMsg, CCU_E_NOT_SUPPORT) {}
};

/* Null pointer where a valid object was required. */
class NullPtrException : public CcuException {
public:
    explicit NullPtrException(const std::string& userDefinedMsg) : CcuException(userDefinedMsg, CCU_E_PTR) {}
};

/*
 * THROW: log the message at ERROR level then throw EXCEPTION. Two overloads
 * mirror hcomm's exception_util.h: a plain string overload and a printf-style
 * template overload.
 */
template <typename EXCEPTION>
[[noreturn]] inline void THROW(const std::string& msg)
{
    HCCL_ERROR("%s", msg.c_str());
    throw EXCEPTION(msg);
}

template <typename EXCEPTION, typename... Args>
[[noreturn]] inline void THROW(const char* format, Args... args)
{
    std::string msg = StringFormat(format, args...);
    HCCL_ERROR("%s", msg.c_str());
    throw EXCEPTION(msg);
}

/* CHECK_NULLPTR: throw NullPtrException when the supplied pointer is null. */
template <typename POINTER>
inline void CHECK_NULLPTR(const POINTER& p, const std::string& msg)
{
    if (UNLIKELY(p == nullptr)) {
        THROW<NullPtrException>(msg);
    }
}

} // namespace hcomm

/* =========================================================================
 *  Macros (placed at global scope so they remain usable from any namespace,
 *  matching hcomm's exception_util.h). CcuException replaces HcclException in
 *  every catch clause below.
 * ========================================================================= */

/* Log the message then throw EXCEPTION. */
#define MACRO_THROW(EXCEPTION, MSG)      \
    do {                                 \
        HCCL_ERROR("%s", (MSG).c_str()); \
        throw EXCEPTION(MSG);            \
    } while (0)

/*
 * CHK_RET_THROW(EXCEPTION, MSG, expr):
 * `expr` is expected to return a boolean success flag (true == failure, same
 * convention as CHK_PRT_THROW used by the migrated CCU data-plane code).
 */
#define CHK_RET_THROW(EXCEPTION, MSG, expr)      \
    do {                                         \
        if (UNLIKELY(static_cast<bool>(expr))) { \
            ::hcomm::THROW<EXCEPTION>(MSG);      \
        }                                        \
    } while (0)

/* If `expr` is truthy, run `exeLog` then throw EXCEPTION(MSG). */
#define CHK_PRT_THROW(expr, exeLog, EXCEPTION, MSG) \
    do {                                            \
        if (UNLIKELY(static_cast<bool>(expr))) {    \
            exeLog;                                 \
            throw EXCEPTION(MSG);                   \
        }                                           \
    } while (0)

/* Wrap EXPR in try/catch; rethrow any captured exception as EXCEPTION. */
#define TRY_CATCH_THROW(EXCEPTION, MSG, EXPR)                                                            \
    do {                                                                                                 \
        try {                                                                                            \
            EXPR;                                                                                        \
        } catch (::hcomm::CcuException & e) {                                                            \
            ::hcomm::THROW<EXCEPTION>(::hcomm::StringFormat("%s, %s", e.what(), (MSG).c_str()));         \
        } catch (std::exception & e) {                                                                   \
            ::hcomm::THROW<EXCEPTION>(::hcomm::StringFormat("%s, %s", e.what(), (MSG).c_str()));         \
        } catch (...) {                                                                                  \
            ::hcomm::THROW<EXCEPTION>(::hcomm::StringFormat("Unknown error occurs, %s", (MSG).c_str())); \
        }                                                                                                \
    } while (0)

/* Destructor guard: swallow all exceptions, log only. */
#define DECTOR_TRY_CATCH(classname, expr)                                                         \
    do {                                                                                          \
        try {                                                                                     \
            expr;                                                                                 \
        } catch (::hcomm::CcuException & e) {                                                     \
            HCCL_ERROR("Exception occurred: %s", e.what());                                       \
        } catch (std::exception & e) {                                                            \
            HCCL_ERROR("Exception occurred: %s", e.what());                                       \
        } catch (...) {                                                                           \
            HCCL_ERROR("Unknown error occurs when destructor object of class \"%s\"", classname); \
        }                                                                                         \
    } while (0)

/* Evaluate `expr`; on exception log + backtrace and return the error code. */
#define TRY_CATCH_RETURN(expr)                                  \
    do {                                                        \
        try {                                                   \
            expr;                                               \
        } catch (::hcomm::CcuException & e) {                   \
            HCCL_ERROR("Exception occurred: %s", e.what());     \
            auto backTraces = e.GetBackTraceStrings();          \
            for (const auto& item : backTraces) {               \
                HCCL_INFO("backTraces item: %s", item.c_str()); \
            }                                                   \
            return e.GetErrorCode();                            \
        } catch (std::exception & e) {                          \
            HCCL_ERROR("Exception occurred: %s", e.what());     \
            return CCU_E_INTERNAL;                              \
        } catch (...) {                                         \
            HCCL_ERROR("Unkown error occurs!");                 \
            return CCU_E_INTERNAL;                              \
        }                                                       \
    } while (0)

/* Evaluate `expr`; swallow all exceptions, log only. */
#define TRY_CATCH_PRINT_ERROR(expr)                             \
    do {                                                        \
        try {                                                   \
            expr;                                               \
        } catch (::hcomm::CcuException & e) {                   \
            HCCL_ERROR("Exception occurred: %s", e.what());     \
            auto backTraces = e.GetBackTraceStrings();          \
            for (const auto& item : backTraces) {               \
                HCCL_INFO("backTraces item: %s", item.c_str()); \
            }                                                   \
        } catch (std::exception & e) {                          \
            HCCL_ERROR("Exception occurred: %s", e.what());     \
        } catch (...) {                                         \
            HCCL_ERROR("Unkown error occurs!");                 \
        }                                                       \
    } while (0)

/* Evaluate EXPR; on exception run PROCESS then rethrow as EXCEPTION. */
#define TRY_CATCH_PROCESS_THROW(EXCEPTION, EXPR, MSG, PROCESS)                                        \
    do {                                                                                              \
        try {                                                                                         \
            EXPR;                                                                                     \
        } catch (::hcomm::CcuException & e) {                                                         \
            PROCESS;                                                                                  \
            HCCL_ERROR("%s due to %s", (MSG), e.what());                                              \
            throw e;                                                                                  \
        } catch (std::exception & e) {                                                                \
            PROCESS;                                                                                  \
            MACRO_THROW(EXCEPTION, ::hcomm::StringFormat("%s due to %s", (MSG), e.what()));           \
        } catch (...) {                                                                               \
            PROCESS;                                                                                  \
            MACRO_THROW(EXCEPTION, ::hcomm::StringFormat("%s due to: Unknown error occurs!", (MSG))); \
        }                                                                                             \
    } while (0)

namespace hcomm {

/*
 * ExceptionHandler: translates a thrown exception into a CcuResult error code.
 * Used by ccu_log.h's CCU_EXCEPTION_HANDLE_END macro to wrap the C entry
 * points of the CCU data-plane API.
 */
class ExceptionHandler {
public:
    static int32_t HandleException(const char* functionName)
    {
        try {
            throw;
        } catch (const CcuException& e) {
            HCCL_ERROR("%s: CcuException, what: %s, code: %d", functionName, e.what(), e.GetErrorCode());
            return e.GetErrorCode();
        } catch (const std::out_of_range& e) {
            HCCL_ERROR("%s: Out of range error, what: %s", functionName, e.what());
            return CCU_E_NOT_FOUND;
        } catch (const std::runtime_error& e) {
            HCCL_ERROR("%s: Runtime error, what: %s", functionName, e.what());
            return CCU_E_RUNTIME;
        } catch (const std::logic_error& e) {
            HCCL_ERROR("%s: Logic error, what: %s", functionName, e.what());
            return CCU_E_INTERNAL;
        } catch (const std::exception& e) {
            HCCL_ERROR("%s: Standard exception, what: %s", functionName, e.what());
            return CCU_E_INTERNAL;
        } catch (...) {
            HCCL_ERROR("%s: Unknown exception", functionName);
            return CCU_E_INTERNAL;
        }
    }

    static int32_t HandleException(const std::string& functionName) { return HandleException(functionName.c_str()); }
};

} // namespace hcomm

#endif // ASCCOMM_EXCEPTION_H
