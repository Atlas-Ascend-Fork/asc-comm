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
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"
#include "hcomm/resource/representation/reps/control/ccu_rep_jump_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_add_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_load_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_load_var_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_store_var_v1.h"

using namespace asc::ccu_rep;

TEST(CcuPrintA5Translate, TranslatesPrintfImmediateBranches)
{
    ccu_ins_generater_v1 generator;
    ccu_rep_context context;
    variable target(&context), expected(&context), condition(&context);
    auto label = std::make_shared<ccu_rep_jump_label>(&generator, "printf.branch.end");
    ccu_instr instructions[64]{};
    ccu_instr* cursor = instructions;
    uint16_t id = 0;
    trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    ASSERT_TRUE(label->translate(nullptr, cursor, id, dependency));

    // wrap compares against ringBytes; full/space compare the sum against zero.
    for (const uint64_t immediate : {uint64_t{1280}, uint64_t{0}}) {
        ccu_rep_jump_ne notEqual(&generator, "printf.branch.end", target, expected, condition, immediate);
        notEqual.reference(label);
        EXPECT_TRUE(notEqual.translate(nullptr, cursor, id, dependency));
        ccu_rep_jump_eq equal(&generator, "printf.branch.end", target, expected, condition, immediate);
        equal.reference(label);
        EXPECT_TRUE(equal.translate(nullptr, cursor, id, dependency));
    }
    // Every CCU version uses addition and immediate comparisons.
    ccu_rep_add add(&generator, condition, condition, expected);
    EXPECT_TRUE(add.translate(nullptr, cursor, id, dependency));
    EXPECT_GT(id, 0U);
}

// Exercise the other instruction forms emitted by printf with the real V1
// translator. This checks translation support, not device execution or tokens.
TEST(CcuPrintA5Translate, TranslatesPrintfMemoryAndArithmeticOperations)
{
    ccu_ins_generater_v1 generator;
    ccu_rep_context context;
    variable address(&context), value(&context), increment(&context);
    ccu_instr instructions[128]{};
    ccu_instr* cursor = instructions;
    uint16_t id = 0;
    trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    ccu_rep_assign immediate(&generator, increment, uint64_t{8});
    EXPECT_TRUE(immediate.translate(nullptr, cursor, id, dependency));
    ccu_rep_assign copy(&generator, value, increment);
    EXPECT_TRUE(copy.translate(nullptr, cursor, id, dependency));
    ccu_rep_add add(&generator, value, value, increment);
    EXPECT_TRUE(add.translate(nullptr, cursor, id, dependency));
    ccu_rep_add addAssign(&generator, value, increment);
    EXPECT_TRUE(addAssign.translate(nullptr, cursor, id, dependency));
    ccu_rep_load loadAddressCell(&generator, uint64_t{0x1000}, address);
    EXPECT_TRUE(loadAddressCell.translate(nullptr, cursor, id, dependency));
    ccu_rep_load_var loadHeader(&generator, address, value);
    EXPECT_TRUE(loadHeader.translate(nullptr, cursor, id, dependency));
    ccu_rep_store_var storeRecord(&generator, value, address);
    EXPECT_TRUE(storeRecord.translate(nullptr, cursor, id, dependency));
    EXPECT_GT(id, 0U);
}
