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

CcuRepLocCpy::CcuRepLocCpy(LocalAddr dst, LocalAddr src, Variable len, CompletedEvent sem, uint16_t mask)
    : dst(dst), src(src), len(len), sem(sem), mask(mask)
{
    type       = CcuRepType::LOCAL_CPY;
    instrCount = 1;
}

CcuRepLocCpy::CcuRepLocCpy(LocalAddr dst, LocalAddr src, Variable len, uint16_t dataType, uint16_t opType, CompletedEvent sem,
                           uint16_t mask)
    : dst(dst), src(src), len(len), sem(sem), mask(mask), dataType(dataType), opType(opType), reduceFlag(1)
{
    type       = CcuRepType::LOCAL_REDUCE;
    instrCount = 1;
}

bool CcuRepLocCpy::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

    if (reduceFlag == 0) {
        TransLocMemToLocMemInstr(instr++, dst.addr.Id(), dst.token.Id(), src.addr.Id(), src.token.Id(), len.Id(),
                                 dep.reserveChannalId[0], sem.Id(), mask, 0, 0, 1, 1);
    } else {
        // 这个翻译需要验证
        TransLocMemToRmtMemInstr(instr++, dst.addr.Id(), dst.token.Id(), src.addr.Id(), src.token.Id(), len.Id(),
                                 dep.reserveChannalId[0], dataType, opType, sem.Id(), mask, 0, 0, 1, 1, reduceFlag);
    }

    instrId += instrCount;

    return translated;
}

std::string CcuRepLocCpy::Describe()
{
    return Hccl::StringFormat(
        "Read LocalAddr[%u] to LocalAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
        src.addr.Id(), dst.addr.Id(), len.Id(), sem.Id(), mask, dataType, opType);
}

}; // namespace CcuRep
}; // namespace hcomm