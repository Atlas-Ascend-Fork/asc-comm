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
 * asc-comm self-contained string utilities, replacing the dependency on
 * hcomm's "utils/string_util.h". The public API mirrors the original
 * templates/functions so migrated call sites compile unchanged.
 *
 * Namespace is `hcomm`.
 */
#ifndef ASCCOMM_STRING_UTIL_H
#define ASCCOMM_STRING_UTIL_H

#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include <securec.h>

#include "asccomm_log.h"

namespace hcomm {

/*
 * printf-like formatter backed by snprintf_s. Logic follows hcomm
 * string_util.h: first attempt into a stack buffer, then on truncation
 * re-format into a heap buffer sized from the returned byte count.
 */
template <typename... Args>
inline std::string StringFormat(const char* format, Args... args)
{
    if (format == nullptr) {
        HCCL_ERROR("[StringFormat] format string is nullptr.");
        return "";
    }
    constexpr size_t bufSize = BUFSIZ;
    char buffer[bufSize];
    int result = snprintf_s(&buffer[0], bufSize, bufSize, format, args...);
    if (result < 0) {
        HCCL_ERROR("[StringFormat] snprintf_s failed.");
        return "";
    }
    size_t actualSize = static_cast<size_t>(result);
    if (actualSize + 1 > bufSize) {
        actualSize++;
        std::vector<char> newBuffer(actualSize);
        if (snprintf_s(newBuffer.data(), actualSize, actualSize, format, args...) < 0) {
            HCCL_ERROR("[StringFormat] snprintf_s (retry) failed.");
            return "";
        }
        return std::string(newBuffer.data());
    }
    return std::string(buffer);
}

/* Convert any integer to a 0x-prefixed hex string. */
template <typename I>
std::string Dec2hex(I i)
{
    static_assert(std::is_integral<I>::value, "Dec2hex requires an integral type");
    std::stringstream ss;
    ss << std::hex << "0x" << i;
    return ss.str();
}

/* Split `str` by single-char delimiter `c`. */
inline std::vector<std::string> SplitString(const std::string& str, const char c)
{
    std::string::size_type startPos = 0;
    std::string::size_type foundPos = str.find(c);

    std::vector<std::string> strVector;
    while (foundPos != std::string::npos) {
        strVector.push_back(str.substr(startPos, foundPos - startPos));
        startPos = foundPos + 1;
        foundPos = str.find(c, startPos);
    }
    if (startPos != str.length()) {
        strVector.push_back(str.substr(startPos));
    }
    return strVector;
}

} // namespace hcomm

#endif // ASCCOMM_STRING_UTIL_H
