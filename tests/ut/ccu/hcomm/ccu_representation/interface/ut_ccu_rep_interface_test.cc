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
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/interface/ccu_funcblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_funccall_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopblock_v1.h"
#include "hcomm/resource/representation/interface/ccu_loopcall_v1.h"
#include "hcomm/resource/representation/interface/ccu_condition_v1.h"
#include "hcomm/resource/representation/interface/ccu_repeat_v1.h"
#include "hcomm/resource/representation/interface/ccu_interface_assist_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "ccu_api_exception.h"

#include <gtest/gtest.h>
#include <string>
#include <memory>

#include "ccu/hcomm/ccu_utils.hpp"

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepInterfaceTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(CcuRepInterfaceTest, CcuPhyRes_IdAndDieId)
{
    ccu_phy_res phy_res_;
    phy_res_.reset(10);
    EXPECT_EQ(phy_res_.id(), 10);

    phy_res_.set_die_id(5);
    EXPECT_EQ(phy_res_.die_id(), 5);
}

TEST_F(CcuRepInterfaceTest, CcuPhyRes_ResetWithIdAndDieId)
{
    ccu_vir_res virRes(nullptr);
    virRes.reset(20, 3);
    EXPECT_EQ(virRes.id(), 20);
    EXPECT_EQ(virRes.die_id(), 3);
}

TEST_F(CcuRepInterfaceTest, CcuVirRes_IdAndDieId)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    ccu_vir_res virRes(&context_);
    virRes.reset(15);
    virRes.set_die_id(7);
    EXPECT_EQ(virRes.id(), 15);
    EXPECT_EQ(virRes.die_id(), 7);
}

TEST_F(CcuRepInterfaceTest, CcuBuffer_Id)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    ccu_buffer buffer(&context_);
    buffer.reset(3);
    buffer.set_die_id(1);
    uint16_t expectedId = 3 + (1 << 15);
    EXPECT_EQ(buffer.id(), expectedId);
}

TEST_F(CcuRepInterfaceTest, CcuBuf_Id)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    ccu_buf buf(&context_);
    buf.reset(4);
    buf.set_die_id(2);
    uint16_t expectedId = 4 + (2 << 15);
    EXPECT_EQ(buf.id(), expectedId);
}

TEST_F(CcuRepInterfaceTest, LocalNotify_Constructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    local_notify notify(&context_);
    notify.reset(8);
    EXPECT_EQ(notify.id(), 8);
}

TEST_F(CcuRepInterfaceTest, Executor_Constructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    executor executor(&context_);
    executor.reset(12);
    EXPECT_EQ(executor.id(), 12);
}

TEST_F(CcuRepInterfaceTest, Variable_CopyConstructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var1(&context_);
    var1.reset(5);

    variable var2(var1);
    EXPECT_EQ(var2.id(), 5);
}

TEST_F(CcuRepInterfaceTest, Address_CopyConstructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr1(&context_);
    addr1.reset(7);

    address addr2(addr1);
    EXPECT_EQ(addr2.id(), 7);
}

TEST_F(CcuRepInterfaceTest, AppendToContext_NullContext)
{
    auto nop = std::make_shared<ccu_rep_nop>(nullptr);
    EXPECT_THROW(append_to_context(nullptr, nop), CcuUtException);
}

TEST_F(CcuRepInterfaceTest, CurrentBlock_NullContext) { EXPECT_THROW(current_block(nullptr), CcuUtException); }

TEST_F(CcuRepInterfaceTest, SetCurrentBlock_NullContext)
{
    std::shared_ptr<ccu_rep_block> block = nullptr;
    EXPECT_THROW(set_current_block(nullptr, block), CcuUtException);
}

TEST_F(CcuRepInterfaceTest, CreateVariable_NullContext) { EXPECT_THROW(create_variable(nullptr), CcuUtException); }

