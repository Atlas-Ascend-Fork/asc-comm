/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

// 覆盖 ccu_primitives_api.cc 中 load/store 的 array<variable> 重载入参校验：
// num 为 0、num 超过数组长度、空数组（size() 为 0）均在取 v_arr[0] 之前返回 CCU_E_PARA，
// 合法范围时把首元素句柄与 num 原样转发给底层 C 接口。
#include "ccu_variable_ut_stub.h"

#include "ccu/ccu_api/ccu_primitives.h"
#include "ccu/ccu_api/ccu_variable.h"

#include <gtest/gtest.h>

namespace ccu = ::asc::ccu;
using asc::ut::kernel_call;

namespace {

class CcuArrayVarPrimitiveTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        asc::ut::set_current_kernel(asc::ut::recording_kernel());
        asc::ut::clear_calls();
    }

    void TearDown() override { asc::ut::set_current_kernel(nullptr); }
};

// ==================== load(立即数地址, array, num) ====================

TEST_F(CcuArrayVarPrimitiveTest, LoadImmAddr_EmptyArray_ReturnsPara)
{
    ccu::array<ccu::variable> vArr(0);
    EXPECT_EQ(ccu::load(0x1000ULL, vArr, 1), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("load_var"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, LoadImmAddr_NumZero_ReturnsPara)
{
    ccu::array<ccu::variable> vArr(2);
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::load(0x1000ULL, vArr, 0), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("load_var"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, LoadImmAddr_NumExceedsSize_ReturnsPara)
{
    ccu::array<ccu::variable> vArr(2);
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::load(0x1000ULL, vArr, 3), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("load_var"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, LoadImmAddr_ValidRange_ForwardsFirstHandle)
{
    ccu::array<ccu::variable> vArr(2);
    const ccu_variable_handle first = vArr[0].handle;
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::load(0x1000ULL, vArr, 2), ccu_result::CCU_SUCCESS);
    const kernel_call* call = asc::ut::last_call("load_var");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->args[0], 0x1000ULL);
    EXPECT_EQ(call->args[1], first);
    EXPECT_EQ(call->args[2], 2U);
}

// ==================== load(variable 地址, array, num) ====================

TEST_F(CcuArrayVarPrimitiveTest, LoadVarAddr_EmptyArray_ReturnsPara)
{
    ccu::variable addrVar;
    ccu::array<ccu::variable> vArr(0);
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::load(addrVar, vArr, 1), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("ccu_load_var_from_var_addr"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, LoadVarAddr_ValidRange_ForwardsFirstHandle)
{
    ccu::variable addrVar;
    ccu::array<ccu::variable> vArr(1);
    const ccu_variable_handle first = vArr[0].handle;
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::load(addrVar, vArr, 1), ccu_result::CCU_SUCCESS);
    const kernel_call* call = asc::ut::last_call("ccu_load_var_from_var_addr");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->args[0], addrVar.handle);
    EXPECT_EQ(call->args[1], first);
    EXPECT_EQ(call->args[2], 1U);
}

// ==================== store(立即数地址, array, num) ====================

TEST_F(CcuArrayVarPrimitiveTest, StoreImmAddr_EmptyArray_ReturnsPara)
{
    ccu::array<ccu::variable> vArr(0);
    EXPECT_EQ(ccu::store(0x2000ULL, vArr, 1), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("store_var"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, StoreImmAddr_ValidRange_ForwardsFirstHandle)
{
    ccu::array<ccu::variable> vArr(2);
    const ccu_variable_handle first = vArr[0].handle;
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::store(0x2000ULL, vArr, 2), ccu_result::CCU_SUCCESS);
    const kernel_call* call = asc::ut::last_call("store_var");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->args[0], 0x2000ULL);
    EXPECT_EQ(call->args[1], first);
    EXPECT_EQ(call->args[2], 2U);
}

// ==================== store(variable 地址, array, num) ====================

TEST_F(CcuArrayVarPrimitiveTest, StoreVarAddr_EmptyArray_ReturnsPara)
{
    ccu::variable addrVar;
    ccu::array<ccu::variable> vArr(0);
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::store(addrVar, vArr, 1), ccu_result::CCU_E_PARA);
    EXPECT_EQ(asc::ut::count_of("ccu_store_var_to_var_addr"), 0U);
}

TEST_F(CcuArrayVarPrimitiveTest, StoreVarAddr_ValidRange_ForwardsFirstHandle)
{
    ccu::variable addrVar;
    ccu::array<ccu::variable> vArr(1);
    const ccu_variable_handle first = vArr[0].handle;
    asc::ut::clear_calls();
    EXPECT_EQ(ccu::store(addrVar, vArr, 1), ccu_result::CCU_SUCCESS);
    const kernel_call* call = asc::ut::last_call("ccu_store_var_to_var_addr");
    ASSERT_NE(call, nullptr);
    EXPECT_EQ(call->args[0], addrVar.handle);
    EXPECT_EQ(call->args[1], first);
    EXPECT_EQ(call->args[2], 1U);
}

} // namespace
