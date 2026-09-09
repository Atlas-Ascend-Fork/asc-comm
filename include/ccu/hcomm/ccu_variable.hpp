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
    variable* var_;
    uint64_t imm;
    ccu_condition_type cond;
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
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_add_var_to_var(this->handle, op.lhs.handle, op.rhs.handle),
            "variable::operator=(Var+Var): ccu_variable_add_var_to_var failed");
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

    void operator+=(const variable& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_variable_add_var_to_var(this->handle, this->handle, other.handle),
            "variable::operator+=(variable): ccu_variable_add_var_to_var failed");
    }

    detail::ccu_arithmetic_operator<variable, variable> operator+(const variable& that) const
    {
        return detail::ccu_arithmetic_operator<variable, variable>(
            *this, that, detail::ccu_arithmetic_operator_type::addition);
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

    cond_expr operator==(uint64_t immediate_) { return cond_expr{this, immediate_, ccu_condition_eq}; }

    cond_expr operator!=(uint64_t immediate_) { return cond_expr{this, immediate_, ccu_condition_ne}; }

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
inline void AscendC::ccu::detail::ccu_shift_operator<AscendC::ccu::variable, AscendC::ccu::variable>::check() const
{}

#endif // CCU_VARIABLE_HPP
