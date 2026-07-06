/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_rep_add_v1.h"
#include "ccu_rep_assign_v1.h"
#include "ccu_datatype_v1.h"
#include "ccu_microcode_v1.h"
#include "ccu_rep_base_v1.h"
#include "ccu_rep_type_v1.h"
#include "ccu_operator_v1.h"
#include <gtest/gtest.h>
#include <string>

namespace hcomm {
namespace CcuRep {
namespace {

class CcuRepAddTest : public ::testing::Test {
protected:
    CcuInstr instr {};
    uint16_t instrId {0};
    TransDep dep {};

    void SetUp() override
    {
        memset(&instr, 0, sizeof(instr));
        dep.reserveGsaId = 1;
        dep.reserveXnId = 2;
    }
};

TEST_F(CcuRepAddTest, Constructor_AddrPlusVarToAddr)
{
    Address addrC;
    Address addrA;
    Variable varB;
    addrC.Reset(1);
    addrA.Reset(2);
    varB.Reset(3);
    CcuRepAdd rep(addrC, addrA, varB);

    EXPECT_EQ(rep.subType, AddSubType::ADDR_PLUS_VAR_TO_ADDR);
    EXPECT_EQ(rep.Type(), CcuRepType::ADD);
}

TEST_F(CcuRepAddTest, Constructor_AddrPlusAddrToAddr)
{
    Address addrC;
    Address addrA;
    Address addrB;
    addrC.Reset(1);
    addrA.Reset(2);
    addrB.Reset(3);
    CcuRepAdd rep(addrC, addrA, addrB);

    EXPECT_EQ(rep.subType, AddSubType::ADDR_PLUS_ADDR_TO_ADDR);
    EXPECT_EQ(rep.Type(), CcuRepType::ADD);
}

TEST_F(CcuRepAddTest, Constructor_VarPlusVarToVar)
{
    Variable varC;
    Variable varA;
    Variable varB;
    varC.Reset(1);
    varA.Reset(2);
    varB.Reset(3);
    CcuRepAdd rep(varC, varA, varB);

    EXPECT_EQ(rep.subType, AddSubType::VAR_PLUS_VAR_TO_VAR);
    EXPECT_EQ(rep.Type(), CcuRepType::ADD);
}

TEST_F(CcuRepAddTest, Constructor_SelfAddAddress)
{
    Address addrA;
    Variable offset;
    addrA.Reset(1);
    offset.Reset(2);
    CcuRepAdd rep(addrA, offset);

    EXPECT_EQ(rep.subType, AddSubType::SELF_ADD_ADDRESS);
    EXPECT_EQ(rep.Type(), CcuRepType::ADD);
}

TEST_F(CcuRepAddTest, Constructor_SelfAddVariable)
{
    Variable varA;
    Variable offset;
    varA.Reset(1);
    offset.Reset(2);
    CcuRepAdd rep(varA, offset);

    EXPECT_EQ(rep.subType, AddSubType::SELF_ADD_VARIABLE);
    EXPECT_EQ(rep.Type(), CcuRepType::ADD);
}

TEST_F(CcuRepAddTest, Translate_AddrPlusVarToAddr)
{
    Address addrC;
    Address addrA;
    Variable varB;
    addrC.Reset(5);
    addrA.Reset(3);
    varB.Reset(7);
    CcuRepAdd rep(addrC, addrA, varB);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instr.v1.loadGSAXn.gsAdId, addrC.Id());
    EXPECT_EQ(instr.v1.loadGSAXn.gsAmId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAXn.xnId, varB.Id());
    EXPECT_EQ(instr.header.type, 0x0);
    EXPECT_EQ(instr.header.code, 0x4);
}

TEST_F(CcuRepAddTest, Translate_AddrPlusAddrToAddr)
{
    Address addrC;
    Address addrA;
    Address addrB;
    addrC.Reset(5);
    addrA.Reset(3);
    addrB.Reset(7);
    CcuRepAdd rep(addrC, addrA, addrB);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAdId, addrC.Id());
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAmId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAnId, addrB.Id());
    EXPECT_EQ(instr.header.type, 0x0);
    EXPECT_EQ(instr.header.code, 0x5);
}

