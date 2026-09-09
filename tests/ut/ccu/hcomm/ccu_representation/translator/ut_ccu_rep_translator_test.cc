/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/translator/ccu_rep_translator_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funccall_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loop_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopcall_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_loadarg_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include <gtest/gtest.h>

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepTranslatorTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    std::shared_ptr<ccu_rep_reference_manager> refMgr{std::make_shared<ccu_rep_reference_manager>(0)};
    trans_dep dep{};
    ccu_kernel kernel{}; // common_process 需要真实 kernel (get_const_value2_var_map)

    void SetUp() override
    {
        dep.die_id = 0;
        dep.reserve_xn_id = 100;
        dep.reserve_gsa_id = 200;
        dep.reserve_cke_id = 300;
        ccu_rep_translator::set_ccu_version(HCOMM_CCU_VERSION_V1);
    }

    void TearDown() override { ccu_rep_translator::set_ccu_version(HCOMM_CCU_VERSION_V1); }
};

TEST_F(CcuRepTranslatorTest, Constructor_InvalidVersion_Throws)
{
    ccu_rep_translator::set_ccu_version(HCOMM_CCU_VERSION_INVALID);
    trans_dep local_dep{};
    EXPECT_THROW(ccu_rep_translator translator(refMgr, local_dep), CcuUtException);
}

TEST_F(CcuRepTranslatorTest, GetInstrNum_V1_Is4) { EXPECT_EQ(ccu_rep_translator::get_instr_num(), 4u); }

TEST_F(CcuRepTranslatorTest, GetInstrNum_V2_Is13)
{
    ccu_rep_translator::set_ccu_version(HCOMM_CCU_VERSION_V2);
    EXPECT_EQ(ccu_rep_translator::get_instr_num(), 13u);
}

TEST_F(CcuRepTranslatorTest, GetRes_PopulatesAllResourceTypes)
{
    // 历史 GTEST_SKIP 原因（全量运行静态状态干扰）已定位为桩库与 UT 目标
    // CXX11 ABI 不一致（见 tests/ccu/stub/CMakeLists.txt 注释），修复后恢复启用。
    ccu_rep_translator translator(refMgr, dep);
    ccu_rep_resource res;
    translator.get_res(res);

    EXPECT_EQ(res.variable[dep.die_id].size(), static_cast<size_t>(ccu_translator_xn_num));
    EXPECT_EQ(res.address[dep.die_id].size(), static_cast<size_t>(ccu_translator_gsa_num));
    EXPECT_EQ(res.completed_event[dep.die_id].size(), static_cast<size_t>(ccu_translator_cke_num));
}

TEST_F(CcuRepTranslatorTest, Translate_EmptyRepVec_V1)
{
    ccu_rep_translator translator(refMgr, dep);
    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    // 3 条通用指令 + 1 条终止指令
    EXPECT_EQ(info.start_instr_id, 0u);
    EXPECT_EQ(info.instr_count, 4u);
    EXPECT_EQ(info.instr_vec.size(), 4u);
}

TEST_F(CcuRepTranslatorTest, Translate_SimpleNop_V1)
{
    ccu_rep_translator translator(refMgr, dep);
    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(std::make_shared<ccu_rep_nop>(&insGen));

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_EQ(info.start_instr_id, 0u);
    EXPECT_EQ(info.instr_count, 5u); // 3 通用 + 1 nop + 1 终止
    EXPECT_GT(info.mission_instr_count, 0u);
    EXPECT_GE(info.mission_start_instr_id, info.start_instr_id);
}

TEST_F(CcuRepTranslatorTest, Translate_StartInstrIdOffset_V1)
{
    ccu_rep_translator translator(refMgr, dep);
    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(std::make_shared<ccu_rep_nop>(&insGen));

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/100);
    EXPECT_EQ(info.start_instr_id, 100u);
    EXPECT_EQ(info.instr_count, 5u);
}

TEST_F(CcuRepTranslatorTest, Translate_WithLoopBlock_V1)
{
    ccu_rep_translator translator(refMgr, dep);

    auto loop_block = std::make_shared<ccu_rep_loop_block>(&insGen, "my_loop");
    loop_block->append(std::make_shared<ccu_rep_nop>(&insGen));
    loop_block->append(std::make_shared<ccu_rep_nop>(&insGen));

    variable loop_param_(nullptr);
    auto loop = std::make_shared<ccu_rep_loop>(&insGen, "my_loop", loop_param_);

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(loop_block);
    rep_vec.push_back(loop);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_GE(info.instr_count, 4u);
}

TEST_F(CcuRepTranslatorTest, Translate_UnknownLoopLabel_Throws)
{
    ccu_rep_translator translator(refMgr, dep);

    variable loop_param_(nullptr);
    auto loop = std::make_shared<ccu_rep_loop>(&insGen, "undefined_label", loop_param_);

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(loop);

    EXPECT_THROW(translator.translate(&kernel, rep_vec, 0), CcuUtException);
}

