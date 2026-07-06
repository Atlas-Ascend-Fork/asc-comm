/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * Description: ccu rep reference manager implementation file
 * Create: 2025-02-20
 */

#include "ccu_rep_reference_manager_v1.h"
#include "exception_util.h"
#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

CcuRepReferenceManager::CcuRepReferenceManager(uint8_t deiId) : dieId(deiId)
{
    funcInVar.resize(FUNC_ARG_MAX);
    funcOutVar.resize(FUNC_ARG_MAX);
    funcCallVar.resize(1 + FUNC_NEST_MAX + 1); // FUNC_NEST_MAX个xn存放返回地址，1个xn存放block的起始地址和1个xn存放函数地址调用时返回地址
}

CcuResReq CcuRepReferenceManager::GetResReq(uint8_t reqDieId)
{
    // xn 资源统一从 continuousXn 池子申请，离散 xn 帐户已废弃
    CcuResReq resReq;
    resReq.continuousXnReq[reqDieId] = FUNC_ARG_MAX + FUNC_ARG_MAX + 1 + FUNC_NEST_MAX + 1;
    return resReq;
}

void CcuRepReferenceManager::GetRes(CcuRepResource &res)
{
    res.continuousVariable[dieId].insert(res.continuousVariable[dieId].end(),
        funcInVar.begin(), funcInVar.end());
    res.continuousVariable[dieId].insert(res.continuousVariable[dieId].end(),
        funcOutVar.begin(), funcOutVar.end());
    res.continuousVariable[dieId].insert(res.continuousVariable[dieId].end(),
        funcCallVar.begin(), funcCallVar.end());
}

bool CcuRepReferenceManager::CheckValid(const std::string &label)
{
    return (referenceMap.find(label) != referenceMap.end());
}

bool CcuRepReferenceManager::CheckUnique(const std::string &label)
{
    return (referenceMap.find(label) == referenceMap.end());
}

std::shared_ptr<CcuRepBlock> CcuRepReferenceManager::GetRefBlock(const std::string &label)
{
    if (!CheckValid(label)) {
        Hccl::THROW<Hccl::CcuApiException>("Invalid Reference: %s", label.c_str());
    }
    return referenceMap[label];
}

void CcuRepReferenceManager::SetRefBlock(const std::string &label, std::shared_ptr<CcuRepBlock> refBlock)
{
    if (!CheckUnique(label)) {
        Hccl::THROW<Hccl::CcuApiException>("Duplicate Definition: %s", label.c_str());
    }
    referenceMap[label] = refBlock;
}

uint16_t CcuRepReferenceManager::GetFuncAddr(const std::string &label)
{
    if (!CheckValid(label)) {
        Hccl::THROW<Hccl::CcuApiException>("Invalid Reference: %s", label.c_str());
    }

    if (referenceMap[label]->Type() != CcuRepType::FUNC_BLOCK) {
        Hccl::THROW<Hccl::CcuApiException>("Invalid Type, %s Must be FuncBlock", label.c_str());
    }

    return referenceMap[label]->StartInstrId();
}

const Variable &CcuRepReferenceManager::GetFuncCall()
{
    return funcCallVar[0];
}

const Variable &CcuRepReferenceManager::GetFuncRet(uint16_t callLayer)
{
    if (callLayer > FUNC_NEST_MAX) {
        Hccl::THROW<Hccl::CcuApiException>("Max Func Call Nest Num is %u, callLayer = %u", FUNC_NEST_MAX, callLayer);
    }
    return funcCallVar[callLayer + 1];
}

const std::vector<Variable> &CcuRepReferenceManager::GetFuncIn()
{
    return funcInVar;
}

const std::vector<Variable> &CcuRepReferenceManager::GetFuncOut()
{
    return funcOutVar;
}

void CcuRepReferenceManager::Dump() const
{
    for (const auto &kv : referenceMap) {
        HCCL_INFO("refBlock[%s]:", kv.first.c_str());
    }
}

void CcuRepReferenceManager::ClearRepReference()
{
    referenceMap.clear();
}

}; // namespace CcuRep
}; // namespace hcomm