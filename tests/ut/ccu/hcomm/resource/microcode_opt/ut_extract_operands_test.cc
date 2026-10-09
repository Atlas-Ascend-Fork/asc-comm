/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/microcode_opt/extract_operands.h"
#include "hcomm/resource/microcode_opt/config/barrier_config.h"
#include "hcomm/resource/microcode/ccu_microcode_v1.h"
#include <gtest/gtest.h>

namespace asc {
namespace ccu_opt {
namespace {

using ccu_rep::ccu_instr;
namespace code = instr_code_v2;

reg_operand make_read(reg_type t, uint16_t id) { return reg_operand{t, id, false}; }

reg_operand make_write(reg_type t, uint16_t id) { return reg_operand{t, id, true}; }

ccu_instr make_instr(uint16_t type, uint16_t opcode)
{
    ccu_instr instr{};
    instr.header = ccu_rep::instr_header(type, opcode);
    return instr;
}

bool contains(const std::vector<reg_operand>& ops, const reg_operand& target)
{
    for (const auto& op : ops) {
        if (op == target) {
            return true;
        }
    }
    return false;
}

TEST(ExtractOperandsV2Test, LoadSqeArgsToX_WritesXn)
{
    ccu_instr instr = make_instr(code::load_type, code::loadsqeargstox_code);
    instr.v2.load_sqe_args_to_x.xn_id = 5;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 1u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 5)));
}

TEST(ExtractOperandsV2Test, LoadImdToX_WritesXn)
{
    ccu_instr instr = make_instr(code::load_type, code::loadimdtox_code);
    instr.v2.load_imd_to_x.xn_id = 7;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 1u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 7)));
}

TEST(ExtractOperandsV2Test, LoadStoreX_ReadWriteXn)
{
    ccu_instr instr = make_instr(code::load_type, code::loadstorex_code);
    instr.v2.load_store_x.xd_id = 1;
    instr.v2.load_store_x.xs_id = 2;
    instr.v2.load_store_x.xso_id = 3;
    instr.v2.load_store_x.xdo_id = 4;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 4u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 2)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 3)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 4)));
}

TEST(ExtractOperandsV2Test, ClearX_WritesBoth)
{
    ccu_instr instr = make_instr(code::load_type, code::clearx_code);
    instr.v2.clear_x.xn_id = 3;
    instr.v2.clear_x.xm_id = 6;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 3)));
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 6)));
}

TEST(ExtractOperandsV2Test, Nop_NoOperands)
{
    ccu_instr instr = make_instr(code::load_type, code::nop_code);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, Load_ReadsAndWritesXn)
{
    ccu_instr instr = make_instr(code::load_type, code::load_code);
    instr.v2.load.xd_id = 10;
    instr.v2.load.xs_id = 11;
    instr.v2.load.xst_id = 12;
    instr.v2.load.xl_id = 13;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 4u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 10)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 11)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 12)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 13)));
}

TEST(ExtractOperandsV2Test, Store_AllReads)
{
    ccu_instr instr = make_instr(code::load_type, code::store_code);
    instr.v2.store.xd_id = 1;
    instr.v2.store.xdt_id = 2;
    instr.v2.store.xs_id = 3;
    instr.v2.store.xl_id = 4;
    instr.v2.store.xh_id = 5;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 5u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 2)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 3)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 4)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 5)));
}

TEST(ExtractOperandsV2Test, Operator_RegisterPair_TwoReads)
{
    ccu_instr instr = make_instr(code::load_type, code::add_code);
    instr.v2.operate.xd_id = 20;
    instr.v2.operate.xn_id = 21;
    instr.v2.operate.xm_id = 22;
    instr.v2.operate.par_mode = 1;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 3u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 20)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 21)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 22)));
}

TEST(ExtractOperandsV2Test, Operator_ImmediateMode_OneRead)
{
    ccu_instr instr = make_instr(code::load_type, code::mul_code);
    instr.v2.operate.xd_id = 20;
    instr.v2.operate.xn_id = 21;
    instr.v2.operate.xm_id = 22;
    instr.v2.operate.par_mode = 0;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 20)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 21)));
    EXPECT_FALSE(contains(ops, make_read(reg_type::xn, 22)));
}

TEST(ExtractOperandsV2Test, AllBinaryOpcodes_Extracted)
{
    for (uint16_t op :
         {code::add_code, code::sub_code, code::mul_code, code::and_code, code::or_code, code::xor_code, code::shl_code,
          code::shr_code, code::popcnt_code}) {
        ccu_instr instr = make_instr(code::load_type, op);
        instr.v2.operate.xd_id = 1;
        instr.v2.operate.xn_id = 2;
        instr.v2.operate.xm_id = 3;
        instr.v2.operate.par_mode = 1;

        auto ops = extract_operands_v2(instr);
        EXPECT_EQ(ops.size(), 3u) << "opcode=0x" << std::hex << op;
    }
}

TEST(ExtractOperandsV2Test, NotOp_TwoOperands)
{
    ccu_instr instr = make_instr(code::load_type, code::not_code);
    instr.v2.operate.xd_id = 30;
    instr.v2.operate.xn_id = 31;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::xn, 30)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 31)));
}

