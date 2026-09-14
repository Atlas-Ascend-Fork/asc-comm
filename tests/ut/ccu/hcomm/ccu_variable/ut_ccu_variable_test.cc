/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// 覆盖 fa84c01（同步 class variable 等接口的功能演进）新增代码：
// 1. ccu::variable 算术(减/乘/立即数变体)、逻辑(and/or/xor/not)、比较(==/!=/</<=/>/>=) 全部重载运算符；
// 2. cond_expr 新增字段（rhs_var_ / is_var_compare / 新条件类型 lt/le/gt/ge）；
// 3. CCU_IF / CCU_WHILE 控制流宏的变量-变量比较分支（ccu_if_begin_var / ccu_while_begin_var 等）；
// 4. ccu_primitives_impl.cc 新增 C 接口（sub/mul/imm/逻辑/not/address+imm/_var 控制流）的成功与错误路径。
#include "ccu_variable_ut_stub.h"

#include "ccu/hcomm/ccu_address.hpp"
#include "ccu/hcomm/ccu_control_flow_macro.h"
#include "ccu/hcomm/ccu_variable.hpp"

#include <gtest/gtest.h>

namespace ccu = ::AscendC::ccu;
using asc::ut::kernel_call;

namespace {

// 断言录制调用 args[0..n] 与期望一致
#define EXPECT_ARGS(call, ...)                                                 \
    do {                                                                       \
        uint64_t expected_[] = {__VA_ARGS__};                                  \
        ASSERT_TRUE((call) != nullptr);                                        \
        for (size_t i_ = 0; i_ < sizeof(expected_) / sizeof(uint64_t); ++i_) { \
            EXPECT_EQ((call)->args[i_], expected_[i_]) << "arg index " << i_;  \
        }                                                                      \
    } while (0)

class CcuVariableOperatorTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        asc::ut::set_current_kernel(asc::ut::recording_kernel());
        asc::ut::clear_calls();
    }

    void TearDown() override { asc::ut::set_current_kernel(nullptr); }
};

// ==================== cond_expr：与立即数比较（is_var_compare=false） ====================

TEST_F(CcuVariableOperatorTest, CondExpr_CompareWithImmediate_AllConditions)
{
    ccu::variable var;
    asc::ut::clear_calls();

    auto cEq = var == uint64_t(100);
    EXPECT_EQ(cEq.var_, &var);
    EXPECT_EQ(cEq.rhs_var_, nullptr);
    EXPECT_EQ(cEq.imm, 100u);
    EXPECT_EQ(cEq.cond, ccu_condition_eq);
    EXPECT_FALSE(cEq.is_var_compare);

    auto cNe = var != uint64_t(101);
    EXPECT_EQ(cNe.cond, ccu_condition_ne);
    EXPECT_EQ(cNe.imm, 101u);
    EXPECT_FALSE(cNe.is_var_compare);

    auto cLt = var < uint64_t(102);
    EXPECT_EQ(cLt.cond, ccu_condition_lt);
    EXPECT_EQ(cLt.imm, 102u);
    EXPECT_FALSE(cLt.is_var_compare);

    auto cLe = var <= uint64_t(103);
    EXPECT_EQ(cLe.cond, ccu_condition_le);
    EXPECT_EQ(cLe.imm, 103u);
    EXPECT_FALSE(cLe.is_var_compare);

    auto cGt = var > uint64_t(104);
    EXPECT_EQ(cGt.cond, ccu_condition_gt);
    EXPECT_EQ(cGt.imm, 104u);
    EXPECT_FALSE(cGt.is_var_compare);

    auto cGe = var >= uint64_t(105);
    EXPECT_EQ(cGe.cond, ccu_condition_ge);
    EXPECT_EQ(cGe.imm, 105u);
    EXPECT_FALSE(cGe.is_var_compare);

    // 纯比较表达式不触发任何 primitive 调用
    EXPECT_TRUE(asc::ut::calls().empty());
}

// ==================== cond_expr：与变量比较（is_var_compare=true） ====================

