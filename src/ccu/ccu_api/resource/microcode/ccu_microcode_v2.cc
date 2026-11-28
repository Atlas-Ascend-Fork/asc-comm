/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "ccu_api/resource/microcode/ccu_microcode_v1.h"
#include "ccu_api/common/ccu_exception.h"

#include <unordered_map>

namespace {
constexpr uint16_t LOAD_TYPE = 0x0;

constexpr uint16_t LOADSQEARGSTOX_CODE = 0x1;
constexpr uint16_t LOADIMDTOX_CODE = 0x2;
constexpr uint16_t LOADX_CODE = 0x6;
constexpr uint16_t STOREX_CODE = 0x7;
constexpr uint16_t CLEARX_CODE = 0x8;
constexpr uint16_t NOP_CODE = 0x9;
constexpr uint16_t LOAD_CODE = 0xA;
constexpr uint16_t STORE_CODE = 0xB;
constexpr uint16_t ADD_CODE = 0xD;
constexpr uint16_t SUB_CODE = 0xE;
constexpr uint16_t MUL_CODE = 0xF;
constexpr uint16_t AND_CODE = 0x10;
constexpr uint16_t OR_CODE = 0x11;
constexpr uint16_t NOT_CODE = 0x12;
constexpr uint16_t XOR_CODE = 0x13;
constexpr uint16_t SHL_CODE = 0x14;
constexpr uint16_t SHR_CODE = 0x15;
constexpr uint16_t POPCNT_CODE = 0x16;

constexpr uint16_t CTRL_TYPE = 0x1;

constexpr uint16_t LOOP_CODE = 0x0;
constexpr uint16_t LOOPGROUP_CODE = 0x1;
constexpr uint16_t SETCKBIT_CODE = 0x2;
constexpr uint16_t CLEARCKBIT_CODE = 0x4;
constexpr uint16_t JMP_CODE = 0x5;
constexpr uint16_t WAIT_CODE = 0x7;
constexpr uint16_t FENCE_CODE = 0x8;

constexpr uint16_t TRANS_TYPE = 0x2;

constexpr uint16_t TRANSLOCMEMTOLOCMS_CODE = 0x0;
constexpr uint16_t TRANSLOCMSTOLOCMEM_CODE = 0x2;
constexpr uint16_t TRANSLOCMSTOLOCMS_CODE = 0x5;
constexpr uint16_t TRANSLOCMEMTOLOCMEM_CODE = 0x6;
constexpr uint16_t TRANSMEM_CODE = 0x10;
constexpr uint16_t SYNCWTX_CODE = 0xD;
constexpr uint16_t SYNCATX_CODE = 0xE;

constexpr uint16_t REDUCE_TYPE = 0x3;

constexpr uint16_t REDUCE_ADD_CODE = 0x0;
constexpr uint16_t REDUCE_MAX_CODE = 0x1;
constexpr uint16_t REDUCE_MIN_CODE = 0x2;

constexpr uint16_t JMP_TARGET_PC_IF_BRANCH = 5;  // Jmp跳转到IF分支的目标PC
constexpr uint16_t JMP_TARGET_PC_NOP_BRANCH = 3; // Jmp跳转到Nop的目标PC
constexpr uint16_t JMP_COND_TYPE_IF = 2;         // Jmp条件类型: IF分支
constexpr uint16_t JMP_COND_TYPE_UNCOND = 6;     // Jmp条件类型: 无条件
} // namespace

namespace asc {
namespace ccu_rep {
namespace ccu_v2 {
// *XnId = *sqe_args_id
void load_sqe_args_to_x(
    ccu_instr* instr, uint16_t xn_id, uint16_t sqe_args_id, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, LOADSQEARGSTOX_CODE);
    instr->v2.load_sqe_args_to_x.xn_id = xn_id;
    instr->v2.load_sqe_args_to_x.sqe_args_id = sqe_args_id;
    instr->v2.load_sqe_args_to_x.set_cke_id = set_cke_id;
    instr->v2.load_sqe_args_to_x.set_cke_mask = set_cke_mask;
}

// *XnId = immediate
void load_imd_to_xn(
    ccu_instr* instr, uint16_t xn_id, uint64_t immediate, uint16_t sec_flag, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, LOADIMDTOX_CODE);
    instr->v2.load_imd_to_x.xn_id = xn_id;
    instr->v2.load_imd_to_x.immediate = immediate;
    instr->v2.load_imd_to_x.sec_flag = sec_flag;
    instr->v2.load_imd_to_x.set_cke_id = set_cke_id;
    instr->v2.load_imd_to_x.set_cke_mask = set_cke_mask;
}

void clear_x(
    ccu_instr* instr, uint16_t xn_id, uint16_t xm_id, uint16_t xnIdMode, uint16_t xmIdMode, uint16_t set_cke_id,
    uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, CLEARX_CODE);
    instr->v2.clear_x.xn_id = xn_id & 0x7FFF;
    instr->v2.clear_x.xn_id_mode = xnIdMode & 0x1;
    instr->v2.clear_x.xm_id = xm_id & 0x7FFF;
    instr->v2.clear_x.xm_id_mode = xmIdMode & 0x1;
    instr->v2.clear_x.set_cke_id = set_cke_id;
    instr->v2.clear_x.set_cke_mask = set_cke_mask;
}

void nop(ccu_instr* instr) { instr->header = instr_header(LOAD_TYPE, NOP_CODE); }

inline void operator_helper(
    ccu_instr* instr, uint16_t xd_id, uint16_t xn_id, uint16_t xm_id, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->v2.operate.xd_id = xd_id;
    instr->v2.operate.xn_id = xn_id;
    instr->v2.operate.xm_id = xm_id;
    instr->v2.operate.set_cke_id = set_cke_id;
    instr->v2.operate.set_cke_mask = set_cke_mask;
}

void assign(ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    add_i(instr, result, operand, 0, set_cke_id, set_cke_mask);
}

void assign_i(ccu_instr* instr, uint16_t result, uint64_t operand, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    load_imd_to_xn(instr, result, operand, 0, set_cke_id, set_cke_mask);
}

void add(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, ADD_CODE);
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void add_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, ADD_CODE);
    instr->v2.operate.par_mode = 0;
    operator_helper(instr, result, operand, imm, set_cke_id, set_cke_mask);
}

void sub(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SUB_CODE);

    // 设置指令模式，Par_Mode = 1’b1时，*Xd = *Xn – *Xm
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void sub_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SUB_CODE);

    // 设置指令模式，Par_Mode = 1’b0时，*Xd = *Xn - {48’b0，Immediate[15:0]}
    instr->v2.operate.par_mode = 0;
    operator_helper(instr, result, operand, imm, set_cke_id, set_cke_mask);
}

void mul(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, MUL_CODE);
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void mul_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, MUL_CODE);
    instr->v2.operate.par_mode = 0;
    operator_helper(instr, result, operand, imm, set_cke_id, set_cke_mask);
}

void and_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, AND_CODE);
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void or_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, OR_CODE);
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void xor_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, XOR_CODE);
    instr->v2.operate.par_mode = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void not_op(ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, NOT_CODE);
    instr->v2.operate.par_mode = 0;
    operator_helper(instr, result, operand, 0, set_cke_id, set_cke_mask);
}

