/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_add_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_type_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_operator_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_mul_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_sub_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include <gtest/gtest.h>
#include <string>

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepAddTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    ccu_instr instr_{};
    uint16_t instrId{0};
    trans_dep dep{};

    void SetUp() override
    {
        memset(&instr_, 0, sizeof(instr_));
        dep.reserve_gsa_id = 1;
        dep.reserve_xn_id = 2;
    }
};

TEST_F(CcuRepAddTest, Constructor_AddrPlusVarToAddr)
{
    address addr_c_;
    address addr_a_;
    variable var_b_;
    addr_c_.reset(1);
    addr_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), add_sub_type::addr_plus_var_to_addr);
    EXPECT_EQ(rep.type(), ccu_rep_type::add);
}

TEST_F(CcuRepAddTest, Constructor_AddrPlusAddrToAddr)
{
    address addr_c_;
    address addr_a_;
    address addr_b_;
    addr_c_.reset(1);
    addr_a_.reset(2);
    addr_b_.reset(3);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, addr_b_);

    EXPECT_EQ(rep.get_sub_type(), add_sub_type::addr_plus_addr_to_addr);
    EXPECT_EQ(rep.type(), ccu_rep_type::add);
}

TEST_F(CcuRepAddTest, Constructor_VarPlusVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_add rep(&insGen, var_c_, var_a_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), add_sub_type::var_plus_var_to_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::add);
}

TEST_F(CcuRepAddTest, Constructor_SelfAddAddress)
{
    address addr_a_;
    variable offset;
    addr_a_.reset(1);
    offset.reset(2);
    ccu_rep_add rep(&insGen, addr_a_, offset);

    EXPECT_EQ(rep.get_sub_type(), add_sub_type::self_add_address);
    EXPECT_EQ(rep.type(), ccu_rep_type::add);
}

TEST_F(CcuRepAddTest, Constructor_SelfAddVariable)
{
    variable var_a_;
    variable offset;
    var_a_.reset(1);
    offset.reset(2);
    ccu_rep_add rep(&insGen, var_a_, offset);

    EXPECT_EQ(rep.get_sub_type(), add_sub_type::self_add_variable);
    EXPECT_EQ(rep.type(), ccu_rep_type::add);
}

TEST_F(CcuRepAddTest, Translate_AddrPlusVarToAddr)
{
    address addr_c_;
    address addr_a_;
    variable var_b_;
    addr_c_.reset(5);
    addr_a_.reset(3);
    var_b_.reset(7);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, var_b_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_ad_id, addr_c_.id());
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_am_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsa_xn.xn_id, var_b_.id());
    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x4);
}

TEST_F(CcuRepAddTest, Translate_AddrPlusAddrToAddr)
{
    address addr_c_;
    address addr_a_;
    address addr_b_;
    addr_c_.reset(5);
    addr_a_.reset(3);
    addr_b_.reset(7);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, addr_b_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_ad_id, addr_c_.id());
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_am_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_an_id, addr_b_.id());
    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x5);
}

TEST_F(CcuRepAddTest, Translate_VarPlusVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(5);
    var_a_.reset(3);
    var_b_.reset(7);
    ccu_rep_add rep(&insGen, var_c_, var_a_, var_b_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_xx.xd_id, var_c_.id());
    EXPECT_EQ(instr_.v1.load_xx.xm_id, var_a_.id());
    EXPECT_EQ(instr_.v1.load_xx.xn_id, var_b_.id());
    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x6);
}

TEST_F(CcuRepAddTest, Translate_SelfAddAddress)
{
    address addr_a_;
    variable offset;
    addr_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, addr_a_, offset);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_ad_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_am_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsa_xn.xn_id, offset.id());
}

TEST_F(CcuRepAddTest, Translate_SelfAddVariable)
{
    variable var_a_;
    variable offset;
    var_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, var_a_, offset);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_xx.xd_id, var_a_.id());
    EXPECT_EQ(instr_.v1.load_xx.xm_id, var_a_.id());
    EXPECT_EQ(instr_.v1.load_xx.xn_id, offset.id());
}