TEST_F(CcuVariableOperatorTest, CondExpr_CompareWithVariable_AllConditions)
{
    ccu::variable lhs;
    ccu::variable rhs;
    asc::ut::clear_calls();

    auto cEq = lhs == rhs;
    EXPECT_EQ(cEq.var_, &lhs);
    EXPECT_EQ(cEq.rhs_var_, &rhs);
    EXPECT_EQ(cEq.imm, 0u);
    EXPECT_EQ(cEq.cond, ccu_condition_eq);
    EXPECT_TRUE(cEq.is_var_compare);

    EXPECT_EQ((lhs != rhs).cond, ccu_condition_ne);
    EXPECT_EQ((lhs < rhs).cond, ccu_condition_lt);
    EXPECT_EQ((lhs <= rhs).cond, ccu_condition_le);
    EXPECT_EQ((lhs > rhs).cond, ccu_condition_gt);
    EXPECT_EQ((lhs >= rhs).cond, ccu_condition_ge);

    for (const kernel_call& call : asc::ut::calls()) {
        EXPECT_EQ(call.name, std::string("variable_alloc")) << "比较表达式不应触发 primitive 调用";
    }
}

// ==================== 算术运算符：operator=（变量-变量） ====================

TEST_F(CcuVariableOperatorTest, Assign_VarArithVarToVar)
{
    ccu::variable res;
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    res = varA + varB;
    ASSERT_NE(asc::ut::last_call("variable_add_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_add_var_to_var"), res.handle, varA.handle, varB.handle);

    res = varA - varB;
    ASSERT_NE(asc::ut::last_call("variable_sub_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_sub_var_to_var"), res.handle, varA.handle, varB.handle);

    res = varA * varB;
    ASSERT_NE(asc::ut::last_call("variable_mul_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_mul_var_to_var"), res.handle, varA.handle, varB.handle);

    EXPECT_EQ(asc::ut::calls().size(), 3u);
}

// ==================== 算术运算符：operator=（变量-立即数） ====================

TEST_F(CcuVariableOperatorTest, Assign_VarArithImmToVar)
{
    ccu::variable res;
    ccu::variable varA;
    asc::ut::clear_calls();

    res = varA + uint16_t(7);
    ASSERT_NE(asc::ut::last_call("variable_add_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_add_imm_to_var"), res.handle, varA.handle, uint64_t(7));

    res = varA - uint16_t(8);
    ASSERT_NE(asc::ut::last_call("variable_sub_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_sub_imm_to_var"), res.handle, varA.handle, uint64_t(8));

    res = varA * uint16_t(9);
    ASSERT_NE(asc::ut::last_call("variable_mul_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_mul_imm_to_var"), res.handle, varA.handle, uint64_t(9));

    EXPECT_EQ(asc::ut::calls().size(), 3u);
}

// ==================== 算术复合赋值：+= -= *= ====================

