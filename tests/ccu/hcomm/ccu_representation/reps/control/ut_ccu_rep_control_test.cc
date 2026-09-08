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
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "ccu_api_exception.h"
#include <gtest/gtest.h>
#include <string>
#include <memory>

namespace hcomm {
namespace ccu_rep {
namespace {

class CcuRepControlTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
    void TearDown() override {}
};

class CcuRepFuncBlockTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepFuncCallTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepJumpTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepJumpLabelTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepFuncBlockTest, Constructor_Label)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    EXPECT_EQ(func_block_.type(), ccu_rep_type::func_block);
    EXPECT_EQ(func_block_.get_label(), "testFunc");
}

TEST_F(CcuRepFuncBlockTest, describe)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    std::string desc = func_block_.describe();
    EXPECT_NE(desc.find("func_block[testFunc]"), std::string::npos);
}

TEST_F(CcuRepFuncBlockTest, set_func_manager)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_reference_manager func_manager_(0);
    func_block_.set_func_manager(&func_manager_);
    EXPECT_NO_FATAL_FAILURE(func_block_.set_func_manager(&func_manager_));
}

TEST_F(CcuRepFuncBlockTest, DefineInArg_SingleVariable)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_context context_;
    variable var_(&context_);
    EXPECT_NO_FATAL_FAILURE(func_block_.define_in_arg(var_));
}

TEST_F(CcuRepFuncBlockTest, DefineOutArg_SingleVariable)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_context context_;
    variable var_(&context_);
    EXPECT_NO_FATAL_FAILURE(func_block_.define_out_arg(var_));
}

TEST_F(CcuRepFuncBlockTest, DefineInArg_VariableList)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_context context_;
    std::vector<variable> var_list = {variable(&context_)};
    EXPECT_NO_FATAL_FAILURE(func_block_.define_in_arg(var_list));
}

TEST_F(CcuRepFuncBlockTest, DefineOutArg_VariableList)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_context context_;
    std::vector<variable> var_list = {variable(&context_)};
    EXPECT_NO_FATAL_FAILURE(func_block_.define_out_arg(var_list));
}

TEST_F(CcuRepFuncBlockTest, SetCallLayer_Valid)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    EXPECT_NO_FATAL_FAILURE(func_block_.set_call_layer(1));
    EXPECT_EQ(func_block_.get_call_layer(), 1);
}

TEST_F(CcuRepFuncBlockTest, instr_count)
{
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    ccu_rep_context context_;
    variable var_(&context_);
    func_block_.define_in_arg(var_);
    func_block_.define_out_arg(var_);
    uint16_t count_ = func_block_.instr_count();
    EXPECT_GE(count_, 2);
}

TEST_F(CcuRepFuncCallTest, Constructor_Label)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    EXPECT_EQ(func_call.type(), ccu_rep_type::func_call);
    EXPECT_EQ(func_call.get_label(), "testCall");
}

TEST_F(CcuRepFuncCallTest, Constructor_FuncAddrVar)
{
    ccu_rep_context context_;
    variable func_addr(&context_);
    ccu_rep_func_call func_call(&insGen, func_addr);
    EXPECT_EQ(func_call.type(), ccu_rep_type::func_call);
}

TEST_F(CcuRepFuncCallTest, describe)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    std::string desc = func_call.describe();
    EXPECT_NE(desc.find("func_call[testCall]"), std::string::npos);
}

TEST_F(CcuRepFuncCallTest, set_func_manager)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_reference_manager func_manager_(0);
    EXPECT_NO_FATAL_FAILURE(func_call.set_func_manager(&func_manager_));
}

TEST_F(CcuRepFuncCallTest, reference)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_func_block func_block_(&insGen, "testFunc");
    auto func_block_ptr = std::make_shared<ccu_rep_func_block>(func_block_);
    EXPECT_NO_FATAL_FAILURE(func_call.reference(func_block_ptr));
}

TEST_F(CcuRepFuncCallTest, SetInArg_SingleVariable)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_context context_;
    variable var_(&context_);
    EXPECT_NO_FATAL_FAILURE(func_call.set_in_arg(var_));
}

TEST_F(CcuRepFuncCallTest, SetOutArg_SingleVariable)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_context context_;
    variable var_(&context_);
    EXPECT_NO_FATAL_FAILURE(func_call.set_out_arg(var_));
}

TEST_F(CcuRepFuncCallTest, SetInArg_VariableList)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_context context_;
    std::vector<variable> var_list = {variable(&context_)};
    EXPECT_NO_FATAL_FAILURE(func_call.set_in_arg(var_list));
}

