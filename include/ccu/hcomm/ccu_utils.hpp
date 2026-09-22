/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_UTILS_HPP
#define CCU_UTILS_HPP

#include <exception>
#include <cstdint>
#include <cstdio>

#include "hcomm/hcomm_ccu_common.h"

namespace AscendC {
namespace ccu {
namespace detail {

struct no_alloc_tag {};

// 该异常对象会跨越"样例二进制(用户 gcc 编译) -> libasccomm_ccu_dataplane.so(出包 gcc 编译)"边界：
// dataplane 侧 catch 后读取 what()/code()。因此对象布局与生成代码不得依赖编译器世代
// (std::string 成员/内联 STL 操作在不同 gcc 世代下布局与实现不同，会导致跨 SO 读取崩溃)。
// 这里使用定长 char 缓冲 + POD 成员，保证布局跨 gcc 冻结；snprintf 为 libc 符号，进程内单份实现。
class ccu_exception : public std::exception {
public:
    static constexpr uint32_t what_buf_len = 192;

    explicit ccu_exception(CcuResult code, const char* what) noexcept : code_(code)
    {
        (void)snprintf(
            what_, sizeof(what_), "[ccu] %s (CcuResult=%d)", (what != nullptr) ? what : "(unknown)",
            static_cast<int>(code));
    }

    const char* what() const noexcept override { return what_; }
    CcuResult code() const noexcept { return code_; }

private:
    CcuResult code_;
    char what_[what_buf_len];
};

// 布局冻结自检：ccu_exception 跨 SO 传递，两端(样例/数据面 SO)各自编译本头文件，
// 总大小必须恰为"成员之和向上取整到对齐"，且不含任何编译器世代敏感成员。
static_assert(
    sizeof(ccu_exception) >= sizeof(std::exception) + sizeof(CcuResult) + ccu_exception::what_buf_len &&
        sizeof(ccu_exception) - (sizeof(std::exception) + sizeof(CcuResult) + ccu_exception::what_buf_len) <
            alignof(ccu_exception),
    "ccu_exception layout is part of the cross-SO boundary and must stay frozen");
enum class ccu_arithmetic_operator_type { addition, invalid };
enum class ccu_shift_operator_type { left, right, invalid };

template <typename lhs_t, typename rhs_t>
class ccu_operator {
public:
    ccu_operator(lhs_t lhs, rhs_t rhs) : lhs(lhs), rhs(rhs) {}
    lhs_t lhs;
    rhs_t rhs;
};

template <typename lhs_t, typename rhs_t>
class ccu_arithmetic_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    ccu_arithmetic_operator(lhs_t lhs, rhs_t rhs, ccu_arithmetic_operator_type type_)
        : ccu_operator<lhs_t, rhs_t>(lhs, rhs), type_(type_)
    {
        check();
    }
    void check() const
    {
        throw ::AscendC::ccu::detail::ccu_exception(
            CcuResult::CCU_E_PARA, "ccu_arithmetic_operator: invalid operand types");
    }

    ccu_arithmetic_operator_type type_{ccu_arithmetic_operator_type::invalid};
};

// Binary shift operators only support variable operands, not immediate_ values.
template <typename lhs_t, typename rhs_t>
class ccu_shift_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    ccu_shift_operator(lhs_t lhs, rhs_t rhs, ccu_shift_operator_type type_)
        : ccu_operator<lhs_t, rhs_t>(lhs, rhs), type_(type_)
    {
        check();
    }
    void check() const
    {
        throw ::AscendC::ccu::detail::ccu_exception(CcuResult::CCU_E_PARA, "ccu_shift_operator: invalid operand types");
    }

    ccu_shift_operator_type type_{ccu_shift_operator_type::invalid};
};

} // namespace detail
} // namespace ccu
} // namespace AscendC

#define CCU_THROW_IF_FAILED(ret, msg)                                     \
    do {                                                                  \
        auto _ccu_ret = (ret);                                            \
        if (_ccu_ret != CcuResult::CCU_SUCCESS) {                         \
            throw ::AscendC::ccu::detail::ccu_exception(_ccu_ret, (msg)); \
        }                                                                 \
    } while (0)

#endif // CCU_UTILS_HPP
