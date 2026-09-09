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
#include <string>

namespace asc {
namespace ccu_rep {
namespace {

class CcuMicrocodeV1Test : public ::testing::Test {
protected:
    ccu_instr instr_{};
    void SetUp() override { memset(&instr_, 0, sizeof(instr_)); }
};

TEST_F(CcuMicrocodeV1Test, load_sqe_args_to_gsa_instr)
{
    uint16_t gsa_id = 5;
    uint16_t sqe_args_id = 10;
    load_sqe_args_to_gsa_instr(&instr_, gsa_id, sqe_args_id);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v1.load_sqe_args_to_gsa.gsa_id, gsa_id);
    EXPECT_EQ(instr_.v1.load_sqe_args_to_gsa.sqe_args_id, sqe_args_id);
}

TEST_F(CcuMicrocodeV1Test, load_sqe_args_to_xn_instr)
{
    uint16_t xn_id = 3;
    uint16_t sqe_args_id = 7;
    load_sqe_args_to_xn_instr(&instr_, xn_id, sqe_args_id);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v1.load_sqe_args_to_xn.xn_id, xn_id);
    EXPECT_EQ(instr_.v1.load_sqe_args_to_xn.sqe_args_id, sqe_args_id);
}

TEST_F(CcuMicrocodeV1Test, load_imd_to_gsa_instr)
{
    uint16_t gsa_id = 8;
    uint64_t immediate_ = 0x12345678ABCD;
    load_imd_to_gsa_instr(&instr_, gsa_id, immediate_);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v1.load_imd_to_gsa.gsa_id, gsa_id);
    EXPECT_EQ(instr_.v1.load_imd_to_gsa.immediate, immediate_);
}

TEST_F(CcuMicrocodeV1Test, LoadImdToXnInstr_Normal)
{
    uint16_t xn_id = 4;
    uint64_t immediate_ = 0xDEADBEEF;
    uint16_t sec_flag = 0;
    load_imd_to_xn_instr(&instr_, xn_id, immediate_, sec_flag);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x3);
    EXPECT_EQ(instr_.v1.load_imd_to_xn.xn_id, xn_id);
    EXPECT_EQ(instr_.v1.load_imd_to_xn.immediate, immediate_);
    EXPECT_EQ(instr_.v1.load_imd_to_xn.sec_flag, sec_flag);
}

TEST_F(CcuMicrocodeV1Test, LoadImdToXnInstr_SecFlag)
{
    uint16_t xn_id = 6;
    uint64_t immediate_ = 0x12345;
    uint16_t sec_flag = ccu_load_to_xn_sec_info;
    load_imd_to_xn_instr(&instr_, xn_id, immediate_, sec_flag);

    EXPECT_EQ(instr_.v1.load_imd_to_xn.sec_flag, ccu_load_to_xn_sec_info);
}

TEST_F(CcuMicrocodeV1Test, load_gsa_xn_instr)
{
    uint16_t gs_ad_id = 1;
    uint16_t gs_am_id = 2;
    uint16_t xn_id = 3;
    load_gsa_xn_instr(&instr_, gs_ad_id, gs_am_id, xn_id);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x4);
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_ad_id, gs_ad_id);
    EXPECT_EQ(instr_.v1.load_gsa_xn.gs_am_id, gs_am_id);
    EXPECT_EQ(instr_.v1.load_gsa_xn.xn_id, xn_id);
}

TEST_F(CcuMicrocodeV1Test, load_gsagsa_instr)
{
    uint16_t gs_ad_id = 1;
    uint16_t gs_am_id = 2;
    uint16_t gs_an_id = 3;
    load_gsagsa_instr(&instr_, gs_ad_id, gs_am_id, gs_an_id);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x5);
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_ad_id, gs_ad_id);
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_am_id, gs_am_id);
    EXPECT_EQ(instr_.v1.load_gsagsa.gs_an_id, gs_an_id);
}

