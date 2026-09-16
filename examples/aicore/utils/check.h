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
 * \file check.h
 * \brief Ascend C样例共享的错误检查宏。
 *
 * 统一各样例原先各自定义的三套检查宏（ACL_CHECK/ACLCHECK、HCCL_CHECK/HCCLCHECK、
 * CHECK/CHECK_RESULT）：
 *   - 失败信息一律输出到stderr，包含文件:行号与表达式文本；
 *   - 返回值一律为错误码：ACL/HCCL宏返回接口自身的错误码，EXAMPLES_CHECK返回-1；
 *   - 表达式只求值一次（宏内临时变量带下划线前缀，避免与外部名冲突）。
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_CHECK_H
#define ASC_COMM_EXAMPLES_UTILS_CHECK_H

#include <cstdio>

// 检查失败时统一返回的错误码（样例自身语义的失败值仍由调用方自行定义/返回）。
#ifndef EXAMPLES_CHECK_FAILED
#define EXAMPLES_CHECK_FAILED (-1)
#endif

// ACL接口检查：expr求值一次，非ACL_SUCCESS时打印并return错误码。
// 用于返回类型可容纳aclError（int32_t）的函数。
#define EXAMPLES_ACLCHECK(expr)                                                                                    \
    do {                                                                                                           \
        const aclError _check_ret = (expr);                                                                        \
        if (_check_ret != ACL_SUCCESS) {                                                                           \
            std::fprintf(                                                                                          \
                stderr, "%s:%d: acl call failed, ret=%d\n", __FILE__, __LINE__, static_cast<int32_t>(_check_ret)); \
            return _check_ret;                                                                                     \
        }                                                                                                          \
    } while (0)

// HCCL接口检查：expr求值一次，非HCCL_SUCCESS时打印并return错误码。
#define EXAMPLES_HCCLCHECK(expr)                                                                                    \
    do {                                                                                                            \
        const HcclResult _check_ret = (expr);                                                                       \
        if (_check_ret != HCCL_SUCCESS) {                                                                           \
            std::fprintf(                                                                                           \
                stderr, "%s:%d: hccl call failed, ret=%d\n", __FILE__, __LINE__, static_cast<int32_t>(_check_ret)); \
            return _check_ret;                                                                                      \
        }                                                                                                           \
    } while (0)

// HCOMM接口检查：expr求值一次，非HCCL_SUCCESS时打印并return错误码。
// HcommResult与HcclResult兼容，成功值同为HCCL_SUCCESS。
#define EXAMPLES_HCOMMCHECK(expr)                                                                                    \
    do {                                                                                                             \
        const HcommResult _check_ret = (expr);                                                                       \
        if (_check_ret != HCCL_SUCCESS) {                                                                            \
            std::fprintf(                                                                                            \
                stderr, "%s:%d: hcomm call failed, ret=%d\n", __FILE__, __LINE__, static_cast<int32_t>(_check_ret)); \
            return _check_ret;                                                                                       \
        }                                                                                                            \
    } while (0)

// 通用期望值检查：call求值一次，不等于expect时打印rank/表达式/实际/期望并return
// EXAMPLES_CHECK_FAILED。用于返回类型为int32_t（或可容纳-1）的函数。
#define EXAMPLES_CHECK(call, expect, rank)                                                     \
    do {                                                                                       \
        const auto _check_ret = (call);                                                        \
        const auto _check_expect = (expect);                                                   \
        if (_check_ret != _check_expect) {                                                     \
            std::fprintf(                                                                      \
                stderr, "[CHECK ERROR][rank=%u] %s failed, ret=%lld, expect=%lld, at %s:%d\n", \
                static_cast<unsigned>(rank), #call, static_cast<long long>(_check_ret),        \
                static_cast<long long>(_check_expect), __FILE__, __LINE__);                    \
            return EXAMPLES_CHECK_FAILED;                                                      \
        }                                                                                      \
    } while (0)

#endif // ASC_COMM_EXAMPLES_UTILS_CHECK_H