TEST_F(CcuRepAddTest, Describe_AddrPlusVarToAddr)
{
    address addr_c_;
    address addr_a_;
    variable var_b_;
    addr_c_.reset(1);
    addr_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_AddrPlusAddrToAddr)
{
    address addr_c_;
    address addr_a_;
    address addr_b_;
    addr_c_.reset(1);
    addr_a_.reset(2);
    addr_b_.reset(3);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, addr_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Address[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_VarPlusVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_add rep(&insGen, var_c_, var_a_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddAddress)
{
    address addr_a_;
    variable offset;
    addr_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, addr_a_, offset);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[5]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[7]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddVariable)
{
    variable var_a_;
    variable offset;
    var_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, var_a_, offset);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[5]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[7]"), std::string::npos);
}

class CcuRepAssignTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    ccu_instr instr_{};
    uint16_t instrId{0};
    trans_dep dep{};

    void SetUp() override
    {
        memset(&instr_, 0, sizeof(instr_));
        dep.reserve_gsa_id = 1;
        dep.reserve_xn_id = 2;
    }
};

TEST_F(CcuRepAssignTest, Constructor_ImdToVariable)
{
    variable var_a_;
    var_a_.reset(1);
    uint64_t immediate_ = 100;
    ccu_rep_assign rep(&insGen, var_a_, immediate_);

    EXPECT_EQ(rep.get_sub_type(), assign_sub_type::imd_to_variable);
    EXPECT_EQ(rep.type(), ccu_rep_type::assign);
    EXPECT_EQ(rep.get_immed(), immediate_);
}

TEST_F(CcuRepAssignTest, Constructor_ImdToAddr)
{
    address addr_a_;
    addr_a_.reset(1);
    uint64_t immediate_ = 200;
    ccu_rep_assign rep(&insGen, addr_a_, immediate_);

    EXPECT_EQ(rep.get_sub_type(), assign_sub_type::imd_to_addr);
    EXPECT_EQ(rep.type(), ccu_rep_type::assign);
    EXPECT_EQ(rep.get_immed(), immediate_);
}

TEST_F(CcuRepAssignTest, Constructor_VarToAddr)
{
    address addr_a_;
    variable var_a_;
    addr_a_.reset(1);
    var_a_.reset(2);
    ccu_rep_assign rep(&insGen, addr_a_, var_a_);

    EXPECT_EQ(rep.get_sub_type(), assign_sub_type::var_to_addr);
    EXPECT_EQ(rep.type(), ccu_rep_type::assign);
}

TEST_F(CcuRepAssignTest, Constructor_AddrToAddr)
{
    address addr_b_;
    address addr_a_;
    addr_b_.reset(1);
    addr_a_.reset(2);
    ccu_rep_assign rep(&insGen, addr_b_, addr_a_);

    EXPECT_EQ(rep.get_sub_type(), assign_sub_type::addr_to_addr);
    EXPECT_EQ(rep.type(), ccu_rep_type::assign);
}

TEST_F(CcuRepAssignTest, Constructor_VarToVar)
{
    variable var_b_;
    variable var_a_;
    var_b_.reset(1);
    var_a_.reset(2);
    ccu_rep_assign rep(&insGen, var_b_, var_a_);

    EXPECT_EQ(rep.get_sub_type(), assign_sub_type::var_to_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::assign);
}

TEST_F(CcuRepAssignTest, Translate_ImdToVariable)
{
    variable var_a_;
    var_a_.reset(5);
    uint64_t immediate_ = 0x12345678ABCD;
    ccu_rep_assign rep(&insGen, var_a_, immediate_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instr_.v1.load_imd_to_xn.xn_id, var_a_.id());
    EXPECT_EQ(instr_.v1.load_imd_to_xn.immediate, immediate_);
    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x3);
}

