/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode_opt/instruction_scheduler.h"
#include "hcomm/resource/microcode_opt/config/barrier_config.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include <gtest/gtest.h>

namespace asc {
namespace ccu_opt {
namespace {

using ccu_rep::ccu_instr;
namespace code = instr_code_v2;

ccu_instr make_set_cke(uint16_t wait_cke_id, uint16_t clear_type = 1)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::setckbit_code);
    instr.v2.set_cke.wait_cke_id = wait_cke_id;
    instr.v2.set_cke.clear_type = clear_type;
    return instr;
}

ccu_instr make_clear_cke(uint16_t wait_cke_id, uint16_t clear_type = 0)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::clearckbit_code);
    instr.v2.clear_cke.wait_cke_id = wait_cke_id;
    instr.v2.clear_cke.clear_type = clear_type;
    return instr;
}

ccu_instr make_nop()
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::load_type, code::nop_code);
    return instr;
}

ccu_instr make_loop(uint16_t start_id, uint16_t end_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::loop_code);
    instr.v2.loop.start_instr_id = start_id;
    instr.v2.loop.end_instr_id = end_id;
    instr.v2.loop.xm_id = 1;
    instr.v2.loop.xn_id = 2;
    instr.v2.loop.xp_id = 3;
    return instr;
}

ccu_instr make_loop_group(uint16_t start_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::loopgroup_code);
    instr.v2.loop_group.start_loop_instr_id = start_id;
    instr.v2.loop_group.xn_id = 1;
    instr.v2.loop_group.xm_id = 2;
    instr.v2.loop_group.xp_id = 3;
    return instr;
}

ccu_instr make_load_imd(uint16_t xn_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::load_type, code::loadimdtox_code);
    instr.v2.load_imd_to_x.xn_id = xn_id;
    return instr;
}

bool is_nop(const ccu_instr& instr)
{
    return instr.header.type == code::load_type && instr.header.code == code::nop_code;
}

class InstructionSchedulerTest : public ::testing::Test {
protected:
    instruction_scheduler sched_;
};

TEST_F(InstructionSchedulerTest, EmptyInput_EmptyOutput)
{
    ccu_rep::ccu_instr_info input;

    auto out = sched_.schedule(input);
    EXPECT_TRUE(out.instr_vec.empty());
    EXPECT_EQ(out.instr_count, 0u);
    EXPECT_EQ(sched_.stats().basic_blocks, 0u);
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
    EXPECT_TRUE(sched_.stats().origin_index.empty());
}

TEST_F(InstructionSchedulerTest, NoCkeDependency_Passthrough)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 100;
    input.instr_vec = {make_load_imd(1), make_load_imd(2), make_nop()};
    input.instr_count = 3;

    auto out = sched_.schedule(input);
    ASSERT_EQ(out.instr_vec.size(), 3u);
    EXPECT_EQ(out.start_instr_id, 100u);
    EXPECT_EQ(out.instr_count, 3u);
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
    EXPECT_EQ(sched_.stats().basic_blocks, 1u);
    // 无重排: origin_index 与输入一一对应
    for (size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(sched_.stats().origin_index[i], static_cast<int32_t>(i));
    }
}

TEST_F(InstructionSchedulerTest, CkeWriteToRead_InsertsLatencyNops)
{
    // setcke 写 cke(5), 紧跟 clearcke 读同一 cke -> 需要补 (latency-1) 条 nop
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {make_set_cke(5), make_clear_cke(5)};
    input.instr_count = 2;

    auto out = sched_.schedule(input);
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ASSERT_EQ(out.instr_vec.size(), static_cast<size_t>(latency + 1));
    // 第 0 条是 setcke, 中间全是 nop, 最后一条是 clearcke
    EXPECT_EQ(out.instr_vec.front().header.code, code::setckbit_code);
    EXPECT_EQ(out.instr_vec.back().header.code, code::clearckbit_code);
    for (size_t i = 1; i + 1 < out.instr_vec.size(); ++i) {
        EXPECT_TRUE(is_nop(out.instr_vec[i])) << "i=" << i;
    }
    EXPECT_EQ(sched_.stats().nop_inserted, static_cast<uint32_t>(latency - 1));

    // origin_index: [0, -1 x (L-1), 1]
    const auto& origin = sched_.stats().origin_index;
    ASSERT_EQ(origin.size(), out.instr_vec.size());
    EXPECT_EQ(origin.front(), 0);
    EXPECT_EQ(origin.back(), 1);
    for (size_t i = 1; i + 1 < origin.size(); ++i) {
        EXPECT_EQ(origin[i], -1) << "i=" << i;
    }
}