TEST_F(CcuRepFuncCallTest, SetOutArg_VariableList)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_context context_;
    std::vector<variable> var_list = {variable(&context_)};
    EXPECT_NO_FATAL_FAILURE(func_call.set_out_arg(var_list));
}

TEST_F(CcuRepFuncCallTest, instr_count)
{
    ccu_rep_func_call func_call(&insGen, "testCall");
    ccu_rep_context context_;
    variable var_(&context_);
    func_call.set_in_arg(var_);
    func_call.set_out_arg(var_);
    uint16_t count_ = func_call.instr_count();
    EXPECT_GE(count_, 4);
}

TEST_F(CcuRepJumpTest, Constructor_Label)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    ccu_rep_jump jump_(&insGen, "testJump", target_instr_id_);
    EXPECT_EQ(jump_.type(), ccu_rep_type::jump);
}

TEST_F(CcuRepJumpTest, describe)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    ccu_rep_jump jump_(&insGen, "testJump", target_instr_id_);
    std::string desc = jump_.describe();
    EXPECT_NE(desc.find("jump To Label[testJump]"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump jump_(&insGen, "testJump", target_instr_id_);
    jump_.reference(jumpLabelPtr);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    bool result = jump_.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_EQ(jump_.translated(), true);
}

TEST_F(CcuRepJumpTest, Constructor_JumpNE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ne jumpNE(&insGen, "testJumpNE", target_instr_id_, condition_, condition_, 100);
    EXPECT_EQ(jumpNE.type(), ccu_rep_type::jump_ne);
}

TEST_F(CcuRepJumpTest, Describe_JumpNE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ne jumpNE(&insGen, "testJumpNE", target_instr_id_, condition_, condition_, 100);
    std::string desc = jumpNE.describe();
    EXPECT_NE(desc.find("jump To Label[testJumpNE]"), std::string::npos);
    EXPECT_NE(desc.find("condition"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpNE_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_ne jumpNE(&insGen, "testJumpNE", target_instr_id_, condition_, condition_, 100);
    jumpNE.reference(jumpLabelPtr);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    bool result = jumpNE.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepJumpTest, Constructor_JumpEQ)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_eq jumpEQ(&insGen, "testJumpEQ", target_instr_id_, condition_, condition_, 50);
    EXPECT_EQ(jumpEQ.type(), ccu_rep_type::jump_eq);
}

TEST_F(CcuRepJumpTest, Describe_JumpEQ)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_eq jumpEQ(&insGen, "testJumpEQ", target_instr_id_, condition_, condition_, 50);
    std::string desc = jumpEQ.describe();
    EXPECT_NE(desc.find("jump To Label[testJumpEQ]"), std::string::npos);
    EXPECT_NE(desc.find("Be equal"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpEQ_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_eq jumpEQ(&insGen, "testJumpEQ", target_instr_id_, condition_, condition_, 50);
    jumpEQ.reference(jumpLabelPtr);

    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    bool result = jumpEQ.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepJumpLabelTest, Constructor_Label)
{
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    EXPECT_EQ(jump_label_.type(), ccu_rep_type::jump_label);
    EXPECT_EQ(jump_label_.get_label(), "testLabel");
}

TEST_F(CcuRepJumpLabelTest, describe)
{
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    std::string desc = jump_label_.describe();
    EXPECT_NE(desc.find("JumpLabel[testLabel]"), std::string::npos);
}

TEST_F(CcuRepJumpLabelTest, AppendRep)
{
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    EXPECT_NO_FATAL_FAILURE(jump_label_.append(nop));
    EXPECT_EQ(jump_label_.get_reps().size(), 2);
}

TEST_F(CcuRepControlTest, CcuRepJumpBase_Reference)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump jump_(&insGen, "testJump", target_instr_id_);
    jump_.reference(jumpLabelPtr);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    bool result = jump_.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_EQ(jump_.translated(), true);
}

TEST_F(CcuRepControlTest, CcuRepTranslator_CheckTypeNullptr_Throw)
{
    ccu_rep_reference_manager func_manager_(0);
    func_manager_.set_ref_block("nullFunc", nullptr);

    ccu_rep_func_block func_block_(&insGen, "outerFunc");
    auto func_call_ptr = std::make_shared<ccu_rep_func_call>(&insGen, "nullFunc");
    func_block_.append(func_call_ptr);
    func_block_.set_func_manager(&func_manager_);

    ccu_instr instr_[128] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    EXPECT_THROW(func_block_.translate(nullptr, instrPtr, instrId, dep), Hccl::CcuApiException);
}

} // namespace
} // namespace ccu_rep
} // namespace hcomm
