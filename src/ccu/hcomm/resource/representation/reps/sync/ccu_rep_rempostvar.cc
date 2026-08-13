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
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asccomm {
namespace CcuRep {

CcuRepRemPostVar::CcuRepRemPostVar(
    CcuInsGeneraterBase* insGenPtr, Variable param, const ChannelHandle channel, uint16_t paramIndex, uint16_t semIndex,
    uint16_t mask)
    : insGenPtr(insGenPtr), param(param), channel(channel), paramIndex(paramIndex), semIndex(semIndex), mask(mask)
{
    type = CcuRepType::REM_POST_VAR;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepRemPostVar::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    CHK_PRT_THROW(
        insGenPtr->CcuRepRemPostVarTranslate(ccuKernel, instr, this) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepRemPostVar][Translate] failed to translate for instrId[%u]", instrId),
        CcuResult::CCU_E_INTERNAL, "CcuRepRemPostVar translate failed");
    CHK_PRT_THROW(
        (instrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepRemPostVar::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId,
            instrCount),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemPostVar::Describe()
{
    return asccomm::FormatCcuMessage(
        "Post Variable[%u] To ParamIndex[%u], Use semIndex[%u] and mask[%04x]", param.Id(), paramIndex, semIndex, mask);
}

}; // namespace CcuRep
}; // namespace asccomm
