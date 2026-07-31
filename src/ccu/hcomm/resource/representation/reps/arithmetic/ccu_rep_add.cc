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
#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

void CcuRepAdd::SetCommonInfo()
{
    type = CcuRepType::ADD;
    instrCount = 1;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrC, const Address& addrA, const Variable& varB)
    : insGenPtr(insGenPtr), subType(AddSubType::ADDR_PLUS_VAR_TO_ADDR), addrA(addrA), addrC(addrC), varB(varB)
{
    SetCommonInfo();
    supportCcuV1 = true;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrC, const Address& addrA, const Address& addrB)
    : insGenPtr(insGenPtr), subType(AddSubType::ADDR_PLUS_ADDR_TO_ADDR), addrA(addrA), addrB(addrB), addrC(addrC)
{
    SetCommonInfo();
    supportCcuV1 = true;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Variable& varA, const Variable& varB)
    : insGenPtr(insGenPtr), subType(AddSubType::VAR_PLUS_VAR_TO_VAR), varA(varA), varB(varB), varC(varC)
{
    SetCommonInfo();
    supportCcuV1 = true;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrA, const Variable& offset)
    : insGenPtr(insGenPtr), subType(AddSubType::SELF_ADD_ADDRESS), addrA(addrA), varB(offset)
{
    SetCommonInfo();
    supportCcuV1 = true;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varA, const Variable& offset)
    : insGenPtr(insGenPtr), subType(AddSubType::SELF_ADD_VARIABLE), varA(varA), varB(offset)
{
    SetCommonInfo();
    supportCcuV1 = true;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::SELF_ADD_IMMED_VARIABLE), varA(varA), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Variable& varA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::VAR_PLUS_IMMED_TO_VAR), varA(varA), varC(varC), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrC, const Address& addrA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::ADDR_PLUS_IMMED_TO_ADDR), addrA(addrA), addrC(addrC), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::SELF_ADD_IMMED_ADDRESS), addrA(addrA), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrC, const Variable& varA, const Variable& varB)
    : insGenPtr(insGenPtr), subType(AddSubType::VAR_PLUS_VAR_TO_ADDR), addrC(addrC), varA(varA), varB(varB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Address& addrC, const Variable& varA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::VAR_PLUS_IMMED_TO_ADDR), addrC(addrC), varA(varA), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Address& addrA, const Address& addrB)
    : insGenPtr(insGenPtr), subType(AddSubType::ADDR_PLUS_ADDR_TO_VAR), addrA(addrA), addrB(addrB), varC(varC)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

CcuRepAdd::CcuRepAdd(CcuInsGeneraterBase* insGenPtr, const Variable& varC, const Address& addrA, const uint16_t immedB)
    : insGenPtr(insGenPtr), subType(AddSubType::ADDR_PLUS_IMMED_TO_VAR), addrA(addrA), varC(varC), immedB(immedB)
{
    SetCommonInfo();
    supportCcuV1 = false;
}

void CcuRepAdd::ValidateInsGenPtrForAdd()
{
    CcuInsGeneraterV1* tmpPtrV1 = dynamic_cast<CcuInsGeneraterV1*>(insGenPtr);
    CHK_PRT_THROW(
        (tmpPtrV1 && !supportCcuV1),
        HCCL_ERROR("[CcuRepAdd][%s]Cannot translate CcuRepAdd for A5 when supportCcuV1 is false", __func__),
        Hccl::CcuApiException, "tmpPtrV1 does not match supportCcuV1");
}

bool CcuRepAdd::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    ValidateInsGenPtrForAdd();
    Hccl::CHECK_NULLPTR(instr, "[CcuRepAdd::Translate] instr is nullptr!");
    this->instrId = instrId;
    translated = true;

    insGenPtr->CcuRepAddTranslate(ccuKernel, instr, this, dep);
    CHK_PRT_THROW(
        (instrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepAdd::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId,
            instrCount),
        Hccl::InternalException, "integer overflow");
    instrId += instrCount;
    return translated;
}

std::string CcuRepAdd::Describe()
{
    switch (subType) {
        case AddSubType::ADDR_PLUS_VAR_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Address[%u] + Variable[%u]", addrC.Id(), addrA.Id(), varB.Id());
        }
        case AddSubType::ADDR_PLUS_ADDR_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Address[%u] + Address[%u]", addrC.Id(), addrA.Id(), addrB.Id());
        }
        case AddSubType::VAR_PLUS_VAR_TO_VAR: {
            return Hccl::StringFormat("Variable[%u] = Variable[%u] + Variable[%u]", varC.Id(), varA.Id(), varB.Id());
        }
        case AddSubType::SELF_ADD_ADDRESS: {
            return Hccl::StringFormat("Address[%u] += Variable[%u]", addrA.Id(), varB.Id());
        }
        case AddSubType::SELF_ADD_VARIABLE: {
            return Hccl::StringFormat("Variable[%u] += Variable[%u]", varA.Id(), varB.Id());
        }
        case AddSubType::SELF_ADD_IMMED_VARIABLE: {
            return Hccl::StringFormat("Variable[%u] += Immed[%u]", varA.Id(), immedB);
        }
        case AddSubType::VAR_PLUS_IMMED_TO_VAR: {
            return Hccl::StringFormat("Variable[%u] = Variable[%u] + Immed[%u]", varC.Id(), varA.Id(), immedB);
        }
        case AddSubType::ADDR_PLUS_IMMED_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] += Address[%u] + Immed[%hu]", addrC.Id(), addrA.Id(), immedB);
        }
        case AddSubType::VAR_PLUS_VAR_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Variable[%u] + Variable[%u]", addrC.Id(), varA.Id(), varB.Id());
        }
        case AddSubType::SELF_ADD_IMMED_ADDRESS: {
            return Hccl::StringFormat("Address[%u] += Immed[%u]", addrA.Id(), immedB);
        }
        case AddSubType::VAR_PLUS_IMMED_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] += varA[%u] + Immed[%hu]", addrC.Id(), varA.Id(), immedB);
        }
        case AddSubType::ADDR_PLUS_IMMED_TO_VAR: {
            return Hccl::StringFormat("Variable[%u] = Address[%u] + Immed[%u]", varC.Id(), addrA.Id(), immedB);
        }
        default: {
            return Hccl::StringFormat("Invalid Add");
        }
    }
}

Address CcuRepAdd::GetAddrA() { return addrA; }

Address CcuRepAdd::GetAddrB() { return addrB; }

Address CcuRepAdd::GetAddrC() { return addrC; }

Variable CcuRepAdd::GetVarA() { return varA; }

Variable CcuRepAdd::GetVarB() { return varB; }

Variable CcuRepAdd::GetVarC() { return varC; }

uint16_t CcuRepAdd::GetImmedB() { return immedB; }

AddSubType CcuRepAdd::GetSubType() { return subType; }
}; // namespace CcuRep
}; // namespace hcomm