TEST_F(CcuVariableOperatorTest, CompoundAssign_VariableAndImmediate)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    varA += varB;
    ASSERT_NE(asc::ut::last_call("variable_add_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_add_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA -= varB;
    ASSERT_NE(asc::ut::last_call("variable_sub_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_sub_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA *= varB;
    ASSERT_NE(asc::ut::last_call("variable_mul_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_mul_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA += uint16_t(3);
    ASSERT_NE(asc::ut::last_call("variable_add_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_add_imm_to_var"), varA.handle, varA.handle, uint64_t(3));

    varA -= uint16_t(4);
    ASSERT_NE(asc::ut::last_call("variable_sub_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_sub_imm_to_var"), varA.handle, varA.handle, uint64_t(4));

    varA *= uint16_t(5);
    ASSERT_NE(asc::ut::last_call("variable_mul_imm_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_mul_imm_to_var"), varA.handle, varA.handle, uint64_t(5));

    EXPECT_EQ(asc::ut::calls().size(), 6u);
}

// ==================== 逻辑运算符：operator= 与复合赋值 ====================

TEST_F(CcuVariableOperatorTest, Assign_LogicOperators)
{
    ccu::variable res;
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    res = varA & varB;
    ASSERT_NE(asc::ut::last_call("variable_and_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_and_var_to_var"), res.handle, varA.handle, varB.handle);

    res = varA | varB;
    ASSERT_NE(asc::ut::last_call("variable_or_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_or_var_to_var"), res.handle, varA.handle, varB.handle);

    res = varA ^ varB;
    ASSERT_NE(asc::ut::last_call("variable_xor_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_xor_var_to_var"), res.handle, varA.handle, varB.handle);

    res = ~varA;
    ASSERT_NE(asc::ut::last_call("variable_not_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_not_var"), res.handle, varA.handle);

    varA &= varB;
    ASSERT_NE(asc::ut::last_call("variable_and_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_and_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA |= varB;
    ASSERT_NE(asc::ut::last_call("variable_or_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_or_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA ^= varB;
    ASSERT_NE(asc::ut::last_call("variable_xor_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_xor_var_to_var"), varA.handle, varA.handle, varB.handle);

    EXPECT_EQ(asc::ut::calls().size(), 7u);
}

// ==================== 移位运算符（回归覆盖） ====================

TEST_F(CcuVariableOperatorTest, Assign_ShiftOperators)
{
    ccu::variable res;
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    res = varA << varB;
    ASSERT_NE(asc::ut::last_call("variable_shl_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_shl_var_to_var"), res.handle, varA.handle, varB.handle);

    res = varA >> varB;
    ASSERT_NE(asc::ut::last_call("variable_shr_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_shr_var_to_var"), res.handle, varA.handle, varB.handle);

    varA <<= varB;
    ASSERT_NE(asc::ut::last_call("variable_shl_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_shl_var_to_var"), varA.handle, varA.handle, varB.handle);

    varA >>= varB;
    ASSERT_NE(asc::ut::last_call("variable_shr_var_to_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_shr_var_to_var"), varA.handle, varA.handle, varB.handle);

    EXPECT_EQ(asc::ut::calls().size(), 4u);
}

// ==================== 运算符对象构造：类型与操作数元数据 ====================

TEST_F(CcuVariableOperatorTest, OperatorObject_Metadata)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    auto addVV = varA + varB;
    EXPECT_EQ(addVV.type_, ccu::detail::ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(addVV.lhs.handle, varA.handle);
    EXPECT_EQ(addVV.rhs.handle, varB.handle);

    auto subVV = varA - varB;
    EXPECT_EQ(subVV.type_, ccu::detail::ccu_arithmetic_operator_type::subtraction);

    auto mulVV = varA * varB;
    EXPECT_EQ(mulVV.type_, ccu::detail::ccu_arithmetic_operator_type::multiplication);

    auto addVI = varA + uint16_t(11);
    EXPECT_EQ(addVI.type_, ccu::detail::ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(addVI.lhs.handle, varA.handle);
    EXPECT_EQ(addVI.rhs, uint16_t(11));

    auto subVI = varA - uint16_t(12);
    EXPECT_EQ(subVI.type_, ccu::detail::ccu_arithmetic_operator_type::subtraction);
    EXPECT_EQ(subVI.rhs, uint16_t(12));

    auto mulVI = varA * uint16_t(13);
    EXPECT_EQ(mulVI.type_, ccu::detail::ccu_arithmetic_operator_type::multiplication);
    EXPECT_EQ(mulVI.rhs, uint16_t(13));

    auto andOp = varA & varB;
    EXPECT_EQ(andOp.type_, ccu::detail::ccu_logic_operator_type::and_op);
    EXPECT_EQ(andOp.lhs.handle, varA.handle);
    EXPECT_EQ(andOp.rhs.handle, varB.handle);

    EXPECT_EQ((varA | varB).type_, ccu::detail::ccu_logic_operator_type::or_op);
    EXPECT_EQ((varA ^ varB).type_, ccu::detail::ccu_logic_operator_type::xor_op);

    auto notOp = ~varA;
    EXPECT_EQ(notOp.type_, ccu::detail::ccu_logic_operator_type::not_op);
    EXPECT_EQ(notOp.lhs.handle, varA.handle);

    EXPECT_EQ((varA << varB).type_, ccu::detail::ccu_shift_operator_type::left);
    EXPECT_EQ((varA >> varB).type_, ccu::detail::ccu_shift_operator_type::right);
}

// ==================== 非法算子类型：operator= default 分支抛 ccu_exception ====================

TEST_F(CcuVariableOperatorTest, Assign_InvalidOperatorType_Throws)
{
    ccu::variable res;
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    ccu::detail::ccu_arithmetic_operator<ccu::variable, ccu::variable> badArithVar(
        varA, varB, ccu::detail::ccu_arithmetic_operator_type::invalid);
    EXPECT_THROW(res = badArithVar, ccu::detail::ccu_exception);

    ccu::detail::ccu_arithmetic_operator<ccu::variable, uint16_t> badArithImm(
        varA, uint16_t(1), ccu::detail::ccu_arithmetic_operator_type::invalid);
    EXPECT_THROW(res = badArithImm, ccu::detail::ccu_exception);

    ccu::detail::ccu_logic_operator<ccu::variable, ccu::variable> badLogic(
        varA, varB, ccu::detail::ccu_logic_operator_type::invalid);
    EXPECT_THROW(res = badLogic, ccu::detail::ccu_exception);

    ccu::detail::ccu_logic_unary_operator<ccu::variable> badUnary(varA, ccu::detail::ccu_logic_operator_type::invalid);
    EXPECT_THROW(res = badUnary, ccu::detail::ccu_exception);

    ccu::detail::ccu_shift_operator<ccu::variable, ccu::variable> badShift(
        varA, varB, ccu::detail::ccu_shift_operator_type::invalid);
    EXPECT_THROW(res = badShift, ccu::detail::ccu_exception);

    // 校验异常码与消息
    try {
        res = badArithVar;
        FAIL() << "should throw";
    } catch (const ccu::detail::ccu_exception& e) {
        EXPECT_EQ(e.code(), CcuResult::CCU_E_PARA);
        EXPECT_NE(std::string(e.what()).find("invalid arithmetic operator"), std::string::npos);
    }
}

// ==================== 失败注入：CCU_THROW_IF_FAILED 抛出路径 ====================

TEST_F(CcuVariableOperatorTest, KernelFailure_ThrowsCcUException)
{
    ccu::variable res;
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    asc::ut::fail_next(CcuResult::CCU_E_PARA);
    EXPECT_THROW(res = varA + varB, ccu::detail::ccu_exception);
    // 失败被消费，后续恢复正常
    res = varA + varB;
    ASSERT_NE(asc::ut::last_call("variable_add_var_to_var"), nullptr);

    asc::ut::fail_next(CcuResult::CCU_E_INTERNAL);
    EXPECT_THROW(res = varA * uint16_t(2), ccu::detail::ccu_exception);
    try {
        asc::ut::fail_next(CcuResult::CCU_E_INTERNAL);
        res = varA & varB;
        FAIL() << "should throw";
    } catch (const ccu::detail::ccu_exception& e) {
        EXPECT_EQ(e.code(), CcuResult::CCU_E_INTERNAL);
        EXPECT_NE(std::string(e.what()).find("ccu_variable_and_var_to_var"), std::string::npos);
    }

    asc::ut::fail_next(CcuResult::CCU_E_PARA);
    EXPECT_THROW(varA += uint16_t(6), ccu::detail::ccu_exception);

    asc::ut::fail_next(CcuResult::CCU_E_PARA);
    EXPECT_THROW(res = ~varA, ccu::detail::ccu_exception);
}

// ==================== 无 current kernel：构造/接口错误路径 ====================

TEST_F(CcuVariableOperatorTest, NoCurrentKernel_ErrorPaths)
{
    asc::ut::set_current_kernel(nullptr);

    // variable 构造即申请句柄：ccu_variable_alloc 返回 CCU_E_PTR 后构造抛异常
    EXPECT_THROW(ccu::variable var, ccu::detail::ccu_exception);
    EXPECT_THROW(ccu::address addr, ccu::detail::ccu_exception);

    // 新增 C 接口在无 kernel 时返回 CCU_E_PTR
    EXPECT_EQ(asc::ccu_variable_sub_var_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_mul_var_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_add_imm_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_sub_imm_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_mul_imm_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_and_var_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_or_var_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_xor_var_to_var(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_variable_not_var(1, 2), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_address_add_imm_to_addr(1, 2, 3), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_if_begin_var(1, 2, ccu_condition_eq, "lbl"), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_while_begin_var(1, 2, ccu_condition_ne, "lbl"), CcuResult::CCU_E_PTR);
    EXPECT_EQ(asc::ccu_do_while_end_var(1, 2, ccu_condition_lt, "lbl"), CcuResult::CCU_E_PTR);
}

// ==================== 新增 C 接口成功路径：直接调用并校验录制实参 ====================

TEST_F(CcuVariableOperatorTest, NewCApi_SuccessPathRecordsArguments)
{
    asc::ut::clear_calls();

    EXPECT_EQ(asc::ccu_variable_sub_var_to_var(11, 22, 33), CcuResult::CCU_SUCCESS);
    EXPECT_ARGS(asc::ut::last_call("variable_sub_var_to_var"), uint64_t(11), uint64_t(22), uint64_t(33));

    EXPECT_EQ(asc::ccu_variable_mul_var_to_var(11, 22, 33), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(asc::ccu_variable_mul_imm_to_var(11, 22, 33), CcuResult::CCU_SUCCESS);
    EXPECT_ARGS(asc::ut::last_call("variable_mul_imm_to_var"), uint64_t(11), uint64_t(22), uint64_t(33));

    EXPECT_EQ(asc::ccu_variable_xor_var_to_var(11, 22, 33), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(asc::ccu_variable_not_var(11, 22), CcuResult::CCU_SUCCESS);
    EXPECT_ARGS(asc::ut::last_call("variable_not_var"), uint64_t(11), uint64_t(22));

    EXPECT_EQ(asc::ccu_address_add_imm_to_addr(44, 55, 66), CcuResult::CCU_SUCCESS);
    EXPECT_ARGS(asc::ut::last_call("address_add_imm_to_addr"), uint64_t(44), uint64_t(55), uint64_t(66));

    EXPECT_EQ(asc::ccu_if_begin_var(7, 8, ccu_condition_ge, "ut_label"), CcuResult::CCU_SUCCESS);
    const kernel_call* ifVar = asc::ut::last_call("if_begin_var");
    ASSERT_NE(ifVar, nullptr);
    EXPECT_EQ(ifVar->args[0], uint64_t(7));
    EXPECT_EQ(ifVar->args[1], uint64_t(8));
    EXPECT_EQ(ifVar->args[2], static_cast<uint64_t>(ccu_condition_ge));
    EXPECT_STREQ(ifVar->label, "ut_label");

    EXPECT_EQ(asc::ccu_while_begin_var(7, 8, ccu_condition_gt, "ut_label_w"), CcuResult::CCU_SUCCESS);
    ASSERT_NE(asc::ut::last_call("while_begin_var"), nullptr);
    EXPECT_EQ(asc::ut::last_call("while_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_gt));

    EXPECT_EQ(asc::ccu_do_while_end_var(7, 8, ccu_condition_le, "ut_label_d"), CcuResult::CCU_SUCCESS);
    ASSERT_NE(asc::ut::last_call("do_while_end_var"), nullptr);
    EXPECT_EQ(asc::ut::last_call("do_while_end_var")->args[2], static_cast<uint64_t>(ccu_condition_le));
}

// ==================== 控制流宏：CCU_IF 变量-变量 / 变量-立即数 ====================

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuIfVarCompare)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    CCU_IF(varA == varB)
    {
        // body 执行一次
    }

    const kernel_call* beginVar = asc::ut::last_call("if_begin_var");
    ASSERT_NE(beginVar, nullptr);
    EXPECT_EQ(beginVar->args[0], varA.handle);
    EXPECT_EQ(beginVar->args[1], varB.handle);
    EXPECT_EQ(beginVar->args[2], static_cast<uint64_t>(ccu_condition_eq));
    EXPECT_NE(beginVar->label, nullptr);

    EXPECT_EQ(asc::ut::count_of("if_begin"), 0u) << "变量比较不应走立即数 if_begin";
    EXPECT_EQ(asc::ut::count_of("if_label_stack_push"), 1u);
    EXPECT_EQ(asc::ut::count_of("if_label_stack_mark_body_done"), 1u);
    EXPECT_EQ(asc::ut::count_of("flush_closable_pending_ifs"), 1u);
}

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuIfImmediateCompare)
{
    ccu::variable varA;
    asc::ut::clear_calls();

    CCU_IF(varA <= uint64_t(20)) {}

    const kernel_call* begin = asc::ut::last_call("if_begin");
    ASSERT_NE(begin, nullptr);
    EXPECT_EQ(begin->args[0], varA.handle);
    EXPECT_EQ(begin->args[1], uint64_t(20));
    EXPECT_EQ(begin->args[2], static_cast<uint64_t>(ccu_condition_le));
    EXPECT_EQ(asc::ut::count_of("if_begin_var"), 0u);
    EXPECT_EQ(asc::ut::count_of("if_label_stack_push"), 1u);
}

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuIfAllVarConditions)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    CCU_IF(varA != varB) {}
    EXPECT_EQ(asc::ut::last_call("if_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_ne));

    CCU_IF(varA < varB) {}
    EXPECT_EQ(asc::ut::last_call("if_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_lt));

    CCU_IF(varA <= varB) {}
    EXPECT_EQ(asc::ut::last_call("if_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_le));

    CCU_IF(varA > varB) {}
    EXPECT_EQ(asc::ut::last_call("if_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_gt));

    CCU_IF(varA >= varB) {}
    EXPECT_EQ(asc::ut::last_call("if_begin_var")->args[2], static_cast<uint64_t>(ccu_condition_ge));

    EXPECT_EQ(asc::ut::count_of("if_begin_var"), 5u);
}

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuIfBeginFailure_SkipsBody)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    asc::ut::fail_next(CcuResult::CCU_E_PARA);
    bool bodyExecuted = false;
    CCU_IF(varA == varB) { bodyExecuted = true; }
    EXPECT_FALSE(bodyExecuted) << "if_begin_var 失败时不应执行 body";
    EXPECT_EQ(asc::ut::count_of("if_label_stack_push"), 0u);
}

// ==================== 控制流宏：CCU_WHILE 变量-变量 / 变量-立即数 ====================

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuWhileVarCompare)
{
    ccu::variable varA;
    ccu::variable varB;
    asc::ut::clear_calls();

    CCU_WHILE(varA != varB) {}

    const kernel_call* beginVar = asc::ut::last_call("while_begin_var");
    ASSERT_NE(beginVar, nullptr);
    EXPECT_EQ(beginVar->args[0], varA.handle);
    EXPECT_EQ(beginVar->args[1], varB.handle);
    EXPECT_EQ(beginVar->args[2], static_cast<uint64_t>(ccu_condition_ne));

    EXPECT_EQ(asc::ut::count_of("while_begin"), 0u) << "变量比较不应走立即数 while_begin";
    EXPECT_EQ(asc::ut::count_of("do_while_label_stack_pop_for_while"), 1u);
    EXPECT_EQ(asc::ut::count_of("while_end"), 1u);
}

TEST_F(CcuVariableOperatorTest, ControlFlow_CcuWhileImmediateCompare)
{
    ccu::variable varA;
    asc::ut::clear_calls();

    CCU_WHILE(varA < uint64_t(8)) {}

    const kernel_call* begin = asc::ut::last_call("while_begin");
    ASSERT_NE(begin, nullptr);
    EXPECT_EQ(begin->args[0], varA.handle);
    EXPECT_EQ(begin->args[1], uint64_t(8));
    EXPECT_EQ(begin->args[2], static_cast<uint64_t>(ccu_condition_lt));
    EXPECT_EQ(asc::ut::count_of("while_begin_var"), 0u);
    EXPECT_EQ(asc::ut::count_of("while_end"), 1u);
}

// ==================== address + 立即数（fa84c01 新增） ====================

TEST_F(CcuVariableOperatorTest, Address_PlusImmediate_Assign)
{
    ccu::address res;
    ccu::address addrA;
    asc::ut::clear_calls();

    res = addrA + uint16_t(9);
    const kernel_call* call = asc::ut::last_call("address_add_imm_to_addr");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->args[0], res.handle);
    EXPECT_EQ(call->args[1], addrA.handle);
    EXPECT_EQ(call->args[2], uint64_t(9));

    // 构造元数据
    auto op = addrA + uint16_t(21);
    EXPECT_EQ(op.type_, ccu::detail::ccu_arithmetic_operator_type::addition);
    EXPECT_EQ(op.lhs.handle, addrA.handle);
    EXPECT_EQ(op.rhs, uint16_t(21));
}

// ==================== variable 基础赋值回归（assign imm/var） ====================

TEST_F(CcuVariableOperatorTest, Assign_BasicImmediateAndVariable)
{
    ccu::variable res;
    ccu::variable varA;
    asc::ut::clear_calls();

    res = uint64_t(0x1234);
    ASSERT_NE(asc::ut::last_call("variable_assign_imm"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_assign_imm"), res.handle, uint64_t(0x1234));

    res = varA;
    ASSERT_NE(asc::ut::last_call("variable_assign_var"), nullptr);
    EXPECT_ARGS(asc::ut::last_call("variable_assign_var"), res.handle, varA.handle);
}

} // namespace