void sll(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SHL_CODE);
    instr->v2.operate.shift_type = 0;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void srl(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SHR_CODE);
    instr->v2.operate.shift_type = 0;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void sla(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SHL_CODE);
    instr->v2.operate.shift_type = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

void sra(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, SHR_CODE);
    instr->v2.operate.shift_type = 1;
    operator_helper(instr, result, operand1, operand2, set_cke_id, set_cke_mask);
}

// LoadChannel、LoadInstruction暂不实现

void load_from_mem(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t src_token, uint16_t len, const cache_config& cache_config,
    uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, LOAD_CODE);
    instr->v2.load.xd_id = dst;
    instr->v2.load.xs_id = src;
    instr->v2.load.xst_id = src_token;
    instr->v2.load.xl_id = len;

    instr->v2.load.alloc_hint = cache_config.alloc_hint & 0x3;
    instr->v2.load.victim_hint = cache_config.victim_hint & 0x3;

    instr->v2.load.set_cke_id = set_cke_id;
    instr->v2.load.set_cke_mask = set_cke_mask;
}

void load_x_from_mem(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t src_token, uint16_t len, const cache_config& cache_config,
    uint16_t set_cke_id, uint16_t set_cke_mask)
{
    if (instr == nullptr) {
        HCCL_ERROR("[ccu_v2::LoadXFromMem] instr is nullptr!");
        return;
    }
    load_from_mem(instr, dst, src, src_token, len, cache_config, set_cke_id, set_cke_mask);
    instr->v2.load.dst_type = 0x0;
}

// HSCB Store暂不实现

void store_x_to_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t len, const cache_config& cache_config,
    uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, STORE_CODE);
    instr->v2.store.xd_id = dst;
    instr->v2.store.xdt_id = dst_token;
    instr->v2.store.xs_id = src;
    instr->v2.store.xl_id = len;
    instr->v2.store.xh_id = 0x0;

    instr->v2.store.src_type = 0x0;
    instr->v2.store.store_type = 0b0;
    instr->v2.store.hscb_broad_cast_dst_type = 0x0;

    instr->v2.store.alloc_hint = cache_config.alloc_hint & 0x3;
    instr->v2.store.victim_hint = cache_config.victim_hint & 0x3;

    instr->v2.store.set_cke_id = set_cke_id;
    instr->v2.store.set_cke_mask = set_cke_mask;
}

// HSCB Broadcast 暂不实现
void hscb_store_x_to_mem(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t len, const cache_config& cache_config, uint16_t set_cke_id,
    uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, STORE_CODE);
    instr->v2.store.xd_id = dst;
    instr->v2.store.xdt_id = 0x0;
    instr->v2.store.xs_id = src;
    instr->v2.store.xl_id = len;
    instr->v2.store.xh_id = 0x0;

    instr->v2.store.src_type = 0x0;
    instr->v2.store.store_type = 0b1;
    instr->v2.store.hscb_type = 0b0;
    instr->v2.store.hscb_broad_cast_dst_type = 0x0;

    instr->v2.store.alloc_hint = cache_config.alloc_hint & 0x3;
    instr->v2.store.victim_hint = cache_config.victim_hint & 0x3;

    instr->v2.store.set_cke_id = set_cke_id;
    instr->v2.store.set_cke_mask = set_cke_mask;
}

void load_x(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t src_offset, uint16_t dst_offset, uint16_t o_mode,
    uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, LOADX_CODE);
    instr->v2.load_store_x.xd_id = dst;
    instr->v2.load_store_x.xs_id = src_offset;
    instr->v2.load_store_x.xso_id = src;
    instr->v2.load_store_x.xdo_id = dst_offset;
    instr->v2.load_store_x.o_mode = o_mode & 0x1;
    instr->v2.load_store_x.set_cke_id = set_cke_id;
    instr->v2.load_store_x.set_cke_mask = set_cke_mask;
}

void store_x(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t src_offset, uint16_t dst_offset, uint16_t o_mode,
    uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(LOAD_TYPE, STOREX_CODE);
    instr->v2.load_store_x.xd_id = dst_offset;
    instr->v2.load_store_x.xs_id = src;
    instr->v2.load_store_x.xso_id = src_offset;
    instr->v2.load_store_x.xdo_id = dst;
    instr->v2.load_store_x.o_mode = o_mode & 0x1;
    instr->v2.load_store_x.set_cke_id = set_cke_id;
    instr->v2.load_store_x.set_cke_mask = set_cke_mask;
}

// startInstrId ~ endInstrId之间的指令构成loop
// Xm寄存器中的内容：LoopCtxId[52:45], Offset[44:13], IterNum[12:0]
// IterNum[12:0] loop执行IterNum次 loop每次执行, 地址偏移为Offset loop在第LoopCtxId个LoopEngine上执行
void loop(
    ccu_instr* instr, uint16_t startInstrId, uint16_t endInstrId, uint16_t iterNum, uint16_t offset, uint16_t contextId)
{
    instr->header = instr_header(CTRL_TYPE, LOOP_CODE);
    instr->v2.loop.start_instr_id = startInstrId;
    instr->v2.loop.end_instr_id = endInstrId;
    instr->v2.loop.xm_id = iterNum;
    instr->v2.loop.xn_id = offset;
    instr->v2.loop.xp_id = contextId;
    instr->v2.loop.mode = 0;
    instr->v2.loop.wish_cke_bit = 0;
}

// startLoopInstrId为LoopGroup所包含的Loop的起始地址
// Xn寄存器中的内容：ExtendNum[22:16], RepeatLoopIndex[15:9], LoopNum[8:0]
// Xm寄存器中的内容：gsaOffset[52:21], MSOffset[20:10], ckeOffset[9:0]
// xnOffset[52:21], xnOffset[31:0]
// 从startLoopInstrId开始，共LoopNum个Loop，并且从RepeatLoopIndex个开始，展开ExtendNum次
// 每个展开的Loop，使用的MSId偏移为msOffset，使用的CKEId偏移为ckeOffset，使用的地址偏移为gsaOffset,
// 使用的XnId偏移为xnOffset
void loop_group(
    ccu_instr* instr, uint16_t startLoopInstrId, uint16_t loopGroupConfig, uint16_t resOffset, uint16_t xnOffset)
{
    instr->header = instr_header(CTRL_TYPE, LOOPGROUP_CODE);
    instr->v2.loop_group.start_loop_instr_id = startLoopInstrId;
    instr->v2.loop_group.xn_id = loopGroupConfig;
    instr->v2.loop_group.xm_id = resOffset;
    instr->v2.loop_group.xp_id = xnOffset;
}

// 后续函数中, 均需要wait到<wait_cke_id, wait_cke_mask>后, 再执行相关操作, 执行完之后再set<set_cke_id, set_cke_mask>
// clear_type = 1时, wait到之后需要对<wait_cke_id, wait_cke_mask>清零, 否则不清零

void set_cke(
    ccu_instr* instr, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(CTRL_TYPE, SETCKBIT_CODE);
    instr->v2.set_cke.clear_type = clear_type & 0x1;
    instr->v2.set_cke.set_cke_id = set_cke_id;
    instr->v2.set_cke.set_cke_mask = set_cke_mask;
    instr->v2.set_cke.wait_cke_id = wait_cke_id;
    instr->v2.set_cke.wait_cke_mask = wait_cke_mask;
    instr->v2.set_cke.user_data = 0;
}