TEST_F(CcuRepAddTest, Translate_VarPlusVarToVar)
{
    Variable varC;
    Variable varA;
    Variable varB;
    varC.Reset(5);
    varA.Reset(3);
    varB.Reset(7);
    CcuRepAdd rep(varC, varA, varB);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadXX.xdId, varC.Id());
    EXPECT_EQ(instr.v1.loadXX.xmId, varA.Id());
    EXPECT_EQ(instr.v1.loadXX.xnId, varB.Id());
    EXPECT_EQ(instr.header.type, 0x0);
    EXPECT_EQ(instr.header.code, 0x6);
}

TEST_F(CcuRepAddTest, Translate_SelfAddAddress)
{
    Address addrA;
    Variable offset;
    addrA.Reset(5);
    offset.Reset(7);
    CcuRepAdd rep(addrA, offset);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadGSAXn.gsAdId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAXn.gsAmId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAXn.xnId, offset.Id());
}

TEST_F(CcuRepAddTest, Translate_SelfAddVariable)
{
    Variable varA;
    Variable offset;
    varA.Reset(5);
    offset.Reset(7);
    CcuRepAdd rep(varA, offset);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadXX.xdId, varA.Id());
    EXPECT_EQ(instr.v1.loadXX.xmId, varA.Id());
    EXPECT_EQ(instr.v1.loadXX.xnId, offset.Id());
}

