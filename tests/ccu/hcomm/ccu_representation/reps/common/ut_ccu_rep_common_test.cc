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

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepCommonBaseTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
    void TearDown() override {}
};

class CcuRepBlockTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepLoadTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepLoadArgTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepLoadVarTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepNopTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepStoreTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepStoreVarTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepBlockTest, Constructor_DefaultLabel)
{
    ccu_rep_block block(&insGen);
    EXPECT_EQ(block.type(), ccu_rep_type::block);
    EXPECT_EQ(block.get_label(), "");
    EXPECT_EQ(block.instr_count(), 0);
}

TEST_F(CcuRepBlockTest, Constructor_WithLabel)
{
    ccu_rep_block block(&insGen, "testBlock");
    EXPECT_EQ(block.type(), ccu_rep_type::block);
    EXPECT_EQ(block.get_label(), "testBlock");
}

TEST_F(CcuRepBlockTest, describe)
{
    ccu_rep_block block(&insGen, "testBlock");
    std::string desc = block.describe();
    EXPECT_NE(desc.find("RepBlock"), std::string::npos);
}

TEST_F(CcuRepBlockTest, Append_And_GetReps)
{
    ccu_rep_block block(&insGen, "testBlock");
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    block.append(nop);
    EXPECT_EQ(block.get_reps().size(), 1);
}

TEST_F(CcuRepBlockTest, InstrCount_SingleNop)
{
    ccu_rep_block block(&insGen, "testBlock");
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    block.append(nop);
    EXPECT_EQ(block.instr_count(), 1);
}

TEST_F(CcuRepBlockTest, InstrCount_MultipleReps)
{
    ccu_rep_block block(&insGen, "testBlock");
    block.append(std::make_shared<ccu_rep_nop>(&insGen));
    block.append(std::make_shared<ccu_rep_nop>(&insGen));
    EXPECT_EQ(block.instr_count(), 2);
}

TEST_F(CcuRepBlockTest, GetRepByInstrId_Found)
{
    ccu_rep_block block(&insGen, "testBlock");
    auto nop1 = std::make_shared<ccu_rep_nop>(&insGen);
    auto nop2 = std::make_shared<ccu_rep_nop>(&insGen);
    block.append(nop1);
    block.append(nop2);
    auto rep = block.get_rep_by_instr_id(0);
    EXPECT_NE(rep, nullptr);
}

TEST_F(CcuRepBlockTest, GetRepByInstrId_NotFound)
{
    ccu_rep_block block(&insGen, "testBlock");
    block.append(std::make_shared<ccu_rep_nop>(&insGen));
    auto rep = block.get_rep_by_instr_id(100);
    EXPECT_EQ(rep, nullptr);
}

TEST_F(CcuRepBlockTest, Translate_SetsTranslated)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_block block(&insGen, "testBlock");
    block.append(std::make_shared<ccu_rep_nop>(&insGen));
    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;
    bool result = block.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(block.translated());
}

TEST_F(CcuRepLoadTest, Constructor)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_load load(&insGen, 0x1000, var_, 1);
    EXPECT_EQ(load.type(), ccu_rep_type::load);
    EXPECT_EQ(load.instr_count(), 7);
}

TEST_F(CcuRepLoadTest, describe)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_load load(&insGen, 0x1000, var_, 1);
    std::string desc = load.describe();
    EXPECT_NE(desc.find("Load("), std::string::npos);
}

TEST_F(CcuRepLoadTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_load load(&insGen, 0x1000, var_, 1);
    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.xn_base_addr[0] = 0x100000;
    dep.comm_gsa[0] = 1;
    dep.comm_gsa[1] = 2;
    dep.comm_xn[0] = 3;
    dep.comm_xn[1] = 4;
    dep.comm_xn[2] = 5;
    dep.reserve_channal_id[0] = 6;
    dep.comm_signal = 7;
    dep.ccu_res_space_token_info = 0x1000;
    dep.mem_token_info = 0x2000;
    bool result = load.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(load.translated());
    EXPECT_EQ(load.start_instr_id(), 0);
    EXPECT_EQ(instrId, 7);
}

TEST_F(CcuRepLoadArgTest, Constructor)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_arg loadArg(&insGen, var_, 1, 1);
    EXPECT_EQ(loadArg.type(), ccu_rep_type::load_arg);
    EXPECT_EQ(loadArg.instr_count(), 1);
    EXPECT_EQ(loadArg.get_var_id(), 2);
}

TEST_F(CcuRepLoadArgTest, describe)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_arg loadArg(&insGen, var_, 1, 1);
    std::string desc = loadArg.describe();
    EXPECT_NE(desc.find("Variable[2]"), std::string::npos);
    EXPECT_NE(desc.find("Arg[1]"), std::string::npos);
}

TEST_F(CcuRepLoadArgTest, Translate_IsFuncBlock)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_arg loadArg(&insGen, var_, 1, 1);
    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.is_func_block = true;
    dep.load_xn_id = 3;
    dep.reserve_xn_id = 4;
    bool result = loadArg.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(loadArg.translated());
    EXPECT_EQ(instrId, 1);
}