TEST_F(CcuRepAssignTest, Translate_ImdToAddr)
{
    address addr_a_;
    addr_a_.reset(5);
    uint64_t immediate_ = 0xDEADBEEF;
    ccu_rep_assign rep(&insGen, addr_a_, immediate_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_imd_to_gsa.gsa_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_imd_to_gsa.immediate, immediate_);
    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x2);
}

TEST_F(CcuRepAssignTest, Translate_VarToAddr)
{
    address addr_a_;
    variable var_a_;
    addr_a_.reset(5);
    var_a_.reset(7);
    ccu_rep_assign rep(&insGen, addr_a_, var_a_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_ad_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_am_id, dep.reserve_gsa_id);
    EXPECT_EQ(instr_.v1.load_gsa_xn.xn_id, var_a_.id());
}

TEST_F(CcuRepAssignTest, Translate_AddrToAddr)
{
    address addr_b_;
    address addr_a_;
    addr_b_.reset(5);
    addr_a_.reset(7);
    ccu_rep_assign rep(&insGen, addr_b_, addr_a_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_ad_id, addr_b_.id());
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_am_id, addr_a_.id());
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_an_id, dep.reserve_gsa_id);
}

TEST_F(CcuRepAssignTest, Translate_VarToVar)
{
    variable var_b_;
    variable var_a_;
    var_b_.reset(5);
    var_a_.reset(7);
    ccu_rep_assign rep(&insGen, var_b_, var_a_);

    ccu_instr* instrPtr = &instr_;
    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr_.v1.load_xx.xd_id, var_b_.id());
    EXPECT_EQ(instr_.v1.load_xx.xm_id, var_a_.id());
    EXPECT_EQ(instr_.v1.load_xx.xn_id, dep.reserve_xn_id);
}

TEST_F(CcuRepAssignTest, Describe_ImdToVariable)
{
    variable var_a_;
    var_a_.reset(1);
    ccu_rep_assign rep(&insGen, var_a_, 100);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Value[100]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_ImdToAddr)
{
    address addr_a_;
    addr_a_.reset(1);
    ccu_rep_assign rep(&insGen, addr_a_, 200);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Value[200]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_VarToAddr)
{
    address addr_a_;
    variable var_a_;
    addr_a_.reset(1);
    var_a_.reset(2);
    ccu_rep_assign rep(&insGen, addr_a_, var_a_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_AddrToAddr)
{
    address addr_b_;
    address addr_a_;
    addr_b_.reset(1);
    addr_a_.reset(2);
    ccu_rep_assign rep(&insGen, addr_b_, addr_a_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_VarToVar)
{
    variable var_b_;
    variable var_a_;
    var_b_.reset(1);
    var_a_.reset(2);
    ccu_rep_assign rep(&insGen, var_b_, var_a_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Var[1]"), std::string::npos);
    EXPECT_NE(desc.find("Var[2]"), std::string::npos);
}

class CcuOperatorTest : public ::testing::Test {};

TEST_F(CcuOperatorTest, VariablePlusVariable)
{
    variable var_a_;
    variable var_b_;
    var_a_.reset(1);
    var_b_.reset(2);
    auto op = var_a_ + var_b_;

    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs.id(), var_b_.id());
}

TEST_F(CcuOperatorTest, VariablePlusAddress)
{
    variable var_a_;
    address addr_b_;
    var_a_.reset(1);
    addr_b_.reset(2);
    auto op = var_a_ + addr_b_;

    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs.id(), addr_b_.id());
}

TEST_F(CcuOperatorTest, AddressPlusVariable)
{
    address addr_a_;
    variable var_b_;
    addr_a_.reset(1);
    var_b_.reset(2);
    auto op = addr_a_ + var_b_;

    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.id(), var_b_.id());
    EXPECT_EQ(op.rhs.id(), addr_a_.id());
}

