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
#include <string>

#include "hcomm/hcomm_ccu_common.h"

namespace AscendC {
namespace ccu {
namespace detail {

struct no_alloc_tag {};

class ccu_exception : public std::exception {
public:
    ccu_exception(CcuResult code, const char* what) : code_(code), what_(build_message(code, what)) {}

    const char* what() const noexcept override { return what_.c_str(); }
    CcuResult code() const noexcept { return code_; }

private:
    static std::string build_message(CcuResult code, const char* what)
    {
        std::string msg = "[ccu] ";
        msg += (what != nullptr) ? what : "(unknown)";
        msg += " (CcuResult=";
        msg += std::to_string(static_cast<int>(code));
        msg += ")";
        return msg;
    }

    CcuResult code_;
    std::string what_;
};
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
        throw ::AscendC::ccu::detail::ccu_exception(
            CcuResult::CCU_E_PARA, "ccu_shift_operator: invalid operand types");
    }

    ccu_shift_operator_type type_{ccu_shift_operator_type::invalid};
};

} // namespace detail
} // namespace ccu
} // namespace AscendC

#define CCU_THROW_IF_FAILED(ret, msg)                                    \
    do {                                                                 \
        auto _ccu_ret = (ret);                                           \
        if (_ccu_ret != CcuResult::CCU_SUCCESS) {                        \
            throw ::AscendC::ccu::detail::ccu_exception(_ccu_ret, (msg)); \
        }                                                                \
    } while (0)

#endif // CCU_UTILS_HPP