void clear_cke(
    ccu_instr* instr, uint16_t clearCKEId, uint16_t clearMask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(CTRL_TYPE, CLEARCKBIT_CODE);
    instr->v2.clear_cke.clear_type = clear_type & 0x1;
    instr->v2.clear_cke.clear_cke_id = clearCKEId;
    instr->v2.clear_cke.clear_mask = clearMask;
    instr->v2.clear_cke.wait_cke_id = wait_cke_id;
    instr->v2.clear_cke.wait_cke_mask = wait_cke_mask;
}

void jump(
    ccu_instr* instr, uint16_t relTarInstrXnId, uint16_t conditionXnId, uint16_t expectedXnId, uint16_t condition_type)
{
    instr->header = instr_header(CTRL_TYPE, JMP_CODE);
    instr->v2.jmp.expected_xn_id = expectedXnId;
    instr->v2.jmp.condition_xn_id = conditionXnId;
    instr->v2.jmp.rel_tar_instr_xn_id = relTarInstrXnId;
    instr->v2.jmp.condition_type = condition_type & 0xF;
}

void wait(ccu_instr* instr, uint16_t conditionXnId, uint16_t expectedXnId, uint16_t condition_type)
{
    instr->header = instr_header(CTRL_TYPE, WAIT_CODE);
    instr->v2.wait.expected_xn_id = expectedXnId;
    instr->v2.wait.condition_xn_id = conditionXnId;
    instr->v2.wait.condition_type = condition_type & 0xF;
}

void fence(ccu_instr* instr) { instr->header = instr_header(CTRL_TYPE, FENCE_CODE); }

// 本端Memory传输到本端MS
void trans_loc_mem_to_loc_ms(
    ccu_instr* instr, uint16_t ms, uint16_t src, uint16_t src_token, uint16_t len, uint16_t offset, uint16_t set_cke_id,
    uint16_t set_cke_mask, const cache_config& cache_config)
{
    instr->header = instr_header(TRANS_TYPE, TRANSLOCMEMTOLOCMS_CODE);
    instr->v2.trans_loc_mem_to_loc_ms.ms_id = ms;
    instr->v2.trans_loc_mem_to_loc_ms.xs_id = src;
    instr->v2.trans_loc_mem_to_loc_ms.xst_id = src_token;
    instr->v2.trans_loc_mem_to_loc_ms.xl_id = len;
    instr->v2.trans_loc_mem_to_loc_ms.xo_id = offset;
    instr->v2.trans_loc_mem_to_loc_ms.alloc_hint = cache_config.alloc_hint & 0x3;
    instr->v2.trans_loc_mem_to_loc_ms.victim_hint = cache_config.victim_hint & 0x3;
    instr->v2.trans_loc_mem_to_loc_ms.set_cke_id = set_cke_id;
    instr->v2.trans_loc_mem_to_loc_ms.set_cke_mask = set_cke_mask;
}

// 本端MS传输到本端Memory
void trans_loc_ms_to_loc_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t ms, uint16_t len, uint16_t offset, uint16_t set_cke_id,
    uint16_t set_cke_mask, const cache_config& cache_config)
{
    instr->header = instr_header(TRANS_TYPE, TRANSLOCMSTOLOCMEM_CODE);
    instr->v2.trans_loc_ms_to_loc_mem.xd_id = dst;
    instr->v2.trans_loc_ms_to_loc_mem.xdt_id = dst_token;
    instr->v2.trans_loc_ms_to_loc_mem.ms_id = ms;
    instr->v2.trans_loc_ms_to_loc_mem.xl_id = len;
    instr->v2.trans_loc_ms_to_loc_mem.xo_id = offset;
    instr->v2.trans_loc_ms_to_loc_mem.alloc_hint = cache_config.alloc_hint & 0x3;
    instr->v2.trans_loc_ms_to_loc_mem.victim_hint = cache_config.victim_hint & 0x3;
    instr->v2.trans_loc_ms_to_loc_mem.set_cke_id = set_cke_id;
    instr->v2.trans_loc_ms_to_loc_mem.set_cke_mask = set_cke_mask;
}

void trans_loc_mem_to_loc_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t src_token, uint16_t len,
    uint16_t usedMSId, uint16_t usedMSNum, uint16_t set_cke_id, uint16_t set_cke_mask,
    const cache_config& srccache_config, const cache_config& dstcacheConfig)
{
    instr->header = instr_header(TRANS_TYPE, TRANSLOCMEMTOLOCMEM_CODE);
    instr->v2.trans_loc_mem_to_loc_mem.xd_id = dst;
    instr->v2.trans_loc_mem_to_loc_mem.xdt_id = dst_token;
    instr->v2.trans_loc_mem_to_loc_mem.xs_id = src;
    instr->v2.trans_loc_mem_to_loc_mem.xst_id = src_token;
    instr->v2.trans_loc_mem_to_loc_mem.xl_id = len;
    instr->v2.trans_loc_mem_to_loc_mem.used_ms_id = usedMSId;
    instr->v2.trans_loc_mem_to_loc_mem.ms_num = usedMSNum;

    instr->v2.trans_loc_mem_to_loc_mem.src_alloc_hint = srccache_config.alloc_hint & 0x3;
    instr->v2.trans_loc_mem_to_loc_mem.src_victim_hint = srccache_config.victim_hint & 0x3;
    instr->v2.trans_loc_mem_to_loc_mem.dst_alloc_hint = dstcacheConfig.alloc_hint & 0x3;
    instr->v2.trans_loc_mem_to_loc_mem.dst_victim_hint = dstcacheConfig.victim_hint & 0x3;

    instr->v2.trans_loc_mem_to_loc_mem.set_cke_id = set_cke_id;
    instr->v2.trans_loc_mem_to_loc_mem.set_cke_mask = set_cke_mask;
}

void trans_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t src_token, uint16_t len,
    uint16_t channel, const trans_mem_notify_info& notify, const trans_mem_reduce_info& reduce,
    const trans_mem_config& config, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    instr->header = instr_header(TRANS_TYPE, TRANSMEM_CODE);
    instr->v2.trans_mem.xd_id = dst;
    instr->v2.trans_mem.xdt_id = dst_token;
    instr->v2.trans_mem.xs_id = src;
    instr->v2.trans_mem.xst_id = src_token;
    instr->v2.trans_mem.xl_id = len;
    instr->v2.trans_mem.xc_id = channel;
    instr->v2.trans_mem.xn_id = notify.xn_id;
    instr->v2.trans_mem.xnt_id = notify.xnt_id;
    instr->v2.trans_mem.value = notify.value;
    instr->v2.trans_mem.udf_type = reduce.udf_type & 0xFF;
    instr->v2.trans_mem.reduce_data_type = reduce.reduce_data_type & 0xF;
    instr->v2.trans_mem.reduce_op_code = reduce.reduce_op_code & 0xF;
    instr->v2.trans_mem.dma_op_code = config.dma_op_code & 0xFF;

    instr->v2.trans_mem.order = config.order & 0x7;
    instr->v2.trans_mem.fence = config.fence & 0x1;
    instr->v2.trans_mem.cqe = config.cqe & 0x1;
    instr->v2.trans_mem.nf = config.nf & 0x1;
    instr->v2.trans_mem.udf_enable = config.udf_enable & 0x1;
    instr->v2.trans_mem.split_mode = config.split_mode & 0x1;
    instr->v2.trans_mem.se = config.se & 0x1;
    instr->v2.trans_mem.rmt_jetty_type = config.rmt_jetty_type & 0x3;

    instr->v2.trans_mem.src_mode = config.src_mode & 0x1;
    instr->v2.trans_mem.dst_mode = config.dst_mode & 0x1;
    instr->v2.trans_mem.ms_id_mode = config.ms_id_mode & 0x1;

    instr->v2.trans_mem.target_hint = 0x0;

    instr->v2.trans_mem.set_cke_id = set_cke_id;
    instr->v2.trans_mem.set_cke_mask = set_cke_mask;
}

