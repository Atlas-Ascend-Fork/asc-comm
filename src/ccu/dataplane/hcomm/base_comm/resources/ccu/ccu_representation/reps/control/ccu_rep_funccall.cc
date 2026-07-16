/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"
#include "ccu_rep_reference_manager_v1.h"

#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

CcuRepFuncCall::CcuRepFuncCall(const std::string &label) : label(label)
{
    type = CcuRepType::FUNC_CALL;
}

CcuRepFuncCall::CcuRepFuncCall(const Variable &funcAddrVar) : label(""), funcAddrVar(funcAddrVar)
{
    type = CcuRepType::FUNC_CALL;
}

const std::string &CcuRepFuncCall::GetLabel() const
{
    return label;
}

void CcuRepFuncCall::Reference(std::shared_ptr<CcuRepFuncBlock> refRep)
{
    funcBlock = refRep;
}

void CcuRepFuncCall::SetFuncManager(CcuRepReferenceManager *funcManager)
{
    this->funcManager = funcManager;
}

void CcuRepFuncCall::SetInArg(const Variable &var)
{
    inArgCount++;
    inArgs.push_back(CcuRepArg(var));
}

void CcuRepFuncCall::SetOutArg(const Variable &var)
{
    outArgCount++;
    if (outArgCount > FUNC_ARG_MAX) {
        Hccl::THROW<Hccl::CcuApiException>("CcuFunc Max ArgCount = %u", FUNC_ARG_MAX);
    }
    outArgs.push_back(CcuRepArg(var));
}

void CcuRepFuncCall::SetInArg(const std::vector<Variable> &varList)
{
    inArgCount += varList.size();
    inArgs.push_back(CcuRepArg(varList));
}

void CcuRepFuncCall::SetOutArg(const std::vector<Variable> &varList)
{
    outArgCount += varList.size();
    if (outArgCount > FUNC_ARG_MAX) {
        Hccl::THROW<Hccl::CcuApiException>("CcuFunc Max ArgCount = %u", FUNC_ARG_MAX);
    }
    outArgs.push_back(CcuRepArg(varList));
}

uint16_t CcuRepFuncCall::InstrCount()
{
    instrCount = inArgCount + outArgCount + 4; // funcCall除去入参和出参的处理外，需要额外4条指令
    return instrCount;
}

bool CcuRepFuncCall::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    if (funcManager == nullptr) {
        Hccl::THROW<Hccl::CcuApiException>("funcManager is nullptr");
    }
    // 未实现, FuncCall和FuncBlock中的args个数校验
    uint32_t extraInstrNum = 4; // funcCall除去入参和出参的处理外，需要额外4条指令
    if (this->instr == nullptr) {
        this->instrId = instrId;
        this->instr   = instr;
        instr += InstrCount();
        instrId += InstrCount();
    }

    if (funcBlock != nullptr && !funcBlock->Translated()) {
        return translated;
    }

    translated = true;

    std::vector<Variable> formalIns;
    if (funcBlock != nullptr) {
        formalIns = funcBlock->GetInArgVars();
        if (static_cast<uint32_t>(formalIns.size()) != inArgCount) {
            Hccl::THROW<Hccl::CcuApiException>("FuncCall arg count mismatch: caller = %u, callee formal = %u",
                                               inArgCount, static_cast<uint32_t>(formalIns.size()));
        }
    }

    auto inArgDstId = [&](uint32_t idx) -> uint16_t {
        return (funcBlock != nullptr) ? formalIns[idx].Id() : funcManager->GetFuncIn()[idx].Id();
    };

    uint32_t iInArg = 0;
    for (uint32_t i = 0; i < inArgs.size(); i++) {
        if (inArgs[i].type == CcuArgType::VARIABLE) {
            LoadXXInstr(this->instr + iInArg, inArgDstId(iInArg), inArgs[i].var.Id(), dep.reserveXnId);
            iInArg++;
        } else if (inArgs[i].type == CcuArgType::VARIABLE_LIST) {
            for (uint32_t j = 0; j < inArgs[i].varList.size(); j++) {
                LoadXXInstr(this->instr + iInArg, inArgDstId(iInArg), inArgs[i].varList[j].Id(), dep.reserveXnId);
                iInArg++;
            }
        }
    }

    uint32_t locId = 0;
    if (funcBlock != nullptr) {
        LoadImdToXnInstr(this->instr + inArgCount + locId++, funcManager->GetFuncCall().Id(),
                         funcBlock->StartInstrId());
    } else {
        LoadXXInstr(this->instr + inArgCount + locId++, funcManager->GetFuncCall().Id(), funcAddrVar.Id(),
                    dep.reserveXnId);
    }

    LoadImdToXnInstr(this->instr + inArgCount + locId++, funcManager->GetFuncRet(GetCallLayer()).Id(),
                     this->instrId + inArgCount + 3); // 需要指向函数返回位置，为输入指令Id + 3
    JumpInstr(this->instr + inArgCount + locId++, funcManager->GetFuncCall().Id(), dep.reserveXnId, 1);
    LoadImdToXnInstr(this->instr + inArgCount + locId++, dep.reserveXnId, 0);

    uint32_t iOutArg = 0;
    for (uint32_t i = 0; i < outArgs.size(); i++) {
        if (outArgs[i].type == CcuArgType::VARIABLE) {
            LoadXXInstr(this->instr + inArgCount + extraInstrNum + iOutArg, outArgs[i].var.Id(),
                        funcManager->GetFuncOut()[iOutArg].Id(), dep.reserveXnId);
            iOutArg++;
        } else if (outArgs[i].type == CcuArgType::VARIABLE_LIST) {
            for (uint32_t j = 0; j < outArgs[i].varList.size(); j++) {
                LoadXXInstr(this->instr + inArgCount + extraInstrNum + iOutArg, outArgs[i].varList[j].Id(),
                            funcManager->GetFuncOut()[iOutArg].Id(), dep.reserveXnId);
                iOutArg++;
            }
        }
    }

    return translated;
}

std::string CcuRepFuncCall::Describe()
{
    return Hccl::StringFormat("FuncCall[%s]", label.c_str());
}

int32_t CcuRepFuncCall::GetCallLayer()
{
    return funcBlock == nullptr ? FUNC_NEST_MAX : funcBlock->GetCallLayer();
}

}; // namespace CcuRep
}; // namespace hcomm