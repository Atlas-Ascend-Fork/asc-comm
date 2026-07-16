/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_rep_loopgroup_bundle_v1.h"
#include "ccu_assist_v1.h"
#include "ccu_microcode_v1.h"
#include "string_util.h"

namespace hcomm {
namespace CcuRep {

CcuRepLoopGroupBundle::CcuRepLoopGroupBundle(const CcuLoopGroupConfig &config,
                                             const Variable &parallelVar, const Variable &offsetVar)
    : config_(config), parallelVar_(parallelVar), offsetVar_(offsetVar)
{
    type = CcuRepType::LOOPGROUP;
}

CcuRepLoopGroupBundle::CcuRepLoopGroupBundle(const Variable &parallelVar, const Variable &offsetVar)
    : parallelVar_(parallelVar), offsetVar_(offsetVar), isGroupVarBased_(true)
{
    type = CcuRepType::LOOPGROUP;
}

void CcuRepLoopGroupBundle::AddLoop(const LoopEntry &entry)
{
    loops_.push_back(entry);
}

uint16_t CcuRepLoopGroupBundle::LoopGroupInstrOffsetInBundle() const
{
    uint16_t loopCount = static_cast<uint16_t>(loops_.size());
    uint16_t varBasedLoopCount = 0;
    for (const auto &loop : loops_) {
        if (loop.isVarBased) {
            varBasedLoopCount++;
        }
    }
    return (loopCount - varBasedLoopCount)
         + (varBasedLoopCount * 2)
         + (isGroupVarBased_ ? 0 : 2);
}

uint16_t CcuRepLoopGroupBundle::GetStartLoopInstrId() const
{
    return instrId + LoopGroupInstrOffsetInBundle() + 3;
}

uint16_t CcuRepLoopGroupBundle::InstrCount()
{
    uint16_t loopCount = static_cast<uint16_t>(loops_.size());
    instrCount = LoopGroupInstrOffsetInBundle()
               + 1
               + 2
               + loopCount
               + 1;
    return instrCount;
}

bool CcuRepLoopGroupBundle::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    for (const auto &loop : loops_) {
        if (!loop.repLoopBlock->Translated()) {
            return false;
        }
    }

    this->instrId = instrId;
    translated = true;

    // 1. Assign loopParam for each loop
    for (const auto &loop : loops_) {
        if (!loop.isVarBased) {
            uint64_t lpImm = GetLoopParam(loop.executor.Id(), loop.config.addrOffset, loop.config.iterNum);
            LoadImdToXnInstr(instr++, loop.loopParamVar.Id(), lpImm);
            instrId++;
        } else {
            uint64_t ctxImm = static_cast<uint64_t>(loop.executor.Id()) << 45;
            LoadImdToXnInstr(instr++, dep.reserveXnId, ctxImm);
            instrId++;
            LoadXXInstr(instr++, loop.loopParamVar.Id(), loop.loopParamVar.Id(), dep.reserveXnId);
            instrId++;
        }
    }

    // 2-3. Assign parallelParam & offsetParam (skip var-based group — registers already set)
    if (!isGroupVarBased_) {
        uint64_t parallelImm = GetParallelParam(config_.cloneNum, repeatLoopIdx_, totalLoopNum_);
        LoadImdToXnInstr(instr++, parallelVar_.Id(), parallelImm);
        instrId++;

        uint64_t offsetImm = ::hcomm::CcuRep::GetOffsetParam(config_.addrOffset, config_.ccuBufferOffset, config_.eventOffset);
        LoadImdToXnInstr(instr++, offsetVar_.Id(), offsetImm);
        instrId++;
    }

    // 4. LoopGroupInstr — startLoopInstrId = instrId + 3
    LoopGroupInstr(instr++, instrId + 3, parallelVar_.Id(), offsetVar_.Id(), 0);
    instrId++;

    // 5. Jump (skip over LoopInstr region)
    uint16_t loopCount = static_cast<uint16_t>(loops_.size());
    uint16_t jumpTargetInstrId = instrId + 2 + loopCount + 1;
    LoadImdToXnInstr(instr++, dep.reserveXnId, jumpTargetInstrId);
    instrId++;
    JumpInstr(instr++, dep.reserveXnId, dep.reserveXnId, 1);
    instrId++;

    // 6. LoopInstr for each loop
    for (const auto &loop : loops_) {
        const auto &block = loop.repLoopBlock;
        LoopInstr(instr++, block->StartInstrId(),
                  block->StartInstrId() + block->InstrCount() - 1,
                  loop.loopParamVar.Id());
        instrId++;
    }

    // 7. NOP (JumpLabel target)
    LoadImdToXnInstr(instr++, dep.reserveXnId, 0);
    instrId++;

    return translated;
}

std::string CcuRepLoopGroupBundle::Describe()
{
    return Hccl::StringFormat("LoopGroupBundle[loops=%zu, totalLoopNum=%lu]",
                              loops_.size(), totalLoopNum_);
}

}; // namespace CcuRep
}; // namespace hcomm