TEST(ExtractOperandsV2Test, UnknownLoadOpcode_Empty)
{
    ccu_instr instr = make_instr(code::load_type, 0x7F);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, Loop_ReadsThreeXn)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::loop_code);
    instr.v2.loop.xm_id = 1;
    instr.v2.loop.xn_id = 2;
    instr.v2.loop.xp_id = 3;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 3u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 2)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 3)));
}

TEST(ExtractOperandsV2Test, LoopGroup_ReadsThreeXn)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::loopgroup_code);
    instr.v2.loop_group.xn_id = 4;
    instr.v2.loop_group.xm_id = 5;
    instr.v2.loop_group.xp_id = 6;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 3u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 4)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 5)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 6)));
}

TEST(ExtractOperandsV2Test, SetCke_AutoClear_ReadsAndWritesCke)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::setckbit_code);
    instr.v2.set_cke.wait_cke_id = 9;
    instr.v2.set_cke.clear_type = 1;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::cke, 9)));
    EXPECT_TRUE(contains(ops, make_write(reg_type::cke, 9)));
}

TEST(ExtractOperandsV2Test, SetCke_NoAutoClear_OnlyReads)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::setckbit_code);
    instr.v2.set_cke.wait_cke_id = 9;
    instr.v2.set_cke.clear_type = 0;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 1u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::cke, 9)));
}

TEST(ExtractOperandsV2Test, SetCke_IdZero_Skipped)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::setckbit_code);
    instr.v2.set_cke.wait_cke_id = 0;
    instr.v2.set_cke.clear_type = 1;

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, ClearCke_AutoClear_ReadsAndWrites)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::clearckbit_code);
    instr.v2.clear_cke.wait_cke_id = 12;
    instr.v2.clear_cke.clear_type = 1;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::cke, 12)));
    EXPECT_TRUE(contains(ops, make_write(reg_type::cke, 12)));
}

TEST(ExtractOperandsV2Test, ClearCke_NoAutoClear_OnlyReads)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::clearckbit_code);
    instr.v2.clear_cke.wait_cke_id = 12;
    instr.v2.clear_cke.clear_type = 0;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 1u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::cke, 12)));
}

TEST(ExtractOperandsV2Test, Jmp_ReadsThreeXn)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::jmp_code);
    instr.v2.jmp.rel_tar_instr_xn_id = 1;
    instr.v2.jmp.condition_xn_id = 2;
    instr.v2.jmp.expected_xn_id = 3;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 3u);
}

TEST(ExtractOperandsV2Test, Wait_ReadsTwoXn)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::wait_code);
    instr.v2.wait.condition_xn_id = 4;
    instr.v2.wait.expected_xn_id = 5;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 2u);
}

TEST(ExtractOperandsV2Test, Fence_NoOperands)
{
    ccu_instr instr = make_instr(code::ctrl_type, code::fence_code);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, UnknownCtrlOpcode_Empty)
{
    ccu_instr instr = make_instr(code::ctrl_type, 0x7F);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, TransLocMemToLocMs)
{
    ccu_instr instr = make_instr(code::trans_type, code::translocmemtolocms_code);
    instr.v2.trans_loc_mem_to_loc_ms.ms_id = 1;
    instr.v2.trans_loc_mem_to_loc_ms.xs_id = 2;
    instr.v2.trans_loc_mem_to_loc_ms.xst_id = 3;
    instr.v2.trans_loc_mem_to_loc_ms.xl_id = 4;
    instr.v2.trans_loc_mem_to_loc_ms.xo_id = 5;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 5u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::ms, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 2)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 3)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 4)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 5)));
}

TEST(ExtractOperandsV2Test, TransLocMsToLocMem)
{
    ccu_instr instr = make_instr(code::trans_type, code::translocmstolocmem_code);
    instr.v2.trans_loc_ms_to_loc_mem.ms_id = 6;
    instr.v2.trans_loc_ms_to_loc_mem.xd_id = 7;
    instr.v2.trans_loc_ms_to_loc_mem.xdt_id = 8;
    instr.v2.trans_loc_ms_to_loc_mem.xl_id = 9;
    instr.v2.trans_loc_ms_to_loc_mem.xo_id = 10;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 5u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 6)));
}

TEST(ExtractOperandsV2Test, TransLocMsToLocMs)
{
    ccu_instr instr = make_instr(code::trans_type, code::translocmstolocms_code);
    instr.v2.trans_loc_ms_to_loc_ms.msd_id = 1;
    instr.v2.trans_loc_ms_to_loc_ms.mss_id = 2;
    instr.v2.trans_loc_ms_to_loc_ms.xl_id = 3;
    instr.v2.trans_loc_ms_to_loc_ms.xo_id = 4;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 4u);
    EXPECT_TRUE(contains(ops, make_write(reg_type::ms, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 2)));
}

