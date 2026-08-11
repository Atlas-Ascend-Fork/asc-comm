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
#include "exception_util.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "internal_exception.h"

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

    insGenPtr->CcuRepRemPostSemTranslate(ccuKernel, instr, this, dep);
    CHK_PRT_THROW(
        (instrId > UINT16_MAX - instrCount),
        HCCL_ERROR(
            "[CcuRepRemPostSem::Translate]uint16 integer overflow occurs, instrId = [%hu], instrCount = [%hu]", instrId,
            instrCount),
        Hccl::InternalException, "integer overflow");
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemPostSem::Describe()
{
    return Hccl::StringFormat("Post, Use semIndex[%u] and mask[%04x]", semIndex, mask);
}

}; // namespace CcuRep
}; // namespace hcomm
