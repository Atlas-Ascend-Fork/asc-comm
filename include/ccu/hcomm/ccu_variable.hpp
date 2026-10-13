/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_VARIABLE_HPP
#define CCU_VARIABLE_HPP

#include <cstdint>
#include <type_traits>

#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_utils.hpp"
#include "ccu/hcomm/ccu_primitives_impl.h"

namespace AscendC {
namespace ccu {

class variable;
class local_addr;
class remote_addr;
template <typename u>
class array;
template <typename t>
t GetResByChannel(ChannelHandle channel_, uint32_t index);

struct cond_expr {
    variable* var_{nullptr};
    const variable* rhs_var_{nullptr};
    uint64_t imm{0};
    ccu_condition_type cond{ccu_condition_eq};
    bool is_var_compare{false};
};

class variable final {
public:
    variable() { CCU_THROW_IF_FAILED(::asc::ccu_variable_alloc(&this->handle), "ccu_variable_alloc: failed"); }

    explicit variable(ccu_variable_handle var_handle, uint32_t index = 0)
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_get_by_index(var_handle, index, &this->handle), "ccu_variable_get_by_index: failed");
    }

    variable(const variable& other) { this->handle = other.handle; }

    variable(variable&& other) noexcept { this->handle = other.handle; }

    void operator=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_assign_var(this->handle, other.handle),
            "variable::operator=(variable): ccu_variable_assign_var failed");
    }

    void operator=(variable&& other) { this->handle = other.handle; }

    void operator=(uint64_t immediate_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_assign_imm(this->handle, immediate_),
            "variable::operator=(uint64_t): ccu_variable_assign_imm failed");
    }

    void operator=(detail::ccu_arithmetic_operator<variable, variable> op) const
    {
        switch (op.type_) {
            case detail::ccu_arithmetic_operator_type::addition:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_add_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var+Var): ccu_variable_add_var_to_var failed");
                break;
            case detail::ccu_arithmetic_operator_type::subtraction:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_sub_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var-Var): ccu_variable_sub_var_to_var failed");
                break;
            case detail::ccu_arithmetic_operator_type::multiplication:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_mul_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var*Var): ccu_variable_mul_var_to_var failed");
                break;
            default:
                throw detail::ccu_exception(
                    CcuResult::CCU_E_PARA, "variable::operator=: invalid arithmetic operator type_");
        }
    }

    void operator=(detail::ccu_arithmetic_operator<variable, uint16_t> op) const
    {
        switch (op.type_) {
            case detail::ccu_arithmetic_operator_type::addition:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_add_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                    "variable::operator=(Var+Imm): ccu_variable_add_imm_to_var failed");
                break;
            case detail::ccu_arithmetic_operator_type::subtraction:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_sub_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                    "variable::operator=(Var-Imm): ccu_variable_sub_imm_to_var failed");
                break;
            case detail::ccu_arithmetic_operator_type::multiplication:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_mul_imm_to_var(this->handle, op.lhs.handle, op.rhs),
                    "variable::operator=(Var*Imm): ccu_variable_mul_imm_to_var failed");
                break;
            default:
                throw detail::ccu_exception(
                    CcuResult::CCU_E_PARA, "variable::operator=: invalid arithmetic operator type_");
        }
    }

    void operator+=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_add_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator+=(variable): ccu_variable_add_var_to_var failed");
    }

    void operator-=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_sub_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator-=(variable): ccu_variable_sub_var_to_var failed");
    }

    void operator*=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_mul_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator*=(variable): ccu_variable_mul_var_to_var failed");
    }

    void operator+=(uint16_t immediate_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_add_imm_to_var(this->handle, this->handle, immediate_),
            "variable::operator+=(uint16_t): ccu_variable_add_imm_to_var failed");
    }

    void operator-=(uint16_t immediate_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_sub_imm_to_var(this->handle, this->handle, immediate_),
            "variable::operator-=(uint16_t): ccu_variable_sub_imm_to_var failed");
    }

    void operator*=(uint16_t immediate_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_mul_imm_to_var(this->handle, this->handle, immediate_),
            "variable::operator*=(uint16_t): ccu_variable_mul_imm_to_var failed");
    }

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

    detail::ccu_arithmetic_operator<variable, uint16_t> operator+(uint16_t immediate_) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate_, detail::ccu_arithmetic_operator_type::addition);
    }

    detail::ccu_arithmetic_operator<variable, uint16_t> operator-(uint16_t immediate_) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate_, detail::ccu_arithmetic_operator_type::subtraction);
    }

    detail::ccu_arithmetic_operator<variable, uint16_t> operator*(uint16_t immediate_) const
    {
        return detail::ccu_arithmetic_operator<variable, uint16_t>(
            *this, immediate_, detail::ccu_arithmetic_operator_type::multiplication);
    }

    void operator=(detail::ccu_logic_operator<variable, variable> op) const
    {
        switch (op.type_) {
            case detail::ccu_logic_operator_type::and_op:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_and_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var&Var): ccu_variable_and_var_to_var failed");
                break;
            case detail::ccu_logic_operator_type::or_op:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_or_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var|Var): ccu_variable_or_var_to_var failed");
                break;
            case detail::ccu_logic_operator_type::xor_op:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_xor_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var^Var): ccu_variable_xor_var_to_var failed");
                break;
            default:
                throw detail::ccu_exception(CcuResult::CCU_E_PARA, "variable::operator=: invalid logic operator type_");
        }
    }

    void operator=(detail::ccu_logic_unary_operator<variable> op) const
    {
        switch (op.type_) {
            case detail::ccu_logic_operator_type::not_op:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_not_var(this->handle, op.lhs.handle),
                    "variable::operator=(~Var): ccu_variable_not_var failed");
                break;
            default:
                throw detail::ccu_exception(
                    CcuResult::CCU_E_PARA, "variable::operator=: invalid unary logic operator type_");
        }
    }

    void operator=(detail::ccu_shift_operator<variable, variable> op) const
    {
        switch (op.type_) {
            case detail::ccu_shift_operator_type::left:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_shl_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var<<Var): ccu_variable_shl_var_to_var failed");
                break;
            case detail::ccu_shift_operator_type::right:
                CCU_THROW_IF_FAILED(
                    ::asc::ccu_variable_shr_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
                    "variable::operator=(Var>>Var): ccu_variable_shr_var_to_var failed");
                break;
            default:
                throw detail::ccu_exception(CcuResult::CCU_E_PARA, "variable::operator=: invalid shift operator type_");
        }
    }

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

    void operator&=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_and_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator&=(variable): ccu_variable_and_var_to_var failed");
    }

    void operator|=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_or_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator|=(variable): ccu_variable_or_var_to_var failed");
    }

    void operator^=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_xor_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator^=(variable): ccu_variable_xor_var_to_var failed");
    }

    detail::ccu_shift_operator<variable, variable> operator<<(const variable& that) const
    {
        return detail::ccu_shift_operator<variable, variable>(*this, that, detail::ccu_shift_operator_type::left);
    }

    detail::ccu_shift_operator<variable, variable> operator>>(const variable& that) const
    {
        return detail::ccu_shift_operator<variable, variable>(*this, that, detail::ccu_shift_operator_type::right);
    }

    void operator<<=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_shl_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator<<=(variable): ccu_variable_shl_var_to_var failed");
    }

    void operator>>=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_shr_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator>>=(variable): ccu_variable_shr_var_to_var failed");
    }

    cond_expr operator==(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_eq, false}; }

    cond_expr operator!=(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_ne, false}; }

    cond_expr operator<(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_lt, false}; }

    cond_expr operator<=(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_le, false}; }

    cond_expr operator>(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_gt, false}; }

    cond_expr operator>=(uint64_t immediate_) { return cond_expr{this, nullptr, immediate_, ccu_condition_ge, false}; }

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
    friend t GetResByChannel(ChannelHandle channel_, uint32_t index);
};

} // namespace ccu
} // namespace AscendC

template <>
inline void AscendC::ccu::detail::ccu_arithmetic_operator<AscendC::ccu::variable, AscendC::ccu::variable>::check() const
{}

template <>
inline void AscendC::ccu::detail::ccu_arithmetic_operator<AscendC::ccu::variable, uint16_t>::check() const
{}

template <>
inline void AscendC::ccu::detail::ccu_logic_operator<AscendC::ccu::variable, AscendC::ccu::variable>::check() const
{}

template <>
inline void AscendC::ccu::detail::ccu_logic_unary_operator<AscendC::ccu::variable>::check() const
{}

template <>
inline void AscendC::ccu::detail::ccu_shift_operator<AscendC::ccu::variable, AscendC::ccu::variable>::check() const
{}

#endif // CCU_VARIABLE_HPP
