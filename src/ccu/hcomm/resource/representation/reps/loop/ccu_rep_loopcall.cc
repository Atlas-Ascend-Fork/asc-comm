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
#include "hcomm/resource/kernel/ccu_kernel.h"

namespace asccomm {
namespace CcuRep {

CcuRepLoopCall::CcuRepLoopCall(CcuInsGeneraterBase* insGeneratorPtr, const std::string& label)
    : insGeneratorPtr_(insGeneratorPtr), label(label)
{
    type = CcuRepType::LOOP_CALL;
}

const std::string& CcuRepLoopCall::GetLabel() const { return label; }

void CcuRepLoopCall::Reference(std::shared_ptr<CcuRepLoopBlock> refRep) { loopBlock = refRep; }

void CcuRepLoopCall::SetInArg(const Variable& var)
{
    inArgCount++;
    inArgInstrCount++;
    inArgs.push_back(CcuRepArg(var));
}

void CcuRepLoopCall::SetInArg(const std::vector<Variable>& varList)
{
    inArgCount += varList.size();
    inArgInstrCount += varList.size();
    inArgs.push_back(CcuRepArg(varList));
}

void CcuRepLoopCall::SetInArg(const Memory& mem)
{
    inArgCount++;
    inArgInstrCount += 2; // 传递Memory需要2条指令
    inArgs.push_back(CcuRepArg(mem));
}

void CcuRepLoopCall::SetInArg(const std::vector<Memory>& memList)
{
    inArgCount += memList.size();
    inArgInstrCount += memList.size() * 2; // 传递Memory需要2条指令
    inArgs.push_back(CcuRepArg(memList));
}

/* 【新增】 */
void CcuRepLoopCall::SetInArg(const LocalAddr& addr)
{
    inArgCount++;
    inArgInstrCount += 2; // 传递LocalAddr需要2条指令
    inArgs.push_back(CcuRepArg(addr));
}

void CcuRepLoopCall::SetInArg(const std::vector<LocalAddr>& addrList)
{
    inArgCount += addrList.size();
    inArgInstrCount += addrList.size() * 2; // 传递LocalAddr需要2条指令
    inArgs.push_back(CcuRepArg(addrList));
}

void CcuRepLoopCall::SetInArg(const RemoteAddr& addr)
{
    inArgCount++;
    inArgInstrCount += 2; // 传递RemoteAddr需要2条指令
    inArgs.push_back(CcuRepArg(addr));
}

void CcuRepLoopCall::SetInArg(const std::vector<RemoteAddr>& addrList)
{
    inArgCount += addrList.size();
    inArgInstrCount += addrList.size() * 2; // 传递RemoteAddr需要2条指令
    inArgs.push_back(CcuRepArg(addrList));
}

uint16_t CcuRepLoopCall::InstrCount()
{
    instrCount = inArgInstrCount;
    return instrCount;
}

bool CcuRepLoopCall::Translate(CcuKernel* ccuKernel, CcuInstr*& instr, uint16_t& instrId, const TransDep& dep)
{
    this->instrId = instrId;
    translated = true;

    asccomm::CheckCcuNotNull(loopBlock, "[CcuRepLoopCall::Translate] LoopBlock is nullptr!");

    if (!loopBlock->Translated()) {
        asccomm::ThrowCcuInternal("Reference To Invalid LoopBlock");
    }

    instrId += InstrCount();

    return translated;
}

std::string CcuRepLoopCall::Describe() { return asccomm::FormatCcuMessage("LoopCall[%s]", label.c_str()); }

}; // namespace CcuRep
}; // namespace asccomm
