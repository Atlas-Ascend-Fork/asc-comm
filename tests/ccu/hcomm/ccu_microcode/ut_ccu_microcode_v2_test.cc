/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include <gtest/gtest.h>

namespace hcomm {
namespace ccu_rep {
namespace ccu_v2 {
namespace {

class CcuMicrocodeV2Test : public ::testing::Test {
protected:
    ccu_instr instr_{};
    void SetUp() override { memset(&instr_, 0, sizeof(instr_)); }
};

TEST_F(CcuMicrocodeV2Test, load_sqe_args_to_x)
{
    uint16_t xn_id = 3;
    uint16_t sqe_args_id = 7;
    uint16_t set_cke_id = 1;
    uint16_t set_cke_mask = 0xF;
    load_sqe_args_to_x(&instr_, xn_id, sqe_args_id, set_cke_id, set_cke_mask);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v2.load_sqe_args_to_x.xn_id, xn_id);
    EXPECT_EQ(instr_.v2.load_sqe_args_to_x.sqe_args_id, sqe_args_id);
    EXPECT_EQ(instr_.v2.load_sqe_args_to_x.set_cke_id, set_cke_id);
    EXPECT_EQ(instr_.v2.load_sqe_args_to_x.set_cke_mask, set_cke_mask);
}

TEST_F(CcuMicrocodeV2Test, load_imd_to_xn)
{
    uint16_t xn_id = 4;
    uint64_t immediate_ = 0xDEADBEEF;
    uint16_t set_cke_id = 2;
    uint16_t set_cke_mask = 0xA;
    load_imd_to_xn(&instr_, xn_id, immediate_, set_cke_id, set_cke_mask);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v2.load_imd_to_x.xn_id, xn_id);
    EXPECT_EQ(instr_.v2.load_imd_to_x.immediate_, immediate_);
    EXPECT_EQ(instr_.v2.load_imd_to_x.set_cke_id, set_cke_id);
    EXPECT_EQ(instr_.v2.load_imd_to_x.set_cke_mask, set_cke_mask);
}

TEST_F(CcuMicrocodeV2Test, nop)
{
    nop(&instr_);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0x9);
}

TEST_F(CcuMicrocodeV2Test, assign)
{
    uint16_t result = 5;
    uint16_t operand = 3;
    assign(&instr_, result, operand, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xD);
    EXPECT_EQ(instr_.v2.operate.par_mode, 0);
    EXPECT_EQ(instr_.v2.operate.xd_id, result);
    EXPECT_EQ(instr_.v2.operate.xn_id, operand);
    EXPECT_EQ(instr_.v2.operate.xm_id, 0);
}

TEST_F(CcuMicrocodeV2Test, add)
{
    uint16_t result = 1;
    uint16_t operand1 = 2;
    uint16_t operand2 = 3;
    add(&instr_, result, operand1, operand2, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xD);
    EXPECT_EQ(instr_.v2.operate.par_mode, 1);
    EXPECT_EQ(instr_.v2.operate.xd_id, result);
    EXPECT_EQ(instr_.v2.operate.xn_id, operand1);
    EXPECT_EQ(instr_.v2.operate.xm_id, operand2);
}

TEST_F(CcuMicrocodeV2Test, add_i)
{
    uint16_t result = 1;
    uint16_t operand = 2;
    uint16_t imm = 10;
    add_i(&instr_, result, operand, imm, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xD);
    EXPECT_EQ(instr_.v2.operate.par_mode, 0);
    EXPECT_EQ(instr_.v2.operate.xd_id, result);
    EXPECT_EQ(instr_.v2.operate.xn_id, operand);
    EXPECT_EQ(instr_.v2.operate.xm_id, imm);
}

TEST_F(CcuMicrocodeV2Test, mul)
{
    uint16_t result = 2;
    uint16_t operand1 = 3;
    uint16_t operand2 = 4;
    mul(&instr_, result, operand1, operand2, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xF);
    EXPECT_EQ(instr_.v2.operate.par_mode, 1);
    EXPECT_EQ(instr_.v2.operate.xd_id, result);
}

TEST_F(CcuMicrocodeV2Test, mul_i)
{
    uint16_t result = 2;
    uint16_t operand = 3;
    uint16_t imm = 5;
    mul_i(&instr_, result, operand, imm, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xF);
    EXPECT_EQ(instr_.v2.operate.par_mode, 0);
    EXPECT_EQ(instr_.v2.operate.xm_id, imm);
}

TEST_F(CcuMicrocodeV2Test, load_x_from_mem)
{
    cache_config config;
    config.alloc_hint = 1;
    config.victim_hint = 2;
    load_x_from_mem(&instr_, 1, 2, 3, 4, config, 5, 0xF);

    EXPECT_EQ(instr_.v2.load.dst_type, 0x0);
}

TEST_F(CcuMicrocodeV2Test, store_x_to_mem)
{
    cache_config config;
    config.alloc_hint = 2;
    config.victim_hint = 1;
    store_x_to_mem(&instr_, 1, 2, 3, 4, config, 5, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x0);
    EXPECT_EQ(instr_.header.code, 0xB);
    EXPECT_EQ(instr_.v2.store.xd_id, 1);
    EXPECT_EQ(instr_.v2.store.xdt_id, 2);
    EXPECT_EQ(instr_.v2.store.xs_id, 3);
    EXPECT_EQ(instr_.v2.store.xl_id, 4);
    EXPECT_EQ(instr_.v2.store.src_type, 0x0);
    EXPECT_EQ(instr_.v2.store.alloc_hint, config.alloc_hint & 0x3);
    EXPECT_EQ(instr_.v2.store.victim_hint, config.victim_hint & 0x3);
}

TEST_F(CcuMicrocodeV2Test, loop)
{
    uint16_t startInstrId = 10;
    uint16_t endInstrId = 20;
    uint16_t iter_num = 5;
    uint16_t offset = 100;
    uint16_t context_id = 3;
    loop(&instr_, startInstrId, endInstrId, iter_num, offset, context_id);

    EXPECT_EQ(instr_.header.type_, 0x1);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v2.loop.start_instr_id, startInstrId);
    EXPECT_EQ(instr_.v2.loop.end_instr_id, endInstrId);
    EXPECT_EQ(instr_.v2.loop.xm_id, iter_num);
    EXPECT_EQ(instr_.v2.loop.xn_id, offset);
    EXPECT_EQ(instr_.v2.loop.xp_id, context_id);
    EXPECT_EQ(instr_.v2.loop.mode, 0);
    EXPECT_EQ(instr_.v2.loop.wish_cke_bit, 0);
}

