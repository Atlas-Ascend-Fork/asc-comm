/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/data/ccu_rep_read_v1.h"
#include "hcomm/resource/representation/ccu_rep_v1.h"

#include "hcomm_c_adpt.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generator_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

CcuRepRead::CcuRepRead(
    CcuInsGeneratorBase* insGenPtr, const ChannelHandle channel, LocalAddr loc, RemoteAddr rem, Variable len,
    CompletedEvent sem, uint16_t mask)
    : insGenPtr(insGenPtr), channel(channel), loc(loc), rem(rem), len(len), sem(sem), mask(mask)
{
    type = CcuRepType::READ;
    instrCount = 1;
}

CcuRepRead::CcuRepRead(
    CcuInsGeneratorBase* insGenPtr, const ChannelHandle channel, LocalAddr loc, RemoteAddr rem, Variable len,
    uint16_t dataType, uint16_t opType, CompletedEvent sem, uint16_t mask)
    : insGenPtr(insGenPtr),
      channel(channel),
      loc(loc),
      rem(rem),
      len(len),
      sem(sem),
      mask(mask),
      dataType(dataType),
      opType(opType),
      reduceFlag(1)
{
    type = CcuRepType::READ;
    instrCount = insGenPtr->GetInstrCount(type);
}

bool CcuRepRead::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    insGenPtr->CcuRepReadTranslate(ccuKernel, instr, this);
    instrId += instrCount;

    return translated;
}

std::string CcuRepRead::Describe()
{
    return hcomm::StringFormat(
        "Read LocalAddr[%u] To RemoteAddr[%u], length[%u], set sem[%u] with mask[%04x], dataType[%u], opType[%u]",
        rem.addr.Id(), loc.addr.Id(), len.Id(), sem.Id(), mask, dataType, opType);
}

}; // namespace CcuRep
}; // namespace hcomm