// 将本端Xn的值写入远端8B地址
inline void sync_wt_x(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t xn, uint16_t channel_id, uint16_t set_cke_id,
    uint16_t set_cke_mask)
{
    instr->header = instr_header(TRANS_TYPE, SYNCWTX_CODE);
    instr->v2.sync_wt_x.xd_id = dst;
    instr->v2.sync_wt_x.xdt_id = dst_token;
    instr->v2.sync_wt_x.xs_id = xn;
    instr->v2.sync_wt_x.xc_id = channel_id;

    instr->v2.sync_wt_x.set_cke_id = set_cke_id;
    instr->v2.sync_wt_x.set_cke_mask = set_cke_mask;
}

// 将本端Xn的值写入远端8B地址并置位远端CKE
void sync_wt_x(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t xn, uint16_t channel_id,
    const trans_mem_notify_info& notify, uint16_t set_cke_id, uint16_t set_cke_mask)
{
    sync_wt_x(instr, dst, dst_token, xn, channel_id, set_cke_id, set_cke_mask);
    instr->v2.sync_wt_x.xn_id = notify.xn_id;
    instr->v2.sync_wt_x.xnt_id = notify.xnt_id;
    instr->v2.sync_wt_x.value = notify.value;

    instr->v2.sync_wt_x.notify_valid = 1;
    instr->v2.sync_wt_x.par_mode = 1;
}

// 置位远端CKE
void sync_wt_x(
    ccu_instr* instr, const trans_mem_notify_info& notify, uint16_t channel_id, uint16_t set_cke_id,
    uint16_t set_cke_mask)
{
    sync_wt_x(instr, notify.xn_id, notify.xnt_id, notify.value, channel_id, set_cke_id, set_cke_mask);

    instr->v2.sync_wt_x.notify_valid = 0;
    instr->v2.sync_wt_x.par_mode = 0;
}

// 将本端Xn的值以atomic store add的方式写入远端8B地址
void sync_at_x(
    ccu_instr* instr, uint16_t dst_addr, uint16_t dst_token, uint16_t src_id, uint16_t channel_id, uint16_t set_cke_id,
    uint16_t set_cke_mask)
{
    instr->header = instr_header(TRANS_TYPE, SYNCATX_CODE);
    instr->v2.sync_at_x.xd_id = dst_addr;
    instr->v2.sync_at_x.xdt_id = dst_token;
    instr->v2.sync_at_x.xs_id = src_id;
    instr->v2.sync_at_x.xc_id = channel_id;

    instr->v2.sync_at_x.par_mode = 1;

    instr->v2.sync_at_x.set_cke_id = set_cke_id;
    instr->v2.sync_at_x.set_cke_mask = set_cke_mask;
}

// MSA~MSH Reduce到 MSA
inline void reduce(
    ccu_instr* instr, const uint16_t (&ms)[ccu_reduce_max_ms], uint16_t count, uint16_t cast_en, uint16_t data_type,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t xn_id_length)
{
    // 由调用者保证传入的count >= 2(reduce的数据源)
    if (count < ccu_reduce_min_ms) {
        HCCL_ERROR("[ccu_v2::Reduce] count %u < min %u", count, ccu_reduce_min_ms);
        return;
    }
    count -= 2; // CCU指令中指定count数为实际参与运算的MS数减2

    for (uint16_t index = 0; index < ccu_reduce_max_ms; index++) {
        instr->v2.reduce.ms_id[index] = ms[index];
    }
    instr->v2.reduce.xn_id_length = xn_id_length;
    instr->v2.reduce.count = count & 0x7;
    instr->v2.reduce.cast_en = cast_en & 0x3;
    instr->v2.reduce.data_type = data_type & 0x1f;
    instr->v2.reduce.set_cke_id = set_cke_id;
    instr->v2.reduce.set_cke_mask = set_cke_mask;
}

void reduce_add(
    ccu_instr* instr, const uint16_t (&ms)[ccu_reduce_max_ms], uint16_t count, uint16_t cast_en, uint16_t data_type,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t xn_id_length)
{
    if (instr == nullptr) {
        HCCL_ERROR("[ccu_v2::ReduceAdd] instr is nullptr!");
        return;
    }
    instr->header = instr_header(REDUCE_TYPE, REDUCE_ADD_CODE);
    reduce(instr, ms, count, cast_en, data_type, set_cke_id, set_cke_mask, xn_id_length);
}

void reduce_max(
    ccu_instr* instr, const uint16_t (&ms)[ccu_reduce_max_ms], uint16_t count, uint16_t data_type, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t xn_id_length)
{
    if (instr == nullptr) {
        HCCL_ERROR("[ccu_v2::ReduceMax] instr is nullptr!");
        return;
    }
    instr->header = instr_header(REDUCE_TYPE, REDUCE_MAX_CODE);
    reduce(instr, ms, count, 0, data_type, set_cke_id, set_cke_mask, xn_id_length);
}

void reduce_min(
    ccu_instr* instr, const uint16_t (&ms)[ccu_reduce_max_ms], uint16_t count, uint16_t data_type, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t xn_id_length)
{
    if (instr == nullptr) {
        HCCL_ERROR("[ccu_v2::ReduceMin] instr is nullptr!");
        return;
    }
    instr->header = instr_header(REDUCE_TYPE, REDUCE_MIN_CODE);
    reduce(instr, ms, count, 0, data_type, set_cke_id, set_cke_mask, xn_id_length);
}

std::string parse_load_sqe_args_to_x(const ccu_instr* instr)
{
    uint16_t xn_id = instr->v2.load_sqe_args_to_x.xn_id;
    uint16_t sqe_args_id = instr->v2.load_sqe_args_to_x.sqe_args_id;
    uint16_t set_cke_id = instr->v2.load_sqe_args_to_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.load_sqe_args_to_x.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Load SqeArg[%u] to Xn[%u], Set CKE[%u:%04x]", sqe_args_id, xn_id, set_cke_id, set_cke_mask);
}