TEST_F(CcuRepAddTest, Describe_AddrPlusVarToAddr)
{
    Address addrC;
    Address addrA;
    Variable varB;
    addrC.Reset(1);
    addrA.Reset(2);
    varB.Reset(3);
    CcuRepAdd rep(addrC, addrA, varB);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_AddrPlusAddrToAddr)
{
    Address addrC;
    Address addrA;
    Address addrB;
    addrC.Reset(1);
    addrA.Reset(2);
    addrB.Reset(3);
    CcuRepAdd rep(addrC, addrA, addrB);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
    EXPECT_NE(desc.find("Address[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_VarPlusVarToVar)
{
    Variable varC;
    Variable varA;
    Variable varB;
    varC.Reset(1);
    varA.Reset(2);
    varB.Reset(3);
    CcuRepAdd rep(varC, varA, varB);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[3]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddAddress)
{
    Address addrA;
    Variable offset;
    addrA.Reset(5);
    offset.Reset(7);
    CcuRepAdd rep(addrA, offset);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[5]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[7]"), std::string::npos);
}

TEST_F(CcuRepAddTest, Describe_SelfAddVariable)
{
    Variable varA;
    Variable offset;
    varA.Reset(5);
    offset.Reset(7);
    CcuRepAdd rep(varA, offset);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Variable[5]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[7]"), std::string::npos);
}

class CcuRepAssignTest : public ::testing::Test {
protected:
    CcuInstr instr {};
    uint16_t instrId {0};
    TransDep dep {};

    void SetUp() override
    {
        memset(&instr, 0, sizeof(instr));
        dep.reserveGsaId = 1;
        dep.reserveXnId = 2;
    }
};

TEST_F(CcuRepAssignTest, Constructor_ImdToVariable)
{
    Variable varA;
    varA.Reset(1);
    uint64_t immediate = 100;
    CcuRepAssign rep(varA, immediate);

    EXPECT_EQ(rep.subType, AssignSubType::IMD_TO_VARIABLE);
    EXPECT_EQ(rep.Type(), CcuRepType::ASSIGN);
    EXPECT_EQ(rep.immediate, immediate);
}

TEST_F(CcuRepAssignTest, Constructor_ImdToAddr)
{
    Address addrA;
    addrA.Reset(1);
    uint64_t immediate = 200;
    CcuRepAssign rep(addrA, immediate);

    EXPECT_EQ(rep.subType, AssignSubType::IMD_TO_ADDR);
    EXPECT_EQ(rep.Type(), CcuRepType::ASSIGN);
    EXPECT_EQ(rep.immediate, immediate);
}

TEST_F(CcuRepAssignTest, Constructor_VarToAddr)
{
    Address addrA;
    Variable varA;
    addrA.Reset(1);
    varA.Reset(2);
    CcuRepAssign rep(addrA, varA);

    EXPECT_EQ(rep.subType, AssignSubType::VAR_TO_ADDR);
    EXPECT_EQ(rep.Type(), CcuRepType::ASSIGN);
}

TEST_F(CcuRepAssignTest, Constructor_AddrToAddr)
{
    Address addrB;
    Address addrA;
    addrB.Reset(1);
    addrA.Reset(2);
    CcuRepAssign rep(addrB, addrA);

    EXPECT_EQ(rep.subType, AssignSubType::ADDR_TO_ADDR);
    EXPECT_EQ(rep.Type(), CcuRepType::ASSIGN);
}

TEST_F(CcuRepAssignTest, Constructor_VarToVar)
{
    Variable varB;
    Variable varA;
    varB.Reset(1);
    varA.Reset(2);
    CcuRepAssign rep(varB, varA);

    EXPECT_EQ(rep.subType, AssignSubType::VAR_TO_VAR);
    EXPECT_EQ(rep.Type(), CcuRepType::ASSIGN);
}

TEST_F(CcuRepAssignTest, Translate_ImdToVariable)
{
    Variable varA;
    varA.Reset(5);
    uint64_t immediate = 0x12345678ABCD;
    CcuRepAssign rep(varA, immediate);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.Translated());
    EXPECT_EQ(instr.v1.loadImdToXn.xnId, varA.Id());
    EXPECT_EQ(instr.v1.loadImdToXn.immediate, immediate);
    EXPECT_EQ(instr.header.type, 0x0);
    EXPECT_EQ(instr.header.code, 0x3);
}

TEST_F(CcuRepAssignTest, Translate_ImdToAddr)
{
    Address addrA;
    addrA.Reset(5);
    uint64_t immediate = 0xDEADBEEF;
    CcuRepAssign rep(addrA, immediate);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadImdToGSA.gsaId, addrA.Id());
    EXPECT_EQ(instr.v1.loadImdToGSA.immediate, immediate);
    EXPECT_EQ(instr.header.type, 0x0);
    EXPECT_EQ(instr.header.code, 0x2);
}

TEST_F(CcuRepAssignTest, Translate_VarToAddr)
{
    Address addrA;
    Variable varA;
    addrA.Reset(5);
    varA.Reset(7);
    CcuRepAssign rep(addrA, varA);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadGSAXn.gsAdId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAXn.gsAmId, dep.reserveGsaId);
    EXPECT_EQ(instr.v1.loadGSAXn.xnId, varA.Id());
}

TEST_F(CcuRepAssignTest, Translate_AddrToAddr)
{
    Address addrB;
    Address addrA;
    addrB.Reset(5);
    addrA.Reset(7);
    CcuRepAssign rep(addrB, addrA);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAdId, addrB.Id());
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAmId, addrA.Id());
    EXPECT_EQ(instr.v1.loadGSAGSA.gsAnId, dep.reserveGsaId);
}

TEST_F(CcuRepAssignTest, Translate_VarToVar)
{
    Variable varB;
    Variable varA;
    varB.Reset(5);
    varA.Reset(7);
    CcuRepAssign rep(varB, varA);

    CcuInstr* instrPtr = &instr;
    bool result = rep.Translate(instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instr.v1.loadXX.xdId, varB.Id());
    EXPECT_EQ(instr.v1.loadXX.xmId, varA.Id());
    EXPECT_EQ(instr.v1.loadXX.xnId, dep.reserveXnId);
}

