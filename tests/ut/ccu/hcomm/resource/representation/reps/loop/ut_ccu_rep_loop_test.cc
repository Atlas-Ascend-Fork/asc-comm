/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <climits>

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_base_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_block_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_arg_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopcall_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loop_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopgroup_bundle_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_setloop_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "ccu_api_exception.h"
#include "null_ptr_exception.h"
#include "internal_exception.h"
#include "ccu/hcomm/ccu_api_types.h"

#include "ccu/hcomm/ccu_utils.hpp"

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepLoopTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

TEST_F(CcuRepLoopTest, Constructor)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    EXPECT_EQ(loop.get_label(), "test_loop");
    EXPECT_EQ(loop.type(), ccu_rep_type::loop);
}

TEST_F(CcuRepLoopTest, get_loop_param)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    variable* param_ = loop.get_loop_param();
    EXPECT_NE(param_, nullptr);
}

TEST_F(CcuRepLoopTest, reference)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);
    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");

    loop.reference(loop_block_);

    EXPECT_EQ(loop.get_loop_block(), loop_block_.get());
}

TEST_F(CcuRepLoopTest, set_loop_param)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    executor executor(nullptr);
    variable var_(nullptr);
    auto setLoop = loop.set_loop_param(executor, var_);

    EXPECT_NE(setLoop, nullptr);
    EXPECT_EQ(setLoop->type(), ccu_rep_type::set_loop);
}

TEST_F(CcuRepLoopTest, describe)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    std::string desc = loop.describe();
    EXPECT_NE(desc.find("test_loop"), std::string::npos);
}

TEST_F(CcuRepLoopTest, translate)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
    loop_block_->append(std::make_shared<ccu_rep_nop>(&insGen));
    loop.reference(loop_block_);

    ccu_instr instr_[2]{};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    loop_block_->translate(nullptr, instrPtr, instrId, dep);
    instrPtr = instr_;
    instrId = 0;

    bool result = loop.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instrId, 1U);
}

TEST_F(CcuRepLoopTest, Translate_EmptyLoopBlock)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
    loop.reference(loop_block_);

    ccu_instr instr_{};
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    loop_block_->translate(nullptr, instrPtr, instrId, dep);
    instrPtr = &instr_;
    instrId = 0;

    bool result = loop.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_FALSE(result);
}

TEST_F(CcuRepLoopTest, Translate_NullLoopBlock)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    ccu_instr instr_{};
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    EXPECT_THROW(loop.translate(nullptr, instrPtr, instrId, dep), CcuUtException);
}

TEST_F(CcuRepLoopTest, Translate_UntranslatedLoopBlock)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
    loop_block_->append(std::make_shared<ccu_rep_nop>(&insGen));
    loop.reference(loop_block_);

    ccu_instr instr_{};
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    EXPECT_THROW(loop.translate(nullptr, instrPtr, instrId, dep), CcuUtException);
}

TEST_F(CcuRepLoopTest, Translate_InstrIdOverflow)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "test_loop", loop_param_);

    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
    loop_block_->append(std::make_shared<ccu_rep_nop>(&insGen));
    loop.reference(loop_block_);

    ccu_instr instr_[2]{};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    loop_block_->translate(nullptr, instrPtr, instrId, dep);
    instrPtr = instr_;
    instrId = USHRT_MAX;

    bool result = loop.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_FALSE(result);
}

class CcuRepLoopBlockTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepLoopBlockTest, Constructor)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");

    EXPECT_EQ(loop_block_.get_label(), "test_block");
    EXPECT_EQ(loop_block_.type(), ccu_rep_type::loop_block);
}