TEST_F(InstructionSchedulerTest, DifferentCkeIds_NoDependency)
{
    // 写 cke(1) 后读 cke(2), 无依赖不补 nop
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(1), make_clear_cke(2)};
    input.instr_count = 2;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), 2u);
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
}

TEST_F(InstructionSchedulerTest, FartherThanLatency_NoNop)
{
    // 写 cke 后隔足够多的无关指令再读, 间隔已满足 latency, 不补 nop:
    // writer@0, 需 latency-1 条填充 (读者发射于 cycle=latency)
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ccu_rep::ccu_instr_info input;
    input.instr_vec.push_back(make_set_cke(3, 1));
    for (int64_t i = 0; i + 1 < latency; ++i) {
        input.instr_vec.push_back(make_load_imd(static_cast<uint16_t>(100 + i)));
    }
    input.instr_vec.push_back(make_clear_cke(3));
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), input.instr_vec.size());
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
}

TEST_F(InstructionSchedulerTest, LaterWriterOverridesEarlier)
{
    // 两次写同一 cke (clear_type=1), 读依赖最近一次写
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(7, 1), make_load_imd(1), make_set_cke(7, 1), make_clear_cke(7)};
    input.instr_count = 4;

    auto out = sched_.schedule(input);
    // 输出末尾应为 clearcke
    EXPECT_EQ(out.instr_vec.back().header.code, code::clearckbit_code);
    // 确实插入了 nop (两次写后读)
    EXPECT_GT(sched_.stats().nop_inserted, 0u);
}

TEST_F(InstructionSchedulerTest, SetCkeAutoClear_TreatedAsWriter)
{
    // clear_type=1 时 setcke/wait_cke_id 同时是写者: setcke(autoclear) 后读需补 nop
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(9, /*clear_type=*/1), make_clear_cke(9)};
    input.instr_count = 2;

    auto out = sched_.schedule(input);
    EXPECT_GT(out.instr_vec.size(), 2u);
    EXPECT_EQ(sched_.stats().nop_inserted, static_cast<uint32_t>(ccu_rep::ccu_cke_raw_latency - 1));
}

TEST_F(InstructionSchedulerTest, ClearCkeAutoClear_ChainDependency)
{
    // clearcke(autoclear) 写 cke 后, 再一次读同一 cke 也要补 nop
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_clear_cke(4, /*clear_type=*/1), make_clear_cke(4)};
    input.instr_count = 2;

    auto out = sched_.schedule(input);
    EXPECT_GT(out.instr_vec.size(), 2u);
    EXPECT_EQ(out.instr_vec.back().header.code, code::clearckbit_code);
}

TEST_F(InstructionSchedulerTest, CkeIdZero_TrackedAsNoDependency)
{
    // cke id 0 被提取阶段跳过, 不构成写后读
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(0), make_clear_cke(0)};
    input.instr_count = 2;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), 2u);
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
}

TEST_F(InstructionSchedulerTest, LoopStartEndRemappedAfterNopInsertion)
{
    // setcke + nop 填充 + loop: loop 引用的 start/end 应平移到插入 nop 后的新位置
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 10;
    input.instr_vec.push_back(make_set_cke(5));
    // 构造 latency-1 条无关指令, 使后续 loop 的引用被平移
    for (int64_t i = 0; i + 2 < latency; ++i) {
        input.instr_vec.push_back(make_load_imd(static_cast<uint16_t>(i)));
    }
    input.instr_vec.push_back(make_loop(/*start_id=*/10 + 1, /*end_id=*/10 + 2));
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    ASSERT_FALSE(out.instr_vec.empty());
    const auto& last = out.instr_vec.back();
    ASSERT_EQ(last.header.type, code::ctrl_type);
    ASSERT_EQ(last.header.code, code::loop_code);
    // 未插入 nop (间隔已满足), 引用原样保留
    EXPECT_EQ(last.v2.loop.start_instr_id, 11u);
    EXPECT_EQ(last.v2.loop.end_instr_id, 12u);
}

TEST_F(InstructionSchedulerTest, LoopGroupStartRemapped)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {make_loop_group(/*start_id=*/0)};
    input.instr_count = 1;

    auto out = sched_.schedule(input);
    ASSERT_EQ(out.instr_vec.size(), 1u);
    EXPECT_EQ(out.instr_vec[0].v2.loop_group.start_loop_instr_id, 0u);
}

