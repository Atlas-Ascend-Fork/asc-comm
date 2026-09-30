/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_STRING_UTIL_H
#define ASCCOMM_STRING_UTIL_H

#include <string>
#include <vector>
#include "securec.h"
#include "asccomm_log.h"

namespace Asc {

template <typename... Args>
inline std::string string_format(const char* format, Args... args)
{
    using namespace std;
    constexpr size_t bufSize = BUFSIZ;
    char buffer[bufSize];
    int result = snprintf_s(&buffer[0], bufSize, bufSize, format, args...);
    if (result < 0) {
        ASCCOMM_ERROR("[string_format] data snprintf_s failed.");
        return "";
    }
    size_t actualSize = static_cast<size_t>(result);
    if (actualSize + 1 > bufSize) {
        actualSize++;
        std::vector<char> newbuffer(actualSize);
        auto ret = snprintf_s(newbuffer.data(), actualSize, actualSize, format, args...);
        if (ret != EOK) {
            ASCCOMM_ERROR("[string_format] data snprintf_s failed.");
            return "";
        }
        return newbuffer.data();
    }
    return buffer;
}

} // namespace Asc

#endif // ASCCOMM_STRING_UTIL_H
