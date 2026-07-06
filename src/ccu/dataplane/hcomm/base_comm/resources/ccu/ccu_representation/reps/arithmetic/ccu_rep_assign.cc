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

CcuRepAssign::CcuRepAssign(const Variable &varA, uint64_t immediate)
    : subType(AssignSubType::IMD_TO_VARIABLE), immediate(immediate), varA(varA)
{
    type       = CcuRepType::ASSIGN;
    instrCount = 1;
}

CcuRepAssign::CcuRepAssign(const Address &addrA, uint64_t immediate)
    : subType(AssignSubType::IMD_TO_ADDR), immediate(immediate), addrA(addrA)
{
    type       = CcuRepType::ASSIGN;
    instrCount = 1;
}

CcuRepAssign::CcuRepAssign(const Address &addrA, const Variable &varA)
    : subType(AssignSubType::VAR_TO_ADDR), immediate(0), varA(varA), addrA(addrA)
{
    type       = CcuRepType::ASSIGN;
    instrCount = 1;
}

CcuRepAssign::CcuRepAssign(const Address &addrB, const Address &addrA)
    : subType(AssignSubType::ADDR_TO_ADDR), immediate(0), addrA(addrA), addrB(addrB)
{
    type       = CcuRepType::ASSIGN;
    instrCount = 1;
}

CcuRepAssign::CcuRepAssign(const Variable &varB, const Variable &varA)
    : subType(AssignSubType::VAR_TO_VAR), immediate(0), varA(varA), varB(varB)
{
    type       = CcuRepType::ASSIGN;
    instrCount = 1;
}

bool CcuRepAssign::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    Hccl::CHECK_NULLPTR(instr, "[CcuRepAssign::Translate] instr is nullptr!");
    this->instrId = instrId;
    translated    = true;

    switch (subType) {
        case AssignSubType::IMD_TO_VARIABLE: {
            LoadImdToXnInstr(instr++, varA.Id(), immediate);
            break;
        }
        case AssignSubType::IMD_TO_ADDR: {
            LoadImdToGSAInstr(instr++, addrA.Id(), immediate);
            break;
        }
        case AssignSubType::VAR_TO_ADDR: {
            LoadGSAXnInstr(instr++, addrA.Id(), dep.reserveGsaId, varA.Id());
            break;
        }
        case AssignSubType::ADDR_TO_ADDR: {
            LoadGSAGSAInstr(instr++, addrB.Id(), addrA.Id(), dep.reserveGsaId);
            break;
        }
        case AssignSubType::VAR_TO_VAR: {
            LoadXXInstr(instr++, varB.Id(), varA.Id(), dep.reserveXnId);
            break;
        }
        default: {
            Hccl::THROW<Hccl::CcuApiException>("Invalid Assign");
        }
    }
    
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
                        HCCL_ERROR("[CcuRepAssign::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
                          Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepAssign::Describe()
{
    switch (subType) {
        case AssignSubType::IMD_TO_VARIABLE: {
            return Hccl::StringFormat("Variable[%u] = Value[%lu]", varA.Id(), immediate);
        }
        case AssignSubType::IMD_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Value[%lu]", addrA.Id(), immediate);
        }
        case AssignSubType::VAR_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Variable[%u]", addrA.Id(), varA.Id());
        }
        case AssignSubType::ADDR_TO_ADDR: {
            return Hccl::StringFormat("Address[%u] = Address[%u]", addrB.Id(), addrA.Id());
        }
        case AssignSubType::VAR_TO_VAR: {
            return Hccl::StringFormat("Var[%u] = Var[%u]", varB.Id(), varA.Id());
        }
        default: {
            return Hccl::StringFormat("Invalid Assign");
        }
    }
}

}; // namespace CcuRep
}; // namespace hcomm