TEST_F(CcuRepLoopBlockTest, describe)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");

    std::string desc = loop_block_.describe();
    EXPECT_NE(desc.find("test_block"), std::string::npos);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_Variable)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    variable var_(nullptr);

    loop_block_.define_arg(var_);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::variable);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_Memory)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    memory mem_(addr_, token);

    loop_block_.define_arg(mem_);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::memory);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_LocalAddr)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    local_addr local_addr(addr_, token);

    loop_block_.define_arg(local_addr);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::local_addr);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_RemoteAddr)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    remote_addr remote_addr(addr_, token);

    loop_block_.define_arg(remote_addr);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::remote_addr);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_VariableList)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    std::vector<variable> var_list = {variable(nullptr), variable(nullptr)};

    loop_block_.define_arg(var_list);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::variable_list);
    EXPECT_EQ(arg.var_list.size(), 2U);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_MemoryList)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<memory> mem_list = {memory(addr_, token), memory(addr_, token)};

    loop_block_.define_arg(mem_list);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::memory_list);
    EXPECT_EQ(arg.mem_list.size(), 2U);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_LocalAddrList)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<local_addr> addr_list = {local_addr(addr_, token), local_addr(addr_, token)};

    loop_block_.define_arg(addr_list);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::local_addr_list);
    EXPECT_EQ(arg.local_addr_list.size(), 2U);
}

TEST_F(CcuRepLoopBlockTest, DefineArg_RemoteAddrList)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<remote_addr> addr_list = {remote_addr(addr_, token), remote_addr(addr_, token)};

    loop_block_.define_arg(addr_list);

    ccu_rep_arg& arg = loop_block_.get_arg(0);
    EXPECT_EQ(arg.type, ccu_arg_type::remote_addr_list);
    EXPECT_EQ(arg.remote_addr_list.size(), 2U);
}

TEST_F(CcuRepLoopBlockTest, GetArg_OutOfRange)
{
    ccu_rep_loop_block loop_block_(&insGen, "test_block");

    EXPECT_THROW(loop_block_.get_arg(0), CcuUtException);
}

class CcuRepLoopCallTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepLoopCallTest, Constructor)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");

    EXPECT_EQ(loopCall.get_label(), "test_call");
    EXPECT_EQ(loopCall.type(), ccu_rep_type::loop_call);
}

TEST_F(CcuRepLoopCallTest, reference)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");

    loopCall.reference(loop_block_);

    EXPECT_EQ(loopCall.get_label(), "test_call");
}

TEST_F(CcuRepLoopCallTest, SetInArg_Variable)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    variable var_(nullptr);

    loopCall.set_in_arg(var_);

    EXPECT_EQ(loopCall.instr_count(), 1U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_VariableList)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    std::vector<variable> var_list = {variable(nullptr), variable(nullptr)};

    loopCall.set_in_arg(var_list);

    EXPECT_EQ(loopCall.instr_count(), 2U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_Memory)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    memory mem_(addr_, token);

    loopCall.set_in_arg(mem_);

    EXPECT_EQ(loopCall.instr_count(), 2U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_MemoryList)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<memory> mem_list = {memory(addr_, token), memory(addr_, token)};

    loopCall.set_in_arg(mem_list);

    EXPECT_EQ(loopCall.instr_count(), 4U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_LocalAddr)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    local_addr local_addr(addr_, token);

    loopCall.set_in_arg(local_addr);

    EXPECT_EQ(loopCall.instr_count(), 2U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_RemoteAddr)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    remote_addr remote_addr(addr_, token);

    loopCall.set_in_arg(remote_addr);

    EXPECT_EQ(loopCall.instr_count(), 2U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_LocalAddrList)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<local_addr> addr_list = {local_addr(addr_, token), local_addr(addr_, token)};

    loopCall.set_in_arg(addr_list);

    EXPECT_EQ(loopCall.instr_count(), 4U);
}

TEST_F(CcuRepLoopCallTest, SetInArg_RemoteAddrList)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");
    address addr_(nullptr);
    variable token(nullptr);
    std::vector<remote_addr> addr_list = {remote_addr(addr_, token), remote_addr(addr_, token)};

    loopCall.set_in_arg(addr_list);

    EXPECT_EQ(loopCall.instr_count(), 4U);
}

TEST_F(CcuRepLoopCallTest, describe)
{
    ccu_rep_loop_call loopCall(&insGen, "test_call");

    std::string desc = loopCall.describe();
    EXPECT_NE(desc.find("test_call"), std::string::npos);
}

