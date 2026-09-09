/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/translator/ccu_rep_reference_manager_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_funcblock_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopblock_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include <gtest/gtest.h>

using CcuUtException = ::AscendC::ccu::detail::ccu_exception;

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepReferenceManagerTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    ccu_rep_reference_manager refMgr{0};
};

TEST_F(CcuRepReferenceManagerTest, SetGetRefBlock_RoundTrip)
{
    auto block = std::make_shared<ccu_rep_loop_block>(&insGen, "label_a");
    refMgr.set_ref_block("label_a", block);

    auto got = refMgr.get_ref_block("label_a");
    EXPECT_EQ(got.get(), block.get());
}

TEST_F(CcuRepReferenceManagerTest, GetRefBlock_UnknownLabel_Throws)
{
    EXPECT_THROW(refMgr.get_ref_block("no_such_label"), CcuUtException);
}

TEST_F(CcuRepReferenceManagerTest, SetRefBlock_DuplicateLabel_Throws)
{
    auto block1 = std::make_shared<ccu_rep_loop_block>(&insGen, "dup");
    auto block2 = std::make_shared<ccu_rep_loop_block>(&insGen, "dup");
    refMgr.set_ref_block("dup", block1);

    EXPECT_THROW(refMgr.set_ref_block("dup", block2), CcuUtException);
}

TEST_F(CcuRepReferenceManagerTest, ClearRepReference_AllowsRedefine)
{
    auto block1 = std::make_shared<ccu_rep_loop_block>(&insGen, "re");
    refMgr.set_ref_block("re", block1);
    refMgr.clear_rep_reference();

    auto block2 = std::make_shared<ccu_rep_loop_block>(&insGen, "re");
    EXPECT_NO_THROW(refMgr.set_ref_block("re", block2));
    EXPECT_EQ(refMgr.get_ref_block("re").get(), block2.get());
}

TEST_F(CcuRepReferenceManagerTest, GetFuncAddr_NotDefined_Throws)
{
    EXPECT_THROW(refMgr.get_func_addr("missing"), CcuUtException);
}

TEST_F(CcuRepReferenceManagerTest, GetFuncAddr_WrongType_Throws)
{
    auto loop_block = std::make_shared<ccu_rep_loop_block>(&insGen, "lb");
    refMgr.set_ref_block("lb", loop_block);

    EXPECT_THROW(refMgr.get_func_addr("lb"), CcuUtException);
}

TEST_F(CcuRepReferenceManagerTest, GetFuncAddr_FuncBlock_NoThrow)
{
    auto func_block = std::make_shared<ccu_rep_func_block>(&insGen, "fn");
    func_block->append(std::make_shared<ccu_rep_nop>(&insGen));
    refMgr.set_ref_block("fn", func_block);
    EXPECT_NO_THROW(refMgr.get_func_addr("fn"));
}

TEST_F(CcuRepReferenceManagerTest, GetFuncRet_CallLayerWithinNest)
{
    EXPECT_NO_THROW(refMgr.get_func_ret(func_nest_max));
    EXPECT_THROW(refMgr.get_func_ret(func_nest_max + 1), CcuUtException);
}

TEST_F(CcuRepReferenceManagerTest, GetFuncVectors_Sized)
{
    EXPECT_EQ(refMgr.get_func_in().size(), func_arg_max);
    EXPECT_EQ(refMgr.get_func_out().size(), func_arg_max);
}

TEST_F(CcuRepReferenceManagerTest, GetRes_AppendsVars)
{
    ccu_rep_resource res;
    size_t before = res.variable[0].size();
    refMgr.get_res(res);
    // func_in(func_arg_max) + func_out(func_arg_max) + func_call(1+nest+1)
    EXPECT_EQ(res.variable[0].size() - before, static_cast<size_t>(func_arg_max * 2 + 1 + func_nest_max + 1));
}

TEST_F(CcuRepReferenceManagerTest, Dump_NoCrash)
{
    auto block = std::make_shared<ccu_rep_loop_block>(&insGen, "dump_me");
    refMgr.set_ref_block("dump_me", block);
    EXPECT_NO_THROW(refMgr.dump());
}

} // namespace
} // namespace ccu_rep
} // namespace asc
