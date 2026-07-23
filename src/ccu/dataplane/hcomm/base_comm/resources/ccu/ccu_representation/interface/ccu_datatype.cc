/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2024-2024. All rights reserved.
 * Description: Ccu representation datatype implementation file
 * Author: sunzhepeng
 * Create: 2024-07-06
 */
#include "ccu_datatype_v1.h"
#include "ccu_rep_v1.h"
#include "ccu_kernel_resource.h"
#include "ccu_interface_assist_v1.h"

#include "ccu_api_exception.h"

namespace hcomm {
namespace CcuRep {

uint16_t CcuPhyRes::Id() const
{
    return id;
}
uint16_t CcuPhyRes::DieId() const
{
    return dieId;
}
void CcuPhyRes::Reset(uint16_t id)
{
    this->id = id;
}
void CcuPhyRes::SetDieId(uint16_t dieId)
{
    this->dieId = dieId;
}

CcuVirRes::CcuVirRes(CcuRepContext *context) : context(context)
{
    phyRes = std::make_shared<CcuPhyRes>();
}

void CcuVirRes::Reset(uint16_t id)
{
    phyRes->Reset(id);
}

void CcuVirRes::Reset(uint16_t id, uint16_t dieId)
{
    phyRes->Reset(id);
    phyRes->SetDieId(dieId);
}

void CcuVirRes::SetDieId(uint16_t dieId)
{
    phyRes->SetDieId(dieId);
}

uint16_t CcuVirRes::Id() const
{
    return phyRes->Id();
}

uint16_t CcuVirRes::DieId() const
{
    return phyRes->DieId();
}

Variable::Variable(CcuRepContext* context) : CcuVirRes(context)
{
}

Variable::Variable(const Variable& other): CcuVirRes(other.context)
{
    phyRes = other.phyRes;
}

void Variable::operator=(Variable&& other)
{
    phyRes = other.phyRes;
    context = other.context;
}

void Variable::operator=(const Variable &other)
{
    AppendToContext(context, std::make_shared<CcuRepAssign>(*this, other));
}

void Variable::operator=(uint64_t immediate)
{
    AppendToContext(context, std::make_shared<CcuRepAssign>(*this, immediate));
}

void Variable::operator=(CcuArithmeticOperator<Variable, Variable> op)
{
    AppendToContext(context, std::make_shared<CcuRepAdd>(*this, op.lhs, op.rhs));
}

void Variable::operator+=(const Variable &other)
{
    AppendToContext(context, std::make_shared<CcuRepAdd>(*this, other));
}

Address::Address(CcuRepContext* context) : CcuVirRes(context)
{
}

Address::Address(const Address& other): CcuVirRes(other.context)
{
    phyRes = other.phyRes;
}

void Address::operator=(Address&& other)
{
    phyRes = other.phyRes;
    context = other.context;
}

void Address::operator=(const Address &other)
{
    AppendToContext(context, std::make_shared<CcuRepAssign>(*this, other));
}

void Address::operator=(const Variable &other)
{
    AppendToContext(context, std::make_shared<CcuRepAssign>(*this, other));
}

void Address::operator=(uint64_t immediate)
{
    AppendToContext(context, std::make_shared<CcuRepAssign>(*this, immediate));
}

void Address::operator=(CcuArithmeticOperator<Variable, Address> op)
{
    AppendToContext(context, std::make_shared<CcuRepAdd>(*this, op.rhs, op.lhs));
}

void Address::operator=(CcuArithmeticOperator<Address, Address> op)
{
    AppendToContext(context, std::make_shared<CcuRepAdd>(*this, op.lhs, op.rhs));
}

void Address::operator+=(const Variable &other)
{
    AppendToContext(context, std::make_shared<CcuRepAdd>(*this, other));
}

LocalNotify::LocalNotify(CcuRepContext* context) : CcuVirRes(context)
{
}


CcuBuffer::CcuBuffer(CcuRepContext* context) : CcuVirRes(context)
{
}

CcuBuf::CcuBuf(CcuRepContext* context) : CcuVirRes(context)
{
}
uint16_t CcuBuffer::Id() const
{
    return phyRes->Id() + CCUBUFFER_DIE_ID_BIT * phyRes->DieId();
}

uint16_t CcuBuf::Id() const
{
    return phyRes->Id() + CCUBUFFER_DIE_ID_BIT * phyRes->DieId();
}

Executor::Executor(CcuRepContext* context) : CcuVirRes(context)
{
}

CompletedEvent::CompletedEvent(CcuRepContext* context) : CcuVirRes(context)
{
}

}; // namespace CcuRep
}; // namespace hcomm