TEST_F(CcuRepLoopCallTest, translate)
{
    ccu_ins_generater_v1 insGen;
    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
    address addr_(nullptr);
    variable token(nullptr);
    loop_block_->define_arg(variable(nullptr));
    loop_block_->define_arg(local_addr(addr_, token));

    ccu_rep_loop_call loopCall(&insGen, "test_call");
    loopCall.reference(loop_block_);
    variable var_(nullptr);
    loopCall.set_in_arg(var_);
    loopCall.set_in_arg(local_addr(addr_, token));

    ccu_instr instr_[10] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};

    loop_block_->translate(nullptr, instrPtr, instrId, dep);
    instrPtr = instr_;
    instrId = 0;

    bool result = loopCall.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
}

class CcuRepLoopGroupBundleTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}

    static ccu_rep_loop_group_bundle::loop_entry MakeTranslatedLoopEntry()
    {
        ccu_ins_generater_v1 insGen;
        ccu_rep_loop_group_bundle::loop_entry entry;
        entry.config = ccu_loop_config{};
        entry.executor_value = executor(nullptr);
        entry.rep_loop_block = std::make_shared<ccu_rep_loop_block>(&insGen, "loop_block");
        entry.rep_loop_block->append(std::make_shared<ccu_rep_nop>(&insGen));
        entry.loop_param_var = variable(nullptr);
        entry.layout_value = ccu_rep_loop_group_bundle::layout::config;

        ccu_instr blockInstr[1]{};
        ccu_instr* blockPtr = blockInstr;
        uint16_t blockInstrId = 0;
        trans_dep blockDep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};
        entry.rep_loop_block->translate(nullptr, blockPtr, blockInstrId, blockDep);
        return entry;
    }
};

TEST_F(CcuRepLoopGroupBundleTest, Constructor)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);

    EXPECT_EQ(bundle.type(), ccu_rep_type::loop_group);
}

TEST_F(CcuRepLoopGroupBundleTest, get_offset_param)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);

    EXPECT_EQ(bundle.get_offset_param().id(), offsetParam.id());
}

TEST_F(CcuRepLoopGroupBundleTest, translate)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);
    bundle.add_loop(MakeTranslatedLoopEntry());

    constexpr uint16_t kExpectedInstrCount = 8;
    ccu_instr instr_[kExpectedInstrCount] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};

    bool result = bundle.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instrId, kExpectedInstrCount);
}

TEST_F(CcuRepLoopGroupBundleTest, get_start_loop_instr_id)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);
    bundle.add_loop(MakeTranslatedLoopEntry());

    constexpr uint16_t kBundleInstrCount = 8;
    ccu_instr instr_[kBundleInstrCount] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 10;
    trans_dep dep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};

    bundle.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_EQ(bundle.get_start_loop_instr_id(), 16U);
}

TEST_F(CcuRepLoopGroupBundleTest, describe)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);

    std::string desc = bundle.describe();
    EXPECT_NE(desc.find("LoopGroupBundle"), std::string::npos);
}

class CcuRepSetLoopTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepSetLoopTest, Constructor)
{
    variable loop_param_(nullptr);
    executor executor(nullptr);
    variable var_(nullptr);
    ccu_rep_set_loop setLoop(&insGen, loop_param_, executor, var_);

    EXPECT_EQ(setLoop.type(), ccu_rep_type::set_loop);
    EXPECT_EQ(setLoop.loop_param.id(), loop_param_.id());
    EXPECT_EQ(setLoop.executor_value.id(), executor.id());
    EXPECT_EQ(setLoop.var.id(), var_.id());
}

TEST_F(CcuRepSetLoopTest, translate)
{
    ccu_ins_generater_v1 insGen;
    variable loop_param_(nullptr);
    executor executor(nullptr);
    variable var_(nullptr);
    ccu_rep_set_loop setLoop(&insGen, loop_param_, executor, var_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};

    bool result = setLoop.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_EQ(instrId, 2U);
}

TEST_F(CcuRepSetLoopTest, describe)
{
    variable loop_param_(nullptr);
    executor executor(nullptr);
    variable var_(nullptr);
    ccu_rep_set_loop setLoop(&insGen, loop_param_, executor, var_);

    std::string desc = setLoop.describe();
    EXPECT_NE(desc.find("loopParam["), std::string::npos);
    EXPECT_NE(desc.find("var["), std::string::npos);
}