std::string parse_load_imd_to_xn(const ccu_instr* instr)
{
    uint16_t xn_id = instr->v2.load_imd_to_x.xn_id;
    uint64_t immediate = instr->v2.load_imd_to_x.immediate;
    uint16_t set_cke_id = instr->v2.load_imd_to_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.load_imd_to_x.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    if (instr->v2.load_imd_to_x.sec_flag == ccu_load_to_xn_sec_info) {
        return asc::format_ccu_message(
            "Load immediate[tokenInfo] to Xn[%u], Set CKE[%u:%04x]", xn_id, set_cke_id, set_cke_mask);
    }
    return asc::format_ccu_message(
        "Load immediate[%llu] to Xn[%u], Set CKE[%u:%04x]", immediate, xn_id, set_cke_id, set_cke_mask);
}

std::string parse_clear_x(const ccu_instr* instr)
{
    uint16_t xn_id = instr->v2.clear_x.xn_id;
    uint16_t xm_id = instr->v2.clear_x.xm_id;
    uint16_t set_cke_id = instr->v2.clear_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.clear_x.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "ClearX: xn_id[%u] to xm_id[%u], Set CKE[%u:%04x]", xn_id, xm_id, set_cke_id, set_cke_mask);
}

std::string parse_nop(const ccu_instr* instr)
{
    (void)instr;
    return asc::format_ccu_message("Nop");
}

std::string parse_add(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;
    uint16_t par_mode = instr->v2.operate.par_mode;

    // 使用 StringFormat 格式化字符串
    if (instr->v2.operate.par_mode == 0) {
        return asc::format_ccu_message(
            "AddI:xd_id[%u] = xn_id[%u] + immediate[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id,
            set_cke_id, set_cke_mask, par_mode);
    } else {
        return asc::format_ccu_message(
            "Add:xd_id[%u] = xn_id[%u] + xm_id[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id, set_cke_id,
            set_cke_mask, par_mode);
    }
}

std::string parse_sub(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;
    uint16_t par_mode = instr->v2.operate.par_mode;

    // 使用 StringFormat 格式化字符串
    if (instr->v2.operate.par_mode == 0) {
        return asc::format_ccu_message(
            "SubI:xd_id[%u] = xn_id[%u] - immediate[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id,
            set_cke_id, set_cke_mask, par_mode);
    } else {
        return asc::format_ccu_message(
            "Sub:xd_id[%u] = xn_id[%u] - xm_id[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id, set_cke_id,
            set_cke_mask, par_mode);
    }
}

std::string parse_mul(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;
    uint16_t par_mode = instr->v2.operate.par_mode;

    // 使用 StringFormat 格式化字符串
    if (instr->v2.operate.par_mode == 0) {
        return asc::format_ccu_message(
            "MulI:xd_id[%u] = xn_id[%u] * immediate[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id,
            set_cke_id, set_cke_mask, par_mode);
    } else {
        return asc::format_ccu_message(
            "Mul:xd_id[%u] = xn_id[%u] * xm_id[%u], Set CKE[%u:%04x], par_mode[%u]", xd_id, xn_id, xm_id, set_cke_id,
            set_cke_mask, par_mode);
    }
}

std::string parse_and(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "And:xd_id[%u] = xn_id[%u] & xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id, set_cke_id, set_cke_mask);
}

std::string parse_or(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Or:xd_id[%u] = xn_id[%u] | xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id, set_cke_id, set_cke_mask);
}

std::string parse_xor(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Xor:xd_id[%u] = xn_id[%u] ^ xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id, set_cke_id, set_cke_mask);
}

std::string parse_not(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Not:xd_id[%u] = ~xn_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, set_cke_id, set_cke_mask);
}

std::string parse_shl(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t shiftType = instr->v2.operate.shift_type;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    if (shiftType == 0) {
        return asc::format_ccu_message(
            "logic left shift:xd_id[%u] = xn_id[%u] << xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id, set_cke_id,
            set_cke_mask);
    } else {
        return asc::format_ccu_message(
            "arithmetic left shift:xd_id[%u] = xn_id[%u] << xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id,
            set_cke_id, set_cke_mask);
    }
}

std::string parse_shr(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.operate.xd_id;
    uint16_t xn_id = instr->v2.operate.xn_id;
    uint16_t xm_id = instr->v2.operate.xm_id;
    uint16_t shiftType = instr->v2.operate.shift_type;
    uint16_t set_cke_id = instr->v2.operate.set_cke_id;
    uint16_t set_cke_mask = instr->v2.operate.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    if (shiftType == 0) {
        return asc::format_ccu_message(
            "logic right shift:xd_id[%u] = xn_id[%u] >> xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id, set_cke_id,
            set_cke_mask);
    } else {
        return asc::format_ccu_message(
            "arithmetic right shift:xd_id[%u] = xn_id[%u] >> xm_id[%u], Set CKE[%u:%04x]", xd_id, xn_id, xm_id,
            set_cke_id, set_cke_mask);
    }
}

std::string parse_load_from_mem(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.load.xd_id;
    uint16_t xs_id = instr->v2.load.xs_id;
    uint16_t xst_id = instr->v2.load.xst_id;
    uint16_t xlId = instr->v2.load.xl_id;
    uint16_t allocHint = instr->v2.load.alloc_hint;
    uint16_t victimHint = instr->v2.load.victim_hint;
    uint16_t set_cke_id = instr->v2.load.set_cke_id;
    uint16_t set_cke_mask = instr->v2.load.set_cke_mask;
    uint16_t dstType = instr->v2.load.dst_type;

    return asc::format_ccu_message(
        "LoadFromMem: xd_id[%u] xs_id[%u] xst_id[%u] xlId[%u], allocHint[%u], victimHint[%u], set_cke_id[%u], "
        "set_cke_mask[%u] dstType[%u]",
        xd_id, xs_id, xst_id, xlId, allocHint, victimHint, set_cke_id, set_cke_mask, dstType);
}

std::string parse_store_x_to_mem(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.store.xd_id;
    uint16_t xdt_id = instr->v2.store.xdt_id;
    uint16_t xs_id = instr->v2.store.xs_id;
    uint16_t xlId = instr->v2.store.xl_id;
    uint16_t xhId = instr->v2.store.xh_id;
    uint16_t srcType = instr->v2.store.src_type;
    uint16_t allocHint = instr->v2.store.alloc_hint;
    uint16_t victimHint = instr->v2.store.victim_hint;
    uint16_t storeType = instr->v2.store.store_type;
    uint16_t hscbType = instr->v2.store.hscb_type;
    uint16_t hscbBroadCastDstType = instr->v2.store.hscb_broad_cast_dst_type;
    uint16_t set_cke_id = instr->v2.store.set_cke_id;
    uint16_t set_cke_mask = instr->v2.store.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "StoreXToMem: xd_id[%u] xdt_id[%u], xs_id[%u], xlId[%u], xhId[%u], srcType[%u], allocHint[%u], "
        "victimHint[%u], storeType[%u], hscbType[%u], hscbBroadCastDstType[%u], set CKE[%u:%04x]",
        xd_id, xdt_id, xs_id, xlId, xhId, srcType, allocHint, victimHint, storeType, hscbType, hscbBroadCastDstType,
        set_cke_id, set_cke_mask);
}

