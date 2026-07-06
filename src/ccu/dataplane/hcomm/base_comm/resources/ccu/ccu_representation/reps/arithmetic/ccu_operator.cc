/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation datatype implementation file
 * Author: sunzhepeng
 * Create: 2024-07-06
 */

#include "ccu_operator_v1.h"
#include "ccu_datatype_v1.h"

namespace hcomm {
namespace CcuRep {

template <> void CcuArithmeticOperator<Variable, Variable>::Check() const
{
    // nothing
}
template <> void CcuArithmeticOperator<Variable, Address>::Check() const
{
    // nothing
}
template <> void CcuArithmeticOperator<Address, Address>::Check() const
{
    // nothing
}

template <> void CcuRelationalOperator<Variable, uint64_t>::Check() const
{
    // nothing
}

CcuArithmeticOperator<Variable, Variable> Variable::operator+(const Variable &varB) const
{
    return CcuArithmeticOperator<Variable, Variable>(*this, varB, CcuArithmeticOperatorType::ADDITION);
}
CcuArithmeticOperator<Variable, Address> Variable::operator+(const Address &addrB) const
{
    return CcuArithmeticOperator<Variable, Address>(*this, addrB, CcuArithmeticOperatorType::ADDITION);
}
CcuArithmeticOperator<Variable, Address> Address::operator+(const Variable &varB) const
{
    return CcuArithmeticOperator<Variable, Address>(varB, *this, CcuArithmeticOperatorType::ADDITION);
}
CcuArithmeticOperator<Address, Address> Address::operator+(const Address &addrB) const
{
    return CcuArithmeticOperator<Address, Address>(*this, addrB, CcuArithmeticOperatorType::ADDITION);
}

CcuRelationalOperator<Variable, uint64_t> Variable::operator!=(uint64_t immediate) const
{
    return CcuRelationalOperator<Variable, uint64_t>(*this, immediate, CcuRelationalOperatorType::NOT_EQUAL);
}

CcuRelationalOperator<Variable, uint64_t> Variable::operator==(uint64_t immediate) const
{
    return CcuRelationalOperator<Variable, uint64_t>(*this, immediate, CcuRelationalOperatorType::EQUAL);
}

}; // namespace CcuRep
}; // namespace hcomm