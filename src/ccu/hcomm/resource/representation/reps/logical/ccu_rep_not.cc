/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/ccu_rep_v1.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
namespace asccomm {
namespace CcuRep {

CcuRepNot::CcuRepNot(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Variable& varB)
    : subType(NotSubType::VAR_EQUALS_NOT_VAR), varB(varB), varC(varC), insGenPtr(insGenPtr)
{
    type = CcuRepType::NOT;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepNot::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& curInstrId, const TransDep& dep)
{
    asccomm::CheckCcuNotNull(instr, "[CcuRepNot::Translate] instr is nullptr!");
    this->instrId = curInstrId;
    translated = true;
    instrCount = insGenPtr->GetInstrCount(type);
    CHK_PRT_THROW(
        insGenPtr->CcuRepNotTranslate(ccuKernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepNot][Translate] failed to translate for instrId[%u]", instrId), CcuResult::CCU_E_INTERNAL,
        "CcuRepNot translate failed");
    CHK_PRT_THROW(
        (curInstrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepNot::Translate]uint16 integer overflow occurs, curInstrId = [%hu], instrCount = [%hu]", curInstrId,
            instrCount),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    curInstrId += instrCount;
    return translated;
}

std::string CcuRepNot::Describe()
{
    switch (subType) {
        case NotSubType::VAR_EQUALS_NOT_VAR: {
            return asccomm::FormatCcuMessage("Variable[%u] = ~Variable[%u]", varC.Id(), varB.Id());
        }
        default: {
            return asccomm::FormatCcuMessage("Invalid Not");
        }
    }
}

}; // namespace CcuRep
}; // namespace asccomm