std::string parse_load_x(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.load_store_x.xd_id;
    uint16_t xs_id = instr->v2.load_store_x.xs_id;
    uint16_t xso = instr->v2.load_store_x.xso_id;
    uint16_t xdo = instr->v2.load_store_x.xdo_id;
    uint16_t o_mode = instr->v2.load_store_x.o_mode;
    uint16_t set_cke_id = instr->v2.load_store_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.load_store_x.set_cke_mask;

    return asc::format_ccu_message(
        "LoadX: xd_id[%u] xs_id[%u] xso[%u] xdo[%u] o_mode[%u], set CKE[%u:%04x]", xd_id, xs_id, xso, xdo, o_mode,
        set_cke_id, set_cke_mask);
}

std::string parse_store_x(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.load_store_x.xd_id;
    uint16_t xs_id = instr->v2.load_store_x.xs_id;
    uint16_t xso = instr->v2.load_store_x.xso_id;
    uint16_t xdo = instr->v2.load_store_x.xdo_id;
    uint16_t o_mode = instr->v2.load_store_x.o_mode;
    uint16_t set_cke_id = instr->v2.load_store_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.load_store_x.set_cke_mask;

    return asc::format_ccu_message(
        "StoreX: xd_id[%u] xs_id[%u] xso[%u] xdo[%u] o_mode[%u], set CKE[%u:%04x]", xd_id, xs_id, xso, xdo, o_mode,
        set_cke_id, set_cke_mask);
}

std::string parse_loop(const ccu_instr* instr)
{
    uint16_t startInstrId = instr->v2.loop.start_instr_id;
    uint16_t endInstrId = instr->v2.loop.end_instr_id;
    uint16_t xm_id = instr->v2.loop.xm_id;
    uint16_t xn_id = instr->v2.loop.xn_id;
    uint16_t xpId = instr->v2.loop.xp_id;
    uint16_t wishCKEBit = instr->v2.loop.wish_cke_bit;
    uint16_t mode = instr->v2.loop.mode;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Loop From startInstrId[%u] to endInstrId[%u] with iterNumXn[%u], OffsetXn[%u], ContextIdXn[%u],"
        "wishCKEBit[%u], mode[%u]",
        startInstrId, endInstrId, xm_id, xn_id, xpId, wishCKEBit, mode);
}

std::string parse_loop_group(const ccu_instr* instr)
{
    uint16_t startLoopInstrId = instr->v2.loop_group.start_loop_instr_id;
    uint16_t xn_id = instr->v2.loop_group.xn_id;
    uint16_t xm_id = instr->v2.loop_group.xm_id;
    uint16_t xpId = instr->v2.loop_group.xp_id;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "LoopGroup From startLoopInstrId[%u] with loopGroupXn[%u], resOffsetXn[%u], xnOffset[%u]", startLoopInstrId,
        xn_id, xm_id, xpId);
}

std::string parse_jump(const ccu_instr* instr)
{
    uint16_t relTarInstrXnId = instr->v2.jmp.rel_tar_instr_xn_id;
    uint16_t conditionXnId = instr->v2.jmp.condition_xn_id;
    uint16_t expectedXnId = instr->v2.jmp.expected_xn_id;
    uint16_t condition_type = instr->v2.jmp.condition_type;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "When conditionXn[%u] condition with[%u] expectedXn[%u], Jump To RelInstrIdXn[%u]", conditionXnId,
        condition_type, expectedXnId, relTarInstrXnId);
}

std::string parse_wait(const ccu_instr* instr)
{
    uint16_t conditionXnId = instr->v2.wait.condition_xn_id;
    uint16_t expectedXnId = instr->v2.wait.expected_xn_id;
    uint16_t condition_type = instr->v2.wait.condition_type;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Wait conditionXn[%u] condition with[%u] expectedXn[%u]", conditionXnId, condition_type, expectedXnId);
}

std::string parse_fence(const ccu_instr* instr)
{
    (void)instr;
    return asc::format_ccu_message("Fence");
}

std::string parse_set_cke(const ccu_instr* instr)
{
    uint16_t set_cke_id = instr->v2.set_cke.set_cke_id;
    uint16_t set_cke_mask = instr->v2.set_cke.set_cke_mask;
    uint16_t wait_cke_id = instr->v2.set_cke.wait_cke_id;
    uint16_t wait_cke_mask = instr->v2.set_cke.wait_cke_mask;
    uint16_t clear_type = instr->v2.set_cke.clear_type;
    uint64_t userData = instr->v2.set_cke.user_data;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Set CKE[%u:%04x], clear_type[%u], userData[%llu]", wait_cke_id, wait_cke_mask, set_cke_id,
        set_cke_mask, clear_type, userData);
}

std::string parse_clear_cke(const ccu_instr* instr)
{
    uint16_t clearCKEId = instr->v2.clear_cke.clear_cke_id;
    uint16_t clearMask = instr->v2.clear_cke.clear_mask;
    uint16_t wait_cke_id = instr->v2.clear_cke.wait_cke_id;
    uint16_t wait_cke_mask = instr->v2.clear_cke.wait_cke_mask;
    uint16_t clear_type = instr->v2.clear_cke.clear_type & 0x1;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Clear CKE[%u:%04x], clear_type[%u]", wait_cke_id, wait_cke_mask, clearCKEId, clearMask,
        clear_type);
}

std::string parse_trans_loc_mem_to_loc_ms(const ccu_instr* instr)
{
    uint16_t ms = instr->v2.trans_loc_mem_to_loc_ms.ms_id;
    uint16_t src = instr->v2.trans_loc_mem_to_loc_ms.xs_id;
    uint16_t src_token = instr->v2.trans_loc_mem_to_loc_ms.xst_id;
    uint16_t len = instr->v2.trans_loc_mem_to_loc_ms.xl_id;
    uint16_t offset = instr->v2.trans_loc_mem_to_loc_ms.xo_id;
    uint16_t allocHint = instr->v2.trans_loc_mem_to_loc_ms.alloc_hint;
    uint16_t victimHint = instr->v2.trans_loc_mem_to_loc_ms.victim_hint;
    uint16_t set_cke_id = instr->v2.trans_loc_mem_to_loc_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v2.trans_loc_mem_to_loc_ms.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Trans LocMem[%u:%u] To LocMS[%u] With LengthXn[%u], Offset[%u], Set CKE[%u:%04x], "
        "allocHint[%u], victimHint[%u]",
        src, src_token, ms, len, offset, set_cke_id, set_cke_mask, allocHint, victimHint);
}

std::string parse_trans_loc_ms_to_loc_mem(const ccu_instr* instr)
{
    uint16_t dst = instr->v2.trans_loc_ms_to_loc_mem.xd_id;
    uint16_t dst_token = instr->v2.trans_loc_ms_to_loc_mem.xdt_id;
    uint16_t ms = instr->v2.trans_loc_ms_to_loc_mem.ms_id;
    uint16_t len = instr->v2.trans_loc_ms_to_loc_mem.xl_id;
    uint16_t offset = instr->v2.trans_loc_ms_to_loc_mem.xo_id;
    uint16_t alloc_hint = instr->v2.trans_loc_ms_to_loc_mem.alloc_hint;
    uint16_t victim_hint = instr->v2.trans_loc_ms_to_loc_mem.victim_hint;
    uint16_t set_cke_id = instr->v2.trans_loc_ms_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v2.trans_loc_ms_to_loc_mem.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Trans LocMS[%u] To LocMem[%u:%u] With LengthXn[%u], Offset[%u], Set CKE[%u:%04x], "
        "alloc_hint[%u], victim_hint[%u]",
        ms, dst, dst_token, len, offset, set_cke_id, set_cke_mask, alloc_hint, victim_hint);
}

