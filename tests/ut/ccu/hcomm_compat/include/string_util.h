/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_STRING_UTIL_H
#define ASCCOMM_CCU_UT_STRING_UTIL_H

#include <cstdio>
#include <string>
#include <vector>

namespace Hccl {

template <typename... args>
inline std::string StringFormat(const char* format, args... args_)
{
    const int required = std::snprintf(nullptr, 0, format, args_...);
    if (required < 0) {
        return "";
    }
    std::vector<char> buffer(static_cast<size_t>(required) + 1U);
    (void)std::snprintf(buffer.data(), buffer.size(), format, args_...);
    return buffer.data();
}

} // namespace Hccl

#endif
