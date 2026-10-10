/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode_opt/microcode_optimizer.h"
#include "hcomm/resource/microcode_opt/config/barrier_config.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include <gtest/gtest.h>

namespace asc {
namespace ccu_opt {
namespace {

using ccu_rep::ccu_instr;
namespace code = instr_code_v2;

ccu_instr make_set_cke(uint16_t wait_cke_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::setckbit_code);
    instr.v2.set_cke.wait_cke_id = wait_cke_id;
    instr.v2.set_cke.clear_type = 1; // auto-clear: 同时是 cke 写者
    return instr;
}

ccu_instr make_clear_cke(uint16_t wait_cke_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::clearckbit_code);
    instr.v2.clear_cke.wait_cke_id = wait_cke_id;
    return instr;
}

ccu_instr make_load_imd(uint16_t xn_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::load_type, code::loadimdtox_code);
    instr.v2.load_imd_to_x.xn_id = xn_id;
    return instr;
}

TEST(MicrocodeOptimizerTest, DefaultOptions_IsCkeOnly)
{
    auto opts = microcode_optimizer::default_options();
    EXPECT_EQ(opts.level, sched_level::cke_only);
}

TEST(MicrocodeOptimizerTest, SetOptions_SyncsSchedulerLevel)
{
    microcode_optimizer opt;
    optimizer_options opts;
    opts.level = sched_level::cke_only;
    opt.set_options(opts);

    EXPECT_EQ(opt.options().level, sched_level::cke_only);
}

TEST(MicrocodeOptimizerTest, Optimize_EmptyInput)
{
    microcode_optimizer opt;
    ccu_rep::ccu_instr_info input;

    auto out = opt.optimize(input);
    EXPECT_TRUE(out.instr_vec.empty());
    EXPECT_EQ(opt.stats().sched.basic_blocks, 0u);
}

TEST(MicrocodeOptimizerTest, Optimize_PassthroughNoDependency)
{
    microcode_optimizer opt;
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 7;
    input.instr_vec = {make_load_imd(1), make_load_imd(2)};
    input.instr_count = 2;

    auto out = opt.optimize(input);
    EXPECT_EQ(out.instr_vec.size(), 2u);
    EXPECT_EQ(out.start_instr_id, 7u);
    EXPECT_EQ(opt.stats().sched.nop_inserted, 0u);
    EXPECT_EQ(opt.stats().sched.nop_removed, 0u);
    EXPECT_EQ(opt.stats().sched.instr_reordered, 0u);
    EXPECT_EQ(opt.stats().sched.basic_blocks, 1u);
}

TEST(MicrocodeOptimizerTest, Optimize_InsertsCkeLatencyNops)
{
    microcode_optimizer opt;
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(5), make_clear_cke(5)};
    input.instr_count = 2;

    auto out = opt.optimize(input);
    EXPECT_EQ(out.instr_vec.size(), static_cast<size_t>(ccu_rep::ccu_cke_raw_latency) + 1);
    EXPECT_EQ(opt.stats().sched.nop_inserted, static_cast<uint32_t>(ccu_rep::ccu_cke_raw_latency - 1));
    // origin 映射: [0, -1 x (L-1), 1]
    const auto& origin = opt.stats().sched.origin_index;
    ASSERT_EQ(origin.size(), out.instr_vec.size());
    EXPECT_EQ(origin.front(), 0);
    EXPECT_EQ(origin.back(), 1);
}

TEST(MicrocodeOptimizerTest, Run_StaticEntry_UsesDefaultOptions)
{
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_load_imd(1)};
    input.instr_count = 1;

    auto out = microcode_optimizer::run(input, /*reserve_xn_id=*/6, /*reserve_cke_id=*/7);
    EXPECT_EQ(out.instr_vec.size(), 1u);
    EXPECT_EQ(out.instr_count, 1u);
}

TEST(MicrocodeOptimizerTest, Run_EmptyInput)
{
    ccu_rep::ccu_instr_info input;

    auto out = microcode_optimizer::run(input, 0, 0);
    EXPECT_TRUE(out.instr_vec.empty());
}

TEST(MicrocodeOptimizerTest, Stats_TimingFieldsPopulated)
{
    microcode_optimizer opt;
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_load_imd(1)};
    input.instr_count = 1;

    (void)opt.optimize(input);
    // 耗时字段被填充 (可能为 0, 但 pass1 <= total)
    EXPECT_LE(opt.stats().pass1_duration_us, opt.stats().total_duration_us);
}

} // namespace
} // namespace ccu_opt
} // namespace asc
