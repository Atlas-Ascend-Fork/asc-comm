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
#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asccomm {
namespace CcuRep {

CcuRepBufLocRead::CcuRepBufLocRead(
    CcuInsGeneraterBase* insGenPtr, LocalAddr src, CcuBuf dst, Variable len, CompletedEvent sem, uint16_t mask)
    : insGenPtr(insGenPtr), src(src), dst(dst), len(len), sem(sem), mask(mask)
{
    type = CcuRepType::BUF_LOC_READ;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepBufLocRead::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    CHK_PRT_THROW(
        insGenPtr->CcuRepBufLocReadTranslate(ccuKernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepBufLocRead][Translate] failed to translate for instrId[%u]", instrId),
        CcuResult::CCU_E_INTERNAL, "CcuRepBufLocRead translate failed");
    instrId += instrCount;

    return translated;
}

std::string CcuRepBufLocRead::Describe()
{
    return asccomm::FormatCcuMessage(
        "Read Loc Mem[%u] To CcuBuf[%u], len[%u], sem[%u], mask[%04x]", src.addr.Id(), dst.Id(), len.Id(), sem.Id(),
        mask);
}

}; // namespace CcuRep
}; // namespace asccomm
