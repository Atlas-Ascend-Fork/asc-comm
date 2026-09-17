/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file arg_parser.h
 * \brief Ascend C样例共享的命令行参数解析工具。
 *
 * 提供通用的整数文本解析函数，供各样例按需组合使用。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H
#define ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>

namespace examples {

// 解析非负整数文本为 uint32_t。
// 返回 true 表示解析成功且 value >= min；返回 false 表示文本非法或越界。
inline bool ParseUint32(const char* text, uint32_t& value, uint32_t min = 0)
{
    if (text == nullptr || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < min || parsed > UINT32_MAX) {
        return false;
    }
    value = static_cast<uint32_t>(parsed);
    return true;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H
