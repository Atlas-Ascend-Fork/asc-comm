/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: ccu representation implementation file
 * Author: sunzhepeng
 * Create: 2024-06-17
 */

#include "ccu_rep_v1.h"
#include "ccu_rep_reference_manager_v1.h"
#include "ccu_rep_translator_v1.h"

#include "string_util.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

CcuRepFuncBlock::CcuRepFuncBlock(const std::string &label) : CcuRepBlock(label)
{
    type = CcuRepType::FUNC_BLOCK;
}

std::string CcuRepFuncBlock::Describe()
{
    return Hccl::StringFormat("FuncBlock[%s]", GetLabel().c_str());
}

void CcuRepFuncBlock::SetFuncManager(CcuRepReferenceManager *funcManager)
{
    this->funcManager = funcManager;
}

void CcuRepFuncBlock::SetCallLayer(uint16_t callLayer)
{
    if (callLayer != FUNC_CALL_LAYER_INVALID) {
        this->callLayer = callLayer;
        return;
    }

    uint16_t innerCallLayer = 0;
    for (const auto &rep : GetReps()) {
        if (rep->Type() == CcuRepType::FUNC_CALL) {
            innerCallLayer  = std::static_pointer_cast<CcuRepFuncCall>(rep)->GetCallLayer() + 1;
            this->callLayer = this->callLayer > innerCallLayer ? this->callLayer : innerCallLayer;
        }
    }
    if (this->callLayer > FUNC_NEST_MAX - 1) {
        Hccl::THROW<Hccl::CcuApiException>("Max Func Call Nest Num is %u", FUNC_NEST_MAX);
    }
}

uint16_t CcuRepFuncBlock::GetCallLayer() const
{
    return callLayer;
}

void CcuRepFuncBlock::DefineInArg(const Variable &var)
{
    inArgCount++;
    inArgs.push_back(CcuRepArg(var));
    HCCL_INFO("Define Input Arg: Index[%u], Type[Variable] Id[%u]", inArgs.size(), var.Id());
}

void CcuRepFuncBlock::DefineOutArg(const Variable &var)
{
    outArgCount++;
    if (outArgCount > FUNC_ARG_MAX) {
        Hccl::THROW<Hccl::CcuApiException>("CcuFunc Max ArgCount = %u", FUNC_ARG_MAX);
    }
    outArgs.push_back(CcuRepArg(var));
    HCCL_INFO("Define Output Arg: Index[%u], Type[Variable] Id[%u]", outArgs.size(), var.Id());
}

void CcuRepFuncBlock::DefineInArg(const std::vector<Variable> &varList)
{
    inArgCount += varList.size();
    inArgs.push_back(CcuRepArg(varList));
    HCCL_INFO("Define Input Arg: Index[%u], Type[Variable List]: ", inArgs.size());
    for (uint32_t index = 0; index < varList.size(); index++) {
        HCCL_INFO("    Index[%u].Id[%u]", index, varList[index].Id());
    }
}

void CcuRepFuncBlock::DefineOutArg(const std::vector<Variable> &varList)
{
    outArgCount += varList.size();
    if (outArgCount > FUNC_ARG_MAX) {
        Hccl::THROW<Hccl::CcuApiException>("CcuFunc Max ArgCount = %u", FUNC_ARG_MAX);
    }
    outArgs.push_back(CcuRepArg(varList));
    HCCL_INFO("Define Output Arg: Index[%u], Type[Variable List]: ", outArgs.size());
    for (uint32_t index = 0; index < varList.size(); index++) {
        HCCL_INFO("    Index[%u].Id[%u]", index, varList[index].Id());
    }
}

std::vector<Variable> CcuRepFuncBlock::GetInArgVars() const
{
    std::vector<Variable> vars;
    for (const auto &arg : inArgs) {
        if (arg.type == CcuArgType::VARIABLE) {
            vars.push_back(arg.var);
        } else if (arg.type == CcuArgType::VARIABLE_LIST) {
            vars.insert(vars.end(), arg.varList.begin(), arg.varList.end());
        }
    }
    return vars;
}

uint16_t CcuRepFuncBlock::InstrCount()
{
    instrCount = CcuRepBlock::InstrCount() + outArgCount + 2;
    return instrCount;
}

bool CcuRepFuncBlock::Translate(CcuInstr *&instr, uint16_t &instrId, const TransDep &dep)
{
    if (funcManager == nullptr) {
        Hccl::THROW<Hccl::CcuApiException>("funcManager is nullptr");
    }

    this->instrId = instrId;
    translated    = true;

    // 函数入口为Nop指令
    LoadImdToXnInstr(instr++, dep.reserveXnId, 0);
    instrId++;


    // 使用空实现的自定义删除器，避免智能指针析构时释放对象
    auto translator
        = CcuRepTranslator(std::shared_ptr<CcuRepReferenceManager>(funcManager, [](CcuRepReferenceManager *ptr) {}), dep);
    translator.Translate(GetReps(), instr, instrId, [](std::shared_ptr<CcuRepBase> rep) -> bool {
        return true;
    });

    uint32_t iOutArg = 0;
    for (uint32_t i = 0; i < outArgs.size(); i++) {
        if (outArgs[i].type == CcuArgType::VARIABLE) {
            LoadXXInstr(instr++, funcManager->GetFuncOut()[iOutArg++].Id(), outArgs[i].var.Id(), dep.reserveXnId);
            instrId++;
        } else if (outArgs[i].type == CcuArgType::VARIABLE_LIST) {
            for (uint32_t j = 0; j < outArgs[i].varList.size(); j++) {
                LoadXXInstr(instr++, funcManager->GetFuncOut()[iOutArg++].Id(), outArgs[i].varList[j].Id(),
                            dep.reserveXnId);
                instrId++;
            }
        }
    }

    JumpInstr(instr++, funcManager->GetFuncRet(callLayer).Id(), dep.reserveXnId, 1);
    instrId++;

    return translated;
}

}; // namespace CcuRep
}; // namespace hcomm