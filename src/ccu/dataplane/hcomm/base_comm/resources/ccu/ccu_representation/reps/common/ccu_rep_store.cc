/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu representation store implementation file
 * Author: zhanhaifeng
 * Create: 2025-03-21
 */

#include "ccu_rep_v1.h"
#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepStore::CcuRepStore(const Variable &var, uint64_t addr, uint32_t num) : var(var), addr(addr), num(num)
{
    type       = CcuRepType::STORE;
    instrCount = 7; // 7: Store包含7条指令
}

bool CcuRepStore::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId    = instrId;
    translated       = true;
    uint64_t varAddr = dep.xnBaseAddr + CCU_RESOURCE_XN_PER_SIZE * var.Id();

    LoadImdToGSAInstr(instr++, dep.commGsa[0], addr);
    LoadImdToGSAInstr(instr++, dep.commGsa[1], varAddr);
    LoadImdToXnInstr(instr++, dep.commXn[0], dep.memTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[1], dep.ccuResSpaceTokenInfo, CCU_LOAD_TO_XN_SEC_INFO);
    LoadImdToXnInstr(instr++, dep.commXn[2], CCU_RESOURCE_XN_PER_SIZE * num);
    TransLocMemToLocMemInstr(instr++, dep.commGsa[0], dep.commXn[0], dep.commGsa[1], dep.commXn[1], dep.commXn[2],
                             dep.reserveChannalId[0], dep.commSignal, mask, 0, 0, 1, 1);
    SetCKEInstr(instr++, 0, 0, dep.commSignal, mask, 1);
    instrId += instrCount;

    return translated;
}

std::string CcuRepStore::Describe()
{
    return Hccl::StringFormat("Store([%u], [%llu], [%u])", var.Id(), addr, num);
}

}; // namespace CcuRep
}; // namespace hcomm