TEST(ExtractOperandsV2Test, TransLocMemToLocMem)
{
    ccu_instr instr = make_instr(code::trans_type, code::translocmemtolocmem_code);
    instr.v2.trans_loc_mem_to_loc_mem.xd_id = 1;
    instr.v2.trans_loc_mem_to_loc_mem.xdt_id = 2;
    instr.v2.trans_loc_mem_to_loc_mem.xs_id = 3;
    instr.v2.trans_loc_mem_to_loc_mem.xst_id = 4;
    instr.v2.trans_loc_mem_to_loc_mem.xl_id = 5;
    instr.v2.trans_loc_mem_to_loc_mem.used_ms_id = 6;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 6u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 3)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 6)));
}

TEST(ExtractOperandsV2Test, TransMem_EightReads)
{
    ccu_instr instr = make_instr(code::trans_type, code::transmem_code);
    instr.v2.trans_mem.xd_id = 1;
    instr.v2.trans_mem.xdt_id = 2;
    instr.v2.trans_mem.xs_id = 3;
    instr.v2.trans_mem.xst_id = 4;
    instr.v2.trans_mem.xl_id = 5;
    instr.v2.trans_mem.xc_id = 6;
    instr.v2.trans_mem.xn_id = 7;
    instr.v2.trans_mem.xnt_id = 8;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 8u);
    for (uint16_t id = 1; id <= 8; ++id) {
        EXPECT_TRUE(contains(ops, make_read(reg_type::xn, id)));
    }
}

TEST(ExtractOperandsV2Test, SyncWtX_NotifyValid_ReadsNotifyRegs)
{
    ccu_instr instr = make_instr(code::trans_type, code::syncwtx_code);
    instr.v2.sync_wt_x.xd_id = 1;
    instr.v2.sync_wt_x.xdt_id = 2;
    instr.v2.sync_wt_x.xs_id = 3;
    instr.v2.sync_wt_x.xc_id = 4;
    instr.v2.sync_wt_x.xn_id = 5;
    instr.v2.sync_wt_x.xnt_id = 6;
    instr.v2.sync_wt_x.notify_valid = 1;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 6u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 5)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 6)));
}

TEST(ExtractOperandsV2Test, SyncWtX_NotifyInvalid_SkipsNotifyRegs)
{
    ccu_instr instr = make_instr(code::trans_type, code::syncwtx_code);
    instr.v2.sync_wt_x.xd_id = 1;
    instr.v2.sync_wt_x.xdt_id = 2;
    instr.v2.sync_wt_x.xs_id = 3;
    instr.v2.sync_wt_x.xc_id = 4;
    instr.v2.sync_wt_x.notify_valid = 0;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 4u);
}

TEST(ExtractOperandsV2Test, SyncAtX_FourReads)
{
    ccu_instr instr = make_instr(code::trans_type, code::syncatx_code);
    instr.v2.sync_at_x.xd_id = 1;
    instr.v2.sync_at_x.xdt_id = 2;
    instr.v2.sync_at_x.xs_id = 3;
    instr.v2.sync_at_x.xc_id = 4;

    auto ops = extract_operands_v2(instr);
    EXPECT_EQ(ops.size(), 4u);
}

TEST(ExtractOperandsV2Test, UnknownTransOpcode_Empty)
{
    ccu_instr instr = make_instr(code::trans_type, 0x7F);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

TEST(ExtractOperandsV2Test, Reduce_MsReadWriteByCount)
{
    ccu_instr instr = make_instr(code::reduce_type, code::reduce_add_code);
    for (uint16_t i = 0; i < ccu_rep::ccu_reduce_max_ms; ++i) {
        instr.v2.reduce.ms_id[i] = i + 1;
    }
    instr.v2.reduce.xn_id_length = 20;
    instr.v2.reduce.count = 2; // real count = 2 + 2 = 4

    auto ops = extract_operands_v2(instr);
    // ms0 读写 (2) + ms1..ms3 读 (3) + xn (1)
    EXPECT_EQ(ops.size(), 6u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 1)));
    EXPECT_TRUE(contains(ops, make_write(reg_type::ms, 1)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 2)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 3)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 4)));
    EXPECT_FALSE(contains(ops, make_read(reg_type::ms, 5)));
    EXPECT_TRUE(contains(ops, make_read(reg_type::xn, 20)));
}

TEST(ExtractOperandsV2Test, Reduce_CountClampedToMax)
{
    ccu_instr instr = make_instr(code::reduce_type, code::reduce_max_code);
    for (uint16_t i = 0; i < ccu_rep::ccu_reduce_max_ms; ++i) {
        instr.v2.reduce.ms_id[i] = i + 1;
    }
    instr.v2.reduce.count = 7; // (7 & 0x7) + 2 = 9 -> clamp to 8

    auto ops = extract_operands_v2(instr);
    // ms0 读写 (2) + ms1..ms7 读 (7) + xn 读 (1)
    EXPECT_EQ(ops.size(), 10u);
    EXPECT_TRUE(contains(ops, make_read(reg_type::ms, 8)));
}

TEST(ExtractOperandsV2Test, UnknownType_Empty)
{
    ccu_instr instr = make_instr(0xF, 0x0);

    auto ops = extract_operands_v2(instr);
    EXPECT_TRUE(ops.empty());
}

} // namespace
} // namespace ccu_opt
} // namespace asc
