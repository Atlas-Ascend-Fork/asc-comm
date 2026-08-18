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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"

namespace hcomm {
namespace CcuRep {

CcuRepRemPostSem::CcuRepRemPostSem(
    CcuInsGeneraterBase* insGenPtr, const ChannelHandle channel, uint16_t semIndex, uint16_t mask)
    : insGenPtr(insGenPtr), channel(channel), semIndex(semIndex), mask(mask)
{
    type = CcuRepType::REM_POST_SEM;
    instrCount = insGenPtr->GetInstrCount(type);
}

uint32_t CcuRepRemPostSem::GetId() { return signalId; }

bool CcuRepRemPostSem::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    CHK_PRT_THROW(
        insGenPtr->CcuRepRemPostSemTranslate(ccuKernel, instr, this, dep) != CcuResult::CCU_SUCCESS,
        HCCL_ERROR("[CcuRepRemPostSem][Translate] failed to translate for instrId[%u]", instrId),
        CcuResult::CCU_E_INTERNAL, "CcuRepRemPostSem translate failed");
    CHK_PRT_THROW(
        (instrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepRemPostSem::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId,
            instrCount),
        CcuResult::CCU_E_INTERNAL, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemPostSem::Describe()
{
    return hcomm::FormatCcuMessage("Post, Use semIndex[%u] and mask[%04x]", semIndex, mask);
}

}; // namespace CcuRep
}; // namespace hcomm
