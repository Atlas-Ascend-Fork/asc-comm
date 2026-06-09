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

namespace hcomm {
namespace CcuRep {

template <typename lhsT, typename rhsT> class CcuOperator {
public:
    CcuOperator(lhsT lhs, rhsT rhs) : lhs(lhs), rhs(rhs)
    {
    }
    lhsT lhs;
    rhsT rhs;
};

enum class CcuArithmeticOperatorType { ADDITION, INVALID };

template <typename lhsT, typename rhsT> class CcuArithmeticOperator : public CcuOperator<lhsT, rhsT> {
public:
    CcuArithmeticOperator(lhsT lhs, rhsT rhs, CcuArithmeticOperatorType type)
        : CcuOperator<lhsT, rhsT>(lhs, rhs), type(type)
    {
        Check();
    }
    void Check() const
    {
        // Hccl::THROW<Hccl::CcuApiException>("Invalid Arithmetic Operator");
        throw std::runtime_error("Invalid Arithmetic Operator");
    }

    CcuArithmeticOperatorType type{CcuArithmeticOperatorType::INVALID};
};

enum class CcuRelationalOperatorType { EQUAL, NOT_EQUAL, INVALID };

template <typename lhsT, typename rhsT> class CcuRelationalOperator : public CcuOperator<lhsT, rhsT> {
public:
    CcuRelationalOperator(lhsT lhs, rhsT rhs, CcuRelationalOperatorType type)
        : CcuOperator<lhsT, rhsT>(lhs, rhs), type(type)
    {
        Check();
    }
    void Check() const
    {
        // Hccl::THROW<Hccl::CcuApiException>("Invalid Relational Operator");
        throw std::runtime_error("Invalid Relational Operator");
    }

    CcuRelationalOperatorType type{CcuRelationalOperatorType::INVALID};
};

}; // namespace CcuRep
}; // namespace hcomm

#endif // _CCU_OPERATOR