TEST_F(CcuMicrocodeV2Test, loop_group)
{
    uint16_t start_loop_instr_id = 5;
    uint16_t loop_group_config = 0x123;
    uint16_t res_offset = 0x456;
    uint16_t xn_offset = 0x789;
    loop_group(&instr_, start_loop_instr_id, loop_group_config, res_offset, xn_offset);

    EXPECT_EQ(instr_.header.type_, 0x1);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v2.loop_group.start_loop_instr_id, start_loop_instr_id);
    EXPECT_EQ(instr_.v2.loop_group.xn_id, loop_group_config);
    EXPECT_EQ(instr_.v2.loop_group.xm_id, res_offset);
    EXPECT_EQ(instr_.v2.loop_group.xp_id, xn_offset);
}

TEST_F(CcuMicrocodeV2Test, set_cke)
{
    uint16_t set_cke_id = 1;
    uint16_t set_cke_mask = 0xF;
    uint16_t wait_cke_id = 2;
    uint16_t wait_cke_mask = 0xA;
    uint16_t clear_type = 1;
    set_cke(&instr_, set_cke_id, set_cke_mask, wait_cke_id, wait_cke_mask, clear_type);

    EXPECT_EQ(instr_.header.type_, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v2.set_cke.set_cke_id, set_cke_id);
    EXPECT_EQ(instr_.v2.set_cke.set_cke_mask, set_cke_mask);
    EXPECT_EQ(instr_.v2.set_cke.wait_cke_id, wait_cke_id);
    EXPECT_EQ(instr_.v2.set_cke.wait_cke_mask, wait_cke_mask);
    EXPECT_EQ(instr_.v2.set_cke.clear_type, clear_type & 0x1);
}

TEST_F(CcuMicrocodeV2Test, clear_cke)
{
    uint16_t clear_cke_id = 3;
    uint16_t clear_mask = 0x5;
    uint16_t wait_cke_id = 1;
    uint16_t wait_cke_mask = 0x3;
    uint16_t clear_type = 0;
    clear_cke(&instr_, clear_cke_id, clear_mask, wait_cke_id, wait_cke_mask, clear_type);

    EXPECT_EQ(instr_.header.type_, 0x1);
    EXPECT_EQ(instr_.header.code, 0x4);
    EXPECT_EQ(instr_.v2.clear_cke.clear_cke_id, clear_cke_id);
    EXPECT_EQ(instr_.v2.clear_cke.clear_mask, clear_mask);
    EXPECT_EQ(instr_.v2.clear_cke.clear_type, clear_type & 0x1);
}