TEST_F(CcuRepAssignTest, Describe_ImdToVariable)
{
    Variable varA;
    varA.Reset(1);
    CcuRepAssign rep(varA, 100);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Variable[1]"), std::string::npos);
    EXPECT_NE(desc.find("Value[100]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_ImdToAddr)
{
    Address addrA;
    addrA.Reset(1);
    CcuRepAssign rep(addrA, 200);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Value[200]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_VarToAddr)
{
    Address addrA;
    Variable varA;
    addrA.Reset(1);
    varA.Reset(2);
    CcuRepAssign rep(addrA, varA);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_AddrToAddr)
{
    Address addrB;
    Address addrA;
    addrB.Reset(1);
    addrA.Reset(2);
    CcuRepAssign rep(addrB, addrA);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Address[1]"), std::string::npos);
    EXPECT_NE(desc.find("Address[2]"), std::string::npos);
}

TEST_F(CcuRepAssignTest, Describe_VarToVar)
{
    Variable varB;
    Variable varA;
    varB.Reset(1);
    varA.Reset(2);
    CcuRepAssign rep(varB, varA);

    std::string desc = rep.Describe();
    EXPECT_NE(desc.find("Var[1]"), std::string::npos);
    EXPECT_NE(desc.find("Var[2]"), std::string::npos);
}

class CcuOperatorTest : public ::testing::Test {
};

TEST_F(CcuOperatorTest, VariablePlusVariable)
{
    Variable varA;
    Variable varB;
    varA.Reset(1);
    varB.Reset(2);
    auto op = varA + varB;

    EXPECT_EQ(op.type, CcuArithmeticOperatorType::ADDITION);
    EXPECT_EQ(op.lhs.Id(), varA.Id());
    EXPECT_EQ(op.rhs.Id(), varB.Id());
}

TEST_F(CcuOperatorTest, VariablePlusAddress)
{
    Variable varA;
    Address addrB;
    varA.Reset(1);
    addrB.Reset(2);
    auto op = varA + addrB;

    EXPECT_EQ(op.type, CcuArithmeticOperatorType::ADDITION);
    EXPECT_EQ(op.lhs.Id(), varA.Id());
    EXPECT_EQ(op.rhs.Id(), addrB.Id());
}

TEST_F(CcuOperatorTest, AddressPlusVariable)
{
    Address addrA;
    Variable varB;
    addrA.Reset(1);
    varB.Reset(2);
    auto op = addrA + varB;

    EXPECT_EQ(op.type, CcuArithmeticOperatorType::ADDITION);
    EXPECT_EQ(op.lhs.Id(), varB.Id());
    EXPECT_EQ(op.rhs.Id(), addrA.Id());
}

TEST_F(CcuOperatorTest, AddressPlusAddress)
{
    Address addrA;
    Address addrB;
    addrA.Reset(1);
    addrB.Reset(2);
    auto op = addrA + addrB;

    EXPECT_EQ(op.type, CcuArithmeticOperatorType::ADDITION);
    EXPECT_EQ(op.lhs.Id(), addrA.Id());
    EXPECT_EQ(op.rhs.Id(), addrB.Id());
}

TEST_F(CcuOperatorTest, VariableNotEqualImmediate)
{
    Variable varA;
    varA.Reset(1);
    auto op = varA != 100;

    EXPECT_EQ(op.type, CcuRelationalOperatorType::NOT_EQUAL);
    EXPECT_EQ(op.lhs.Id(), varA.Id());
    EXPECT_EQ(op.rhs, 100);
}

TEST_F(CcuOperatorTest, VariableEqualImmediate)
{
    Variable varA;
    varA.Reset(1);
    auto op = varA == 200;

    EXPECT_EQ(op.type, CcuRelationalOperatorType::EQUAL);
    EXPECT_EQ(op.lhs.Id(), varA.Id());
    EXPECT_EQ(op.rhs, 200);
}

}
}
}
