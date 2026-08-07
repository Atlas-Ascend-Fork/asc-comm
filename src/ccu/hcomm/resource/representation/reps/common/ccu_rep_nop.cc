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
#include "exception_util.h"
#include "ccu_api_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asccomm {
namespace CcuRep {

using namespace Hccl;

CcuRepNop::CcuRepNop(CcuInsGeneraterBase* insGeneratorPtr) : insGeneratorPtr_(insGeneratorPtr)
{
    type = CcuRepType::NOP;
    instrCount = 1;
}

bool CcuRepNop::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    CHK_RET_THROW(
        Hccl::CcuApiException,
        Hccl::StringFormat("[CcuRepNop][%s] failed to translate repNop for instrId[%u] ", __func__, instrId),
        insGeneratorPtr_->CcuRepNopTranslate(ccuKernel, instr, instrId, this, dep));

    instrId += instrCount;

    return translated;
}

std::string CcuRepNop::Describe() { return Hccl::StringFormat("Nop"); }

}; // namespace CcuRep
}; // namespace asccomm