TEST_F(CcuMicrocodeV2Test, jump)
{
    uint16_t rel_tar_instr_xn_id = 10;
    uint16_t condition_xn_id = 5;
    uint16_t expected_xn_id = 3;
    uint16_t condition_type = 0x5;
    jump(&instr_, rel_tar_instr_xn_id, condition_xn_id, expected_xn_id, condition_type);

    EXPECT_EQ(instr_.header.type_, 0x1);
    EXPECT_EQ(instr_.header.code, 0x5);
    EXPECT_EQ(instr_.v2.jmp.expected_xn_id, expected_xn_id);
    EXPECT_EQ(instr_.v2.jmp.condition_xn_id, condition_xn_id);
    EXPECT_EQ(instr_.v2.jmp.rel_tar_instr_xn_id, rel_tar_instr_xn_id);
    EXPECT_EQ(instr_.v2.jmp.condition_type, condition_type & 0xF);
}

TEST_F(CcuMicrocodeV2Test, trans_loc_mem_to_loc_ms)
{
    cache_config config;
    config.alloc_hint = 1;
    config.victim_hint = 2;
    trans_loc_mem_to_loc_ms(&instr_, 1, 2, 3, 4, 5, 6, 0xF, config);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.ms_id, 1);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.xs_id, 2);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.xst_id, 3);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.xl_id, 4);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.xo_id, 5);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.alloc_hint, config.alloc_hint & 0x3);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_ms.victim_hint, config.victim_hint & 0x3);
}

TEST_F(CcuMicrocodeV2Test, trans_loc_ms_to_loc_mem)
{
    cache_config config;
    config.alloc_hint = 2;
    config.victim_hint = 1;
    trans_loc_ms_to_loc_mem(&instr_, 1, 2, 3, 4, 5, 6, 0xF, config);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v2.trans_loc_ms_to_loc_mem.xd_id, 1);
    EXPECT_EQ(instr_.v2.trans_loc_ms_to_loc_mem.xdt_id, 2);
    EXPECT_EQ(instr_.v2.trans_loc_ms_to_loc_mem.ms_id, 3);
    EXPECT_EQ(instr_.v2.trans_loc_ms_to_loc_mem.xl_id, 4);
    EXPECT_EQ(instr_.v2.trans_loc_ms_to_loc_mem.xo_id, 5);
}

TEST_F(CcuMicrocodeV2Test, trans_loc_mem_to_loc_mem)
{
    cache_config srcConfig;
    srcConfig.alloc_hint = 1;
    srcConfig.victim_hint = 1;
    cache_config dstConfig;
    dstConfig.alloc_hint = 2;
    dstConfig.victim_hint = 2;
    trans_loc_mem_to_loc_mem(&instr_, 1, 2, 3, 4, 5, 6, 7, 0xF, srcConfig, dstConfig);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0x6);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.xd_id, 1);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.xdt_id, 2);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.xs_id, 3);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.xst_id, 4);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.xl_id, 5);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.used_ms_id, 6);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.ms_num, ccu_ms_interleave);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.src_alloc_hint, srcConfig.alloc_hint & 0x3);
    EXPECT_EQ(instr_.v2.trans_loc_mem_to_loc_mem.dst_alloc_hint, dstConfig.alloc_hint & 0x3);
}

TEST_F(CcuMicrocodeV2Test, trans_mem)
{
    trans_mem_notify_info notify;
    notify.xn_id = 1;
    notify.xnt_id = 2;
    notify.value = 0x123;

    trans_mem_reduce_info reduce;
    reduce.udf_type = 0x10;
    reduce.reduce_data_type = 0x5;
    reduce.reduce_op_code = 0x3;

    trans_mem_config config;
    config.order = 0x2;
    config.fence = 1;
    config.cqe = 0;
    config.nf = 1;
    config.udf_enable = 0;
    config.split_mode = 1;
    config.se = 0;
    config.rmt_jetty_type = 0x2;

    trans_mem(&instr_, 1, 2, 3, 4, 5, 6, notify, reduce, config, 7, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0x10);
    EXPECT_EQ(instr_.v2.trans_mem.xd_id, 1);
    EXPECT_EQ(instr_.v2.trans_mem.xdt_id, 2);
    EXPECT_EQ(instr_.v2.trans_mem.xs_id, 3);
    EXPECT_EQ(instr_.v2.trans_mem.xst_id, 4);
    EXPECT_EQ(instr_.v2.trans_mem.xl_id, 5);
    EXPECT_EQ(instr_.v2.trans_mem.xc_id, 6);
    EXPECT_EQ(instr_.v2.trans_mem.xn_id, notify.xn_id);
    EXPECT_EQ(instr_.v2.trans_mem.udf_type, reduce.udf_type & 0xFF);
    EXPECT_EQ(instr_.v2.trans_mem.reduce_data_type, reduce.reduce_data_type & 0xF);
    EXPECT_EQ(instr_.v2.trans_mem.reduce_op_code, reduce.reduce_op_code & 0xF);
    EXPECT_EQ(instr_.v2.trans_mem.order, config.order & 0x7);
    EXPECT_EQ(instr_.v2.trans_mem.fence, config.fence & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.cqe, config.cqe & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.nf, config.nf & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.udf_enable, config.udf_enable & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.split_mode, config.split_mode & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.se, config.se & 0x1);
    EXPECT_EQ(instr_.v2.trans_mem.rmt_jetty_type, config.rmt_jetty_type & 0x3);
}

