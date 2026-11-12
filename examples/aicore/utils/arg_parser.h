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
 * 提供通用的参数解析原语，供各样例按需组合使用：
 *   - 整数文本解析：ParseUint32 / ParseUint64；
 *   - 逗号分隔 token → 位掩码：ParseMask（"all" 为整表全置的语法糖）；
 *   - 幂数范围 → 数据量档位表：BuildLenLadder（2^min 起逐档指数 +1，含两端）。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H
#define ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

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

// 解析非负整数文本为 uint64_t。
// 返回 true 表示解析成功且 value >= min；返回 false 表示文本非法或越界。
inline bool ParseUint64(const char* text, uint64_t& value, uint64_t min = 0)
{
    if (text == nullptr || *text == '\0' || *text == '-') {
        return false;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' || parsed < min) {
        return false;
    }
    value = static_cast<uint64_t>(parsed);
    return true;
}

// 逗号分隔的 token → 位掩码：token 命中 nameTable 的一项就置上对应位，
// 有不认识的 token 返回 false。"all" 是整表全置的语法糖。
// nameTable 以 nullptr 结尾，第 i 项对应掩码位 1u << i（≤ 32 项）。
inline bool ParseMask(const char* s, const char* const names[], uint32_t allMask, uint32_t& mask)
{
    if (s == nullptr) {
        return false;
    }
    const std::string src = s;
    if (src == "all") {
        mask = allMask;
        return true;
    }
    uint32_t m = 0;
    size_t start = 0;
    while (start <= src.size()) {
        size_t comma = src.find(',', start);
        const std::string token = src.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        uint32_t bit = 0;
        for (size_t i = 0; names[i] != nullptr; ++i) {
            if (token == names[i]) {
                bit = 1u << i;
                break;
            }
        }
        if (bit == 0) {
            return false;
        }
        m |= bit;
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    mask = m;
    return true;
}

// 解析幂数范围 → 数据量档位表（2^min 起逐档指数 +1，至 2^max，含两端）。
// minExp < 3 时返回空表（破坏 4B 对齐校验粒度，由调用方拦掉）；上限只防
// 1ULL << e 溢出（真实可用上限由样例的落点区容量约束，调用方逐档校验）。
inline std::vector<uint64_t> BuildLenLadder(uint64_t minExp, uint64_t maxExp)
{
    std::vector<uint64_t> lens;
    if (minExp < 3 || maxExp < minExp || maxExp > 63) {
        return lens;
    }
    for (uint64_t e = minExp; e <= maxExp; ++e) {
        lens.push_back(1ULL << e);
    }
    return lens;
}

} // namespace examples

#endif // ASC_COMM_EXAMPLES_UTILS_ARG_PARSER_H
