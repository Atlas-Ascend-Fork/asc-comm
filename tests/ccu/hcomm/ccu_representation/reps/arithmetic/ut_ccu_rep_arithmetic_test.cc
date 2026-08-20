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

namespace hcomm {
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
    EXPECT_EQ(instr_.header.type_, 0x0);
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
    EXPECT_EQ(instr_.header.type_, 0x0);
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
    EXPECT_EQ(instr_.header.type_, 0x0);
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
    EXPECT_NE(desc.find("address[1]"), std::string::npos);
    EXPECT_NE(desc.find("address[2]"), std::string::npos);
    EXPECT_NE(desc.find("variable[3]"), std::string::npos);
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
    EXPECT_NE(desc.find("address[1]"), std::string::npos);
    EXPECT_NE(desc.find("address[2]"), std::string::npos);
    EXPECT_NE(desc.find("address[3]"), std::string::npos);
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
    EXPECT_NE(desc.find("variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("variable[2]"), std::string::npos);
    EXPECT_NE(desc.find("variable[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddAddress)
{
    address addr_a_;
    variable offset;
    addr_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, addr_a_, offset);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("address[5]"), std::string::npos);
    EXPECT_NE(desc.find("variable[7]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddVariable)
{
    variable var_a_;
    variable offset;
    var_a_.reset(5);
    offset.reset(7);
    ccu_rep_add rep(&insGen, var_a_, offset);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("variable[5]"), std::string::npos);
    EXPECT_NE(desc.find("variable[7]"), std::string::npos);
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
    EXPECT_EQ(instr_.v1.load_imd_to_xn.immediate_, immediate_);
    EXPECT_EQ(instr_.header.type_, 0x0);
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
    EXPECT_EQ(instr_.v1.load_imd_to_gsa.immediate_, immediate_);
    EXPECT_EQ(instr_.header.type_, 0x0);
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
    EXPECT_NE(desc.find("variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Value[100]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_ImdToAddr)
{
    address addr_a_;
    addr_a_.reset(1);
    ccu_rep_assign rep(&insGen, addr_a_, 200);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("address[1]"), std::string::npos);
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
    EXPECT_NE(desc.find("address[1]"), std::string::npos);
    EXPECT_NE(desc.find("variable[2]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_AddrToAddr)
{
    address addr_b_;
    address addr_a_;
    addr_b_.reset(1);
    addr_a_.reset(2);
    ccu_rep_assign rep(&insGen, addr_b_, addr_a_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("address[1]"), std::string::npos);
    EXPECT_NE(desc.find("address[2]"), std::string::npos);
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

    EXPECT_EQ(op.type_, ccu_arithmetic_operator_type::addition);
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

    EXPECT_EQ(op.type_, ccu_arithmetic_operator_type::addition);
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

    EXPECT_EQ(op.type_, ccu_arithmetic_operator_type::addition);
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

    EXPECT_EQ(op.type_, ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.id(), addr_a_.id());
    EXPECT_EQ(op.rhs.id(), addr_b_.id());
}

TEST_F(CcuOperatorTest, VariableNotEqualImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ != 100;

    EXPECT_EQ(op.type_, ccu_relational_operator_type::not_equal);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs, 100);
}

TEST_F(CcuOperatorTest, VariableEqualImmediate)
{
    variable var_a_;
    var_a_.reset(1);
    auto op = var_a_ == 200;

    EXPECT_EQ(op.type_, ccu_relational_operator_type::equal);
    EXPECT_EQ(op.lhs.id(), var_a_.id());
    EXPECT_EQ(op.rhs, 200);
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
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, var_a_, var_b_).describe().find("address"), std::string::npos);
    EXPECT_NE(ccu_rep_mul(&insGen, addr_c_, var_a_, addr_b_).describe().find("address"), std::string::npos);
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
    EXPECT_NE(ccu_rep_sub(&insGen, addr_c_, addr_a_, var_b_).describe().find("variable"), std::string::npos);
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

} // namespace
} // namespace ccu_rep
} // namespace hcomm