TEST_F(CcuRepLoadArgTest, Translate_NotFuncBlock)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_arg loadArg(&insGen, var_, 1, 1);
    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.is_func_block = false;
    bool result = loadArg.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(loadArg.translated());
    EXPECT_EQ(instrId, 1);
}

TEST_F(CcuRepLoadVarTest, Constructor)
{
    ccu_rep_context context_;
    variable src_(&context_);
    src_.reset(1);
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_var loadVar(&insGen, src_, var_);
    EXPECT_EQ(loadVar.type(), ccu_rep_type::load_var);
    EXPECT_EQ(loadVar.instr_count(), 7);
}

TEST_F(CcuRepLoadVarTest, describe)
{
    ccu_rep_context context_;
    variable src_(&context_);
    src_.reset(1);
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_var loadVar(&insGen, src_, var_);
    std::string desc = loadVar.describe();
    EXPECT_NE(desc.find("Load Var("), std::string::npos);
}

TEST_F(CcuRepLoadVarTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable src_(&context_);
    src_.reset(1);
    variable var_(&context_);
    var_.reset(2);
    ccu_rep_load_var loadVar(&insGen, src_, var_);
    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.xn_base_addr[0] = 0x100000;
    dep.comm_gsa[0] = 1;
    dep.comm_gsa[1] = 2;
    dep.comm_xn[0] = 3;
    dep.comm_xn[1] = 4;
    dep.comm_xn[2] = 5;
    dep.reserve_channal_id[0] = 6;
    dep.comm_signal = 7;
    dep.reserve_gsa_id = 8;
    dep.ccu_res_space_token_info = 0x1000;
    dep.mem_token_info = 0x2000;
    bool result = loadVar.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(loadVar.translated());
    EXPECT_EQ(instrId, 7);
}

TEST_F(CcuRepNopTest, Constructor)
{
    ccu_rep_nop nop(&insGen);
    EXPECT_EQ(nop.type(), ccu_rep_type::nop);
    EXPECT_EQ(nop.instr_count(), 1);
}

TEST_F(CcuRepNopTest, describe)
{
    ccu_rep_nop nop(&insGen);
    std::string desc = nop.describe();
    EXPECT_NE(desc.find("Nop"), std::string::npos);
}

TEST_F(CcuRepNopTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_nop nop(&insGen);
    ccu_instr instr_[5] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 10;
    trans_dep dep = {};
    dep.reserve_xn_id = 1;
    bool result = nop.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(nop.translated());
    EXPECT_EQ(nop.start_instr_id(), 10);
    EXPECT_EQ(instrId, 11);
}

TEST_F(CcuRepStoreTest, Constructor)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_store store(&insGen, var_, 0x2000);
    EXPECT_EQ(store.type(), ccu_rep_type::store);
    EXPECT_EQ(store.instr_count(), 7);
}

TEST_F(CcuRepStoreTest, describe)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_store store(&insGen, var_, 0x2000);
    std::string desc = store.describe();
    EXPECT_NE(desc.find("Store"), std::string::npos);
}

TEST_F(CcuRepStoreTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    ccu_rep_store store(&insGen, var_, 0x2000);
    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.xn_base_addr[0] = 0x100000;
    dep.comm_gsa[0] = 1;
    dep.comm_gsa[1] = 2;
    dep.comm_xn[0] = 3;
    dep.comm_xn[1] = 4;
    dep.comm_xn[2] = 5;
    dep.reserve_channal_id[0] = 6;
    dep.comm_signal = 7;
    dep.ccu_res_space_token_info = 0x1000;
    dep.mem_token_info = 0x2000;
    bool result = store.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(store.translated());
    EXPECT_EQ(instrId, 7);
}

TEST_F(CcuRepStoreVarTest, Constructor)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    variable dst_(&context_);
    dst_.reset(2);
    ccu_rep_store_var storeVar(&insGen, var_, dst_);
    EXPECT_EQ(storeVar.type(), ccu_rep_type::store_var);
    EXPECT_EQ(storeVar.instr_count(), 7);
}

TEST_F(CcuRepStoreVarTest, describe)
{
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    variable dst_(&context_);
    dst_.reset(2);
    ccu_rep_store_var storeVar(&insGen, var_, dst_);
    std::string desc = storeVar.describe();
    EXPECT_NE(desc.find("Store Var"), std::string::npos);
}

TEST_F(CcuRepStoreVarTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable var_(&context_);
    var_.reset(1);
    variable dst_(&context_);
    dst_.reset(2);
    ccu_rep_store_var storeVar(&insGen, var_, dst_);
    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.xn_base_addr[0] = 0x100000;
    dep.comm_gsa[0] = 1;
    dep.comm_gsa[1] = 2;
    dep.comm_xn[0] = 3;
    dep.comm_xn[1] = 4;
    dep.comm_xn[2] = 5;
    dep.reserve_channal_id[0] = 6;
    dep.comm_signal = 7;
    dep.reserve_gsa_id = 8;
    dep.ccu_res_space_token_info = 0x1000;
    dep.mem_token_info = 0x2000;
    bool result = storeVar.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(storeVar.translated());
}

} // namespace
} // namespace ccu_rep
} // namespace asc
