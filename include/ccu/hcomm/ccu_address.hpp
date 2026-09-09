/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_ADDRESS_HPP
#define CCU_ADDRESS_HPP

#include <cstdint>
#include <type_traits>

#include "ccu/hcomm/ccu_api_types.h"
#include "ccu/hcomm/ccu_utils.hpp"
#include "ccu/hcomm/ccu_primitives_impl.h"
#include "ccu/hcomm/ccu_variable.hpp"

namespace AscendC {
namespace ccu {

class local_addr;
class remote_addr;
template <typename u>
class array;

class address final {
public:
    address() { CCU_THROW_IF_FAILED(::asc::ccu_address_alloc(&this->handle), "ccu_address_alloc: failed"); }

    address(const address& other) { this->handle = other.handle; }

    address(address&& other) noexcept { this->handle = other.handle; }

    void operator=(const address& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_assign_addr(this->handle, other.handle),
            "address::operator=(address): ccu_address_assign_addr failed");
    }

    void operator=(address&& other) { this->handle = other.handle; }

    void operator=(uint64_t immediate_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_assign_imm(this->handle, immediate_),
            "address::operator=(uint64_t): ccu_address_assign_imm failed");
    }

    void operator=(const variable& var_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_assign_var(this->handle, var_.handle),
            "address::operator=(variable): ccu_address_assign_var failed");
    }

    void operator=(detail::ccu_arithmetic_operator<address, address> op) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_add_addr_to_addr(this->handle, op.lhs.handle, op.rhs.handle),
            "address::operator=(Addr+Addr): ccu_address_add_addr_to_addr failed");
    }

    void operator=(detail::ccu_arithmetic_operator<address, variable> op) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_add_var_to_addr(this->handle, op.lhs.handle, op.rhs.handle),
            "address::operator=(Addr+Var): ccu_address_add_var_to_addr failed");
    }

    void operator=(detail::ccu_arithmetic_operator<variable, address> op) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_add_var_to_addr(this->handle, op.rhs.handle, op.lhs.handle),
            "address::operator=(Var+Addr): ccu_address_add_var_to_addr failed");
    }

    // addr_ + addr_
    detail::ccu_arithmetic_operator<address, address> operator+(const address& that) const
    {
        return detail::ccu_arithmetic_operator<address, address>(
            *this, that, detail::ccu_arithmetic_operator_type::addition);
    }

    // addr_ + variable
    detail::ccu_arithmetic_operator<address, variable> operator+(const variable& var_) const
    {
        return detail::ccu_arithmetic_operator<address, variable>(
            *this, var_, detail::ccu_arithmetic_operator_type::addition);
    }

    void operator+=(const variable& var_) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_add_assign_var(this->handle, var_.handle),
            "address::operator+=(variable): ccu_address_add_assign_var failed");
    }

    // addr_ += addr_
    void operator+=(const address& other) const
    {
        CCU_THROW_IF_FAILED(
            ::asc::ccu_address_add_addr_to_addr(this->handle, this->handle, other.handle),
            "address::operator+=(address): ccu_address_add_addr_to_addr failed");
    }

    ccu_address_handle handle{0};

private:
    explicit address(detail::no_alloc_tag) {}
    template <typename u>
    friend class array;
    friend class local_addr;
    friend class remote_addr;
};

// variable + addr_（交换律）
inline detail::ccu_arithmetic_operator<variable, address> operator+(const variable& var_, const address& addr_)
{
    return detail::ccu_arithmetic_operator<variable, address>(
        var_, addr_, detail::ccu_arithmetic_operator_type::addition);
}

} // namespace ccu
} // namespace AscendC

template <>
inline void AscendC::ccu::detail::ccu_arithmetic_operator<AscendC::ccu::address, AscendC::ccu::address>::check() const
{}
template <>
inline void AscendC::ccu::detail::ccu_arithmetic_operator<AscendC::ccu::address, AscendC::ccu::variable>::check() const
{}
template <>
inline void AscendC::ccu::detail::ccu_arithmetic_operator<AscendC::ccu::variable, AscendC::ccu::address>::check() const
{}

#endif // CCU_ADDRESS_HPP