TEST_F(CcuRepInterfaceTest, CcuRepContext_AppendAndCurrentBlock)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);

    auto block1 = context_.current_block();
    EXPECT_NE(block1, nullptr);

    auto rep_func_block_ = std::make_shared<ccu_rep_func_block>(nullptr, "testFunc");
    append_to_context(&context_, rep_func_block_);

    auto block2 = context_.current_block();
    EXPECT_NE(block2, nullptr);
}

TEST_F(CcuRepInterfaceTest, FuncBlock_ConstructorAndDestructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    {
        func_block func_block_(&context_, "testFunc", 1);
        auto curBlock = current_block(&context_);
        EXPECT_NE(curBlock, nullptr);
    }
    auto blockAfter = current_block(&context_);
    EXPECT_NE(blockAfter, nullptr);
}

TEST_F(CcuRepInterfaceTest, FuncCall_ConstructorWithLabel)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen;
    context_.set_ins_generater(&insGen);
    func_call func_call(&context_, "testCall");
    func_call.append_to_context();

    auto reps = context_.get_rep_sequence();
    EXPECT_EQ(reps.size(), 1);
}

TEST_F(CcuRepInterfaceTest, FuncCall_ConstructorWithVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen;
    context_.set_ins_generater(&insGen);
    variable func_addr(&context_);
    func_call func_call(&context_, func_addr);
    func_call.append_to_context();

    auto reps = context_.get_rep_sequence();
    EXPECT_EQ(reps.size(), 1);
}

TEST_F(CcuRepInterfaceTest, LoopBlock_ConstructorAndDestructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    {
        loop_block loop_block_(&context_, "testLoop");
        auto curBlock = current_block(&context_);
        EXPECT_NE(curBlock, nullptr);
    }
    EXPECT_THROW(current_block(&context_), CcuUtException);
}

TEST_F(CcuRepInterfaceTest, LoopCall_ConstructorAndAppend)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    loop_call loopCall(&context_, "testLoopCall");

    EXPECT_EQ(loopCall.get_label(), "testLoopCall");
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetDieId)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.set_die_id(3);
    EXPECT_EQ(context_.get_die_id(), 3);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetMissionId)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.set_mission_id(5);
    EXPECT_EQ(context_.get_mission_id(), 5);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetMissionKey)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.set_mission_key(100);
    EXPECT_EQ(context_.get_mission_key(), 100);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignmentOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var1(&context_);
    var1.reset(1);

    variable var2(&context_);
    var2.reset(2);

    var1 = 100;
}

TEST_F(CcuRepInterfaceTest, Address_AssignmentOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr1(&context_);
    addr1.reset(1);

    address addr2(&context_);
    addr2.reset(2);

    addr1 = 200;
}

TEST_F(CcuRepInterfaceTest, Variable_NotEqualOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_(&context_);
    var_.reset(0);

    auto rel = var_ != 10;
    EXPECT_EQ(rel.type, ccu_relational_operator_type::not_equal);
}

TEST_F(CcuRepInterfaceTest, Variable_EqualOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_(&context_);
    var_.reset(0);

    auto rel = var_ == 5;
    EXPECT_EQ(rel.type, ccu_relational_operator_type::equal);
}

TEST_F(CcuRepInterfaceTest, FuncBlock_OperatorParentheses)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    func_block func_block_(&context_, "testFunc", 1);
}

TEST_F(CcuRepInterfaceTest, LoopBlock_OperatorParentheses)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    loop_block loop_block_(&context_, "testLoop");
}

TEST_F(CcuRepInterfaceTest, FuncCall_OperatorParentheses)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen;
    context_.set_ins_generater(&insGen);
    func_call func_call(&context_, "testCall");
    func_call();
}

TEST_F(CcuRepInterfaceTest, LoopCall_OperatorParentheses)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    loop_call loopCall(&context_, "testLoopCall");
}

TEST_F(CcuRepInterfaceTest, Condition_EQ_RelationalOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable counter(&context_);
    counter.reset(0);

    auto rel = (counter == 10);
    EXPECT_EQ(rel.type, ccu_relational_operator_type::equal);
}