TEST_F(CcuMicrocodeV1Test, load_xx_instr)
{
    uint16_t xd_id = 1;
    uint16_t xm_id = 2;
    uint16_t xn_id = 3;
    load_xx_instr(&instr_, xd_id, xm_id, xn_id);

    EXPECT_EQ(instr_.header.type, 0x0);
    EXPECT_EQ(instr_.header.code, 0x6);
    EXPECT_EQ(instr_.v1.load_xx.xd_id, xd_id);
    EXPECT_EQ(instr_.v1.load_xx.xm_id, xm_id);
    EXPECT_EQ(instr_.v1.load_xx.xn_id, xn_id);
}

TEST_F(CcuMicrocodeV1Test, loop_instr)
{
    uint16_t startInstrId = 10;
    uint16_t endInstrId = 20;
    uint16_t xn_id = 5;
    loop_instr(&instr_, startInstrId, endInstrId, xn_id);

    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v1.loop.start_instr_id, startInstrId);
    EXPECT_EQ(instr_.v1.loop.end_instr_id, endInstrId);
    EXPECT_EQ(instr_.v1.loop.xn_id, xn_id);
}

TEST_F(CcuMicrocodeV1Test, loop_group_instr)
{
    uint16_t start_loop_instr_id = 5;
    uint16_t xn_id = 2;
    uint16_t xm_id = 3;
    uint16_t high_perf_mode_en = 1;
    loop_group_instr(&instr_, start_loop_instr_id, xn_id, xm_id, high_perf_mode_en);

    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v1.loop_group.start_loop_instr_id, start_loop_instr_id);
    EXPECT_EQ(instr_.v1.loop_group.xn_id, xn_id);
    EXPECT_EQ(instr_.v1.loop_group.xm_id, xm_id);
    EXPECT_EQ(instr_.v1.loop_group.high_perf_mode_en, high_perf_mode_en & 0x1);
}

TEST_F(CcuMicrocodeV1Test, LoopGroupInstr_HighPerfModeZero)
{
    uint16_t high_perf_mode_en = 0xFE;
    loop_group_instr(&instr_, 0, 0, 0, high_perf_mode_en);
    EXPECT_EQ(instr_.v1.loop_group.high_perf_mode_en, 0);
}

TEST_F(CcuMicrocodeV1Test, set_cke_instr)
{
    uint16_t set_cke_id = 1;
    uint16_t set_cke_mask = 0xF;
    uint16_t wait_cke_id = 2;
    uint16_t wait_cke_mask = 0xA;
    uint16_t clear_type = 1;
    set_cke_instr(&instr_, set_cke_id, set_cke_mask, wait_cke_id, wait_cke_mask, clear_type);

    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v1.set_cke.set_cke_id, set_cke_id);
    EXPECT_EQ(instr_.v1.set_cke.set_cke_mask, set_cke_mask);
    EXPECT_EQ(instr_.v1.set_cke.wait_cke_id, wait_cke_id);
    EXPECT_EQ(instr_.v1.set_cke.wait_cke_mask, wait_cke_mask);
    EXPECT_EQ(instr_.v1.set_cke.clear_type, clear_type & 0x1);
}

TEST_F(CcuMicrocodeV1Test, clear_cke_instr)
{
    uint16_t clear_cke_id = 3;
    uint16_t clear_mask = 0x5;
    uint16_t wait_cke_id = 1;
    uint16_t wait_cke_mask = 0x3;
    uint16_t clear_type = 0;
    clear_cke_instr(&instr_, clear_cke_id, clear_mask, wait_cke_id, wait_cke_mask, clear_type);

    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x4);
    EXPECT_EQ(instr_.v1.clear_cke.clear_cke_id, clear_cke_id);
    EXPECT_EQ(instr_.v1.clear_cke.clear_mask, clear_mask);
    EXPECT_EQ(instr_.v1.clear_cke.clear_type, clear_type & 0x1);
}

