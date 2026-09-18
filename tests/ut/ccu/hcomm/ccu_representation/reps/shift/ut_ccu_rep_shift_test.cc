/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/shift/ccu_rep_shl.h"
#include "hcomm/resource/representation/reps/shift/ccu_rep_shr.h"
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

class CcuRepShiftTest : public ::testing::Test {
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

TEST_F(CcuRepShiftTest, Shl_Constructor_VarEqualsVarShiftVar)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_l rep(&insGen, var_d_, var_n_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::var_equals_var_shift_var);
    EXPECT_EQ(rep.get_shift_type(), shift_type::logical_shift);
    EXPECT_EQ(rep.type(), ccu_rep_type::shl);
    EXPECT_EQ(rep.get_var_d().id(), var_d_.id());
    EXPECT_EQ(rep.get_var_n().id(), var_n_.id());
    EXPECT_EQ(rep.get_var_m().id(), var_m_.id());
}

TEST_F(CcuRepShiftTest, Shl_Constructor_VarShiftAssignVar)
{
    variable var_d_;
    variable var_m_;
    var_d_.reset(1);
    var_m_.reset(2);
    ccu_rep_sh_l rep(&insGen, var_d_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::var_shift_assign_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::shl);
}

TEST_F(CcuRepShiftTest, Shl_Constructor_AddrEqualsVarShiftVar)
{
    address addr_d_;
    variable var_n_;
    variable var_m_;
    addr_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_l rep(&insGen, addr_d_, var_n_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::addr_equals_var_shift_var);
    EXPECT_EQ(rep.get_shift_type(), shift_type::logical_shift);
    EXPECT_EQ(rep.type(), ccu_rep_type::shl);
    EXPECT_EQ(rep.get_address_d().id(), addr_d_.id());
}

TEST_F(CcuRepShiftTest, Shl_Constructor_AddrShiftAssignVar)
{
    address addr_d_;
    variable var_m_;
    addr_d_.reset(1);
    var_m_.reset(2);
    ccu_rep_sh_l rep(&insGen, addr_d_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::addr_shift_assign_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::shl);
}

TEST_F(CcuRepShiftTest, Shl_Describe_AllSubTypes)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    address addr_d_;
    addr_d_.reset(4);

    EXPECT_NE(
        ccu_rep_sh_l(&insGen, var_d_, var_n_, var_m_).describe().find("Variable[1] = Variable[2] << Variable[3]"),
        std::string::npos);
    EXPECT_NE(ccu_rep_sh_l(&insGen, var_d_, var_m_).describe().find("Variable[1] <<= Variable[3]"), std::string::npos);
    EXPECT_NE(
        ccu_rep_sh_l(&insGen, addr_d_, var_n_, var_m_).describe().find("Address[4] = Variable[2] << Variable[3]"),
        std::string::npos);
    EXPECT_NE(ccu_rep_sh_l(&insGen, addr_d_, var_m_).describe().find("Address[4] <<= Variable[3]"), std::string::npos);
}

TEST_F(CcuRepShiftTest, Shl_Translate_ThrowsNotSupport)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_l rep(&insGen, var_d_, var_n_, var_m_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepShiftTest, Shl_Translate_NullInstrThrows)
{
    variable var_d_;
    variable var_m_;
    var_d_.reset(1);
    var_m_.reset(2);
    ccu_rep_sh_l rep(&insGen, var_d_, var_m_);

    ccu_instr* instrPtr = nullptr;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

TEST_F(CcuRepShiftTest, Shr_Constructor_VarEqualsVarShiftVar)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_r rep(&insGen, var_d_, var_n_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::var_equals_var_shift_var);
    EXPECT_EQ(rep.get_shift_type(), shift_type::logical_shift);
    EXPECT_EQ(rep.type(), ccu_rep_type::shr);
    EXPECT_EQ(rep.get_var_d().id(), var_d_.id());
    EXPECT_EQ(rep.get_var_n().id(), var_n_.id());
    EXPECT_EQ(rep.get_var_m().id(), var_m_.id());
}

TEST_F(CcuRepShiftTest, Shr_Constructor_VarShiftAssignVar)
{
    variable var_d_;
    variable var_m_;
    var_d_.reset(1);
    var_m_.reset(2);
    ccu_rep_sh_r rep(&insGen, var_d_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::var_shift_assign_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::shr);
}

TEST_F(CcuRepShiftTest, Shr_Constructor_AddrEqualsVarShiftVar)
{
    address addr_d_;
    variable var_n_;
    variable var_m_;
    addr_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_r rep(&insGen, addr_d_, var_n_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::addr_equals_var_shift_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::shr);
    EXPECT_EQ(rep.get_address_d().id(), addr_d_.id());
}

TEST_F(CcuRepShiftTest, Shr_Constructor_AddrShiftAssignVar)
{
    address addr_d_;
    variable var_m_;
    addr_d_.reset(1);
    var_m_.reset(2);
    ccu_rep_sh_r rep(&insGen, addr_d_, var_m_);

    EXPECT_EQ(rep.get_shift_sub_type(), shift_sub_type::addr_shift_assign_var);
    EXPECT_EQ(rep.type(), ccu_rep_type::shr);
}

TEST_F(CcuRepShiftTest, Shr_Describe_AllSubTypes)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    address addr_d_;
    addr_d_.reset(4);

    EXPECT_NE(
        ccu_rep_sh_r(&insGen, var_d_, var_n_, var_m_).describe().find("Variable[1] = Variable[2] >> Variable[3]"),
        std::string::npos);
    EXPECT_NE(ccu_rep_sh_r(&insGen, var_d_, var_m_).describe().find("Variable[1] >>= Variable[3]"), std::string::npos);
    EXPECT_NE(
        ccu_rep_sh_r(&insGen, addr_d_, var_n_, var_m_).describe().find("Address[4] = Variable[2] >> Variable[3]"),
        std::string::npos);
    EXPECT_NE(ccu_rep_sh_r(&insGen, addr_d_, var_m_).describe().find("Address[4] >>= Variable[3]"), std::string::npos);
}

TEST_F(CcuRepShiftTest, Shr_Translate_ThrowsNotSupport)
{
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);
    ccu_rep_sh_r rep(&insGen, var_d_, var_n_, var_m_);

    ccu_instr* instrPtr = &instr_;
    EXPECT_ANY_THROW(rep.translate(nullptr, instrPtr, instrId, dep));
}

class fake_shift_success_generater : public fake_ins_generater {
public:
    HcclResult ccu_rep_sh_l_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_l* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
    HcclResult ccu_rep_sh_r_translate(
        ccu_kernel* ccu_kernel, ccu_instr*& instr, ccu_rep_sh_r* rep, const trans_dep& dep) override
    {
        return HcclResult::HCCL_SUCCESS;
    }
};

TEST_F(CcuRepShiftTest, Translate_WithSuccessGenerator)
{
    fake_shift_success_generater fakeGen{};
    variable var_d_;
    variable var_n_;
    variable var_m_;
    var_d_.reset(1);
    var_n_.reset(2);
    var_m_.reset(3);

    ccu_instr instr_[8] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t localInstrId = 0;
    trans_dep localDep{};

    ccu_rep_sh_l shlRep(&fakeGen, var_d_, var_n_, var_m_);
    EXPECT_TRUE(shlRep.translate(nullptr, instrPtr, localInstrId, localDep));

    ccu_rep_sh_r shrRep(&fakeGen, var_d_, var_n_, var_m_);
    EXPECT_TRUE(shrRep.translate(nullptr, instrPtr, localInstrId, localDep));
}

} // namespace
} // namespace ccu_rep
} // namespace asc
