/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "extract_operands.h"

#include "hcomm/resource/microcode_opt/config/barrier_config.h"

namespace asc {
namespace ccu_opt {

namespace {

// reduce.count 字段以 "实际 ms 数 - 2" 编码, 且仅占低 3 位:
// 实际 ms 数 = (count_field & 0x7) + 2.
constexpr uint16_t ccu_reduce_count_mask = 0x7;
constexpr uint16_t ccu_reduce_count_bias = 2;
// operate.par_mode == 1: 第二操作数为寄存器 (Xd = Xn @ Xm); == 0 时为 imm16.
constexpr uint16_t parmode_register_pair = 1;
// set/clear_cke.clear_type == 1: 命中后自动清零 wait_cke_id, 该 cke 位既读又写.
constexpr uint16_t cleartype_auto = 1;

inline void add_read(std::vector<reg_operand>& out, reg_type type, uint16_t reg_id)
{
    if (reg_id == 0 && type == reg_type::cke) {
        return;
    }
    out.push_back(reg_operand{type, reg_id, /*is_def=*/false});
}

inline void add_write(std::vector<reg_operand>& out, reg_type type, uint16_t reg_id)
{
    if (reg_id == 0 && type == reg_type::cke) {
        return;
    }
    out.push_back(reg_operand{type, reg_id, /*is_def=*/true});
}

// load 类中的访存 / 清零指令 (非算子). 返回 true 表示已处理该 code.
bool extract_load_mem_ops(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    switch (instr.header.code) {
        case loadsqeargstox_code:
            add_write(out, reg_type::xn, instr.v2.load_sqe_args_to_x.xn_id);
            return true;
        case loadimdtox_code:
            add_write(out, reg_type::xn, instr.v2.load_imd_to_x.xn_id);
            return true;
        case loadstorex_code:
            // Xd = *Xs, *Xdo = Xso (读改写形式), 保守全部当作读写.
            add_read(out, reg_type::xn, instr.v2.load_store_x.xs_id);
            add_read(out, reg_type::xn, instr.v2.load_store_x.xso_id);
            add_read(out, reg_type::xn, instr.v2.load_store_x.xdo_id);
            add_write(out, reg_type::xn, instr.v2.load_store_x.xd_id);
            return true;
        case clearx_code:
            // clear_x 语义为把指定 Xn / Xm 清零, 两者都是 def.
            add_write(out, reg_type::xn, instr.v2.clear_x.xn_id);
            add_write(out, reg_type::xn, instr.v2.clear_x.xm_id);
            return true;
        case nop_code:
            // 无寄存器操作数.
            return true;
        case load_code:
            add_read(out, reg_type::xn, instr.v2.load.xs_id);
            add_read(out, reg_type::xn, instr.v2.load.xst_id);
            add_read(out, reg_type::xn, instr.v2.load.xl_id);
            add_write(out, reg_type::xn, instr.v2.load.xd_id);
            return true;
        case store_code:
            add_read(out, reg_type::xn, instr.v2.store.xd_id);
            add_read(out, reg_type::xn, instr.v2.store.xdt_id);
            add_read(out, reg_type::xn, instr.v2.store.xs_id);
            add_read(out, reg_type::xn, instr.v2.store.xl_id);
            add_read(out, reg_type::xn, instr.v2.store.xh_id);
            return true;
        default:
            return false;
    }
}

// load 类中的算子指令 (add / sub / mul / and_op / or_op / xor_op / Shl / Shr / Popcnt / not_op).
// Xd = Xn @ Xm (par_mode == 1) 或 Xd = Xn @ imm16 (par_mode == 0); not_op / Popcnt 仅使用 Xn.
void extract_load_operator(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    switch (instr.header.code) {
        case add_code:
        case sub_code:
        case mul_code:
        case and_code:
        case or_code:
        case xor_code:
        case shl_code:
        case shr_code:
        case popcnt_code:
            add_write(out, reg_type::xn, instr.v2.operate.xd_id);
            add_read(out, reg_type::xn, instr.v2.operate.xn_id);
            if (instr.v2.operate.par_mode == parmode_register_pair) {
                add_read(out, reg_type::xn, instr.v2.operate.xm_id);
            }
            break;
        case not_code:
            add_write(out, reg_type::xn, instr.v2.operate.xd_id);
            add_read(out, reg_type::xn, instr.v2.operate.xn_id);
            break;
        default:
            break;
    }
}

void extract_load_type(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    if (!extract_load_mem_ops(out, instr)) {
        extract_load_operator(out, instr);
    }
    // set_cke_id 不作为 cke def, 不提取 (见文件顶部 set 语义说明).
}

void extract_ctrl_type(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    switch (instr.header.code) {
        case loop_code:
            // xm_id = iter_num, xn_id = Offset, xp_id = context_id; 三者都是 Xn 寄存器.
            add_read(out, reg_type::xn, instr.v2.loop.xm_id);
            add_read(out, reg_type::xn, instr.v2.loop.xn_id);
            add_read(out, reg_type::xn, instr.v2.loop.xp_id);
            break;
        case loopgroup_code:
            add_read(out, reg_type::xn, instr.v2.loop_group.xn_id);
            add_read(out, reg_type::xn, instr.v2.loop_group.xm_id);
            add_read(out, reg_type::xn, instr.v2.loop_group.xp_id);
            break;
        case setckbit_code:
            add_read(out, reg_type::cke, instr.v2.set_cke.wait_cke_id);
            if (instr.v2.set_cke.clear_type == cleartype_auto) {
                add_write(out, reg_type::cke, instr.v2.set_cke.wait_cke_id);
            }
            break;
        case clearckbit_code:
            // 与 setcke 对称: 只有 clear_type=1 自动清零的 wait_cke_id 才是 cke 写者 (read + def);
            // clear_cke_id 只是主动清某位, 同样不作为触发写后读的 def.
            add_read(out, reg_type::cke, instr.v2.clear_cke.wait_cke_id);
            if (instr.v2.clear_cke.clear_type == cleartype_auto) {
                add_write(out, reg_type::cke, instr.v2.clear_cke.wait_cke_id);
            }
            break;
        case jmp_code:
            add_read(out, reg_type::xn, instr.v2.jmp.rel_tar_instr_xn_id);
            add_read(out, reg_type::xn, instr.v2.jmp.condition_xn_id);
            add_read(out, reg_type::xn, instr.v2.jmp.expected_xn_id);
            break;
        case wait_code:
            add_read(out, reg_type::xn, instr.v2.wait.condition_xn_id);
            add_read(out, reg_type::xn, instr.v2.wait.expected_xn_id);
            break;
        case fence_code:
            break;
        default:
            break;
    }
}

// Trans 类中的纯搬运指令 (Mem<->ms / Mem<->Mem / trans_mem). 返回 true 表示已处理该 code.
bool extract_trans_move_ops(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    switch (instr.header.code) {
        case translocmemtolocms_code:
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_ms.xs_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_ms.xst_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_ms.xl_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_ms.xo_id);
            add_write(out, reg_type::ms, instr.v2.trans_loc_mem_to_loc_ms.ms_id);
            return true;
        case translocmstolocmem_code:
            add_read(out, reg_type::ms, instr.v2.trans_loc_ms_to_loc_mem.ms_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_mem.xd_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_mem.xdt_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_mem.xl_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_mem.xo_id);
            return true;
        case translocmstolocms_code:
            add_read(out, reg_type::ms, instr.v2.trans_loc_ms_to_loc_ms.mss_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_ms.xl_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_ms_to_loc_ms.xo_id);
            add_write(out, reg_type::ms, instr.v2.trans_loc_ms_to_loc_ms.msd_id);
            return true;
        case translocmemtolocmem_code:
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_mem.xd_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_mem.xdt_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_mem.xs_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_mem.xst_id);
            add_read(out, reg_type::xn, instr.v2.trans_loc_mem_to_loc_mem.xl_id);
            // used_ms_id / ms_num 描述临时 ms 区段, 保守视作 read.
            add_read(out, reg_type::ms, instr.v2.trans_loc_mem_to_loc_mem.used_ms_id);
            return true;
        case transmem_code:
            add_read(out, reg_type::xn, instr.v2.trans_mem.xd_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xdt_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xs_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xst_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xl_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xc_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xn_id);
            add_read(out, reg_type::xn, instr.v2.trans_mem.xnt_id);
            return true;
        default:
            return false;
    }
}

// Trans 类中的同步指令 (sync_wt_x / sync_at_x).
void extract_trans_sync_ops(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    switch (instr.header.code) {
        case syncwtx_code:
            add_read(out, reg_type::xn, instr.v2.sync_wt_x.xd_id);
            add_read(out, reg_type::xn, instr.v2.sync_wt_x.xdt_id);
            add_read(out, reg_type::xn, instr.v2.sync_wt_x.xs_id);
            add_read(out, reg_type::xn, instr.v2.sync_wt_x.xc_id);
            if (instr.v2.sync_wt_x.notify_valid != 0) {
                add_read(out, reg_type::xn, instr.v2.sync_wt_x.xn_id);
                add_read(out, reg_type::xn, instr.v2.sync_wt_x.xnt_id);
            }
            break;
        case syncatx_code:
            add_read(out, reg_type::xn, instr.v2.sync_at_x.xd_id);
            add_read(out, reg_type::xn, instr.v2.sync_at_x.xdt_id);
            add_read(out, reg_type::xn, instr.v2.sync_at_x.xs_id);
            add_read(out, reg_type::xn, instr.v2.sync_at_x.xc_id);
            break;
        default:
            break;
    }
}

void extract_trans_type(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    if (!extract_trans_move_ops(out, instr)) {
        extract_trans_sync_ops(out, instr);
    }
    // set_cke_id 不作为 cke def, 不提取 (见文件顶部 set 语义说明).
}

void extract_reduce_type(std::vector<reg_operand>& out, const ccu_rep::ccu_instr& instr)
{
    // reduce_add / reduce_max / reduce_min: MSA~MSH reduce to MSA, ms_id[0] 既读又写.
    uint16_t count_in_instr = instr.v2.reduce.count;
    // count_in_instr 是 "count_ - 2" 后存进去的, 实际 ms 数 = count_field + 2.
    uint16_t real_count = (count_in_instr & ccu_reduce_count_mask) + ccu_reduce_count_bias;
    if (real_count > ccu_rep::ccu_reduce_max_ms) {
        real_count = ccu_rep::ccu_reduce_max_ms;
    }

    add_read(out, reg_type::ms, instr.v2.reduce.ms_id[0]);
    add_write(out, reg_type::ms, instr.v2.reduce.ms_id[0]);
    for (uint16_t i = 1; i < real_count; ++i) {
        add_read(out, reg_type::ms, instr.v2.reduce.ms_id[i]);
    }
    add_read(out, reg_type::xn, instr.v2.reduce.xn_id_length);
    // set_cke_id 不作为 cke def, 不提取 (见文件顶部 set 语义说明).
}

} // namespace

std::vector<reg_operand> extract_operands_v2(const ccu_rep::ccu_instr& instr)
{
    using namespace instr_code_v2;
    std::vector<reg_operand> out;
    out.reserve(8);
    switch (instr.header.type) {
        case load_type:
            extract_load_type(out, instr);
            break;
        case ctrl_type:
            extract_ctrl_type(out, instr);
            break;
        case trans_type:
            extract_trans_type(out, instr);
            break;
        case reduce_type:
            extract_reduce_type(out, instr);
            break;
        default:
            break;
    }
    return out;
}

} // namespace ccu_opt
} // namespace asc
