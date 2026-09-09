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
 * @file ccu_exception.h
 * @brief 提供 asc-comm CCU 数据面使用的异常构造与格式化兼容能力。
 */

#ifndef ASCCOMM_CCU_EXCEPTION_H
#define ASCCOMM_CCU_EXCEPTION_H

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "ccu/hcomm/ccu_utils.hpp"
#include "hcomm/common/ccu_log.h"

namespace asc {

// 解耦后 asc-comm 不再依赖 hcomm 的异常格式化工具，此处仅保留 printf 风格消息的本地兼容实现。
template <typename... args_t>
std::string format_ccu_message(const char* format, args_t... args)
{
    const int length = std::snprintf(nullptr, 0, format, args...);
    if (length <= 0) {
        return format == nullptr ? std::string{} : std::string(format);
    }
    std::vector<char> buffer(static_cast<size_t>(length) + 1U);
    (void)std::snprintf(buffer.data(), buffer.size(), format, args...);
    return std::string(buffer.data(), static_cast<size_t>(length));
}

inline std::string format_ccu_message(const std::string& message) { return message; }

template <typename message_t, typename... args_t>
[[noreturn]] void throw_ccu(CcuResult code, message_t&& message, args_t... args)
{
    const std::string formatted = format_ccu_message(std::forward<message_t>(message), args...);
    HCCL_ERROR("%s", formatted.c_str());
    throw ::AscendC::ccu::detail::ccu_exception(code, formatted.c_str());
}

template <typename message_t, typename... args_t>
[[noreturn]] void throw_ccu_internal(message_t&& message, args_t... args)
{
    throw_ccu(CcuResult::CCU_E_INTERNAL, std::forward<message_t>(message), args...);
}

template <typename message_t, typename... args_t>
[[noreturn]] void throw_ccu_not_support(message_t&& message, args_t... args)
{
    throw_ccu(CcuResult::CCU_E_NOT_SUPPORT, std::forward<message_t>(message), args...);
}

template <typename pointer_t, typename message_t>
void check_ccu_not_null(const pointer_t& pointer, message_t&& message)
{
    if (UNLIKELY(pointer == nullptr)) {
        throw_ccu(CcuResult::CCU_E_PTR, std::forward<message_t>(message));
    }
}

} // namespace asc

#define CHK_RET_THROW(exceptionCode, message, call)                         \
    do {                                                                    \
        const auto ccuThrowRet = (call);                                    \
        if (UNLIKELY(ccuThrowRet != 0)) {                                   \
            asc::throw_ccu(static_cast<CcuResult>(ccuThrowRet), (message)); \
        }                                                                   \
    } while (0)

#define CHK_PRT_THROW(condition, logStatement, exceptionCode, message) \
    do {                                                               \
        if (UNLIKELY(condition)) {                                     \
            logStatement;                                              \
            asc::throw_ccu((exceptionCode), (message));                \
        }                                                              \
    } while (0)

#endif // ASCCOMM_CCU_EXCEPTION_H