TEST_F(CcuRepLoopTest, Constructor_A6Variant)
{
    variable loop_param_(nullptr);
    variable loop_iter_num_(nullptr);
    variable loop_gsa_offset_(nullptr);
    ccu_rep_loop loop(&insGen, "a6_loop", loop_param_, loop_iter_num_, loop_gsa_offset_);

    EXPECT_EQ(loop.type(), ccu_rep_type::loop);
    EXPECT_EQ(loop.get_label(), "a6_loop");
}

TEST_F(CcuRepLoopTest, Translate_A6Variant_V1GeneratorThrows)
{
    // A6 构造(supportCcuV1=false) + v1 生成器: validate 抛错
    variable loop_param_(nullptr);
    variable loop_iter_num_(nullptr);
    variable loop_gsa_offset_(nullptr);
    ccu_rep_loop loop(&insGen, "a6_loop", loop_param_, loop_iter_num_, loop_gsa_offset_);

    ccu_instr instr_[2]{};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    EXPECT_THROW(loop.translate(nullptr, instrPtr, instrId, dep), CcuUtException);
}

TEST_F(CcuRepLoopTest, TranslateViaGenerator_Loop)
{
    variable loop_param_(nullptr);
    ccu_rep_loop loop(&insGen, "gen_loop", loop_param_);

    auto loop_block_ = std::make_shared<ccu_rep_loop_block>(&insGen, "gen_loop_block");
    loop_block_->append(std::make_shared<ccu_rep_nop>(&insGen));
    loop.reference(loop_block_);

    ccu_instr instr_[2]{};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    loop_block_->translate(nullptr, instrPtr, instrId, dep);
    instrPtr = instr_;
    instrId = 0;

    HcclResult ret = insGen.ccu_rep_loop_translate(nullptr, instrPtr, instrId, &loop);
    EXPECT_EQ(ret, HcclResult::HCCL_SUCCESS);
}

class CcuRepLoopCallGeneratorTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    trans_dep dep{};

    void SetUp() override
    {
        dep.reserve_gsa_id = 1;
        dep.reserve_xn_id = 2;
    }
};

TEST_F(CcuRepLoopCallGeneratorTest, TranslateViaGenerator_AllArgTypes)
{
    address addr_(nullptr);
    variable token(nullptr);
    variable var_(nullptr);
    memory mem_(addr_, token);
    local_addr local_(addr_, token);
    remote_addr remote_(addr_, token);

    auto block = std::make_shared<ccu_rep_loop_block>(&insGen, "args_block");
    block->define_arg(var_);
    block->define_arg(std::vector<variable>{var_, var_});
    block->define_arg(mem_);
    block->define_arg(std::vector<memory>{mem_, mem_});
    block->define_arg(local_);
    block->define_arg(std::vector<local_addr>{local_, local_});
    block->define_arg(remote_);
    block->define_arg(std::vector<remote_addr>{remote_, remote_});

    ccu_rep_loop_call call(&insGen, "args_block");
    call.set_in_arg(var_);
    call.set_in_arg(std::vector<variable>{var_, var_});
    call.set_in_arg(mem_);
    call.set_in_arg(std::vector<memory>{mem_, mem_});
    call.set_in_arg(local_);
    call.set_in_arg(std::vector<local_addr>{local_, local_});
    call.set_in_arg(remote_);
    call.set_in_arg(std::vector<remote_addr>{remote_, remote_});
    call.reference(block);

    ccu_instr instrs[32] = {};
    ccu_instr* instrPtr = instrs;
    uint16_t instrId = 0;

    HcclResult ret = insGen.ccu_rep_loop_call_translate(nullptr, instrPtr, instrId, &call, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_SUCCESS);
}

