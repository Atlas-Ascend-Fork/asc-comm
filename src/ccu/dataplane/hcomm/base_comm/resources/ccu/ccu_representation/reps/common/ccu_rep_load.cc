/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation load implementation file
 * Author: zhanhaifeng
 * Create: 2025-03-21
 */

#include "ccu_rep_v1.h"
#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepLoad::CcuRepLoad(uint64_t addr, const Variable &var, uint32_t num) : var(var), addr(addr), num(num)
{
    type       = CcuRepType::LOAD;
    instrCount = 7; // 7: Load包含7条指令
}

bool CcuRepLoad::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    Hccl::CHECK_NULLPTR(instr, "[HCcuRepLoad::Translate] instr is nullptr!");
    this->instrId    = instrId;
    translated       = true;
    uint64_t varAddr = dep.xnBaseAddr + CCU_RESOURCE_XN_PER_SIZE * var.Id();

    LoadImdToGSAInstr(instr++, dep.commGsa[0], varAddr);
    LoadImdToGSAInstr(instr++, dep.commGsa[1], addr);
    LoadImdToXnInstr(instr++, dep.commXn[0], dep.ccuResSpaceTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[1], dep.memTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[2], CCU_RESOURCE_XN_PER_SIZE * num);
    TransLocMemToLocMemInstr(instr++, dep.commGsa[0], dep.commXn[0], dep.commGsa[1], dep.commXn[1], dep.commXn[2],
                             dep.reserveChannalId[0], dep.commSignal, mask, 0, 0, 1, 1);
    SetCKEInstr(instr++, 0, 0, dep.commSignal, mask, 1);
    CHK_PRT_THROW((instrId > UINT16_MAX - instrCount),
                        HCCL_ERROR("[CcuRepLoad::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId, instrCount),
                          Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepLoad::Describe()
{
    return Hccl::StringFormat("Load([%llu], [%u], [%u])", addr, var.Id(), num);
}

}; // namespace CcuRep
}; // namespace hcomm
