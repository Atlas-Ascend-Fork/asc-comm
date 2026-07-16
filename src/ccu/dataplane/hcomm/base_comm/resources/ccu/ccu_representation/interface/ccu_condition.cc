/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"
#include "ccu_interface_assist_v1.h"

#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

Condition::Condition(CcuRepContext *context, CcuRelationalOperator<Variable, uint64_t> rel) : context(context)
{
    std::string label = "Condition";
    endLabel          = std::make_shared<CcuRepJumpLabel>(label);

    // 当传入条件为真时, 执行Block, 对应不执行Jump
    if (rel.type == CcuRelationalOperatorType::NOT_EQUAL) {
        jump = std::make_shared<CcuRepJumpEQ>(label, CreateVariable(context), rel.lhs, rel.rhs);
    } else if (rel.type == CcuRelationalOperatorType::EQUAL) {
        jump = std::make_shared<CcuRepJumpNE>(label, CreateVariable(context), rel.lhs, rel.rhs);
    } else {
        Hccl::THROW<Hccl::CcuApiException>("Unsupported relational operation");
    }
    jump->Reference(endLabel);

    AppendToContext(context, jump);
}

Condition::~Condition()
{
    AppendToContext(context, endLabel);
}

bool Condition::Check() const
{
    return !isExecuted;
}

void Condition::Run()
{
    isExecuted = true;
}

}; // namespace CcuRep
}; // namespace hcomm