TEST_F(CcuRepLoopCallGeneratorTest, TranslateViaGenerator_ListSizeMismatch)
{
    variable var_(nullptr);

    auto block = std::make_shared<ccu_rep_loop_block>(&insGen, "mismatch_block");
    block->define_arg(std::vector<variable>{var_, var_});

    ccu_rep_loop_call call(&insGen, "mismatch_block");
    call.set_in_arg(std::vector<variable>{var_});
    call.reference(block);

    ccu_instr instrs[8] = {};
    ccu_instr* instrPtr = instrs;
    uint16_t instrId = 0;

    HcclResult ret = insGen.ccu_rep_loop_call_translate(nullptr, instrPtr, instrId, &call, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_PARA);
}

TEST_F(CcuRepLoopCallGeneratorTest, TranslateViaGenerator_ArgTypeMismatch)
{
    address addr_(nullptr);
    variable token(nullptr);
    variable var_(nullptr);
    memory mem_(addr_, token);

    auto block = std::make_shared<ccu_rep_loop_block>(&insGen, "type_block");
    block->define_arg(var_);

    ccu_rep_loop_call call(&insGen, "type_block");
    call.set_in_arg(mem_);
    call.reference(block);

    ccu_instr instrs[8] = {};
    ccu_instr* instrPtr = instrs;
    uint16_t instrId = 0;

    HcclResult ret = insGen.ccu_rep_loop_call_translate(nullptr, instrPtr, instrId, &call, dep);
    EXPECT_EQ(ret, HcclResult::HCCL_E_PARA);
}

TEST_F(CcuRepLoopCallGeneratorTest, TranslateViaGenerator_NullLoopBlock)
{
    variable var_(nullptr);
    ccu_rep_loop_call call(&insGen, "null_block");

    ccu_instr instrs[8] = {};
    ccu_instr* instrPtr = instrs;
    uint16_t instrId = 0;

    HcclResult ret = insGen.ccu_rep_loop_call_translate(nullptr, instrPtr, instrId, &call, dep);
    EXPECT_NE(ret, HcclResult::HCCL_SUCCESS);
}

TEST_F(CcuRepSetLoopTest, TranslateViaGenerator)
{
    variable loop_param_(nullptr);
    executor executor_(nullptr);
    variable var_(nullptr);
    ccu_rep_set_loop setLoop(&insGen, loop_param_, executor_, var_);

    ccu_instr instr_[2] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{};

    HcclResult ret = insGen.ccu_rep_set_loop_translate(nullptr, instrPtr, instrId, &setLoop);
    EXPECT_EQ(ret, HcclResult::HCCL_SUCCESS);
}

TEST_F(CcuRepLoopGroupBundleTest, TranslatePackedVarLayout)
{
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, parallel_param, offsetParam);
    auto entry = MakeTranslatedLoopEntry();
    entry.layout_value = ccu_rep_loop_group_bundle::layout::version_v2;
    bundle.add_loop(entry);

    ccu_instr instr_[16] = {};
    ccu_instr* instrPtr = instr_;
    uint16_t instrId = 0;
    trans_dep dep{0, 0, 1, 1, 0, {0, 0}, {0}, 0, 0, {0, 0, 0}, {0, 0}, 0, false};

    bool result = bundle.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    // 1个var类loop: 2条参数载入 + 1条loopgroup + 2条跳过 + 1条loop + 1条收尾 = 7
    EXPECT_EQ(instrId, 7U);
}

TEST_F(CcuRepLoopGroupBundleTest, InstrCount_ConfigLayout)
{
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, grpCfg, parallel_param, offsetParam);
    bundle.add_loop(MakeTranslatedLoopEntry());

    EXPECT_EQ(bundle.instr_count(), 8U);
}

TEST_F(CcuRepLoopGroupBundleTest, InstrCount_PackedVarLayout)
{
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    ccu_rep_loop_group_bundle bundle(&insGen, parallel_param, offsetParam);
    auto entry = MakeTranslatedLoopEntry();
    entry.layout_value = ccu_rep_loop_group_bundle::layout::version_v2;
    bundle.add_loop(entry);

    EXPECT_EQ(bundle.instr_count(), 7U);
}

TEST_F(CcuRepLoopGroupBundleTest, InstrCount_NullBundleThrows)
{
    EXPECT_THROW(insGen.ccu_rep_loop_group_bundle_instr_count(nullptr), CcuUtException);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