std::string parse_trans_loc_mem_to_loc_mem(const ccu_instr* instr)
{
    uint16_t dst = instr->v2.trans_loc_mem_to_loc_mem.xd_id;
    uint16_t dst_token = instr->v2.trans_loc_mem_to_loc_mem.xdt_id;
    uint16_t src = instr->v2.trans_loc_mem_to_loc_mem.xs_id;
    uint16_t src_token = instr->v2.trans_loc_mem_to_loc_mem.xst_id;
    uint16_t len = instr->v2.trans_loc_mem_to_loc_mem.xl_id;
    uint16_t usedMSId = instr->v2.trans_loc_mem_to_loc_mem.used_ms_id;
    uint16_t ms_num = instr->v2.trans_loc_mem_to_loc_mem.ms_num;

    uint16_t srcAllocHint = instr->v2.trans_loc_mem_to_loc_mem.src_alloc_hint;
    uint16_t srcVictimHint = instr->v2.trans_loc_mem_to_loc_mem.src_victim_hint;
    uint16_t dstAllocHint = instr->v2.trans_loc_mem_to_loc_mem.dst_alloc_hint;
    uint16_t dstVictimHint = instr->v2.trans_loc_mem_to_loc_mem.dst_victim_hint;

    uint16_t set_cke_id = instr->v2.trans_loc_mem_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v2.trans_loc_mem_to_loc_mem.set_cke_mask;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Trans LocMem[%u:%u] To LocMem[%u:%u] With Length[%u], Set CKE[%u:%04x], usedMSId[%u], ms_num[%u],"
        "srcAllocHint[%u], srcVictimHint[%u], dstAllocHint[%u], dstVictimHint[%u]",
        src, src_token, dst, dst_token, len, set_cke_id, set_cke_mask, usedMSId, ms_num, srcAllocHint, srcVictimHint,
        dstAllocHint, dstVictimHint);
}

std::string parse_trans_mem(const ccu_instr* instr)
{
    uint16_t dst = instr->v2.trans_mem.xd_id;
    uint16_t dst_token = instr->v2.trans_mem.xdt_id;
    uint16_t src = instr->v2.trans_mem.xs_id;
    uint16_t src_token = instr->v2.trans_mem.xst_id;
    uint16_t len = instr->v2.trans_mem.xl_id;
    uint16_t channel = instr->v2.trans_mem.xc_id;

    uint16_t notify_xn_id = instr->v2.trans_mem.xn_id;
    uint16_t notify_xnt_id = instr->v2.trans_mem.xnt_id;
    uint16_t notify_value = instr->v2.trans_mem.value;

    uint16_t reduce_udf_type = instr->v2.trans_mem.udf_type;
    uint16_t reduce_data_type = instr->v2.trans_mem.reduce_data_type;
    uint16_t reduce_op_code = instr->v2.trans_mem.reduce_op_code;

    uint16_t config_op_code = instr->v2.trans_mem.dma_op_code;
    uint16_t config_order = instr->v2.trans_mem.order;
    uint16_t config_fence = instr->v2.trans_mem.fence;
    uint16_t config_cqe = instr->v2.trans_mem.cqe;
    uint16_t config_nf = instr->v2.trans_mem.nf;
    uint16_t config_udf_enable = instr->v2.trans_mem.udf_enable;
    uint16_t config_split_mode = instr->v2.trans_mem.split_mode;
    uint16_t config_se = instr->v2.trans_mem.se;
    uint16_t config_rmt_jetty_type = instr->v2.trans_mem.rmt_jetty_type;
    uint16_t config_target_hint = instr->v2.trans_mem.target_hint;

    uint16_t set_cke_id = instr->v2.trans_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v2.trans_mem.set_cke_mask;

    uint16_t src_mode = instr->v2.trans_mem.src_mode;
    uint16_t dst_mode = instr->v2.trans_mem.dst_mode;
    uint16_t ms_id_mode = instr->v2.trans_mem.ms_id_mode;

    // 使用 StringFormat 格式化字符串
    return asc::format_ccu_message(
        "Trans Mem[%u:%u] To Mem[%u:%u] With Len[%u] Use Channel[%u] Set CKE[%u:%04x], notify_xn_id[%u], "
        "notify_xnt_id[%u], notify_value[%u], reduce_udf_type[%u], reduce_data_type[%u], reduce_op_code[%u], "
        "config_op_code[%u], Order[%u], Fence[%u], Cqe[%u], Nf[%u], UdfEnable[%u], SplitMode[%u], Se[%u], "
        "RmtJettyType[%u], config_target_hint[%u], src_mode[%u], dst_mode[%u], ms_id_mode[%u]",
        src, src_token, dst, dst_token, len, channel, set_cke_id, set_cke_mask, notify_xn_id, notify_xnt_id,
        notify_value, reduce_udf_type, reduce_data_type, reduce_op_code, config_op_code, config_order, config_fence,
        config_cqe, config_nf, config_udf_enable, config_split_mode, config_se, config_rmt_jetty_type,
        config_target_hint, src_mode, dst_mode, ms_id_mode);
}

std::string parse_sync_wt_x(const ccu_instr* instr)
{
    uint16_t dst = instr->v2.sync_wt_x.xd_id;
    uint16_t dst_token = instr->v2.sync_wt_x.xdt_id;
    uint16_t xn = instr->v2.sync_wt_x.xs_id;
    uint16_t channel_id = instr->v2.sync_wt_x.xc_id;

    uint16_t set_cke_id = instr->v2.sync_wt_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.sync_wt_x.set_cke_mask;

    // 检查是否包含通知信息
    bool hasNotify = (instr->v2.sync_wt_x.notify_valid != 0);
    uint16_t par_mode = instr->v2.sync_wt_x.par_mode;
    uint16_t notify_xn_id = hasNotify ? instr->v2.sync_wt_x.xn_id : 0;
    uint16_t notify_xnt_id = hasNotify ? instr->v2.sync_wt_x.xnt_id : 0;
    uint16_t notify_value = hasNotify ? instr->v2.sync_wt_x.value : 0;

    // 使用 StringFormat 格式化字符串
    if (hasNotify) {
        return asc::format_ccu_message(
            "Sync Xn[%u] To Mem[%u:%u] Use ChannelXn[%u], with notify[%u:%u:%04x], and Set CKE[%u:%04x], "
            "par_mode[%u]",
            xn, dst, dst_token, channel_id, notify_xn_id, notify_xnt_id, notify_value, set_cke_id, set_cke_mask,
            par_mode);
    } else {
        return asc::format_ccu_message(
            "Sync Immd[%u] To Mem[%u:%u] Use ChannelXn[%u], and Set CKE[%u:%04x], par_mode[%u]", xn, dst, dst_token,
            channel_id, set_cke_id, set_cke_mask, par_mode);
    }
}