TEST_F(CcuRepInterfaceTest, Repeat_NE_RelationalOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable counter(&context_);
    counter.reset(0);

    auto rel = (counter != 10);
    EXPECT_EQ(rel.type, ccu_relational_operator_type::not_equal);
}

TEST_F(CcuRepInterfaceTest, Variable_ArithmeticAssignment)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    variable var_b_(&context_);
    var_b_.reset(2);

    var_a_ = var_a_ + var_b_;
    var_a_ = var_a_ - var_b_;
    var_a_ = var_a_ * var_b_;
    var_a_ += var_b_;
    var_a_ += uint16_t(10);
    var_a_ -= var_b_;
    var_a_ -= uint16_t(10);
    var_a_ *= var_b_;
    var_a_ *= uint16_t(10);
}

TEST_F(CcuRepInterfaceTest, Address_ArithmeticAssignment)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    addr_a_.reset(1);
    address addr_b_(&context_);
    addr_b_.reset(2);
    variable var_b_(&context_);
    var_b_.reset(3);

    addr_a_ = addr_a_ + var_b_;
    addr_a_ = addr_a_ + addr_b_;
    addr_a_ = addr_a_ + uint16_t(10);
    addr_a_ += var_b_;
}

TEST_F(CcuRepInterfaceTest, Variable_VariableAssignment)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    variable var_b_(&context_);
    var_b_.reset(2);

    var_a_ = var_b_;
}

TEST_F(CcuRepInterfaceTest, Address_AddressAndVariableAssignment)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    addr_a_.reset(1);
    address addr_b_(&context_);
    addr_b_.reset(2);
    variable var_b_(&context_);
    var_b_.reset(3);

    addr_a_ = addr_b_;
    addr_a_ = var_b_;
}

TEST_F(CcuRepInterfaceTest, Variable_PlusEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_a_ += var_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_PlusEqualImmediate)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    var_a_ += uint16_t(10);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_MulEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_a_ *= var_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_MulEqualImmediate)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    var_a_ *= uint16_t(10);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_SubEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_a_ -= var_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_SubEqualImmediate)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    var_a_ -= uint16_t(10);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_AndEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    EXPECT_ANY_THROW(var_a_ &= var_b_);
}

TEST_F(CcuRepInterfaceTest, Variable_OrEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    EXPECT_ANY_THROW(var_a_ |= var_b_);
}

TEST_F(CcuRepInterfaceTest, Variable_XorEqualVariable)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    EXPECT_ANY_THROW(var_a_ ^= var_b_);
}

TEST_F(CcuRepInterfaceTest, Variable_ShiftLeftEqual)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    EXPECT_ANY_THROW(var_a_ <<= var_b_);
}

TEST_F(CcuRepInterfaceTest, Variable_ShiftRightEqual)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    EXPECT_ANY_THROW(var_a_ >>= var_b_);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignArithmeticVarVar)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    variable var_c_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    var_c_ = var_a_ + var_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignArithmeticVarImmed)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    var_a_ = var_a_ + uint16_t(10);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignArithmeticAddrAddr)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    address addr_b_(&context_);
    variable var_c_(&context_);
    addr_a_.reset(1);
    addr_b_.reset(2);
    var_c_.reset(3);
    var_c_ = addr_a_ + addr_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Address_AssignArithmeticAddrAddr)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    address addr_b_(&context_);
    address addr_c_(&context_);
    addr_a_.reset(1);
    addr_b_.reset(2);
    addr_c_.reset(3);
    addr_c_ = addr_a_ + addr_b_;
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Address_AssignImmediate)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    addr_a_.reset(1);
    addr_a_ = uint64_t(0xABCD);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignImmediate)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(1);
    var_a_ = uint64_t(0xDEAD);
    EXPECT_EQ(context_.current_block()->get_reps().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, Variable_MoveConstructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    var_a_.reset(5);
    variable var_b_(std::move(var_a_));
    EXPECT_EQ(var_b_.id(), 5u);
}

