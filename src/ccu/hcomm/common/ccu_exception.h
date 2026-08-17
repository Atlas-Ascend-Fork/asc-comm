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

namespace asccomm {

// 解耦后 asc-comm 不再依赖 hcomm 的异常格式化工具，此处仅保留 printf 风格消息的本地兼容实现。
template <typename... Args>
std::string FormatCcuMessage(const char *format, Args... args)
{
    const int length = std::snprintf(nullptr, 0, format, args...);
    if (length <= 0) {
        return format == nullptr ? std::string{} : std::string(format);
    }
    std::vector<char> buffer(static_cast<size_t>(length) + 1U);
    (void)std::snprintf(buffer.data(), buffer.size(), format, args...);
    return std::string(buffer.data(), static_cast<size_t>(length));
}

inline std::string FormatCcuMessage(const std::string &message)
{
    return message;
}

template <typename Message, typename... Args>
[[noreturn]] void ThrowCcu(CcuResult code, Message &&message, Args... args)
{
    const std::string formatted = FormatCcuMessage(std::forward<Message>(message), args...);
    HCCL_ERROR("%s", formatted.c_str());
    throw ::AscendC::ccu::detail::CcuException(code, formatted.c_str());
}

template <typename Message, typename... Args>
[[noreturn]] void ThrowCcuInternal(Message &&message, Args... args)
{
    ThrowCcu(CcuResult::CCU_E_INTERNAL, std::forward<Message>(message), args...);
}

template <typename Message, typename... Args>
[[noreturn]] void ThrowCcuNotSupport(Message &&message, Args... args)
{
    ThrowCcu(CcuResult::CCU_E_NOT_SUPPORT, std::forward<Message>(message), args...);
}

template <typename Pointer, typename Message>
void CheckCcuNotNull(const Pointer &pointer, Message &&message)
{
    if (UNLIKELY(pointer == nullptr)) {
        ThrowCcu(CcuResult::CCU_E_PTR, std::forward<Message>(message));
    }
}

} // namespace asccomm

#define CHK_RET_THROW(exceptionCode, message, call)                                  \
    do {                                                                             \
        const auto ccuThrowRet = (call);                                              \
        if (UNLIKELY(ccuThrowRet != 0)) {                                             \
            asccomm::ThrowCcu(static_cast<CcuResult>(ccuThrowRet), (message));          \
        }                                                                            \
    } while (0)

#define CHK_PRT_THROW(condition, logStatement, exceptionCode, message) \
    do {                                                               \
        if (UNLIKELY(condition)) {                                     \
            logStatement;                                              \
            asccomm::ThrowCcu((exceptionCode), (message));               \
        }                                                              \
    } while (0)

#endif // ASCCOMM_CCU_EXCEPTION_H
