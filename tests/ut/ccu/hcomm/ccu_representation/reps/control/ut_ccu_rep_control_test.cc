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

#include "ccu/hcomm/ccu_utils.hpp"

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
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

TEST_F(CcuRepJumpTest, Constructor_JumpLE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_le jumpLE(&insGen, "testJumpLE", target_instr_id_, condition_, condition_);
    EXPECT_EQ(jumpLE.type(), ccu_rep_type::jump_le);
}

TEST_F(CcuRepJumpTest, Describe_JumpLE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_le jumpLE(&insGen, "testJumpLE", target_instr_id_, condition_, condition_);
    std::string desc = jumpLE.describe();
    EXPECT_NE(desc.find("Jump To Label[testJumpLE]"), std::string::npos);
    EXPECT_NE(desc.find("<="), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpLE_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_le jumpLE(&insGen, "testJumpLE", target_instr_id_, condition_, condition_);
    jumpLE.reference(jumpLabelPtr);

    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    EXPECT_ANY_THROW(jumpLE.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepJumpTest, Constructor_JumpLE_ImmediateVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_le jumpLE(&insGen, "testJumpLEImm", target_instr_id_, condition_, condition_, 100);
    EXPECT_EQ(jumpLE.type(), ccu_rep_type::jump_le);
}

TEST_F(CcuRepJumpTest, Constructor_JumpGE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ge jumpGE(&insGen, "testJumpGE", target_instr_id_, condition_, condition_);
    EXPECT_EQ(jumpGE.type(), ccu_rep_type::jump_ge);
}

TEST_F(CcuRepJumpTest, Describe_JumpGE)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ge jumpGE(&insGen, "testJumpGE", target_instr_id_, condition_, condition_);
    std::string desc = jumpGE.describe();
    EXPECT_NE(desc.find("Jump To Label[testJumpGE]"), std::string::npos);
    EXPECT_NE(desc.find(">="), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpGE_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_ge jumpGE(&insGen, "testJumpGE", target_instr_id_, condition_, condition_);
    jumpGE.reference(jumpLabelPtr);

    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    EXPECT_ANY_THROW(jumpGE.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepJumpTest, Constructor_JumpGE_ImmediateVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ge jumpGE(&insGen, "testJumpGEImm", target_instr_id_, condition_, condition_, 100);
    EXPECT_EQ(jumpGE.type(), ccu_rep_type::jump_ge);
}

TEST_F(CcuRepJumpTest, Constructor_JumpGT)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_gt jumpGT(&insGen, "testJumpGT", target_instr_id_, condition_, condition_);
    EXPECT_EQ(jumpGT.type(), ccu_rep_type::jump_gt);
}

TEST_F(CcuRepJumpTest, Describe_JumpGT)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_gt jumpGT(&insGen, "testJumpGT", target_instr_id_, condition_, condition_);
    std::string desc = jumpGT.describe();
    EXPECT_NE(desc.find("Jump To Label[testJumpGT]"), std::string::npos);
    EXPECT_NE(desc.find(">"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpGT_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_gt jumpGT(&insGen, "testJumpGT", target_instr_id_, condition_, condition_);
    jumpGT.reference(jumpLabelPtr);

    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    EXPECT_ANY_THROW(jumpGT.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepJumpTest, Constructor_JumpGT_ImmediateVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_gt jumpGT(&insGen, "testJumpGTImm", target_instr_id_, condition_, condition_, 100);
    EXPECT_EQ(jumpGT.type(), ccu_rep_type::jump_gt);
}

TEST_F(CcuRepJumpTest, Constructor_JumpLT)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_lt jumpLT(&insGen, "testJumpLT", target_instr_id_, condition_, condition_);
    EXPECT_EQ(jumpLT.type(), ccu_rep_type::jump_lt);
}

TEST_F(CcuRepJumpTest, Describe_JumpLT)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_lt jumpLT(&insGen, "testJumpLT", target_instr_id_, condition_, condition_);
    std::string desc = jumpLT.describe();
    EXPECT_NE(desc.find("Jump To Label[testJumpLT]"), std::string::npos);
    EXPECT_NE(desc.find("<"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Translate_JumpLT_WithJumpLabel)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_label jump_label_(&insGen, "testLabel");
    auto jumpLabelPtr = std::make_shared<ccu_rep_jump_label>(jump_label_);

    ccu_rep_jump_lt jumpLT(&insGen, "testJumpLT", target_instr_id_, condition_, condition_);
    jumpLT.reference(jumpLabelPtr);

    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;

    jumpLabelPtr->translate(nullptr, instrPtr, instrId, dep);
    instrId = 0;
    instrPtr = instr_;
    EXPECT_ANY_THROW(jumpLT.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepJumpTest, Constructor_JumpLT_ImmediateVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_lt jumpLT(&insGen, "testJumpLTImm", target_instr_id_, condition_, condition_, 100);
    EXPECT_EQ(jumpLT.type(), ccu_rep_type::jump_lt);
}

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
    EXPECT_NE(desc.find("FuncBlock[testFunc]"), std::string::npos);
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
    EXPECT_NE(desc.find("FuncCall[testCall]"), std::string::npos);
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
    EXPECT_NE(desc.find("Jump To Label[testJump]"), std::string::npos);
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
    EXPECT_NE(desc.find("Jump To Label[testJumpNE]"), std::string::npos);
    EXPECT_NE(desc.find("Condition"), std::string::npos);
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
    EXPECT_NE(desc.find("Jump To Label[testJumpEQ]"), std::string::npos);
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

    EXPECT_THROW(func_block_.translate(nullptr, instrPtr, instrId, dep), CcuUtException);
}

TEST_F(CcuRepJumpTest, TranslateViaGenerator_JumpLE_NotSupported)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_le rep(&insGen, "genJumpLE", target_instr_id_, condition_, condition_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_jump_le_translate(nullptr, instrPtr, instrId, &rep, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepJumpTest, TranslateViaGenerator_JumpGE_NotSupported)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_ge rep(&insGen, "genJumpGE", target_instr_id_, condition_, condition_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_jump_ge_translate(nullptr, instrPtr, instrId, &rep, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepJumpTest, TranslateViaGenerator_JumpGT_NotSupported)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_gt rep(&insGen, "genJumpGT", target_instr_id_, condition_, condition_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_jump_gt_translate(nullptr, instrPtr, instrId, &rep, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepJumpTest, TranslateViaGenerator_JumpLT_NotSupported)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    ccu_rep_jump_lt rep(&insGen, "genJumpLT", target_instr_id_, condition_, condition_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_jump_lt_translate(nullptr, instrPtr, instrId, &rep, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepFuncBlockTest, SetCallLayer_InvalidNoInnerCall_NoThrow)
{
    auto func_block_ = std::make_shared<ccu_rep_func_block>(&insGen, "nestFunc");
    func_block_->append(std::make_shared<ccu_rep_nop>(&insGen));

    // 无内层 func_call: 嵌套扫描后层数保持 0, 不抛错
    EXPECT_NO_THROW(func_block_->set_call_layer(0xFFFF));
    EXPECT_EQ(func_block_->get_call_layer(), 0U);
}

TEST_F(CcuRepFuncBlockTest, SetCallLayer_InvalidWithInnerCall_Throws)
{
    auto func_block_ = std::make_shared<ccu_rep_func_block>(&insGen, "nestFunc");
    // 未 reference 的 func_call: get_call_layer() 返回 func_nest_max
    auto inner_call = std::make_shared<ccu_rep_func_call>(&insGen, "inner");
    func_block_->append(inner_call);

    // func_nest_max = 1: 内层 func_call 使层数超上限抛错
    EXPECT_ANY_THROW(func_block_->set_call_layer(0xFFFF));
}

TEST_F(CcuRepFuncBlockTest, Translate_WithoutManagerThrows)
{
    ccu_rep_func_block func_block_(&insGen, "noMgrFunc");
    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    EXPECT_ANY_THROW(func_block_.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepFuncBlockTest, DefineOutArg_ExceedsMaxThrows)
{
    ccu_rep_context context_;
    ccu_rep_func_block func_block_(&insGen, "argFunc");
    variable v1(&context_);
    variable v2(&context_);
    EXPECT_NO_FATAL_FAILURE(func_block_.define_out_arg(v1));
    // func_arg_max = 1, 第二个出参抛错
    EXPECT_ANY_THROW(func_block_.define_out_arg(v2));
}

TEST_F(CcuRepFuncCallTest, SetOutArg_ExceedsMaxThrows)
{
    ccu_rep_context context_;
    ccu_rep_func_call func_call_(&insGen, "argCall");
    variable v1(&context_);
    variable v2(&context_);
    EXPECT_NO_FATAL_FAILURE(func_call_.set_out_arg(v1));
    EXPECT_ANY_THROW(func_call_.set_out_arg(v2));
}

TEST_F(CcuRepFuncCallTest, SetOutArg_VarListExceedsMaxThrows)
{
    ccu_rep_context context_;
    ccu_rep_func_call func_call_(&insGen, "argListCall");
    variable v1(&context_);
    variable v2(&context_);
    // func_arg_max = 1, 两个元素的出参列表抛错
    EXPECT_ANY_THROW(func_call_.set_out_arg(std::vector<variable>{v1, v2}));
}

TEST_F(CcuRepJumpTest, Constructor_JumpNE_VarExpectedVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    variable expected_var_(&context_);
    // var 期望值变体: (ins, label, target, condition, expected_var), 仅用于 A6 翻译
    ccu_rep_jump_ne jumpNE(&insGen, "testJumpNEVar", target_instr_id_, condition_, expected_var_);
    EXPECT_EQ(jumpNE.type(), ccu_rep_type::jump_ne);
}

TEST_F(CcuRepJumpTest, Constructor_JumpEQ_VarExpectedVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    variable expected_var_(&context_);
    // var 期望值变体: (ins, label, target, condition, expected_var), 仅用于 A6 翻译
    ccu_rep_jump_eq jumpEQ(&insGen, "testJumpEQVar", target_instr_id_, condition_, expected_var_);
    EXPECT_EQ(jumpEQ.type(), ccu_rep_type::jump_eq);
}

TEST_F(CcuRepJumpTest, Describe_JumpNE_VarExpectedVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    variable expected_var_(&context_);
    ccu_rep_jump_ne jumpNE(&insGen, "testJumpNEVar", target_instr_id_, condition_, expected_var_);
    std::string desc = jumpNE.describe();
    EXPECT_NE(desc.find("testJumpNEVar"), std::string::npos);
}

TEST_F(CcuRepJumpTest, Describe_JumpEQ_VarExpectedVariant)
{
    ccu_rep_context context_;
    variable target_instr_id_(&context_);
    variable condition_(&context_);
    variable expected_var_(&context_);
    ccu_rep_jump_eq jumpEQ(&insGen, "testJumpEQVar", target_instr_id_, condition_, expected_var_);
    std::string desc = jumpEQ.describe();
    EXPECT_NE(desc.find("testJumpEQVar"), std::string::npos);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