TEST_F(CcuRepInterfaceTest, Address_MoveConstructor)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    addr_a_.reset(7);
    address addr_b_(std::move(addr_a_));
    EXPECT_EQ(addr_b_.id(), 7u);
}

TEST_F(CcuRepInterfaceTest, Address_MoveAssign)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    address addr_a_(&context_);
    address addr_b_(&context_);
    addr_a_.reset(3);
    addr_b_.reset(4);
    addr_b_ = std::move(addr_a_);
    EXPECT_EQ(addr_b_.id(), 3u);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignLogicOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    variable var_c_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    EXPECT_ANY_THROW(var_c_ = (var_a_ & var_b_));
}

TEST_F(CcuRepInterfaceTest, Variable_AssignShiftOperator)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_a_(&context_);
    variable var_b_(&context_);
    variable var_c_(&context_);
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    EXPECT_ANY_THROW(var_c_ = (var_a_ << var_b_));
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_MissionId)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.set_mission_id(100);
    EXPECT_EQ(context_.get_mission_id(), 100u);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_MissionKey)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.set_mission_key(42);
    EXPECT_EQ(context_.get_mission_key(), 42u);
}

TEST_F(CcuRepInterfaceTest, Variable_GetCurContext)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_(&context_);
    EXPECT_EQ(var_.get_cur_context(), &context_);
}

TEST_F(CcuRepInterfaceTest, Variable_GetCurPhyRes)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    variable var_(&context_);
    auto phy = var_.get_cur_phy_res();
    EXPECT_NE(phy, nullptr);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_AddSqeProfiling)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.add_sqe_profiling("test_kernel");
    EXPECT_EQ(context_.get_profiling_info().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_AddProfiling_NameMask)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    EXPECT_EQ(context_.add_profiling("wait_cke", 0xF), HCCL_SUCCESS);
    EXPECT_EQ(context_.get_profiling_info().size(), 1u);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetDependencyInfo)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0x5, rep);
    auto deps = context_.get_dependency_info(1);
    EXPECT_EQ(deps.size(), 2u); // bit 0 和 bit 2
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_GetDependencyInfo_NotFound)
{
    ccu_rep_context context_;
    auto deps = context_.get_dependency_info(999);
    EXPECT_TRUE(deps.empty());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_EraseDependencyInfo)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0x1, rep);
    context_.erase_dependency_info(1);
    auto deps = context_.get_dependency_info(1);
    EXPECT_TRUE(deps.empty());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_ClearDependencyInfo)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0x1, rep);
    context_.set_dependency_info(2, 0x2, rep);
    context_.clear_dependency_info();
    EXPECT_TRUE(context_.get_dependency_info(1).empty());
    EXPECT_TRUE(context_.get_dependency_info(2).empty());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_GetRepByInstrId_NotFound)
{
    ccu_rep_context context_;
    auto rep = context_.get_rep_by_instr_id(999);
    EXPECT_EQ(rep, nullptr);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_DumpReprestation_NoCrash)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    EXPECT_NO_THROW(context_.dump_represtation());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetCurrentBlock)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    auto block = std::make_shared<ccu_rep_block>(&insGen);
    context_.set_current_block(block);
    EXPECT_EQ(context_.current_block().get(), block.get());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_GetProfilingInfo)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    context_.add_sqe_profiling("k1");
    context_.add_sqe_profiling("k2");
    auto& info = context_.get_profiling_info();
    EXPECT_EQ(info.size(), 2u);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_GetLgProfilingInfo)
{
    ccu_rep_context context_;
    auto& lg = context_.get_lg_profiling_info();
    EXPECT_EQ(lg.ccu_profiling_infos.size(), 0u);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_GetWaitsCkeProfilingReps_Empty)
{
    ccu_rep_context context_;
    const auto& reps = context_.get_waite_cke_profiling_reps();
    EXPECT_TRUE(reps.empty());
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_AppendNop_IncreasesRepSequence)
{
    ccu_rep_context context_;
    ccu_ins_generater_v1 insGen{};
    context_.set_ins_generater(&insGen);
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop);
    EXPECT_EQ(context_.get_rep_sequence().size(), 1u);
}