TEST_F(CcuMicrocodeV2Test, SyncWtX_NotifyOnly)
{
    trans_mem_notify_info notify;
    notify.xn_id = 1;
    notify.xnt_id = 2;
    notify.value = 0x123;
    sync_wt_x(&instr_, notify, 5, 6, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0xD);
    EXPECT_EQ(instr_.v2.sync_wt_x.notify_valid, 0);
    EXPECT_EQ(instr_.v2.sync_wt_x.par_mode, 0);
}

TEST_F(CcuMicrocodeV2Test, SyncWtX_WithXn)
{
    trans_mem_notify_info notify;
    notify.xn_id = 10;
    notify.xnt_id = 11;
    notify.value = 0x123;
    sync_wt_x(&instr_, 1, 2, 3, 4, notify, 5, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0xD);
    EXPECT_EQ(instr_.v2.sync_wt_x.xd_id, 1);
    EXPECT_EQ(instr_.v2.sync_wt_x.xdt_id, 2);
    EXPECT_EQ(instr_.v2.sync_wt_x.xs_id, 3);
    EXPECT_EQ(instr_.v2.sync_wt_x.xc_id, 4);
    EXPECT_EQ(instr_.v2.sync_wt_x.notify_valid, 1);
    EXPECT_EQ(instr_.v2.sync_wt_x.par_mode, 1);
}

TEST_F(CcuMicrocodeV2Test, SyncWtX_FullNotify)
{
    trans_mem_notify_info notify;
    notify.xn_id = 1;
    notify.xnt_id = 2;
    notify.value = 0x123;
    sync_wt_x(&instr_, 3, 4, 5, 6, notify, 7, 0xF);

    EXPECT_EQ(instr_.v2.sync_wt_x.xd_id, 3);
    EXPECT_EQ(instr_.v2.sync_wt_x.xdt_id, 4);
    EXPECT_EQ(instr_.v2.sync_wt_x.xs_id, 5);
    EXPECT_EQ(instr_.v2.sync_wt_x.xc_id, 6);
    EXPECT_EQ(instr_.v2.sync_wt_x.xn_id, notify.xn_id);
    EXPECT_EQ(instr_.v2.sync_wt_x.xnt_id, notify.xnt_id);
    EXPECT_EQ(instr_.v2.sync_wt_x.value, notify.value);
    EXPECT_EQ(instr_.v2.sync_wt_x.notify_valid, 1);
    EXPECT_EQ(instr_.v2.sync_wt_x.par_mode, 1);
}

TEST_F(CcuMicrocodeV2Test, sync_at_x)
{
    sync_at_x(&instr_, 1, 2, 3, 4, 5, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x2);
    EXPECT_EQ(instr_.header.code, 0xE);
    EXPECT_EQ(instr_.v2.sync_at_x.xd_id, 1);
    EXPECT_EQ(instr_.v2.sync_at_x.xdt_id, 2);
    EXPECT_EQ(instr_.v2.sync_at_x.xs_id, 3);
    EXPECT_EQ(instr_.v2.sync_at_x.xc_id, 4);
    EXPECT_EQ(instr_.v2.sync_at_x.par_mode, 1);
}

TEST_F(CcuMicrocodeV2Test, reduce_add)
{
    uint16_t ms[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 4;
    uint16_t cast_en = 2;
    uint16_t data_type_ = 3;
    reduce_add(&instr_, ms, count_, cast_en, data_type_, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x3);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v2.reduce.count_, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v2.reduce.cast_en, cast_en & 0x3);
    EXPECT_EQ(instr_.v2.reduce.data_type, data_type_ & 0x1f);
}

TEST_F(CcuMicrocodeV2Test, reduce_max)
{
    uint16_t ms[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 5;
    uint16_t data_type_ = 4;
    reduce_max(&instr_, ms, count_, data_type_, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x3);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v2.reduce.count_, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v2.reduce.data_type, data_type_ & 0x1f);
    EXPECT_EQ(instr_.v2.reduce.cast_en, 0);
}

TEST_F(CcuMicrocodeV2Test, reduce_min)
{
    uint16_t ms[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 6;
    uint16_t data_type_ = 5;
    reduce_min(&instr_, ms, count_, data_type_, 1, 0xF);

    EXPECT_EQ(instr_.header.type_, 0x3);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v2.reduce.count_, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v2.reduce.data_type, data_type_ & 0x1f);
}

} // namespace
} // namespace ccu_v2
} // namespace ccu_rep
} // namespace hcomm
