/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation load var file
 * Create: 2025-04-22
 */

#include "ccu_rep_v1.h"
#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepLoadVar::CcuRepLoadVar(const Variable &src, const Variable &var, uint32_t num) : src(src), var(var), num(num)
{
    type       = CcuRepType::LOAD_VAR;
    instrCount = 7; // 7: Load包含7条指令
}

bool CcuRepLoadVar::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    Hccl::CHECK_NULLPTR(instr, "[CcuRepLoadVar::Translate] instr is nullptr!");
    this->instrId    = instrId;
    translated       = true;
    uint64_t varAddr = dep.xnBaseAddr + CCU_RESOURCE_XN_PER_SIZE * var.Id();
    LoadImdToGSAInstr(instr++, dep.commGsa[0], varAddr);
    LoadGSAXnInstr(instr++, dep.commGsa[1], dep.reserveGsaId, src.Id());
    LoadImdToXnInstr(instr++, dep.commXn[0], dep.ccuResSpaceTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[1], dep.memTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[2], CCU_RESOURCE_XN_PER_SIZE * num);
    TransLocMemToLocMemInstr(instr++, dep.commGsa[0], dep.commXn[0], dep.commGsa[1], dep.commXn[1], dep.commXn[2],
                             dep.reserveChannalId[0], dep.commSignal, mask, 0, 0, 1, 1);
    SetCKEInstr(instr++, 0, 0, dep.commSignal, mask, 1);
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
                        HCCL_ERROR("[CcuRepLoadVar::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
                          Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepLoadVar::Describe()
{
    return Hccl::StringFormat("Load Var([%llu], [%u], [%u])", src.Id(), var.Id(), num);
}

}; // namespace CcuRep
}; // namespace hcomm