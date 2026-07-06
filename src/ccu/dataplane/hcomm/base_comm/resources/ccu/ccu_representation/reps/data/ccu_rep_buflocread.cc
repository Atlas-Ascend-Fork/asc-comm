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

CcuRepBufLocRead::CcuRepBufLocRead(LocalAddr src, CcuBuf dst, Variable len, CompletedEvent sem, uint16_t mask)
    : src(src), dst(dst), len(len), sem(sem), mask(mask)
{
    type       = CcuRepType::BUF_LOC_READ;
    instrCount = 1;
}

bool CcuRepBufLocRead::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    this->instrId = instrId;
    translated    = true;

        TransLocMemToLocMSInstr(instr++, dst.Id(), src.addr.Id(), src.token.Id(), len.Id(), dep.reserveChannalId[0],
                                sem.Id(), mask, 0, 0, 1, 1);
    
    instrId += instrCount;

    return translated;
}

std::string CcuRepBufLocRead::Describe()
{
    return Hccl::StringFormat("Read Loc Mem[%u] To CcuBuf[%u], len[%u], sem[%u], mask[%04x]",
    src.addr.Id(), dst.Id(), len.Id(), sem.Id(), mask);
}

}; // namespace CcuRep
}; // namespace hcomm