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
 * @file ccu_address.h
 * @brief CCU 地址类型 address 的声明。
 */

#ifndef CCU_ADDRESS_H
#define CCU_ADDRESS_H

#include <cstdint>
#include <type_traits>

#include "ccu/ccu_api/ccu_types.h"
#include "ccu/ccu_api/ccu_utils.h"
#include "ccu/ccu_api/ccu_variable.h"

namespace asc {
namespace ccu {

class local_addr;
class remote_addr;
template <typename u>
class array;

class address final {
public:
    address();

    address(const address& other) { this->handle = other.handle; }

    address(address&& other) noexcept { this->handle = other.handle; }

    void operator=(const address& other) const;

    void operator=(address&& other) { this->handle = other.handle; }

    void operator=(uint64_t immediate) const;

    void operator=(const variable& var) const;

    void operator=(detail::ccu_arithmetic_operator<address, address> op) const;

    void operator=(detail::ccu_arithmetic_operator<address, variable> op) const;

    void operator=(detail::ccu_arithmetic_operator<variable, address> op) const;

    void operator=(detail::ccu_arithmetic_operator<address, uint16_t> op) const;

    // addr + addr
    detail::ccu_arithmetic_operator<address, address> operator+(const address& that) const
    {
        return detail::ccu_arithmetic_operator<address, address>(
            *this, that, detail::ccu_arithmetic_operator_type::addition);
    }

    // addr + variable
    detail::ccu_arithmetic_operator<address, variable> operator+(const variable& var) const
    {
        return detail::ccu_arithmetic_operator<address, variable>(
            *this, var, detail::ccu_arithmetic_operator_type::addition);
    }

    // address + immediate
    detail::ccu_arithmetic_operator<address, uint16_t> operator+(const uint16_t imm) const
    {
        return detail::ccu_arithmetic_operator<address, uint16_t>(
            *this, imm, detail::ccu_arithmetic_operator_type::addition);
    }

    void operator+=(const variable& var) const;

    // addr += addr
    void operator+=(const address& other) const;

    ccu_address_handle handle{0};

private:
    explicit address(detail::no_alloc_tag) {}
    template <typename u>
    friend class array;
    friend class local_addr;
    friend class remote_addr;
};

// variable + addr（交换律）
inline detail::ccu_arithmetic_operator<variable, address> operator+(const variable& var, const address& addr)
{
    return detail::ccu_arithmetic_operator<variable, address>(
        var, addr, detail::ccu_arithmetic_operator_type::addition);
}

} // namespace ccu
} // namespace asc

template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::address, asc::ccu::address>::check() const
{}
template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::address, asc::ccu::variable>::check() const
{}
template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::variable, asc::ccu::address>::check() const
{}
template <>
inline void asc::ccu::detail::ccu_arithmetic_operator<asc::ccu::address, uint16_t>::check() const
{}

#endif // CCU_ADDRESS_H