class fake_count_ins_generater : public ccu_ins_generater_v1 {
public:
    uint32_t get_instr_count(ccu_rep_type rep_type) override { return 1; }
};

class CcuDatatypeOperatorTest : public ::testing::Test {
protected:
    fake_count_ins_generater insGen{};
    ccu_rep_context context_{};

    void SetUp() override { context_.set_ins_generater(&insGen); }
};

TEST_F(CcuDatatypeOperatorTest, Address_MoveCtorFromVariable)
{
    variable v(&context_);
    v.reset(3);
    address addr(std::move(v));
    EXPECT_EQ(addr.id(), 3u);
}

TEST_F(CcuDatatypeOperatorTest, VariableArithmeticAssignmentOps)
{
    variable a(&context_), b(&context_), c(&context_);
    a.reset(1);
    b.reset(2);
    c.reset(3);

    EXPECT_NO_FATAL_FAILURE({ a = b + c; });
    EXPECT_NO_FATAL_FAILURE({ a = b - c; });
    EXPECT_NO_FATAL_FAILURE({ a = b * c; });
    EXPECT_NO_FATAL_FAILURE({ a = b + uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ a = b - uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ a = b * uint16_t(10); });

    EXPECT_NO_FATAL_FAILURE({ a += b; });
    EXPECT_NO_FATAL_FAILURE({ a += uint16_t(5); });
    EXPECT_NO_FATAL_FAILURE({ a -= b; });
    EXPECT_NO_FATAL_FAILURE({ a -= uint16_t(5); });
    EXPECT_NO_FATAL_FAILURE({ a *= b; });
    EXPECT_NO_FATAL_FAILURE({ a *= uint16_t(5); });
}

TEST_F(CcuDatatypeOperatorTest, AddressArithmeticAssignmentOps)
{
    address x(&context_), y(&context_);
    variable b(&context_), c(&context_);
    x.reset(1);
    y.reset(2);
    b.reset(3);
    c.reset(4);

    EXPECT_NO_FATAL_FAILURE({ x = b + y; });
    EXPECT_NO_FATAL_FAILURE({ x = y + y; });
    EXPECT_NO_FATAL_FAILURE({ x = b + uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ x = y + uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ x = b + c; });
    EXPECT_NO_FATAL_FAILURE({ x = b * y; });
    EXPECT_NO_FATAL_FAILURE({ x = b * uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ x = y * uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ x = b + y; });
    EXPECT_NO_FATAL_FAILURE({ x = y + y; });
    EXPECT_NO_FATAL_FAILURE({ x = b - y; });
    EXPECT_NO_FATAL_FAILURE({ x = y - uint16_t(10); });

    EXPECT_NO_FATAL_FAILURE({ x += b; });
    EXPECT_NO_FATAL_FAILURE({ x += uint16_t(5); });
    EXPECT_NO_FATAL_FAILURE({ x -= b; });
    EXPECT_NO_FATAL_FAILURE({ x -= uint16_t(5); });
    EXPECT_NO_FATAL_FAILURE({ x *= b; });
    EXPECT_NO_FATAL_FAILURE({ x *= uint16_t(5); });
}

TEST_F(CcuDatatypeOperatorTest, VariableLogicAssignmentOps)
{
    variable a(&context_), b(&context_), c(&context_);
    a.reset(1);
    b.reset(2);
    c.reset(3);

    EXPECT_NO_FATAL_FAILURE({ a = b & c; });
    EXPECT_NO_FATAL_FAILURE({ a = b | c; });
    EXPECT_NO_FATAL_FAILURE({ a = b ^ c; });
    EXPECT_NO_FATAL_FAILURE({ a = ~b; });
    EXPECT_NO_FATAL_FAILURE({ a &= b; });
    EXPECT_NO_FATAL_FAILURE({ a |= b; });
    EXPECT_NO_FATAL_FAILURE({ a ^= b; });
}

