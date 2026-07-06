/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"

#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

void CcuRepAdd::SetCommonInfo()
{
    type       = CcuRepType::ADD;
    instrCount = 1;
}

CcuRepAdd::CcuRepAdd(const Address &addrC, const Address &addrA, const Variable &varB)
    : subType(AddSubType::ADDR_PLUS_VAR_TO_ADDR), addrA(addrA), addrC(addrC), varB(varB)
{
    SetCommonInfo();
}

CcuRepAdd::CcuRepAdd(const Address &addrC, const Address &addrA, const Address &addrB)
    : subType(AddSubType::ADDR_PLUS_ADDR_TO_ADDR), addrA(addrA), addrB(addrB), addrC(addrC)
{
    SetCommonInfo();
}

CcuRepAdd::CcuRepAdd(const Variable &varC, const Variable &varA, const Variable &varB)
    : subType(AddSubType::VAR_PLUS_VAR_TO_VAR), varA(varA), varB(varB),
      varC(varC)
{
    SetCommonInfo();
}

CcuRepAdd::CcuRepAdd(const Address &addrA, const Variable &offset)
    : subType(AddSubType::SELF_ADD_ADDRESS), addrA(addrA), varB(offset)
{
    SetCommonInfo();
}

CcuRepAdd::CcuRepAdd(const Variable &varA, const Variable &offset)
    : subType(AddSubType::SELF_ADD_VARIABLE), varA(varA), varB(offset)
{
    SetCommonInfo();
}

bool CcuRepAdd::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    Hccl::CHECK_NULLPTR(instr, "[CcuRepAdd::Translate] instr is nullptr!");
    this->instrId = instrId;
    translated    = true;

    switch (subType) {
        case AddSubType::ADDR_PLUS_VAR_TO_ADDR: {
            LoadGSAXnInstr(instr++, addrC.Id(), addrA.Id(), varB.Id());
            break;
        }
        case AddSubType::ADDR_PLUS_ADDR_TO_ADDR: {
            LoadGSAGSAInstr(instr++, addrC.Id(), addrA.Id(), addrB.Id());
            break;
        }
        case AddSubType::VAR_PLUS_VAR_TO_VAR: {
            LoadXXInstr(instr++, varC.Id(), varA.Id(), varB.Id());
            break;
        }
        case AddSubType::SELF_ADD_ADDRESS: {
            LoadGSAXnInstr(instr++, addrA.Id(), addrA.Id(), varB.Id());
            break;
        }
        case AddSubType::SELF_ADD_VARIABLE: {
            LoadXXInstr(instr++, varA.Id(), varA.Id(), varB.Id());
            break;
        }
        default: {
            Hccl::THROW<Hccl::CcuApiException>("Invalid Add");
        }
    }
    
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
                        HCCL_ERROR("[CcuRepAdd::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
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
        default: {
            return Hccl::StringFormat("Invalid Add");
        }
    }
}

}; // namespace CcuRep
}; // namespace hcomm