TEST_F(CcuMicrocodeV1Test, jump_instr)
{
    uint16_t dst_instr_xn_id = 10;
    uint16_t condition_xn_id = 5;
    uint64_t expect_data = 0x123;
    jump_instr(&instr_, dst_instr_xn_id, condition_xn_id, expect_data);

    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x5);
    EXPECT_EQ(instr_.v1.jmp.dst_instr_xn_id, dst_instr_xn_id);
    EXPECT_EQ(instr_.v1.jmp.condition_xn_id, condition_xn_id);
    EXPECT_EQ(instr_.v1.jmp.expect_data, expect_data);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_mem_to_loc_ms_instr)
{
    trans_loc_mem_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 1, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.loc_ms_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.loc_gsa_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.loc_xn_id, 3);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.length_xn_id, 4);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.channel_id, 5);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.clear_type, 1 & 0x1);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_ms.length_en, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, trans_rmt_mem_to_loc_ms_instr)
{
    trans_rmt_mem_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 0, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_ms.clear_type, 0 & 0x1);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_ms.length_en, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_ms_to_loc_mem_instr)
{
    trans_loc_ms_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 1, 0);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_mem.loc_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_mem.loc_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_mem.loc_ms_id, 3);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_mem.clear_type, 1 & 0x1);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_mem.length_en, 0 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_ms_to_rmt_mem_instr)
{
    trans_loc_ms_to_rmt_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 1, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x3);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_mem.rmt_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_mem.rmt_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_mem.loc_ms_id, 3);
}

TEST_F(CcuMicrocodeV1Test, trans_rmt_ms_to_loc_mem_instr)
{
    trans_rmt_ms_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 0, 0);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x4);
    EXPECT_EQ(instr_.v1.trans_rmt_ms_to_loc_mem.loc_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_rmt_ms_to_loc_mem.loc_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_rmt_ms_to_loc_mem.rmt_ms_id, 3);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_ms_to_loc_ms_instr)
{
    trans_loc_ms_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x5);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_ms.dst_ms_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_ms.src_ms_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_ms.length_xn_id, 3);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_ms.channel_id, 4);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_loc_ms.set_cke_id, 5);
}

TEST_F(CcuMicrocodeV1Test, trans_rmt_ms_to_loc_ms_instr)
{
    trans_rmt_ms_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x6);
    EXPECT_EQ(instr_.v1.trans_rmt_ms_to_loc_ms.loc_ms_id, 1);
    EXPECT_EQ(instr_.v1.trans_rmt_ms_to_loc_ms.rmt_ms_id, 2);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_ms_to_rmt_ms_instr)
{
    trans_loc_ms_to_rmt_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0xA, 8, 0xB, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x7);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_ms.rmt_ms_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_ms.loc_ms_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_id, 5);
    EXPECT_EQ(instr_.v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_mask, 6);
}