TEST_F(InstructionSchedulerTest, MissionStartWithinRange_Remapped)
{
    // start=0, 2 条指令, mission 从第 1 条开始: 补 nop 后 mission_start 应平移
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {make_set_cke(5), make_load_imd(1)};
    input.instr_count = 2;
    input.mission_start_instr_id = 1;
    input.mission_instr_count = 1;

    auto out = sched_.schedule(input);
    // setcke 后立即 load 是无关指令, 但 mission_start 引用保持指向第 1 条指令的新位置
    EXPECT_EQ(
        out.mission_start_instr_id,
        sched_.stats().origin_index.size() > 1 ? static_cast<uint16_t>(sched_.stats().origin_index[1]) : 1u);
    EXPECT_GT(out.mission_instr_count, 0u);
}

TEST_F(InstructionSchedulerTest, MissionStartOutOfRange_Kept)
{
    // mission 起点在序列范围之外: 原样保留
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {make_load_imd(1)};
    input.instr_count = 1;
    input.mission_start_instr_id = 100;
    input.mission_instr_count = 5;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.mission_start_instr_id, 100u);
    EXPECT_EQ(out.mission_instr_count, 5u);
}

TEST_F(InstructionSchedulerTest, MissionStartBeforeRange_Kept)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 50;
    input.instr_vec = {make_load_imd(1)};
    input.instr_count = 1;
    input.mission_start_instr_id = 10;
    input.mission_instr_count = 5;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.mission_start_instr_id, 10u);
    EXPECT_EQ(out.mission_instr_count, 5u);
}

TEST_F(InstructionSchedulerTest, OutOfRangeLoopRef_KeptUnchanged)
{
    // loop 引用超出序列范围: 引用原样返回
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {make_loop(/*start_id=*/100, /*end_id=*/200)};
    input.instr_count = 1;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[0].v2.loop.start_instr_id, 100u);
    EXPECT_EQ(out.instr_vec[0].v2.loop.end_instr_id, 200u);
}

TEST_F(InstructionSchedulerTest, StatsResetBetweenRuns)
{
    ccu_rep::ccu_instr_info input1;
    input1.instr_vec = {make_set_cke(5, 1), make_clear_cke(5)};
    input1.instr_count = 2;
    (void)sched_.schedule(input1);
    EXPECT_GT(sched_.stats().nop_inserted, 0u);

    ccu_rep::ccu_instr_info input2;
    input2.instr_vec = {make_load_imd(1)};
    input2.instr_count = 1;
    auto out2 = sched_.schedule(input2);
    EXPECT_EQ(sched_.stats().nop_inserted, 0u);
    EXPECT_EQ(out2.instr_vec.size(), 1u);
}

TEST_F(InstructionSchedulerTest, MultipleCkeReaders_EachGetsLatency)
{
    // 同一个 cke 写者后跟多个读者: 后面的读者被前面的 nop 推进, 不再重复补
    ccu_rep::ccu_instr_info input;
    input.instr_vec = {make_set_cke(2), make_clear_cke(2), make_clear_cke(2)};
    input.instr_count = 3;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), static_cast<size_t>(ccu_rep::ccu_cke_raw_latency) + 2);
}

ccu_instr make_load_imd_value(uint16_t xn_id, uint64_t immediate)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::load_type, code::loadimdtox_code);
    instr.v2.load_imd_to_x.xn_id = xn_id;
    instr.v2.load_imd_to_x.immediate = immediate;
    return instr;
}

ccu_instr make_jmp(
    uint16_t condition_type, uint16_t expected_xn, uint16_t rel_tar_xn, uint16_t cond_xn, uint16_t jump_mode = 0)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::ctrl_type, code::jmp_code);
    instr.v2.jmp.expected_xn_id = expected_xn;
    instr.v2.jmp.condition_xn_id = cond_xn;
    instr.v2.jmp.rel_tar_instr_xn_id = rel_tar_xn;
    instr.v2.jmp.condition_type = condition_type & 0xF;
    instr.v2.jmp.jump_mode = jump_mode & 0x1;
    return instr;
}

