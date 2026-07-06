/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"

#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepNop::CcuRepNop()
{
    type       = CcuRepType::NOP;
    instrCount = 1;
}

bool CcuRepNop::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

    LoadImdToXnInstr(instr++, dep.reserveXnId, 0);

    instrId += instrCount;

    return translated;
}

std::string CcuRepNop::Describe()
{
    return Hccl::StringFormat("Nop");
}

}; // namespace CcuRep
}; // namespace hcomm