TEST_F(CcuMicrocodeV1Test, trans_rmt_mem_to_loc_mem_instr)
{
    trans_rmt_mem_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xF, 10, 0xA, 1, 1, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x8);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.loc_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.loc_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.rmt_gsa_id, 3);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.rmt_xn_id, 4);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.reduce_data_type, 7 & 0xf);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.reduce_op_code, 8 & 0xf);
    EXPECT_EQ(instr_.v1.trans_rmt_mem_to_loc_mem.reduce_en, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_mem_to_rmt_mem_instr)
{
    trans_loc_mem_to_rmt_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xF, 10, 0xA, 0, 0, 0);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0x9);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_rmt_mem.rmt_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_rmt_mem.rmt_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_rmt_mem.loc_gsa_id, 3);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_rmt_mem.loc_xn_id, 4);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_rmt_mem.reduce_en, 0 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, trans_loc_mem_to_loc_mem_instr)
{
    trans_loc_mem_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 7, 0xF, 8, 0xA, 1, 0);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0xa);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.dst_gsa_id, 1);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.dst_xn_id, 2);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.src_gsa_id, 3);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.src_xn_id, 4);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.clear_type, 1 & 0x1);
    EXPECT_EQ(instr_.v1.trans_loc_mem_to_loc_mem.length_en, 0 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, sync_cke_instr)
{
    sync_cke_instr(&instr_, 1, 2, 0xF, 3, 4, 0xA, 5, 0xB, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0xb);
    EXPECT_EQ(instr_.v1.sync_cke.rmt_cke_id, 1);
    EXPECT_EQ(instr_.v1.sync_cke.loc_cke_id, 2);
    EXPECT_EQ(instr_.v1.sync_cke.loc_cke_mask, 0xF);
    EXPECT_EQ(instr_.v1.sync_cke.channel_id, 3);
    EXPECT_EQ(instr_.v1.sync_cke.clear_type, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, sync_gsa_instr)
{
    sync_gsa_instr(&instr_, 1, 2, 3, 4, 0xF, 5, 0xA, 6, 0xB, 0);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0xc);
    EXPECT_EQ(instr_.v1.sync_gsa.rmt_gsa_id, 1);
    EXPECT_EQ(instr_.v1.sync_gsa.loc_gsa_id, 2);
    EXPECT_EQ(instr_.v1.sync_gsa.channel_id, 3);
    EXPECT_EQ(instr_.v1.sync_gsa.set_rmt_cke_id, 4);
    EXPECT_EQ(instr_.v1.sync_gsa.set_rmt_cke_mask, 0xF);
    EXPECT_EQ(instr_.v1.sync_gsa.clear_type, 0 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, sync_xn_instr)
{
    sync_xn_instr(&instr_, 1, 2, 3, 4, 0xF, 5, 0xA, 6, 0xB, 1);

    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0xd);
    EXPECT_EQ(instr_.v1.sync_xn.rmt_xn_id, 1);
    EXPECT_EQ(instr_.v1.sync_xn.loc_xn_id, 2);
    EXPECT_EQ(instr_.v1.sync_xn.channel_id, 3);
    EXPECT_EQ(instr_.v1.sync_xn.clear_type, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, add_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 4;
    uint16_t cast_en = 2;
    uint16_t data_type_ = 3;
    add_instr(&instr_, ms_id, count_, cast_en, data_type_, 1, 0xF, 2, 0xA, 1, 5);

    EXPECT_EQ(instr_.header.type, 0x3);
    EXPECT_EQ(instr_.header.code, 0x0);
    EXPECT_EQ(instr_.v1.add.count, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v1.add.cast_en, cast_en & 0x3);
    EXPECT_EQ(instr_.v1.add.data_type, data_type_ & 0x1f);
    EXPECT_EQ(instr_.v1.add.clear_type, 1 & 0x1);
    EXPECT_EQ(instr_.v1.add.xn_id_length, 5);
}

TEST_F(CcuMicrocodeV1Test, AddInstr_CountMinValue)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 2;
    add_instr(&instr_, ms_id, count_, 0, 0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(instr_.v1.add.count, 0);
}

TEST_F(CcuMicrocodeV1Test, max_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 5;
    uint16_t data_type_ = 4;
    max_instr(&instr_, ms_id, count_, data_type_, 1, 0xF, 2, 0xA, 1, 3);

    EXPECT_EQ(instr_.header.type, 0x3);
    EXPECT_EQ(instr_.header.code, 0x1);
    EXPECT_EQ(instr_.v1.max.count, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v1.max.data_type, data_type_ & 0x1f);
    EXPECT_EQ(instr_.v1.max.clear_type, 1 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, min_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint16_t count_ = 6;
    uint16_t data_type_ = 5;
    min_instr(&instr_, ms_id, count_, data_type_, 1, 0xF, 2, 0xA, 0, 7);

    EXPECT_EQ(instr_.header.type, 0x3);
    EXPECT_EQ(instr_.header.code, 0x2);
    EXPECT_EQ(instr_.v1.min.count, (count_ - 2) & 0x7);
    EXPECT_EQ(instr_.v1.min.data_type, data_type_ & 0x1f);
    EXPECT_EQ(instr_.v1.min.clear_type, 0 & 0x1);
}

TEST_F(CcuMicrocodeV1Test, parse_load_sqe_args_to_gsa_instr)
{
    load_sqe_args_to_gsa_instr(&instr_, 5, 10);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("GSA[5]"), std::string::npos);
    EXPECT_NE(result.find("SqeArg[10]"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, ParseLoadImdToXnInstr_SecFlag)
{
    load_imd_to_xn_instr(&instr_, 3, 0xDEAD, ccu_load_to_xn_sec_info);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("tokenInfo"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, ParseLoadImdToXnInstr_Normal)
{
    load_imd_to_xn_instr(&instr_, 3, 0xDEAD, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Xn[3]"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_loop_instr)
{
    loop_instr(&instr_, 10, 20, 5);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("startInstrId[10]"), std::string::npos);
    EXPECT_NE(result.find("endInstrId[20]"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_add_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0x8000, 0x8001, 0x8002, 0x8003};
    add_instr(&instr_, ms_id, 4, 1, 2, 0, 0, 0, 0, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Add "), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_max_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0x8000, 0x8001, 0x8002, 0x8003};
    max_instr(&instr_, ms_id, 4, 2, 0, 0, 0, 0, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Max"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_min_instr)
{
    uint16_t ms_id[ccu_reduce_max_ms] = {0x8000, 0x8001, 0x8002, 0x8003};
    min_instr(&instr_, ms_id, 4, 2, 0, 0, 0, 0, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Min"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_set_cke_instr)
{
    set_cke_instr(&instr_, 1, 0xF, 2, 0xA, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Set CKE"), std::string::npos);
    EXPECT_NE(result.find("clearType[1]"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_clear_cke_instr)
{
    clear_cke_instr(&instr_, 3, 0x5, 1, 0x3, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Clear CKE"), std::string::npos);
    EXPECT_NE(result.find("clearType[0]"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_jump_instr)
{
    jump_instr(&instr_, 10, 5, 0x123);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Jump"), std::string::npos);
}

TEST_F(CcuMicrocodeV1Test, parse_load_imd_to_gsa_instr)
{
    load_imd_to_gsa_instr(&instr_, 5, 0xDEAD);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_load_imd_to_xn_instr)
{
    load_imd_to_xn_instr(&instr_, 3, 0xBEEF, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_load_gsa_xn_instr)
{
    load_gsa_xn_instr(&instr_, 5, 3, 7);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_load_gsagsa_instr)
{
    load_gsagsa_instr(&instr_, 5, 3, 7);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_load_xx_instr)
{
    load_xx_instr(&instr_, 5, 3, 7);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_loop_group_instr)
{
    loop_group_instr(&instr_, 10, 5, 3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_sync_cke_instr)
{
    sync_cke_instr(&instr_, 1, 2, 0xF, 3, 4, 0xA, 5, 0x3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_sync_gsa_instr)
{
    sync_gsa_instr(&instr_, 1, 2, 3, 4, 0xF, 5, 0xA, 6, 0x3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_sync_xn_instr)
{
    sync_xn_instr(&instr_, 1, 2, 3, 4, 0xF, 5, 0xA, 6, 0x3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_mem_to_loc_ms_instr)
{
    trans_loc_mem_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_rmt_mem_to_loc_ms_instr)
{
    trans_rmt_mem_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_ms_to_loc_mem_instr)
{
    trans_loc_ms_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_ms_to_rmt_mem_instr)
{
    trans_loc_ms_to_rmt_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_rmt_ms_to_loc_mem_instr)
{
    trans_rmt_ms_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_ms_to_loc_ms_instr)
{
    trans_loc_ms_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_rmt_ms_to_loc_ms_instr)
{
    trans_rmt_ms_to_loc_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_ms_to_rmt_ms_instr)
{
    trans_loc_ms_to_rmt_ms_instr(&instr_, 1, 2, 3, 4, 5, 6, 0xF, 7, 0x3, 1, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_rmt_mem_to_loc_mem_instr)
{
    trans_rmt_mem_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0, 0, 7, 0xF, 8, 0x3, 1, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_mem_to_rmt_mem_instr)
{
    trans_loc_mem_to_rmt_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 0, 0, 7, 0xF, 8, 0x3, 1, 0, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_trans_loc_mem_to_loc_mem_instr)
{
    trans_loc_mem_to_loc_mem_instr(&instr_, 1, 2, 3, 4, 5, 6, 7, 0xF, 8, 0x3, 1, 0);
    std::string result = parse_instr(&instr_);
    EXPECT_FALSE(result.empty());
}

TEST_F(CcuMicrocodeV1Test, parse_unsupported_instr)
{
    instr_.header = instr_header(0xF, 0x7F);
    std::string result = parse_instr(&instr_);
    EXPECT_NE(result.find("Unsupported"), std::string::npos);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