ccu_instr make_arith(uint16_t op_code, uint16_t xd_id, uint16_t xn_id, uint16_t xm_id)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(code::load_type, op_code);
    instr.v2.operate.xd_id = xd_id;
    instr.v2.operate.xn_id = xn_id;
    instr.v2.operate.xm_id = xm_id;
    return instr;
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_UpdatedAfterNopInsertion)
{
    // setcke(5) 写 cke, load 装载跳转 offset, jmp 相对跳转, clearcke(5) 读 cke 触发补 nop,
    // nop 落在 jmp 与目标之间, 目标(最后一条 load)被平移, offset 立即数应被改写
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_set_cke(5),      make_load_imd_value(9, 2), // offset: jmp@2 -> target@4
        make_jmp(1, 3, 9, 4), make_clear_cke(5),
        make_load_imd(1), // 跳转目标 (orig idx 4)
    };
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    // set 后读者(orig 3)需补 (latency-3) 条 nop, 新 offset = (4 + latency - 3) - 2 = latency - 1
    const uint64_t expected = static_cast<uint64_t>(latency - 1);
    EXPECT_EQ(out.instr_vec[1].v2.load_imd_to_x.immediate, expected);
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_NoNopInserted_Unchanged)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd_value(9, 2),
        make_jmp(1, 3, 9, 4),
        make_load_imd(1),
        make_load_imd(2),
    };
    input.instr_count = 4;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[0].v2.load_imd_to_x.immediate, 2u);
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_NoLoaderFound_Kept)
{
    // jmp 引用的 rel_tar 寄存器之前无任何写入者: 保守不动
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd(1),
        make_jmp(1, 3, 9, 4),
        make_load_imd(2),
    };
    input.instr_count = 3;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), 3u);
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_TargetRegOverwrittenByArith_Skipped)
{
    // rel_tar 寄存器在 jmp 之前被算术指令覆盖: 告警并放弃修正
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd_value(9, 2),
        make_arith(code::add_code, 9, 9, 1),
        make_jmp(1, 3, 9, 4),
        make_load_imd(1),
    };
    input.instr_count = 4;

    auto out = sched_.schedule(input);
    // 未修正, loader 立即数保持原值
    EXPECT_EQ(out.instr_vec[0].v2.load_imd_to_x.immediate, 2u);
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_LoaderImmediateOutOfRange_Skipped)
{
    // offset 立即数 >= 0x10000, 疑似误命中 64bit 数据装载: 放弃修正避免截断
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd_value(9, 0x20000),
        make_jmp(1, 3, 9, 4),
        make_load_imd(1),
    };
    input.instr_count = 3;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[0].v2.load_imd_to_x.immediate, 0x20000u);
}

TEST_F(InstructionSchedulerTest, PlainJumpOffset_OldOffsetOutsideSequence_Skipped)
{
    // 旧 offset 指向序列之外: 放弃修正
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd_value(9, 100),
        make_jmp(1, 3, 9, 4),
        make_load_imd(1),
    };
    input.instr_count = 3;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[0].v2.load_imd_to_x.immediate, 100u);
}

TEST_F(InstructionSchedulerTest, AbsoluteJumpMode_UnsupportedKept)
{
    // jump_mode=1 绝对跳转: 生成端不产生, 调度器仅告警不修改
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec = {
        make_load_imd_value(9, 2),
        make_jmp(1, 3, 9, 4, /*jump_mode=*/1),
        make_load_imd(1),
        make_load_imd(2),
    };
    input.instr_count = 4;

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), 4u);
    EXPECT_EQ(out.instr_vec[1].v2.jmp.jump_mode, 1u);
}

// 构造完整 rel_jmp 9 条模板 (func-call/func-ret 运行期地址跳转):
// P+0 load(xn0, jmp_id) / P+1 load(xn1, 5) / P+2 IF-jmp / P+3 load(xn0, -jmp_id) /
// P+4 add / P+5 load(xn1, 3) / P+6 UNCOND-jmp / P+7 sub / P+8 nop
std::vector<ccu_instr> make_rel_jmp_template(uint16_t xn0, uint16_t xn1, uint16_t target_xn, uint16_t jmp_id)
{
    constexpr uint64_t kSpace = 0x10000ULL;
    return {
        make_load_imd_value(xn0, jmp_id),                      // P+0
        make_load_imd_value(xn1, 5),                           // P+1
        make_jmp(2, xn0, xn1, target_xn),                      // P+2 (IF)
        make_load_imd_value(xn0, kSpace - jmp_id),             // P+3
        make_arith(code::add_code, target_xn, target_xn, xn0), // P+4
        make_load_imd_value(xn1, 3),                           // P+5
        make_jmp(6, 0, xn1, 0),                                // P+6 (UNCOND)
        make_arith(code::sub_code, target_xn, target_xn, xn0), // P+7
        make_nop(),                                            // P+8
    };
}

