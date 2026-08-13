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
#include "string_util.h"
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
namespace hcomm {
namespace CcuRep {

CcuRepXor::CcuRepXor(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Variable& varA, const Variable& varB)
    : subType(XorSubType::VAR_XOR_VAR_TO_VAR), varA(varA), varB(varB), varC(varC), insGenPtr(insGenPtr)
{
    type = CcuRepType::XOR;
    instrCount = insGenPtr->GetInstrCount(type);
}

CcuRepXor::CcuRepXor(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Variable& varB)
    : subType(XorSubType::SELF_XOR_VAR_VARIABLE), varB(varB), varC(varC), insGenPtr(insGenPtr)
{
    type = CcuRepType::XOR;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepXor::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& curInstrId, const TransDep& dep)
{
    hcomm::CheckCcuNotNull(instr, "[CcuRepXor::Translate] instr is nullptr!");
    this->instrId = curInstrId;
    translated = true;
    instrCount = insGenPtr->GetInstrCount(type);
    insGenPtr->CcuRepXorTranslate(ccuKernel, instr, this, dep);
    CHK_PRT_THROW(
        (curInstrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepXor::Translate]uint16 integer overflow occurs, curInstrId = [%hu], instrCount = [%hu]", curInstrId,
            instrCount),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    curInstrId += instrCount;
    return translated;
}

std::string CcuRepXor::Describe()
{
    switch (subType) {
        case XorSubType::VAR_XOR_VAR_TO_VAR: {
            return hcomm::FormatCcuMessage("Variable[%u] = Variable[%u] ^ Variable[%u]", varC.Id(), varA.Id(), varB.Id());
        }
        case XorSubType::SELF_XOR_VAR_VARIABLE: {
            return hcomm::FormatCcuMessage("Variable[%u] ^= Variable[%u]", varC.Id(), varB.Id());
        }
        default: {
            return hcomm::FormatCcuMessage("Invalid Xor");
        }
    }
}

}; // namespace CcuRep
}; // namespace hcomm
