/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/logical/ccu_rep_and.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_or.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_not.h"
#include "hcomm/resource/representation/reps/logical/ccu_rep_xor.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_base.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include <gtest/gtest.h>
#include <string>

namespace asc {
namespace ccu_rep {
namespace {

class fake_ins_generater : public ccu_ins_generater_v1 {
public:
    uint32_t get_instr_count(ccu_rep_type rep_type) override { return static_cast<uint32_t>(rep_type); }
};

class CcuRepLogicalTest : public ::testing::Test {
protected:
    fake_ins_generater insGen{};
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

TEST_F(CcuRepLogicalTest, And_Constructor_VarAndVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_and rep(&insGen, var_c_, var_a_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), and_sub_type::var_and_var_to_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::and_op);
    EXPECT_EQ(rep.get_var_a().id(), var_a_.id());
    EXPECT_EQ(rep.get_var_b().id(), var_b_.id());
    EXPECT_EQ(rep.get_var_c().id(), var_c_.id());
}

TEST_F(CcuRepLogicalTest, And_Constructor_SelfAndVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(1);
    var_b_.reset(2);
    ccu_rep_and rep(&insGen, var_c_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), and_sub_type::self_and_var_variable);
    EXPECT_EQ(rep.type(), ccu_rep_type::and_op);
}

TEST_F(CcuRepLogicalTest, And_Describe_VarAndVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_and rep(&insGen, var_c_, var_a_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1] = Variable[2] & Variable[3]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, And_Describe_SelfAndVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(5);
    var_b_.reset(7);
    ccu_rep_and rep(&insGen, var_c_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[5] &= Variable[7]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, And_Translate_ThrowsNotSupport)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_and rep(&insGen, var_c_, var_a_, var_b_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepLogicalTest, And_Translate_NullInstrThrows)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_and rep(&insGen, var_c_, var_a_, var_b_);

    ccu_instr* instrPtr = nullptr;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepLogicalTest, Or_Constructor_VarOrVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_or rep(&insGen, var_c_, var_a_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), or_sub_type::var_or_var_to_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::or_op);
    EXPECT_EQ(rep.get_var_a().id(), var_a_.id());
    EXPECT_EQ(rep.get_var_b().id(), var_b_.id());
    EXPECT_EQ(rep.get_var_c().id(), var_c_.id());
}

TEST_F(CcuRepLogicalTest, Or_Constructor_SelfOrVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(1);
    var_b_.reset(2);
    ccu_rep_or rep(&insGen, var_c_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), or_sub_type::self_or_var_variable);
    EXPECT_EQ(rep.type(), ccu_rep_type::or_op);
}

TEST_F(CcuRepLogicalTest, Or_Describe_VarOrVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_or rep(&insGen, var_c_, var_a_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1] = Variable[2] | Variable[3]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, Or_Describe_SelfOrVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(5);
    var_b_.reset(7);
    ccu_rep_or rep(&insGen, var_c_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[5] |= Variable[7]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, Or_Translate_ThrowsNotSupport)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_or rep(&insGen, var_c_, var_a_, var_b_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepLogicalTest, Xor_Constructor_VarXorVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_xor rep(&insGen, var_c_, var_a_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), xor_sub_type::var_xor_var_to_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::xor_op);
    EXPECT_EQ(rep.get_var_a().id(), var_a_.id());
    EXPECT_EQ(rep.get_var_b().id(), var_b_.id());
    EXPECT_EQ(rep.get_var_c().id(), var_c_.id());
}

TEST_F(CcuRepLogicalTest, Xor_Constructor_SelfXorVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(1);
    var_b_.reset(2);
    ccu_rep_xor rep(&insGen, var_c_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), xor_sub_type::self_xor_var_variable);
    EXPECT_EQ(rep.type(), ccu_rep_type::xor_op);
}

TEST_F(CcuRepLogicalTest, Xor_Describe_VarXorVarToVar)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_xor rep(&insGen, var_c_, var_a_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[1] = Variable[2] ^ Variable[3]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, Xor_Describe_SelfXorVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(5);
    var_b_.reset(7);
    ccu_rep_xor rep(&insGen, var_c_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[5] ^= Variable[7]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, Xor_Translate_ThrowsNotSupport)
{
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);
    ccu_rep_xor rep(&insGen, var_c_, var_a_, var_b_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepLogicalTest, Not_Constructor_VarEqualsNotVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(1);
    var_b_.reset(2);
    ccu_rep_not rep(&insGen, var_c_, var_b_);

    EXPECT_EQ(rep.get_sub_type(), not_sub_type::var_equals_not_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::not_op);
    EXPECT_EQ(rep.get_var_b().id(), var_b_.id());
    EXPECT_EQ(rep.get_var_c().id(), var_c_.id());
}

TEST_F(CcuRepLogicalTest, Not_Describe_VarEqualsNotVar)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(5);
    var_b_.reset(7);
    ccu_rep_not rep(&insGen, var_c_, var_b_);

    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Variable[5] = ~Variable[7]"), std::string::npos);
}

TEST_F(CcuRepLogicalTest, Not_Translate_ThrowsNotSupport)
{
    variable var_c_;
    variable var_b_;
    var_c_.reset(1);
    var_b_.reset(2);
    ccu_rep_not rep(&insGen, var_c_, var_b_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

class fake_logic_success_generater : public fake_ins_generater {
public:
    HcclResult ccu_rep_and_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_and* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
    HcclResult ccu_rep_or_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_or* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
    HcclResult ccu_rep_xor_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_xor* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
    HcclResult ccu_rep_not_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_not* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
};

TEST_F(CcuRepLogicalTest, Translate_WithSuccessGenerator)
{
    fake_logic_success_generater fakeGen{};
    variable var_c_;
    variable var_a_;
    variable var_b_;
    var_c_.reset(1);
    var_a_.reset(2);
    var_b_.reset(3);

    ccu_instr instr_[8] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    ccu_rep_and andRep(&fakeGen, var_c_, var_a_, var_b_);
    EXPECT_TRUE(andRep.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_or orRep(&fakeGen, var_c_, var_a_, var_b_);
    EXPECT_TRUE(orRep.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_xor xorRep(&fakeGen, var_c_, var_a_, var_b_);
    EXPECT_TRUE(xorRep.translate(nullptr, instrPtr, instrId, dep));

    ccu_rep_not notRep(&fakeGen, var_c_, var_a_);
    EXPECT_TRUE(notRep.translate(nullptr, instrPtr, instrId, dep));
}

} // namespace
} // namespace ccu_rep
} // namespace asc