TEST_F(CcuOperatorTest, AddressPlusAddress)
{
    address addr_a_;
    address addr_b_;
    addr_a_.reset(1);
    addr_b_.reset(2);
    auto op = addr_a_ + addr_b_;

    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.id(), addr_a_.id());
    EXPECT_EQ(op.rhs.id(), addr_b_.id());
}

TEST_F(CcuOperatorTest, VariableNotEqualImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ != 100;

    EXPECT_EQ(op.type, ccu_relational_operator_type::not_equal);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs, 100);
}

TEST_F(CcuOperatorTest, VariableEqualImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ == 200;

    EXPECT_EQ(op.type, ccu_relational_operator_type::equal);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs, 200);
}

TEST_F(CcuOperatorTest, VariableMulVariable)
{
    variable var_a_;
    variable var_b_;
    var_a_.reset(1);
    var_b_.reset(2);
    auto op = var_a_ * var_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs.id(), var_b_.id());
}

TEST_F(CcuOperatorTest, VariableMulAddress)
{
    variable var_a_;
    address addr_b_;
    var_a_.reset(1);
    addr_b_.reset(2);
    auto op = var_a_ * addr_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
}

TEST_F(CcuOperatorTest, VariableMulImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ * uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
}

TEST_F(CcuOperatorTest, VariableSubVariable)
{
    variable var_a_;
    variable var_b_;
    var_a_.reset(1);
    var_b_.reset(2);
    auto op = var_a_ - var_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, VariableSubAddress)
{
    variable var_a_;
    address addr_b_;
    var_a_.reset(1);
    addr_b_.reset(2);
    auto op = var_a_ - addr_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, VariableSubImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ - uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, AddressMulVariable)
{
    address addr_a_;
    variable var_b_;
    addr_a_.reset(1);
    var_b_.reset(2);
    auto op = addr_a_ * var_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
}

TEST_F(CcuOperatorTest, AddressMulAddress)
{
    address addr_a_;
    address addr_b_;
    addr_a_.reset(1);
    addr_b_.reset(2);
    auto op = addr_a_ * addr_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
}

TEST_F(CcuOperatorTest, AddressMulImmediate)
{
    address addr_a_;
    addr_a_.reset(1);
    auto op = addr_a_ * uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::multiplication);
}

TEST_F(CcuOperatorTest, AddressSubVariable)
{
    address addr_a_;
    variable var_b_;
    addr_a_.reset(1);
    var_b_.reset(2);
    auto op = addr_a_ - var_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, AddressSubAddress)
{
    address addr_a_;
    address addr_b_;
    addr_a_.reset(1);
    addr_b_.reset(2);
    auto op = addr_a_ - addr_b_;
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, AddressSubImmediate)
{
    address addr_a_;
    addr_a_.reset(1);
    auto op = addr_a_ - uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::subtraction);
}

TEST_F(CcuOperatorTest, AddressPlusVariable_OrderConsistency)
{
    address addr_a_;
    variable var_b_;
    addr_a_.reset(1);
    var_b_.reset(2);
    auto op = addr_a_ + var_b_;
    // address + variable 内部 lhs=variable, rhs=address (与 variable+address 对称)
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
}

TEST_F(CcuOperatorTest, AddressPlusImmediate)
{
    address addr_a_;
    addr_a_.reset(1);
    auto op = addr_a_ + uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
}

TEST_F(CcuOperatorTest, VariablePlusImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ + uint16_t(10);
    EXPECT_EQ(op.type, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.rhs, 10);
}

class CcuRepMulTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepMulTest, Constructor_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_rep_mul m1(&insGen, var_c_, var_a_, var_b_);
    EXPECT_EQ(m1.get_sub_type(), mul_sub_type::var_mul_var_to_var);
    EXPECT_EQ(m1.type(), ccu_rep_type::mul);

    ccu_rep_mul m2(&insGen, var_c_, var_a_, uint16_t(10));
    EXPECT_EQ(m2.get_sub_type(), mul_sub_type::var_mul_immed_to_var);

    ccu_rep_mul m3(&insGen, var_a_, var_b_);
    EXPECT_EQ(m3.get_sub_type(), mul_sub_type::self_mul_var_variable);

    ccu_rep_mul m4(&insGen, var_a_, uint16_t(10));
    EXPECT_EQ(m4.get_sub_type(), mul_sub_type::self_mul_immed_variable);

    ccu_rep_mul m5(&insGen, addr_c_, var_a_, var_b_);
    EXPECT_EQ(m5.get_sub_type(), mul_sub_type::var_mul_var_to_addr);

    ccu_rep_mul m6(&insGen, addr_c_, var_a_, addr_b_);
    EXPECT_EQ(m6.get_sub_type(), mul_sub_type::var_mul_addr_to_addr);

    ccu_rep_mul m7(&insGen, addr_a_, var_b_);
    EXPECT_EQ(m7.get_sub_type(), mul_sub_type::self_mul_var_address);

    ccu_rep_mul m8(&insGen, addr_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(m8.get_sub_type(), mul_sub_type::addr_mul_immed_to_addr);

    ccu_rep_mul m9(&insGen, addr_c_, var_a_, uint16_t(10));
    EXPECT_EQ(m9.get_sub_type(), mul_sub_type::var_mul_immed_to_addr);

    ccu_rep_mul m10(&insGen, addr_a_, uint16_t(10));
    EXPECT_EQ(m10.get_sub_type(), mul_sub_type::self_mul_immed_address);

    ccu_rep_mul m11(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(m11.get_sub_type(), mul_sub_type::addr_mul_immed_to_var);
}

TEST_F(CcuRepMulTest, Describe_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    EXPECT_NE(ccu_rep_mul(&insGen, var_c_, var_a_, var_b_).describe().find("*"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, var_c_, var_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, var_a_, var_b_).describe().find("*="), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, var_a_, uint16_t(10)).describe().find("*="), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, var_a_, var_b_).describe().find("Address"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, var_a_, addr_b_).describe().find("Address"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_a_, var_b_).describe().find("*="), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, var_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, var_c_, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
}

TEST_F(CcuRepMulTest, Getters)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_rep_mul mVarVar(&insGen, var_c_, var_a_, var_b_);
    EXPECT_EQ(mVarVar.get_var_a().id(), var_a_.id());
    EXPECT_EQ(mVarVar.get_var_b().id(), var_b_.id());
    EXPECT_EQ(mVarVar.get_var_c().id(), var_c_.id());

    ccu_rep_mul mAddrAddr(&insGen, addr_c_, var_a_, addr_b_);
    EXPECT_EQ(mAddrAddr.get_addr_b().id(), addr_b_.id());
    EXPECT_EQ(mAddrAddr.get_addr_c().id(), addr_c_.id());
    EXPECT_EQ(mAddrAddr.get_var_a().id(), var_a_.id());

    ccu_rep_mul mImmed(&insGen, var_c_, var_a_, uint16_t(10));
    EXPECT_EQ(mImmed.get_immed_b(), uint16_t(10));

    ccu_rep_mul mAddrA(&insGen, addr_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(mAddrA.get_addr_a().id(), addr_a_.id());
    EXPECT_EQ(mAddrA.get_addr_c().id(), addr_c_.id());
}

class CcuRepSubTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepSubTest, Constructor_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_rep_sub s1(&insGen, var_c_, var_a_, var_b_);
    EXPECT_EQ(s1.get_sub_type(), minus_sub_type::var_minus_var_to_var);
    EXPECT_EQ(s1.type(), ccu_rep_type::sub);

    ccu_rep_sub s2(&insGen, var_c_, var_a_, uint16_t(10));
    EXPECT_EQ(s2.get_sub_type(), minus_sub_type::var_minus_immed_to_var);

    ccu_rep_sub s3(&insGen, var_a_, var_b_);
    EXPECT_EQ(s3.get_sub_type(), minus_sub_type::self_sub_var_variable);

    ccu_rep_sub s4(&insGen, var_a_, uint16_t(10));
    EXPECT_EQ(s4.get_sub_type(), minus_sub_type::self_sub_immed_variable);

    ccu_rep_sub s5(&insGen, addr_c_, addr_a_, var_b_);
    EXPECT_EQ(s5.get_sub_type(), minus_sub_type::addr_minus_var_to_addr);

    ccu_rep_sub s6(&insGen, addr_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(s6.get_sub_type(), minus_sub_type::addr_minus_immed_to_addr);

    ccu_rep_sub s7(&insGen, addr_a_, var_b_);
    EXPECT_EQ(s7.get_sub_type(), minus_sub_type::self_sub_var_address);

    ccu_rep_sub s8(&insGen, addr_a_, uint16_t(10));
    EXPECT_EQ(s8.get_sub_type(), minus_sub_type::self_sub_immed_address);

    ccu_rep_sub s9(&insGen, addr_c_, var_a_, uint16_t(10));
    EXPECT_EQ(s9.get_sub_type(), minus_sub_type::var_minus_immed_to_addr);

    ccu_rep_sub s10(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(s10.get_sub_type(), minus_sub_type::addr_minus_immed_to_var);
}

TEST_F(CcuRepSubTest, Describe_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    EXPECT_NE(ccu_rep_sub(&insGen, var_c_, var_a_, var_b_).describe().find("-"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, var_c_, var_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, var_a_, var_b_).describe().find("-="), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, var_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, addr_c_, addr_a_, var_b_).describe().find("Variable"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, addr_c_, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, addr_a_, var_b_).describe().find("-="), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, addr_c_, var_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
    EXPECT_NE(ccu_rep_sub(&insGen, var_c_, addr_a_, uint16_t(10)).describe().find("Immed"), std::string::npos);
}

TEST_F(CcuRepSubTest, Getters)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_rep_sub sVarVar(&insGen, var_c_, var_a_, var_b_);
    EXPECT_EQ(sVarVar.get_var_a().id(), var_a_.id());
    EXPECT_EQ(sVarVar.get_var_b().id(), var_b_.id());
    EXPECT_EQ(sVarVar.get_var_c().id(), var_c_.id());

    ccu_rep_sub sAddrVar(&insGen, addr_c_, addr_a_, var_b_);
    EXPECT_EQ(sAddrVar.get_addr_a().id(), addr_a_.id());
    EXPECT_EQ(sAddrVar.get_addr_c().id(), addr_c_.id());
    EXPECT_EQ(sAddrVar.get_var_b().id(), var_b_.id());

    ccu_rep_sub sImmed(&insGen, var_c_, var_a_, uint16_t(10));
    EXPECT_EQ(sImmed.get_immed_b(), uint16_t(10));

    ccu_rep_sub sAddrImmed(&insGen, addr_a_, uint16_t(10));
    EXPECT_EQ(sAddrImmed.get_addr_a().id(), addr_a_.id());
}

