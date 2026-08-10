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
#include "string_util.h"
#include "hcomm/common/ccu_exception.h"

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace hcomm {
namespace CcuRep {

using namespace Hccl;

CcuRepLoadArg::CcuRepLoadArg(CcuInsGeneraterBase* insGenPtr, const Variable& var, uint16_t argId, uint16_t fullArgId)
    : insGeneratorPtr_(insGenPtr), var(var), argId(argId), fullArgId(fullArgId)
{
    type = CcuRepType::LOAD_ARG;
    instrCount = insGeneratorPtr_->GetInstrCount(type);
}

bool CcuRepLoadArg::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    CHK_RET_THROW(
        CcuResult::CCU_E_INTERNAL,
        hcomm::FormatCcuMessage("[CcuRepLoadArg][%s] failed to translate repLoadArg for instrId[%u] ", __func__, instrId),
        insGeneratorPtr_->CcuRepLoadArgTranslate(ccuKernel, instr, instrId, this, dep));

    instrId += instrCount;

    return translated;
}

std::string CcuRepLoadArg::Describe()
{
    return hcomm::FormatCcuMessage("Variable[%u] = Arg[%u] (slot=%u)", var.Id(), fullArgId, argId);
}

}; // namespace CcuRep
}; // namespace hcomm