TEST_F(CcuDatatypeOperatorTest, VariableShiftAssignmentOps)
{
    variable a(&context_), b(&context_), c(&context_);
    a.reset(1);
    b.reset(2);
    c.reset(3);

    EXPECT_NO_FATAL_FAILURE({ a = b >> c; });
    EXPECT_NO_FATAL_FAILURE({ a = b << c; });
    EXPECT_NO_FATAL_FAILURE({ a <<= c; });
    EXPECT_NO_FATAL_FAILURE({ a >>= c; });
}

TEST_F(CcuDatatypeOperatorTest, AddressShiftAssignmentOps)
{
    address x(&context_);
    variable b(&context_), c(&context_);
    x.reset(1);
    b.reset(2);
    c.reset(3);

    EXPECT_NO_FATAL_FAILURE({ x = b >> c; });
    EXPECT_NO_FATAL_FAILURE({ x = b << c; });
    EXPECT_NO_FATAL_FAILURE({ x <<= c; });
    EXPECT_NO_FATAL_FAILURE({ x >>= c; });
}

TEST_F(CcuDatatypeOperatorTest, VariableAddressAssignFromAddrArithmetic)
{
    variable a(&context_);
    address x(&context_), y(&context_);
    a.reset(1);
    x.reset(2);
    y.reset(3);

    EXPECT_NO_FATAL_FAILURE({ a = x + uint16_t(10); });
    EXPECT_NO_FATAL_FAILURE({ a = x + y; });
}

TEST_F(CcuDatatypeOperatorTest, MixedLogicRelationalOperatorConstruction)
{
    variable v(&context_), w(&context_);
    address a1(&context_), a2(&context_);
    v.reset(1);
    w.reset(2);
    a1.reset(3);
    a2.reset(4);

    EXPECT_NO_FATAL_FAILURE({ (void)(v & w); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v | w); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v ^ w); });
    EXPECT_NO_FATAL_FAILURE({ (void)(~v); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v & a1); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v | a1); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v ^ a1); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 & a2); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 | a2); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 ^ a2); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 & v); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 | v); });
    EXPECT_NO_FATAL_FAILURE({ (void)(a1 ^ v); });

    EXPECT_NO_FATAL_FAILURE({ (void)(v == uint64_t(1)); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v != uint64_t(1)); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v <= uint64_t(1)); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v > uint64_t(1)); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v <= w); });
    EXPECT_NO_FATAL_FAILURE({ (void)(v > w); });
}

TEST_F(CcuDatatypeOperatorTest, AddressArithmeticUnsupportedTypes_Throw)
{
    address x(&context_), y(&context_);
    variable b(&context_);
    x.reset(1);
    y.reset(2);
    b.reset(3);

    // address (op addr) addr 仅支持 addition; 其它抛 not_support
    EXPECT_ANY_THROW({ x = y * y; });
    EXPECT_ANY_THROW({ x = y - y; });
    // address = var (op) var 仅支持 addition 与 multiplication
    EXPECT_ANY_THROW({ x = b - b; });
}

TEST_F(CcuDatatypeOperatorTest, VariableArithmeticUnsupportedDefault_Throw)
{
    variable a(&context_), b(&context_), c(&context_);
    a.reset(1);
    b.reset(2);
    c.reset(3);

    // variable = var (op) var 仅支持 add/sub/mul; 通过非法 type 触发 default
    ccu_arithmetic_operator<variable, variable> op(b, c, ccu_arithmetic_operator_type::invalid);
    EXPECT_ANY_THROW({ a = op; });
}

TEST_F(CcuDatatypeOperatorTest, VariableAssignAddrArithUnsupported_Throw)
{
    variable a(&context_);
    address x(&context_), y(&context_);
    a.reset(1);
    x.reset(2);
    y.reset(3);

    // variable = addr (op) addr 仅支持 addition
    EXPECT_ANY_THROW({ a = x * y; });
}

} // namespace
} // namespace ccu_rep
} // namespace asc