TEST_F(CcuRepMulTest, Translate_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_instr instr_[20] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_gsa_id = 1;
    dep.reserve_xn_id = 2;

    ccu_rep_mul m1(&insGen, var_c_, var_a_, var_b_);
    EXPECT_ANY_THROW(m1.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_mul m2(&insGen, addr_c_, var_a_, var_b_);
    EXPECT_ANY_THROW(m2.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_mul m3(&insGen, addr_a_, var_b_);
    EXPECT_ANY_THROW(m3.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_mul m4(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_ANY_THROW(m4.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepSubTest, Translate_AllSubTypes)
{
    variable var_a_;
    variable var_b_;
    variable var_c_;
    var_a_.reset(1);
    var_b_.reset(2);
    var_c_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_instr instr_[20] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_gsa_id = 1;
    dep.reserve_xn_id = 2;

    ccu_rep_sub s1(&insGen, var_c_, var_a_, var_b_);
    EXPECT_ANY_THROW(s1.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_sub s2(&insGen, addr_c_, addr_a_, var_b_);
    EXPECT_ANY_THROW(s2.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_sub s3(&insGen, addr_a_, var_b_);
    EXPECT_ANY_THROW(s3.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_sub s4(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_ANY_THROW(s4.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepMulTest, TranslateViaGenerator_NotSupported)
{
    variable var_c_(nullptr);
    variable var_a_(nullptr);
    variable var_b_(nullptr);
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);

    ccu_rep_mul rep(&insGen, var_c_, var_a_, var_b_);
    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_mul_translate(nullptr, instrPtr, &rep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepSubTest, TranslateViaGenerator_NotSupported)
{
    variable var_c_(nullptr);
    variable var_a_(nullptr);
    variable var_b_(nullptr);
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);

    ccu_rep_sub rep(&insGen, var_c_, var_a_, var_b_);
    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_sub_translate(nullptr, instrPtr, &rep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_NOT_SUPPORT);
}

TEST_F(CcuRepAddTest, Constructor_A6SubTypes_AndDescribe)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    address addr_a_;
    address addr_b_;
    address addr_c_;
    addr_a_.reset(4);
    addr_b_.reset(5);
    addr_c_.reset(6);

    ccu_rep_add r1(&insGen, addr_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(r1.get_sub_type(), add_sub_type::addr_plus_immed_to_addr);
    EXPECT_NE(r1.describe().find("Addr"), std::string::npos);

    ccu_rep_add r2(&insGen, addr_a_, uint16_t(10));
    EXPECT_EQ(r2.get_sub_type(), add_sub_type::self_add_immed_address);

    ccu_rep_add r3(&insGen, addr_c_, var_a_, var_b_);
    EXPECT_EQ(r3.get_sub_type(), add_sub_type::var_plus_var_to_addr);

    ccu_rep_add r4(&insGen, addr_c_, var_a_, uint16_t(10));
    EXPECT_EQ(r4.get_sub_type(), add_sub_type::var_plus_immed_to_addr);

    ccu_rep_add r5(&insGen, var_c_, addr_a_, addr_b_);
    EXPECT_EQ(r5.get_sub_type(), add_sub_type::addr_plus_addr_to_var);

    ccu_rep_add r6(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(r6.get_sub_type(), add_sub_type::addr_plus_immed_to_var);
    EXPECT_NE(r6.describe().find("Immed"), std::string::npos);
}

class fake_success_ins_generater : public ccu_ins_generater_v1 {
public:
    uint32_t get_instr_count(ccu_rep_type rep_type) override { return 1; }
};

TEST_F(CcuRepMulTest, Constructor_RemainingSubTypes_AndDescribe)
{
    variable var_c_;
    variable var_a_;
    address addr_a_;
    address addr_c_;
    var_c_.reset(1);
    var_a_.reset(2);
    addr_a_.reset(3);
    addr_c_.reset(4);

    ccu_rep_mul r1(&insGen, addr_a_, var_a_);
    EXPECT_EQ(r1.get_sub_type(), mul_sub_type::self_mul_var_address);

    ccu_rep_mul r2(&insGen, addr_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(r2.get_sub_type(), mul_sub_type::addr_mul_immed_to_addr);

    ccu_rep_mul r3(&insGen, addr_c_, var_a_, uint16_t(10));
    EXPECT_EQ(r3.get_sub_type(), mul_sub_type::var_mul_immed_to_addr);

    ccu_rep_mul r4(&insGen, addr_a_, uint16_t(10));
    EXPECT_EQ(r4.get_sub_type(), mul_sub_type::self_mul_immed_address);

    ccu_rep_mul r5(&insGen, var_c_, addr_a_, uint16_t(10));
    EXPECT_EQ(r5.get_sub_type(), mul_sub_type::addr_mul_immed_to_var);
    EXPECT_NE(r5.describe().find("Immed"), std::string::npos);
}

TEST_F(CcuRepMulTest, Translate_A6SubType_V1GeneratorThrows)
{
    // v1 生成器对 mul 恒返回 NOT_SUPPORT, translate 抛错
    variable var_c_;
    address addr_a_;
    var_c_.reset(1);
    addr_a_.reset(2);

    ccu_rep_mul rep(&insGen, var_c_, addr_a_, uint16_t(10));
    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

// ==================== V2 imm 变体 describe 覆盖 ====================

TEST_F(CcuRepAddTest, Describe_SelfAddImmedVariable)
{
    variable var_a_;
    var_a_.reset(7);
    ccu_rep_add rep(&insGen, var_a_, uint16_t(10));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[7]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[10]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_VarPlusImmedToVar)
{
    variable var_c_;
    variable var_a_;
    var_c_.reset(1);
    var_a_.reset(2);
    ccu_rep_add rep(&insGen, var_c_, var_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_AddrPlusImmedToAddr)
{
    address addr_c_;
    address addr_a_;
    addr_c_.reset(1);
    addr_a_.reset(2);
    ccu_rep_add rep(&insGen, addr_c_, addr_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddImmedAddress)
{
    address addr_a_;
    addr_a_.reset(5);
    ccu_rep_add rep(&insGen, addr_a_, uint16_t(9));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[5]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[9]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_VarPlusImmedToAddr)
{
    address addr_c_;
    variable var_a_;
    addr_c_.reset(1);
    var_a_.reset(2);
    ccu_rep_add rep(&insGen, addr_c_, var_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("varA[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_AddrPlusImmedToVar)
{
    variable var_c_;
    address addr_a_;
    var_c_.reset(1);
    addr_a_.reset(2);
    ccu_rep_add rep(&insGen, var_c_, addr_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepSubTest, Describe_AddrMinusImmedToVar)
{
    variable var_c_;
    address addr_a_;
    var_c_.reset(1);
    addr_a_.reset(2);
    ccu_rep_sub rep(&insGen, var_c_, addr_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepSubTest, Getters_AddrFields)
{
    address addr_c_;
    address addr_a_;
    addr_c_.reset(1);
    addr_a_.reset(11);
    variable var_b_;
    var_b_.reset(22);
    // (addr_c, addr_a, var_b): Address[1] = Address[11] - Variable[22]
    ccu_rep_sub rep(&insGen, addr_c_, addr_a_, var_b_);
    EXPECT_EQ(rep.get_addr_a().id(), 11u);
    EXPECT_EQ(rep.get_addr_c().id(), 1u);
    EXPECT_EQ(rep.get_var_b().id(), 22u);
}

TEST_F(CcuRepMulTest, Describe_AddrMulImmedToVar)
{
    variable var_c_;
    address addr_a_;
    var_c_.reset(1);
    addr_a_.reset(2);
    ccu_rep_mul rep(&insGen, var_c_, addr_a_, uint16_t(3));
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Immed[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, LogicAndShiftOperatorCheckSpecializations)
{
    // 触发 ccu_logic_operator / ccu_arithmetic_operator 各模板特化的 check():
    // var&addr, addr&var, addr*addr, addr*var, addr*imm16, ~var
    ccu_rep_context context_;
    variable var_(&context_);
    variable var_b_(&context_);
    address addr_;

    EXPECT_NO_THROW((void)(var_ & addr_));
    EXPECT_NO_THROW((void)(addr_ & var_));
    EXPECT_NO_THROW((void)(addr_ * addr_));
    EXPECT_NO_THROW((void)(addr_ * var_));
    EXPECT_NO_THROW((void)(addr_ * uint16_t(5)));
    EXPECT_NO_THROW((void)(~var_));
}

TEST_F(CcuRepAddTest, VarMulVarAssignAppendsMulRep)
{
    // 覆盖 variable::operator=(ccu_arithmetic_operator<variable, variable>) 的 multiplication 分支
    ccu_rep_context context_;
    variable var_(&context_);
    variable lhs_(&context_);
    variable rhs_(&context_);

    EXPECT_NO_THROW(var_ = lhs_ * rhs_);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
