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
#include "hcomm/resource/microcode/ccu_assist_v1.h"


#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

CcuRepRemMem::CcuRepRemMem(CcuInsGeneraterBase* insGenPtr, const ChannelHandle channel, RemoteAddr rem)
    : insGenPtr(insGenPtr), channel(channel), rem(rem)
{
    type = CcuRepType::REM_MEM;
    instrCount = 2; // 指令数为2个
}

bool CcuRepRemMem::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    instrCount = insGenPtr->GetInstrCount(type);
    insGenPtr->CcuRepRemMemTranslate(ccuKernel, instr, this);
    instrId += instrCount;

    return translated;
}

std::string CcuRepRemMem::Describe() { return hcomm::FormatCcuMessage("Get Remote Buffer Addr and TokenInfo By Transport"); }

}; // namespace CcuRep
}; // namespace hcomm
