/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"
#include "ccu_assist_v1.h"
#include <climits>

#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepSetLoop::CcuRepSetLoop(const Variable &loopParam, const Executor &executor, const Variable &var)
    : loopParam(loopParam), executor(executor), var(var)
{
    type       = CcuRepType::SET_LOOP;
    instrCount = 2;  // set loop 指令数量为2
}

bool CcuRepSetLoop::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

    LoadImdToXnInstr(instr++, loopParam.Id(), GetLoopParam(executor.Id(), 0, 0));
    LoadXXInstr(instr++, loopParam.Id(), loopParam.Id(), var.Id());

    if (instrId > USHRT_MAX - instrCount) {
        Hccl::THROW<Hccl::InternalException>(Hccl::StringFormat("[CcuRepSetLoop][Translate] instrId[%u] + instrCount[%u] exceeds the "
            "maximum value of unsigned short int.", instrId, instrCount));
    }
    instrId += instrCount;

    return translated;
}

std::string CcuRepSetLoop::Describe()
{
    return Hccl::StringFormat("loopParam[%u] = var[%u], execute on LoopEngine[%u]", loopParam.Id(), var.Id(), executor.Id());
}

}; // namespace CcuRep
}; // namespace hcomm