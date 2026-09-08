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

namespace hcomm {
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
    ccu_vir_res virRes(&context_);
    virRes.reset(15);
    virRes.set_die_id(7);
    EXPECT_EQ(virRes.id(), 15);
    EXPECT_EQ(virRes.die_id(), 7);
}

TEST_F(CcuRepInterfaceTest, CcuBuffer_Id)
{
    ccu_rep_context context_;
    ccu_buffer buffer(&context_);
    buffer.reset(3);
    buffer.set_die_id(1);
    uint16_t expectedId = 3 + (1 << 15);
    EXPECT_EQ(buffer.id(), expectedId);
}

TEST_F(CcuRepInterfaceTest, CcuBuf_Id)
{
    ccu_rep_context context_;
    ccu_buf buf(&context_);
    buf.reset(4);
    buf.set_die_id(2);
    uint16_t expectedId = 4 + (2 << 15);
    EXPECT_EQ(buf.id(), expectedId);
}

TEST_F(CcuRepInterfaceTest, LocalNotify_Constructor)
{
    ccu_rep_context context_;
    local_notify notify(&context_);
    notify.reset(8);
    EXPECT_EQ(notify.id(), 8);
}

TEST_F(CcuRepInterfaceTest, Executor_Constructor)
{
    ccu_rep_context context_;
    executor executor(&context_);
    executor.reset(12);
    EXPECT_EQ(executor.id(), 12);
}

TEST_F(CcuRepInterfaceTest, Variable_CopyConstructor)
{
    ccu_rep_context context_;
    variable var1(&context_);
    var1.reset(5);

    variable var2(var1);
    EXPECT_EQ(var2.id(), 5);
}

TEST_F(CcuRepInterfaceTest, Address_CopyConstructor)
{
    ccu_rep_context context_;
    address addr1(&context_);
    addr1.reset(7);

    address addr2(addr1);
    EXPECT_EQ(addr2.id(), 7);
}

TEST_F(CcuRepInterfaceTest, AppendToContext_NullContext)
{
    auto nop = std::make_shared<ccu_rep_nop>(nullptr);
    EXPECT_THROW(append_to_context(nullptr, nop), Hccl::CcuApiException);
}

TEST_F(CcuRepInterfaceTest, CurrentBlock_NullContext) { EXPECT_THROW(current_block(nullptr), Hccl::CcuApiException); }

TEST_F(CcuRepInterfaceTest, SetCurrentBlock_NullContext)
{
    std::shared_ptr<ccu_rep_block> block = nullptr;
    EXPECT_THROW(set_current_block(nullptr, block), Hccl::CcuApiException);
}

TEST_F(CcuRepInterfaceTest, CreateVariable_NullContext)
{
    EXPECT_THROW(create_variable(nullptr), Hccl::CcuApiException);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_AppendAndCurrentBlock)
{
    ccu_rep_context context_;

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
    {
        func_block func_block_(&context_, "testFunc", 1);
        auto current_block = current_block(&context_);
        EXPECT_NE(current_block, nullptr);
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
    {
        loop_block loop_block_(&context_, "testLoop");
        auto current_block = current_block(&context_);
        EXPECT_NE(current_block, nullptr);
    }
    EXPECT_THROW(current_block(&context_), Hccl::CcuApiException);
}

TEST_F(CcuRepInterfaceTest, LoopCall_ConstructorAndAppend)
{
    ccu_rep_context context_;
    loop_call loopCall(&context_, "testLoopCall");

    EXPECT_EQ(loopCall.get_label(), "testLoopCall");
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetDieId)
{
    ccu_rep_context context_;
    context_.set_die_id(3);
    EXPECT_EQ(context_.get_die_id(), 3);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetMissionId)
{
    ccu_rep_context context_;
    context_.set_mission_id(5);
    EXPECT_EQ(context_.get_mission_id(), 5);
}

TEST_F(CcuRepInterfaceTest, CcuRepContext_SetAndGetMissionKey)
{
    ccu_rep_context context_;
    context_.set_mission_key(100);
    EXPECT_EQ(context_.get_mission_key(), 100);
}

TEST_F(CcuRepInterfaceTest, Variable_AssignmentOperator)
{
    ccu_rep_context context_;
    variable var1(&context_);
    var1.reset(1);

    variable var2(&context_);
    var2.reset(2);

    var1 = 100;
}

TEST_F(CcuRepInterfaceTest, Address_AssignmentOperator)
{
    ccu_rep_context context_;
    address addr1(&context_);
    addr1.reset(1);

    address addr2(&context_);
    addr2.reset(2);

    addr1 = 200;
}

TEST_F(CcuRepInterfaceTest, Variable_NotEqualOperator)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(0);

    auto rel = var_ != 10;
    EXPECT_EQ(rel.type_, ccu_relational_operator_type::not_equal);
}

TEST_F(CcuRepInterfaceTest, Variable_EqualOperator)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(0);

    auto rel = var_ == 5;
    EXPECT_EQ(rel.type_, ccu_relational_operator_type::equal);
}

TEST_F(CcuRepInterfaceTest, FuncBlock_OperatorParentheses)
{
    ccu_rep_context context_;
    func_block func_block_(&context_, "testFunc", 1);
}

TEST_F(CcuRepInterfaceTest, LoopBlock_OperatorParentheses)
{
    ccu_rep_context context_;
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
    loop_call loopCall(&context_, "testLoopCall");
}

TEST_F(CcuRepInterfaceTest, Condition_EQ_RelationalOperator)
{
    ccu_rep_context context_;
    variable counter(&context_);
    counter.reset(0);

    auto rel = (counter == 10);
    EXPECT_EQ(rel.type_, ccu_relational_operator_type::equal);
}

TEST_F(CcuRepInterfaceTest, Repeat_NE_RelationalOperator)
{
    ccu_rep_context context_;
    variable counter(&context_);
    counter.reset(0);

    auto rel = (counter != 10);
    EXPECT_EQ(rel.type_, ccu_relational_operator_type::not_equal);
}

TEST_F(CcuRepInterfaceTest, Variable_ArithmeticAssignment)
{
    ccu_rep_context context_;
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
    variable var_a_(&context_);
    var_a_.reset(1);
    variable var_b_(&context_);
    var_b_.reset(2);

    var_a_ = var_b_;
}

TEST_F(CcuRepInterfaceTest, Address_AddressAndVariableAssignment)
{
    ccu_rep_context context_;
    address addr_a_(&context_);
    addr_a_.reset(1);
    address addr_b_(&context_);
    addr_b_.reset(2);
    variable var_b_(&context_);
    var_b_.reset(3);

    addr_a_ = addr_b_;
    addr_a_ = var_b_;
}

} // namespace
} // namespace ccu_rep
} // namespace hcomm
