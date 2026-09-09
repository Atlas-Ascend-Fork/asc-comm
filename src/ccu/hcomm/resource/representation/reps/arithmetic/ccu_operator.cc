/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/arithmetic/ccu_operator_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"

namespace asc {
namespace ccu_rep {

template <>
void ccu_arithmetic_operator<variable, variable>::check() const
{
    // nothing
}
template <>
void ccu_arithmetic_operator<variable, address>::check() const
{
    // nothing
}
template <>
void ccu_arithmetic_operator<variable, uint16_t>::check() const
{
    // nothing
}
template <>
void ccu_arithmetic_operator<address, address>::check() const
{
    // nothing
}
template <>
void ccu_arithmetic_operator<address, uint16_t>::check() const
{
    // nothing
}

template <>
void ccu_relational_operator<variable, uint64_t>::check() const
{
    // nothing
}
template <>
void ccu_relational_operator<variable, variable>::check() const
{
    // nothing
}

ccu_arithmetic_operator<variable, variable> variable::operator+(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, variable>(*this, var_b, ccu_arithmetic_operator_type::addition);
}
ccu_arithmetic_operator<variable, address> variable::operator+(const address& addr_b) const
{
    return ccu_arithmetic_operator<variable, address>(*this, addr_b, ccu_arithmetic_operator_type::addition);
}

ccu_arithmetic_operator<variable, uint16_t> variable::operator+(const uint16_t offset) const
{
    return ccu_arithmetic_operator<variable, uint16_t>(*this, offset, ccu_arithmetic_operator_type::addition);
}

ccu_arithmetic_operator<variable, variable> variable::operator*(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, variable>(*this, var_b, ccu_arithmetic_operator_type::multiplication);
}
ccu_arithmetic_operator<variable, address> variable::operator*(const address& addr_b) const
{
    return ccu_arithmetic_operator<variable, address>(*this, addr_b, ccu_arithmetic_operator_type::multiplication);
}
ccu_arithmetic_operator<variable, uint16_t> variable::operator*(const uint16_t offset) const
{
    return ccu_arithmetic_operator<variable, uint16_t>(*this, offset, ccu_arithmetic_operator_type::multiplication);
}

ccu_arithmetic_operator<variable, variable> variable::operator-(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, variable>(*this, var_b, ccu_arithmetic_operator_type::subtraction);
}
ccu_arithmetic_operator<variable, address> variable::operator-(const address& addr_b) const
{
    return ccu_arithmetic_operator<variable, address>(*this, addr_b, ccu_arithmetic_operator_type::subtraction);
}
ccu_arithmetic_operator<variable, uint16_t> variable::operator-(const uint16_t offset) const
{
    return ccu_arithmetic_operator<variable, uint16_t>(*this, offset, ccu_arithmetic_operator_type::subtraction);
}

ccu_arithmetic_operator<variable, address> address::operator+(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, address>(var_b, *this, ccu_arithmetic_operator_type::addition);
}
ccu_arithmetic_operator<address, address> address::operator+(const address& addr_b) const
{
    return ccu_arithmetic_operator<address, address>(*this, addr_b, ccu_arithmetic_operator_type::addition);
}

ccu_arithmetic_operator<address, uint16_t> address::operator+(const uint16_t offset) const
{
    return ccu_arithmetic_operator<address, uint16_t>(*this, offset, ccu_arithmetic_operator_type::addition);
}

ccu_arithmetic_operator<variable, address> address::operator*(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, address>(var_b, *this, ccu_arithmetic_operator_type::multiplication);
}
ccu_arithmetic_operator<address, address> address::operator*(const address& addr_b) const
{
    return ccu_arithmetic_operator<address, address>(*this, addr_b, ccu_arithmetic_operator_type::multiplication);
}
ccu_arithmetic_operator<address, uint16_t> address::operator*(const uint16_t offset) const
{
    return ccu_arithmetic_operator<address, uint16_t>(*this, offset, ccu_arithmetic_operator_type::multiplication);
}

ccu_arithmetic_operator<variable, address> address::operator-(const variable& var_b) const
{
    return ccu_arithmetic_operator<variable, address>(var_b, *this, ccu_arithmetic_operator_type::subtraction);
}
ccu_arithmetic_operator<address, address> address::operator-(const address& addr_b) const
{
    return ccu_arithmetic_operator<address, address>(*this, addr_b, ccu_arithmetic_operator_type::subtraction);
}
ccu_arithmetic_operator<address, uint16_t> address::operator-(const uint16_t offset) const
{
    return ccu_arithmetic_operator<address, uint16_t>(*this, offset, ccu_arithmetic_operator_type::subtraction);
}

ccu_relational_operator<variable, uint64_t> variable::operator!=(uint64_t immediate) const
{
    return ccu_relational_operator<variable, uint64_t>(*this, immediate, ccu_relational_operator_type::not_equal);
}

ccu_relational_operator<variable, uint64_t> variable::operator==(uint64_t immediate) const
{
    return ccu_relational_operator<variable, uint64_t>(*this, immediate, ccu_relational_operator_type::equal);
}

ccu_relational_operator<variable, uint64_t> variable::operator<=(uint64_t immediate) const
{
    return ccu_relational_operator<variable, uint64_t>(*this, immediate, ccu_relational_operator_type::less_equal);
}

ccu_relational_operator<variable, uint64_t> variable::operator>(uint64_t immediate) const
{
    return ccu_relational_operator<variable, uint64_t>(*this, immediate, ccu_relational_operator_type::greater_than);
}

ccu_relational_operator<variable, variable> variable::operator<=(const variable& var_b) const
{
    return ccu_relational_operator<variable, variable>(*this, var_b, ccu_relational_operator_type::less_equal);
}

ccu_relational_operator<variable, variable> variable::operator>(const variable& var_b) const
{
    return ccu_relational_operator<variable, variable>(*this, var_b, ccu_relational_operator_type::greater_than);
}

template <>
void ccu_logic_operator<variable, variable>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<variable, address>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<variable, uint64_t>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<address, address>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<address, uint16_t>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<variable>::check() const
{
    // nothing
}

template <>
void ccu_logic_operator<address>::check() const
{
    // nothing
}

ccu_logic_operator<variable, variable> variable::operator&(const variable& var_b) const
{
    return ccu_logic_operator<variable, variable>(*this, var_b, ccu_logic_operator_type::and_op);
}

ccu_logic_operator<variable, address> variable::operator&(const address& addr_b) const
{
    return ccu_logic_operator<variable, address>(*this, addr_b, ccu_logic_operator_type::and_op);
}

ccu_logic_operator<variable, variable> variable::operator|(const variable& var_b) const
{
    return ccu_logic_operator<variable, variable>(*this, var_b, ccu_logic_operator_type::or_op);
}

ccu_logic_operator<variable, address> variable::operator|(const address& addr_b) const
{
    return ccu_logic_operator<variable, address>(*this, addr_b, ccu_logic_operator_type::or_op);
}

ccu_logic_operator<variable, variable> variable::operator^(const variable& var_b) const
{
    return ccu_logic_operator<variable, variable>(*this, var_b, ccu_logic_operator_type::xor_op);
}

ccu_logic_operator<variable, address> variable::operator^(const address& addr_b) const
{
    return ccu_logic_operator<variable, address>(*this, addr_b, ccu_logic_operator_type::xor_op);
}

ccu_logic_operator<variable> variable::operator~() const
{
    return ccu_logic_operator<variable>(*this, ccu_logic_operator_type::not_op);
}

ccu_logic_operator<address, address> address::operator^(const address& addr_b) const
{
    return ccu_logic_operator<address, address>(*this, addr_b, ccu_logic_operator_type::xor_op);
}

ccu_logic_operator<variable, address> address::operator^(const variable& var_b) const
{
    return ccu_logic_operator<variable, address>(var_b, *this, ccu_logic_operator_type::xor_op);
}

ccu_logic_operator<address, address> address::operator&(const address& addr_b) const
{
    return ccu_logic_operator<address, address>(*this, addr_b, ccu_logic_operator_type::and_op);
}

ccu_logic_operator<variable, address> address::operator&(const variable& var_b) const
{
    return ccu_logic_operator<variable, address>(var_b, *this, ccu_logic_operator_type::and_op);
}

ccu_logic_operator<address, address> address::operator|(const address& addr_b) const
{
    return ccu_logic_operator<address, address>(*this, addr_b, ccu_logic_operator_type::or_op);
}

ccu_logic_operator<variable, address> address::operator|(const variable& var_b) const
{
    return ccu_logic_operator<variable, address>(var_b, *this, ccu_logic_operator_type::or_op);
}

template <>
void ccu_shift_operator<variable, variable>::check() const
{
    // nothing
}

ccu_shift_operator<variable, variable> variable::operator>>(const variable& other) const
{
    return ccu_shift_operator<variable, variable>(*this, other, ccu_shift_type::right);
}

ccu_shift_operator<variable, variable> variable::operator<<(const variable& other) const
{
    return ccu_shift_operator<variable, variable>(*this, other, ccu_shift_type::left);
}

}; // namespace ccu_rep
}; // namespace asc
