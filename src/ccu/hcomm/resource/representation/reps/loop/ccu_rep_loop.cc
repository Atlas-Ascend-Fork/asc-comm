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
#include <climits>

#include "hcomm/common/ccu_exception.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

namespace asccomm {
namespace CcuRep {

CcuRepLoop::CcuRepLoop(CcuInsGeneraterBase* insGeneratorPtr, const std::string& label, const Variable& loopParam)
    : insGeneratorPtr_(insGeneratorPtr), label(label), loopParam(loopParam)
{
    type = CcuRepType::LOOP;
    instrCount = insGeneratorPtr_->GetInstrCount(type);
    supportCcuV1 = true;
}

CcuRepLoop::CcuRepLoop(
    CcuInsGeneraterBase* insGeneratorPtr, const std::string& label, const Variable& loopParam,
    const Variable& loopIterNum, const Variable& loopGsaOffset)
    : insGeneratorPtr_(insGeneratorPtr),
      label(label),
      loopParam(loopParam),
      loopIterNum(loopIterNum),
      loopGsaOffset(loopGsaOffset)
{
    type = CcuRepType::LOOP;
    instrCount = insGeneratorPtr_->GetInstrCount(type);
    supportCcuV1 = false; // loopParam按照A6格式填写，不适用A5
}

void CcuRepLoop::ValidateInsGeneratorForLoop()
{
    CcuInsGeneraterV1* tmpPtrV1 = dynamic_cast<CcuInsGeneraterV1*>(insGeneratorPtr_);
    if (tmpPtrV1 && !supportCcuV1) {
        // 在A5场景下没有使用A5的loop调用方式
        asccomm::ThrowCcuInternal("Cannot translate CcuRepLoop for A5 when supportCcuV1 is false!");
    }
}

const std::string& CcuRepLoop::GetLabel() const { return label; }

void CcuRepLoop::Reference(std::shared_ptr<CcuRepLoopBlock> refRep) { loopBlock = refRep; }

std::shared_ptr<CcuRepBase> CcuRepLoop::SetLoopParam(Executor executor, Variable var)
{
    return std::make_shared<CcuRepSetLoop>(insGeneratorPtr_, loopParam, executor, var);
}

bool CcuRepLoop::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    (void)dep;
    ValidateInsGeneratorForLoop();

    this->instrId = instrId;
    translated = true;

    asccomm::CheckCcuNotNull(loopBlock, "[CcuRepLoop::Translate] LoopBlock is nullptr!");

    if (!loopBlock->Translated()) {
        asccomm::ThrowCcuInternal("Reference To Invalid LoopBlock");
    }

    uint16_t startInstrId = loopBlock->StartInstrId();
    uint16_t loopBlockInstrCount = loopBlock->InstrCount();
    if (loopBlockInstrCount == 0) {
        HCCL_ERROR(
            "[CcuRepLoop][Translate] loopBlockInstrCount[%u] is 0, which causes underflow in endInstrId "
            "calculation.",
            loopBlockInstrCount);
        return false;
    }
    if (startInstrId > USHRT_MAX - loopBlockInstrCount) {
        HCCL_ERROR(
            "[CcuRepLoop][Translate] startInstrId[%u] + loopBlockInstrCount[%u] exceeds the maximum value "
            "of unsigned short int.",
            startInstrId, loopBlockInstrCount);
        return false;
    }

    if (instrId > USHRT_MAX - instrCount) {
        HCCL_ERROR("[CcuRepLoop][Translate] instrId[%u] exceeds the maximum value of unsigned short int.", instrId);
        return false;
    }

    uint16_t endInstrId = startInstrId + loopBlockInstrCount - 1;

    LoopInstr(instr++, startInstrId, endInstrId, loopParam.Id());

    instrId += instrCount;

    return translated;
}

std::string CcuRepLoop::Describe() { return asccomm::FormatCcuMessage("Loop reference to [%s]", label.c_str()); }

}; // namespace CcuRep
}; // namespace asccomm
