/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_OPERATOR
#define CCU_OPERATOR

#include <stdexcept>

namespace asc {
namespace ccu_rep {

template <typename lhs_t, typename rhs_t>
class ccu_operator {
public:
    ccu_operator(lhs_t lhs, rhs_t rhs) : lhs(lhs), rhs(rhs) {}
    lhs_t lhs;
    rhs_t rhs;
};

enum class ccu_arithmetic_operator_type { addition, multiplication, subtraction, invalid };

template <typename lhs_t, typename rhs_t>
class ccu_arithmetic_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    ccu_arithmetic_operator(lhs_t lhs, rhs_t rhs, ccu_arithmetic_operator_type type)
        : ccu_operator<lhs_t, rhs_t>(lhs, rhs), type(type)
    {
        check();
    }
    void check() const
    {
        // asc::ThrowCcuInternal("Invalid Arithmetic Operator");
        throw std::runtime_error("Invalid Arithmetic Operator");
    }

    ccu_arithmetic_operator_type type{ccu_arithmetic_operator_type::invalid};
};

enum class ccu_relational_operator_type {
    equal,
    not_equal,
    greater_than,
    greater_equal,
    less_than,
    less_equal,
    invalid
};

template <typename lhs_t, typename rhs_t>
class ccu_relational_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    ccu_relational_operator(lhs_t lhs, rhs_t rhs, ccu_relational_operator_type type)
        : ccu_operator<lhs_t, rhs_t>(lhs, rhs), type(type)
    {
        check();
    }
    void check() const
    {
        // asc::ThrowCcuInternal("Invalid Relational Operator");
        throw std::runtime_error("Invalid Relational Operator");
    }

    ccu_relational_operator_type type{ccu_relational_operator_type::invalid};
};

enum class ccu_logic_operator_type { and_op, or_op, xor_op, not_op, invalid };

template <typename lhs_t, typename rhs_t = std::nullptr_t>
class ccu_logic_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    template <typename u = rhs_t, typename std::enable_if<!std::is_same<u, std::nullptr_t>::value, int>::type = 0>
    ccu_logic_operator(lhs_t lhs, u rhs, ccu_logic_operator_type type) : ccu_operator<lhs_t, u>(lhs, rhs), type(type)
    {
        check();
    }

    template <typename u = rhs_t, typename std::enable_if<std::is_same<u, std::nullptr_t>::value, int>::type = 0>
    ccu_logic_operator(lhs_t lhs, ccu_logic_operator_type type) : ccu_operator<lhs_t, u>(lhs, {}), type(type)
    {
        check();
    }

    void check() const { throw std::runtime_error("Invalid LogicOperator Operator"); }

    ccu_logic_operator_type type{ccu_logic_operator_type::invalid};
};

enum class ccu_shift_type { left, right, invalid };

template <typename lhs_t, typename rhs_t>
class ccu_shift_operator : public ccu_operator<lhs_t, rhs_t> {
public:
    ccu_shift_operator(lhs_t lhs, rhs_t rhs, ccu_shift_type type) : ccu_operator<lhs_t, rhs_t>(lhs, rhs), type(type)
    {
        check();
    }
    void check() const
    {
        // THROW<CcuResult::CCU_E_INTERNAL>("Invalid ShiftT Operator");
        throw std::runtime_error("Invalid ShiftT Operator");
    }

    ccu_shift_type type{ccu_shift_type::invalid};
};

}; // namespace ccu_rep
}; // namespace asc

#endif // _CCU_OPERATOR