std::string parse_sync_at_x(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v2.sync_at_x.xd_id;
    uint16_t xdt_id = instr->v2.sync_at_x.xdt_id;
    uint16_t xs_id = instr->v2.sync_at_x.xs_id;
    uint16_t xc_id = instr->v2.sync_at_x.xc_id;
    uint16_t par_mode = instr->v2.sync_at_x.par_mode;
    uint16_t set_cke_id = instr->v2.sync_at_x.set_cke_id;
    uint16_t set_cke_mask = instr->v2.sync_at_x.set_cke_mask;

    return asc::format_ccu_message(
        "Atomic Sync xs_id[%u] To Mem[%u:%u] Use ChannelXn[%u], par_mode[%u], set_cke_id[%u:%04x]", xs_id, xd_id,
        xdt_id, xc_id, par_mode, set_cke_id, set_cke_mask);
}

std::string parse_ms_list_v2(const uint16_t* ms_id, uint16_t ms_num)
{
    std::string res = "MS[";
    for (uint16_t i = 0; i < ms_num + 2 && i < ccu_reduce_max_ms; i++) { // 循环范围 0 ~ count+ 2，防止越界
        if (i == ms_num + 1) {
            res += std::to_string(ms_id[i] / 0x8000) + ":" + std::to_string(ms_id[i] % 0x8000) + "]";
        } else {
            res += std::to_string(ms_id[i] / 0x8000) + ":" + std::to_string(ms_id[i] % 0x8000) + ", ";
        }
    }

    return res;
}

std::string parse_reduce(const ccu_instr* instr)
{
    uint16_t reduceType = instr->header.code;
    uint16_t count = instr->v2.reduce.count;
    uint16_t cast_en = instr->v2.reduce.cast_en;
    uint16_t data_type = instr->v2.reduce.data_type;
    uint16_t set_cke_id = instr->v2.reduce.set_cke_id;
    uint16_t set_cke_mask = instr->v2.reduce.set_cke_mask;

    return asc::format_ccu_message(
        "Reduce %s, count[%u], reduceType[%u], cast_en[%u] and data_type[%u], Set CKE[%u:%04x]",
        parse_ms_list_v2(instr->v2.reduce.ms_id, count).c_str(), count, reduceType, cast_en, data_type, set_cke_id,
        set_cke_mask);
}

using ParseInstrFunc = std::string (*)(const ccu_instr*);
static const std::unordered_map<uint16_t, ParseInstrFunc> g_parseInstrSqeMapV2 = {
    {instr_header(LOAD_TYPE, LOADSQEARGSTOX_CODE).header, &parse_load_sqe_args_to_x},
    {instr_header(LOAD_TYPE, LOADIMDTOX_CODE).header, &parse_load_imd_to_xn},
    {instr_header(LOAD_TYPE, CLEARX_CODE).header, &parse_clear_x},
    {instr_header(LOAD_TYPE, NOP_CODE).header, &parse_nop},
    {instr_header(LOAD_TYPE, ADD_CODE).header, &parse_add},
    {instr_header(LOAD_TYPE, SUB_CODE).header, &parse_sub},
    {instr_header(LOAD_TYPE, MUL_CODE).header, &parse_mul},
    {instr_header(LOAD_TYPE, AND_CODE).header, &parse_and},
    {instr_header(LOAD_TYPE, OR_CODE).header, &parse_or},
    {instr_header(LOAD_TYPE, XOR_CODE).header, &parse_xor},
    {instr_header(LOAD_TYPE, NOT_CODE).header, &parse_not},
    {instr_header(LOAD_TYPE, SHL_CODE).header, &parse_shl},
    {instr_header(LOAD_TYPE, SHR_CODE).header, &parse_shr},
    {instr_header(LOAD_TYPE, LOAD_CODE).header, &parse_load_from_mem},
    {instr_header(LOAD_TYPE, STORE_CODE).header, &parse_store_x_to_mem},
    {instr_header(LOAD_TYPE, LOADX_CODE).header, &parse_load_x},
    {instr_header(LOAD_TYPE, STOREX_CODE).header, &parse_store_x},
    {instr_header(CTRL_TYPE, LOOP_CODE).header, &parse_loop},
    {instr_header(CTRL_TYPE, LOOPGROUP_CODE).header, &parse_loop_group},
    {instr_header(CTRL_TYPE, JMP_CODE).header, &parse_jump},
    {instr_header(CTRL_TYPE, WAIT_CODE).header, &parse_wait},
    {instr_header(CTRL_TYPE, FENCE_CODE).header, &parse_fence},
    {instr_header(CTRL_TYPE, SETCKBIT_CODE).header, &parse_set_cke},
    {instr_header(CTRL_TYPE, CLEARCKBIT_CODE).header, &parse_clear_cke},
    {instr_header(TRANS_TYPE, TRANSLOCMEMTOLOCMS_CODE).header, &parse_trans_loc_mem_to_loc_ms},
    {instr_header(TRANS_TYPE, TRANSLOCMSTOLOCMEM_CODE).header, &parse_trans_loc_ms_to_loc_mem},
    {instr_header(TRANS_TYPE, TRANSLOCMEMTOLOCMEM_CODE).header, &parse_trans_loc_mem_to_loc_mem},
    {instr_header(TRANS_TYPE, TRANSMEM_CODE).header, &parse_trans_mem},
    {instr_header(TRANS_TYPE, SYNCWTX_CODE).header, &parse_sync_wt_x},
    {instr_header(TRANS_TYPE, SYNCATX_CODE).header, &parse_sync_at_x},
    {instr_header(REDUCE_TYPE, REDUCE_ADD_CODE).header, &parse_reduce},
    {instr_header(REDUCE_TYPE, REDUCE_MAX_CODE).header, &parse_reduce},
    {instr_header(REDUCE_TYPE, REDUCE_MIN_CODE).header, &parse_reduce}};

std::string parse_instr_v2(const ccu_instr* instr)
{
    if (instr == nullptr) {
        HCCL_ERROR("ParseInstr input nullptr!");
        return std::string();
    }

    auto it = g_parseInstrSqeMapV2.find(instr->header.header);
    if (it == g_parseInstrSqeMapV2.end()) {
        HCCL_ERROR("Not support instr type %u code %u", instr->header.type, instr->header.code);
        return std::string();
    }

    return it->second(instr);
}

void rel_jmp(ccu_instr* instr, uint16_t target_xn_id, uint32_t jmp_instr_id, uint16_t xn0, uint16_t xn1)
{
    ccu_v2::load_imd_to_xn(instr++, xn0, jmp_instr_id);
    ccu_v2::load_imd_to_xn(instr++, xn1, JMP_TARGET_PC_IF_BRANCH);
    ccu_v2::jump(instr++, xn1, target_xn_id, xn0, JMP_COND_TYPE_IF);
    ccu_v2::load_imd_to_xn(instr++, xn0, 0x10000 - jmp_instr_id);
    ccu_v2::add(instr++, target_xn_id, target_xn_id, xn0);
    ccu_v2::load_imd_to_xn(instr++, xn1, JMP_TARGET_PC_NOP_BRANCH);
    ccu_v2::jump(instr++, xn1, 0, 0, JMP_COND_TYPE_UNCOND);
    ccu_v2::sub(instr++, target_xn_id, target_xn_id, xn0);
    ccu_v2::nop(instr++);
}
}; // namespace ccu_v2

}; // namespace ccu_rep
}; // namespace asc
