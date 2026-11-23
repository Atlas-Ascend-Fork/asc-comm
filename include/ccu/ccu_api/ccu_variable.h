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
 * @file ccu_variable.h
 * @brief CCU 变量类型 variable 的声明。
 */

#ifndef CCU_VARIABLE_H
#define CCU_VARIABLE_H

#include <cstdint>
#include <type_traits>

#include "ccu/ccu_api/ccu_types.h"
#include "ccu/ccu_api/ccu_utils.h"

namespace asc {
namespace ccu {

class variable;
class local_addr;
class remote_addr;
template <typename u>
class array;
template <typename t>
t get_res_by_channel(ChannelHandle channel, uint32_t index);

struct cond_expr {
    variable* var_{nullptr};
    const variable* rhs_var_{nullptr};
    uint64_t imm{0};
    ccu_condition_type cond{ccu_condition_eq};
    bool is_var_compare{false};
};

class variable final {
public:
    variable();

    explicit variable(ccu_variable_handle var_handle, uint32_t index = 0);

    variable(const variable& other) { this->handle = other.handle; }

    variable(variable&& other) noexcept { this->handle = other.handle; }

    void operator=(const variable& other) const;

    void operator=(variable&& other) { this->handle = other.handle; }

    void operator=(uint64_t immediate) const;

    void operator=(detail::ccu_arithmetic_operator<variable, variable> op) const;

    void operator=(detail::ccu_arithmetic_operator<variable, uint16_t> op) const;

    void operator+=(const variable& other) const;

    void operator-=(const variable& other) const;

    void operator*=(const variable& other) const;

    void operator+=(uint16_t immediate) const;

    void operator-=(uint16_t immediate) const;

    void operator*=(uint16_t immediate) const;

    detail::ccu_arithmetic_operator<variable, variable> operator+(const variable& that) const
    {
        return detail::ccu_arithmetic_operator<variable, variable>(
            *this, that, detail::ccu_arithmetic_operator_type::addition);
    }

    detail::ccu_arithmetic_operator<variable, variable> operator-(const variable& that) const
    {
        return detail::ccu_arithmetic_operator<variable, variable>(
            *this, that, detail::ccu_arithmetic_operator_type::subtraction);
    }

    detail::ccu_arithmetic_operator<variable, variable> operator*(const variable& that) const
    {
        return detail::ccu_arithmetic_operator<variable, variable>(
            *this, that, detail::ccu_arithmetic_operator_type::multiplication);
    }

    detail::ccu_arithmetic_operator<variable, uint16_t> operator+(uint16_t immediate) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate, detail::ccu_arithmetic_operator_type::addition);
    }

    detail::ccu_arithmetic_operator<variable, uint16_t> operator-(uint16_t immediate) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate, detail::ccu_arithmetic_operator_type::subtraction);
    }

    detail::ccu_arithmetic_operator<variable, uint16_t> operator*(uint16_t immediate) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate, detail::ccu_arithmetic_operator_type::multiplication);
    }

    void operator=(detail::ccu_logic_operator<variable, variable> op) const;

    void operator=(detail::ccu_logic_unary_operator<variable> op) const;

    void operator=(detail::ccu_shift_operator<variable, variable> op) const;

    detail::ccu_logic_operator<variable, variable> operator&(const variable& that) const
    {
        return detail::ccu_logic_operator<variable, variable>(*this, that, detail::ccu_logic_operator_type::and_op);
    }

    detail::ccu_logic_operator<variable, variable> operator|(const variable& that) const
    {
        return detail::ccu_logic_operator<variable, variable>(*this, that, detail::ccu_logic_operator_type::or_op);
    }

    detail::ccu_logic_operator<variable, variable> operator^(const variable& that) const
    {
        return detail::ccu_logic_operator<variable, variable>(*this, that, detail::ccu_logic_operator_type::xor_op);
    }

    detail::ccu_logic_unary_operator<variable> operator~() const
    {
        return detail::ccu_logic_unary_operator<variable>(*this, detail::ccu_logic_operator_type::not_op);
    }

    void operator&=(const variable& other) const;

    void operator|=(const variable& other) const;

    void operator^=(const variable& other) const;

    detail::ccu_shift_operator<variable, variable> operator<<(const variable& that) const
    {
        return detail::ccu_shift_operator<variable, variable>(*this, that, detail::ccu_shift_operator_type::left);
    }

    detail::ccu_shift_operator<variable, variable> operator>>(const variable& that) const
    {
        return detail::ccu_shift_operator<variable, variable>(*this, that, detail::ccu_shift_operator_type::right);
    }

    void operator<<=(const variable& other) const;

    void operator>>=(const variable& other) const;

    cond_expr operator==(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_eq, false}; }

    cond_expr operator!=(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_ne, false}; }

    cond_expr operator<(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_lt, false}; }

    cond_expr operator<=(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_le, false}; }

    cond_expr operator>(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_gt, false}; }

    cond_expr operator>=(uint64_t immediate) { return cond_expr{this, nullptr, immediate, ccu_condition_ge, false}; }

    cond_expr operator==(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_eq, true}; }

    cond_expr operator!=(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_ne, true}; }

    cond_expr operator<(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_lt, true}; }

    cond_expr operator<=(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_le, true}; }

    cond_expr operator>(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_gt, true}; }

    cond_expr operator>=(const variable& other) { return cond_expr{this, &other, 0, ccu_condition_ge, true}; }

    ccu_variable_handle handle{0};

private:
    explicit variable(detail::no_alloc_tag) {}
    template <typename u>
    friend class array;
    friend class local_addr;
    friend class remote_addr;
    template <typename t>
    friend t get_res_by_channel(ChannelHandle channel, uint32_t index);
};

} // namespace ccu
} // namespace asc

template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::variable, asc::ccu::variable>::check() const
{}

template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::variable, uint16_t>::check() const
{}

template <>
inline void asc::ccu::detail::ccu_logic_operator<asc::ccu::variable, asc::ccu::variable>::check() const
{}

template <>
inline void asc::ccu::detail::ccu_logic_unary_operator<asc::ccu::variable>::check() const
{}

template <>
inline void asc::ccu::detail::ccu_shift_operator<asc::ccu::variable, asc::ccu::variable>::check() const
{}

#endif // CCU_VARIABLE_H