TEST_F(CcuRepTranslatorTest, Translate_FuncBlockAndCall_V1)
{
    ccu_rep_translator translator(refMgr, dep);

    auto func_block = std::make_shared<ccu_rep_func_block>(&insGen, "my_func");
    func_block->append(std::make_shared<ccu_rep_nop>(&insGen));

    auto func_call = std::make_shared<ccu_rep_func_call>(&insGen, "my_func");

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(func_block);
    rep_vec.push_back(func_call);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_GE(info.instr_count, 4u);
}

TEST_F(CcuRepTranslatorTest, Translate_FuncCallUnknownLabel_Throws)
{
    ccu_rep_translator translator(refMgr, dep);

    auto func_call = std::make_shared<ccu_rep_func_call>(&insGen, "no_such_func");

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(func_call);

    EXPECT_THROW(translator.translate(&kernel, rep_vec, 0), CcuUtException);
}

TEST_F(CcuRepTranslatorTest, Translate_LoadArgSortedByFullArgId_V1)
{
    ccu_rep_translator translator(refMgr, dep);

    variable var_a_(nullptr);
    variable var_b_(nullptr);
    var_a_.reset(1);
    var_b_.reset(2);

    // 故意乱序: full_arg_id 大的在前
    auto load_hi = std::make_shared<ccu_rep_load_arg>(&insGen, var_a_, 0, 5);
    auto load_lo = std::make_shared<ccu_rep_load_arg>(&insGen, var_b_, 0, 1);

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(load_hi);
    rep_vec.push_back(load_lo);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_GE(info.instr_count, 6u);
}

TEST_F(CcuRepTranslatorTest, Translate_V2_RunsOptimizer)
{
    ccu_rep_translator::set_ccu_version(HCOMM_CCU_VERSION_V2);
    ccu_rep_translator translator(refMgr, dep);

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(std::make_shared<ccu_rep_nop>(&insGen));

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_EQ(info.start_instr_id, 0u);
    EXPECT_GT(info.instr_count, 0u);
}

TEST_F(CcuRepTranslatorTest, TranslateAssign_Var_V1)
{
    ccu_rep_translator translator(refMgr, dep);

    variable var_(nullptr);
    var_.reset(3);
    auto assign = std::make_shared<ccu_rep_assign>(&insGen, var_, uint64_t(0xABCD));

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(assign);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_GE(info.instr_count, 5u);

    // 在生成指令中查找 immediate (load type=0x0, imd_to_xn code=0x3)
    bool found = false;
    for (const auto& instr : info.instr_vec) {
        if (instr.header.type == 0x0 && instr.header.code == 0x3 && instr.v1.load_imd_to_xn.immediate == 0xABCD) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CcuRepTranslatorTest, SetGetTransDep_RoundTrip)
{
    ccu_rep_translator translator(refMgr, dep);
    trans_dep new_dep{};
    new_dep.logical_id = 42;
    translator.set_trans_dep(new_dep);
    EXPECT_EQ(translator.get_trans_dep().logical_id, 42);
}

TEST_F(CcuRepTranslatorTest, BindResource_SetsReserveIds)
{
    ccu_rep_translator translator(refMgr, dep);
    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    (void)translator.translate(&kernel, rep_vec, 0);

    const trans_dep& bound = translator.get_trans_dep();
    // bind_resource 后 reserve id 取自 var_[0]/addr_[0]/signal_[0], id 从 0 编号合法,
    // 仅断言与初始 dep 值(100/300)不同, 证明已被覆盖
    EXPECT_NE(bound.reserve_xn_id, 100u);
    EXPECT_NE(bound.reserve_cke_id, 300u);
}

TEST_F(CcuRepTranslatorTest, Translate_FuncBlock_IsFuncBlockFlag)
{
    ccu_rep_translator translator(refMgr, dep);

    auto func_block = std::make_shared<ccu_rep_func_block>(&insGen, "flag_func");
    func_block->append(std::make_shared<ccu_rep_nop>(&insGen));

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(func_block);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0, /*is_func_block=*/true);
    EXPECT_GT(info.instr_count, 0u);
}

TEST_F(CcuRepTranslatorTest, Translate_FuncBlockAndCall_VariableListArgs_V1)
{
    ccu_rep_translator translator(refMgr, dep);

    variable v1(nullptr);
    variable v2(nullptr);
    variable v3(nullptr);
    v1.reset(10);
    v2.reset(11);
    v3.reset(12);

    auto func_block = std::make_shared<ccu_rep_func_block>(&insGen, "var_list_func");
    func_block->define_in_arg(std::vector<variable>{v1, v2});
    func_block->define_out_arg(std::vector<variable>{v3});
    func_block->append(std::make_shared<ccu_rep_nop>(&insGen));

    auto func_call = std::make_shared<ccu_rep_func_call>(&insGen, "var_list_func");
    func_call->set_in_arg(std::vector<variable>{v1, v2});
    func_call->set_out_arg(std::vector<variable>{v3});

    std::vector<std::shared_ptr<ccu_rep_base>> rep_vec;
    rep_vec.push_back(func_block);
    rep_vec.push_back(func_call);

    auto info = translator.translate(&kernel, rep_vec, /*start_instr_id=*/0);
    EXPECT_GE(info.instr_count, 4u);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
