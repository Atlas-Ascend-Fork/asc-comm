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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/common/ccu_exception.h"
namespace hcomm {
namespace CcuRep {

CcuRepShL::CcuRepShL(CcuInsGeneraterBase* insGenPtr, const Variable& varD, const Variable& varN, const Variable& varM)
    : subType(ShiftSubType::VAR_EQUALS_VAR_SHIFT_VAR),
      shiftType(ShiftType::LOGICAL_SHIFT),
      varN(varN),
      varM(varM),
      varD(varD),
      insGenPtr(insGenPtr)
{
    type = CcuRepType::SHL;
    instrCount = insGenPtr->GetInstrCount(type);
}

CcuRepShL::CcuRepShL(CcuInsGeneraterBase* insGenPtr, const Variable& varD, const Variable& varM)
    : subType(ShiftSubType::VAR_SHIFT_ASSIGN_VAR),
      shiftType(ShiftType::LOGICAL_SHIFT),
      varM(varM),
      varD(varD),
      insGenPtr(insGenPtr)
{
    type = CcuRepType::SHL;
    instrCount = insGenPtr->GetInstrCount(type);
}

CcuRepShL::CcuRepShL(CcuInsGeneraterBase* insGenPtr, const Address& addrD, const Variable& varN, const Variable& varM)
    : subType(ShiftSubType::ADDR_EQUALS_VAR_SHIFT_VAR),
      shiftType(ShiftType::LOGICAL_SHIFT),
      varN(varN),
      varM(varM),
      addrD(addrD),
      insGenPtr(insGenPtr)
{
    type = CcuRepType::SHL;
    instrCount = insGenPtr->GetInstrCount(type);
}

CcuRepShL::CcuRepShL(CcuInsGeneraterBase* insGenPtr, const Address& addrD, const Variable& varM)
    : subType(ShiftSubType::ADDR_SHIFT_ASSIGN_VAR),
      shiftType(ShiftType::LOGICAL_SHIFT),
      varM(varM),
      addrD(addrD),
      insGenPtr(insGenPtr)
{
    type = CcuRepType::SHL;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepShL::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& curInstrId, const TransDep& dep)
{
    hcomm::CheckCcuNotNull(instr, "[CcuRepShL::Translate] instr is nullptr!");
    this->instrId = curInstrId;
    translated = true;
    instrCount = insGenPtr->GetInstrCount(type);
    CHK_PRT_THROW(
        insGenPtr->CcuRepShLTranslate(ccuKernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepShL][Translate] failed to translate for instrId[%u]", instrId), CcuResult::CCU_E_INTERNAL,
        "CcuRepShL translate failed");
    CHK_PRT_THROW(
        (curInstrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepShL::Translate]uint16 integer overflow occurs, curInstrId = [%hu], instrCount = [%hu]", curInstrId,
            instrCount),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    curInstrId += instrCount;
    return translated;
}

std::string CcuRepShL::Describe()
{
    switch (subType) {
        case ShiftSubType::VAR_EQUALS_VAR_SHIFT_VAR: {
            return hcomm::FormatCcuMessage("Variable[%u] = Variable[%u] << Variable[%u]", varD.Id(), varN.Id(), varM.Id());
        }
        case ShiftSubType::VAR_SHIFT_ASSIGN_VAR: {
            return hcomm::FormatCcuMessage("Variable[%u] <<= Variable[%u]", varD.Id(), varM.Id());
        }
        case ShiftSubType::ADDR_EQUALS_VAR_SHIFT_VAR: {
            return hcomm::FormatCcuMessage("Address[%u] = Variable[%u] << Variable[%u]", addrD.Id(), varN.Id(), varM.Id());
        }
        case ShiftSubType::ADDR_SHIFT_ASSIGN_VAR: {
            return hcomm::FormatCcuMessage("Address[%u] <<= Variable[%u]", addrD.Id(), varM.Id());
        }
        default: {
            return hcomm::FormatCcuMessage("Invalid Shift");
        }
    }
    return hcomm::FormatCcuMessage("Invalid Shift");
}

}; // namespace CcuRep
}; // namespace hcomm