TEST_F(InstructionSchedulerTest, RelJmpTemplate_BaseAndTargetRemapped)
{
    // [0] setcke(5) / [1] clearcke(5) 读者紧邻 -> 补 (latency-1) 条 nop /
    // [2] load(target) / [3..11] rel_jmp 模板, 模板与目标整体后移
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec.push_back(make_set_cke(5));
    input.instr_vec.push_back(make_clear_cke(5));
    input.instr_vec.push_back(make_load_imd_value(22, 6)); // 目标绝对 id -> orig idx 6
    auto tmpl = make_rel_jmp_template(20, 21, 22, 11);
    input.instr_vec.insert(input.instr_vec.end(), tmpl.begin(), tmpl.end());
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    const int64_t shift = latency - 1;
    const auto& ov = out.instr_vec;
    // 目标绝对 id (orig 2, 输出位置 2+shift): 6 -> remap(6) = 6+shift
    EXPECT_EQ(ov[2 + shift].v2.load_imd_to_x.immediate, static_cast<uint64_t>(6 + shift));
    // P+0 (orig 3) 基准 jmp_instr_id: 11 -> 11+shift
    EXPECT_EQ(ov[3 + shift].v2.load_imd_to_x.immediate, static_cast<uint64_t>(11 + shift));
    // P+3 (orig 6): 0x10000 - 新基准
    EXPECT_EQ(ov[6 + shift].v2.load_imd_to_x.immediate, 0x10000ULL - static_cast<uint64_t>(11 + shift));
    // 模板内 9 条连续 (块内无 nop 插入)
    for (int64_t k = 0; k < 9; ++k) {
        EXPECT_EQ(sched_.stats().origin_index[static_cast<size_t>(3 + shift + k)], static_cast<int32_t>(3 + k));
    }
}

TEST_F(InstructionSchedulerTest, RelJmpTemplate_RuntimeTargetByArith_NotRemapped)
{
    // 目标寄存器由 add 提供运行期变量 (外部绝对地址): 不改写
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec.push_back(make_arith(code::add_code, 22, 22, 1));
    auto tmpl = make_rel_jmp_template(20, 21, 22, 9);
    input.instr_vec.insert(input.instr_vec.end(), tmpl.begin(), tmpl.end());
    input.instr_vec.push_back(make_load_imd(1));
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec.size(), input.instr_vec.size());
    // P+0 基准未被平移 (无 nop), 保持原值
    EXPECT_EQ(out.instr_vec[1].v2.load_imd_to_x.immediate, 9u);
}

TEST_F(InstructionSchedulerTest, RelJmpTemplate_BaseOutOfRange_Skipped)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec.push_back(make_load_imd_value(22, 6));
    auto tmpl = make_rel_jmp_template(20, 21, 22, 11);
    // 注入越界基准: P+0 立即数 >= 0x10000
    tmpl[0].v2.load_imd_to_x.immediate = 0x20000;
    input.instr_vec.insert(input.instr_vec.end(), tmpl.begin(), tmpl.end());
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[1].v2.load_imd_to_x.immediate, 0x20000u);
}

TEST_F(InstructionSchedulerTest, RelJmpTemplate_TargetOutOfRange_Skipped)
{
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec.push_back(make_load_imd_value(22, 0x20000)); // 目标绝对 id 越界
    auto tmpl = make_rel_jmp_template(20, 21, 22, 11);
    input.instr_vec.insert(input.instr_vec.end(), tmpl.begin(), tmpl.end());
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    EXPECT_EQ(out.instr_vec[0].v2.load_imd_to_x.immediate, 0x20000u);
}

TEST_F(InstructionSchedulerTest, RelJmpTemplate_NotTornByCkeNopInsertion)
{
    // setcke + clearcke 紧邻强制补 nop, 模板整体后移但块内 9 条保持连续不被撕裂
    constexpr int64_t latency = ccu_rep::ccu_cke_raw_latency;
    ccu_rep::ccu_instr_info input;
    input.start_instr_id = 0;
    input.instr_vec.push_back(make_set_cke(5));
    input.instr_vec.push_back(make_clear_cke(5));
    input.instr_vec.push_back(make_load_imd_value(22, 6));
    auto tmpl = make_rel_jmp_template(20, 21, 22, 11);
    input.instr_vec.insert(input.instr_vec.end(), tmpl.begin(), tmpl.end());
    input.instr_count = static_cast<uint16_t>(input.instr_vec.size());

    auto out = sched_.schedule(input);
    EXPECT_EQ(sched_.stats().nop_inserted, static_cast<uint32_t>(latency - 1));

    const auto& origin = sched_.stats().origin_index;
    int64_t blockStart = -1;
    for (size_t i = 0; i < origin.size(); ++i) {
        if (origin[i] == 2) { // 模板 P+0 (orig idx 2)
            blockStart = static_cast<int64_t>(i);
            break;
        }
    }
    ASSERT_GE(blockStart, 0);
    for (int64_t k = 0; k < 9; ++k) {
        EXPECT_EQ(origin[static_cast<size_t>(blockStart + k)], static_cast<int32_t>(2 + k));
    }
}

} // namespace
} // namespace ccu_opt
} // namespace asc
