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

#include <unordered_map>

#include "hcomm/common/ccu_exception.h"

namespace {
constexpr uint16_t load_type = 0x0;
constexpr uint16_t ctrl_type = 0x1;
constexpr uint16_t trans_type = 0x2;
constexpr uint16_t reduce_type = 0x3;

constexpr uint16_t loadsqeargstogsa_code = 0x0;
constexpr uint16_t loadsqeargstoxn_code = 0x1;
constexpr uint16_t loadimdtogsa_code = 0x2;
constexpr uint16_t loadimdtoxn_code = 0x3;
constexpr uint16_t loadgsaxn_code = 0x4;
constexpr uint16_t loadgsagsa_code = 0x5;
constexpr uint16_t loadxx_code = 0x6;

constexpr uint16_t loop_code = 0x0;
constexpr uint16_t loopgroup_code = 0x1;
constexpr uint16_t setcke_code = 0x2;
constexpr uint16_t clearcke_code = 0x4;
constexpr uint16_t jmp_code = 0x5;

constexpr uint16_t translocmemtolocms_code = 0x0;
constexpr uint16_t transrmtmemtolocms_code = 0x1;
constexpr uint16_t translocmstolocmem_code = 0x2;
constexpr uint16_t translocmstormtmem_code = 0x3;
constexpr uint16_t transrmtmstolocmem_code = 0x4;
constexpr uint16_t translocmstolocms_code = 0x5;
constexpr uint16_t transrmtmstolocms_code = 0x6;
constexpr uint16_t translocmstormtms_code = 0x7;
constexpr uint16_t transrmtmemtolocmem_code = 0x8;
constexpr uint16_t translocmemtormtmem_code = 0x9;
constexpr uint16_t translocmemtolocmem_code = 0xa;
constexpr uint16_t synccke_code = 0xb;
constexpr uint16_t syncgsa_code = 0xc;
constexpr uint16_t syncxn_code = 0xd;

constexpr uint16_t add_code = 0x0;
constexpr uint16_t max_code = 0x1;
constexpr uint16_t min_code = 0x2;
} // namespace

namespace asc {
namespace ccu_rep {

// *GSAId = *sqeArgsId
void load_sqe_args_to_gsa_instr(ccu_instr* instr, uint16_t gsa_id, uint16_t sqe_args_id)
{
    instr->header = instr_header(load_type, loadsqeargstogsa_code);
    instr->v1.load_sqe_args_to_gsa.gsa_id = gsa_id;
    instr->v1.load_sqe_args_to_gsa.sqe_args_id = sqe_args_id;
}

// *XnId = *sqeArgsId
void load_sqe_args_to_xn_instr(ccu_instr* instr, uint16_t xn_id, uint16_t sqe_args_id)
{
    instr->header = instr_header(load_type, loadsqeargstoxn_code);
    instr->v1.load_sqe_args_to_xn.xn_id = xn_id;
    instr->v1.load_sqe_args_to_xn.sqe_args_id = sqe_args_id;
}

// *GSAId = immediate
void load_imd_to_gsa_instr(ccu_instr* instr, uint16_t gsa_id, uint64_t immediate)
{
    instr->header = instr_header(load_type, loadimdtogsa_code);
    instr->v1.load_imd_to_gsa.gsa_id = gsa_id;
    instr->v1.load_imd_to_gsa.immediate = immediate;
}

// *XnId = immediate，secFlag用于打印时判断immediate是否是敏感信息，如果是，请设置secFlag=CCU_LOAD_TO_XN_SEC_INFO
void load_imd_to_xn_instr(ccu_instr* instr, uint16_t xn_id, uint64_t immediate, uint16_t sec_flag)
{
    instr->header = instr_header(load_type, loadimdtoxn_code);
    instr->v1.load_imd_to_xn.xn_id = xn_id;
    instr->v1.load_imd_to_xn.immediate = immediate;
    instr->v1.load_imd_to_xn.sec_flag = sec_flag;
}

// *GSAdId = *GSAmId + *XnId
void load_gsa_xn_instr(ccu_instr* instr, uint16_t gs_ad_id, uint16_t gs_am_id, uint16_t xn_id)
{
    instr->header = instr_header(load_type, loadgsaxn_code);
    instr->v1.load_gsa_xn.gs_ad_id = gs_ad_id;
    instr->v1.load_gsa_xn.gs_am_id = gs_am_id;
    instr->v1.load_gsa_xn.xn_id = xn_id;
}

// *GSAdId = *GSAmId + *GSAnId
void load_gsagsa_instr(ccu_instr* instr, uint16_t gs_ad_id, uint16_t gs_am_id, uint16_t gs_an_id)
{
    instr->header = instr_header(load_type, loadgsagsa_code);
    instr->v1.load_gsagsa.gs_ad_id = gs_ad_id;
    instr->v1.load_gsagsa.gs_am_id = gs_am_id;
    instr->v1.load_gsagsa.gs_an_id = gs_an_id;
}

// *XdId = *XmId + *XnId
void load_xx_instr(ccu_instr* instr, uint16_t xd_id, uint16_t xm_id, uint16_t xn_id)
{
    instr->header = instr_header(load_type, loadxx_code);
    instr->v1.load_xx.xd_id = xd_id;
    instr->v1.load_xx.xm_id = xm_id;
    instr->v1.load_xx.xn_id = xn_id;
}

// startInstrId ~ endInstrId之间的指令构成loop
// Xn寄存器中的内容：LoopNum[61:55], RepeatNum[54:48], NoRepeatNum[47:41], LoopCtxId[40:33], Offset[32:13],
// IterNum[12:0] loop执行IterNum次 loop每次执行, *GSA偏移为Offset loop在第LoopCtxId个LoopEngine上执行
void loop_instr(ccu_instr* instr, uint16_t start_instr_id, uint16_t end_instr_id, uint16_t xn_id)
{
    instr->header = instr_header(ctrl_type, loop_code);
    instr->v1.loop.start_instr_id = start_instr_id;
    instr->v1.loop.end_instr_id = end_instr_id;
    instr->v1.loop.xn_id = xn_id;
}

// startLoopInstrId为LoopGroup所包含的Loop的起始地址
// Xn寄存器中的内容：LoopNum[61:55], RepeatNum[54:48], NoRepeatNum[47:41], LoopCtxId[40:33], Offset[32:13],
// IterNum[12:0] 不自动展开的Loop的个数：NoRepeatNum 自动展开的Loop的个数：RepeatNum 自动展开成LoopNum个Loop
void loop_group_instr(
    ccu_instr* instr, uint16_t start_loop_instr_id, uint16_t xn_id, uint16_t xm_id, uint16_t high_perf_mode_en)
{
    instr->header = instr_header(ctrl_type, loopgroup_code);
    instr->v1.loop_group.start_loop_instr_id = start_loop_instr_id;
    instr->v1.loop_group.xn_id = xn_id;
    instr->v1.loop_group.xm_id = xm_id;
    instr->v1.loop_group.high_perf_mode_en = high_perf_mode_en & 0x1;
}

// 后续函数中, 均需要wait到<waitCKEId, waitCKEMask>后, 再执行相关操作, 执行完之后再set<setCKEId, setCKEMask>
// clearType = 1时, wait到之后需要对<waitCKEId, waitCKEMask>清零, 否则不清零

// set<setCKEId, setCKEMask>
void set_cke_instr(
    ccu_instr* instr, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(ctrl_type, setcke_code);
    instr->v1.set_cke.clear_type = clear_type & 0x1;
    instr->v1.set_cke.set_cke_id = set_cke_id;
    instr->v1.set_cke.set_cke_mask = set_cke_mask;
    instr->v1.set_cke.wait_cke_id = wait_cke_id;
    instr->v1.set_cke.wait_cke_mask = wait_cke_mask;
}

// clear<setCKEId, setCKEMask>
void clear_cke_instr(
    ccu_instr* instr, uint16_t clear_cke_id, uint16_t clear_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(ctrl_type, clearcke_code);
    instr->v1.clear_cke.clear_type = clear_type & 0x1;
    instr->v1.clear_cke.clear_cke_id = clear_cke_id;
    instr->v1.clear_cke.clear_mask = clear_mask;
    instr->v1.clear_cke.wait_cke_id = wait_cke_id;
    instr->v1.clear_cke.wait_cke_mask = wait_cke_mask;
}

void jump_instr(ccu_instr* instr, uint16_t dst_instr_xn_id, uint16_t condition_xn_id, uint64_t expect_data)
{
    instr->header = instr_header(ctrl_type, jmp_code);
    instr->v1.jmp.dst_instr_xn_id = dst_instr_xn_id;
    instr->v1.jmp.condition_xn_id = condition_xn_id;
    instr->v1.jmp.expect_data = expect_data;
}

// 本端Memory传输到本端MS
// locMSId: 本端MSId
// <locGSAId, locXnId>: 本端Memory地址和Token
// 数据长度: length
void trans_loc_mem_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmemtolocms_code);
    instr->v1.trans_loc_mem_to_loc_ms.loc_ms_id = loc_ms_id;
    instr->v1.trans_loc_mem_to_loc_ms.loc_gsa_id = loc_gsa_id;
    instr->v1.trans_loc_mem_to_loc_ms.loc_xn_id = loc_xn_id;
    instr->v1.trans_loc_mem_to_loc_ms.length_xn_id = length_xn_id;
    instr->v1.trans_loc_mem_to_loc_ms.channel_id = channel_id;
    instr->v1.trans_loc_mem_to_loc_ms.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_mem_to_loc_ms.length_en = length_en & 0x1;
    instr->v1.trans_loc_mem_to_loc_ms.set_cke_id = set_cke_id;
    instr->v1.trans_loc_mem_to_loc_ms.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_mem_to_loc_ms.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_mem_to_loc_ms.wait_cke_mask = wait_cke_mask;
}

// 远端Memory传输到本端MS
// locMSId: 本端MSId
// <rmtGSAId, rmtXnId>: 远端Memory地址和Token
// 数据长度: length
// 路径: channelId
void trans_rmt_mem_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, transrmtmemtolocms_code);
    instr->v1.trans_rmt_mem_to_loc_ms.loc_ms_id = loc_ms_id;
    instr->v1.trans_rmt_mem_to_loc_ms.rmt_gsa_id = rmt_gsa_id;
    instr->v1.trans_rmt_mem_to_loc_ms.rmt_xn_id = rmt_xn_id;
    instr->v1.trans_rmt_mem_to_loc_ms.length_xn_id = length_xn_id;
    instr->v1.trans_rmt_mem_to_loc_ms.channel_id = channel_id;
    instr->v1.trans_rmt_mem_to_loc_ms.clear_type = clear_type & 0x1;
    instr->v1.trans_rmt_mem_to_loc_ms.length_en = length_en & 0x1;
    instr->v1.trans_rmt_mem_to_loc_ms.set_cke_id = set_cke_id;
    instr->v1.trans_rmt_mem_to_loc_ms.set_cke_mask = set_cke_mask;
    instr->v1.trans_rmt_mem_to_loc_ms.wait_cke_id = wait_cke_id;
    instr->v1.trans_rmt_mem_to_loc_ms.wait_cke_mask = wait_cke_mask;
}

// 本端MS传输到本端Memory
// <locGSAId, locXnId>: 本端Memory地址和Token
// locMSId: 本端MSId
// 数据长度: length
void trans_loc_ms_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t loc_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmstolocmem_code);
    instr->v1.trans_loc_ms_to_loc_mem.loc_gsa_id = loc_gsa_id;
    instr->v1.trans_loc_ms_to_loc_mem.loc_xn_id = loc_xn_id;
    instr->v1.trans_loc_ms_to_loc_mem.loc_ms_id = loc_ms_id;
    instr->v1.trans_loc_ms_to_loc_mem.length_xn_id = length_xn_id;
    instr->v1.trans_loc_ms_to_loc_mem.channel_id = channel_id;
    instr->v1.trans_loc_ms_to_loc_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_ms_to_loc_mem.length_en = length_en & 0x1;
    instr->v1.trans_loc_ms_to_loc_mem.set_cke_id = set_cke_id;
    instr->v1.trans_loc_ms_to_loc_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_ms_to_loc_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_ms_to_loc_mem.wait_cke_mask = wait_cke_mask;
}

// 本端MS传输到远端Memory
// locMSId: 本端MSId
// <rmtGSAId, rmtXnId>: 远端Memory地址和Token
// 数据长度: length
// 路径: channelId
void trans_loc_ms_to_rmt_mem_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t loc_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmstormtmem_code);
    instr->v1.trans_loc_ms_to_rmt_mem.rmt_gsa_id = rmt_gsa_id;
    instr->v1.trans_loc_ms_to_rmt_mem.rmt_xn_id = rmt_xn_id;
    instr->v1.trans_loc_ms_to_rmt_mem.loc_ms_id = loc_ms_id;
    instr->v1.trans_loc_ms_to_rmt_mem.length_xn_id = length_xn_id;
    instr->v1.trans_loc_ms_to_rmt_mem.channel_id = channel_id;
    instr->v1.trans_loc_ms_to_rmt_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_ms_to_rmt_mem.length_en = length_en & 0x1;
    instr->v1.trans_loc_ms_to_rmt_mem.set_cke_id = set_cke_id;
    instr->v1.trans_loc_ms_to_rmt_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_ms_to_rmt_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_ms_to_rmt_mem.wait_cke_mask = wait_cke_mask;
}

// 远端MS传输到本端Memory
// <locGSAId, locXnId>: 本端Memory地址和Token
// rmtMSId: 远端MSId
// 数据长度: length
// 路径: channelId
void trans_rmt_ms_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t rmt_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, transrmtmstolocmem_code);
    instr->v1.trans_rmt_ms_to_loc_mem.loc_gsa_id = loc_gsa_id;
    instr->v1.trans_rmt_ms_to_loc_mem.loc_xn_id = loc_xn_id;
    instr->v1.trans_rmt_ms_to_loc_mem.rmt_ms_id = rmt_ms_id;
    instr->v1.trans_rmt_ms_to_loc_mem.length_xn_id = length_xn_id;
    instr->v1.trans_rmt_ms_to_loc_mem.channel_id = channel_id;
    instr->v1.trans_rmt_ms_to_loc_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_rmt_ms_to_loc_mem.length_en = length_en & 0x1;
    instr->v1.trans_rmt_ms_to_loc_mem.set_cke_id = set_cke_id;
    instr->v1.trans_rmt_ms_to_loc_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_rmt_ms_to_loc_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_rmt_ms_to_loc_mem.wait_cke_mask = wait_cke_mask;
}

// 本端MS传输到本端MS
// dstMSId: 本端目的MSId
// srcMSId: 本端源MSId
// 数据长度: length
void trans_loc_ms_to_loc_ms_instr(
    ccu_instr* instr, uint16_t dst_ms_id, uint16_t src_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type,
    uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmstolocms_code);
    instr->v1.trans_loc_ms_to_loc_ms.dst_ms_id = dst_ms_id;
    instr->v1.trans_loc_ms_to_loc_ms.src_ms_id = src_ms_id;
    instr->v1.trans_loc_ms_to_loc_ms.length_xn_id = length_xn_id;
    instr->v1.trans_loc_ms_to_loc_ms.channel_id = channel_id;
    instr->v1.trans_loc_ms_to_loc_ms.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_ms_to_loc_ms.length_en = length_en & 0x1;
    instr->v1.trans_loc_ms_to_loc_ms.set_cke_id = set_cke_id;
    instr->v1.trans_loc_ms_to_loc_ms.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_ms_to_loc_ms.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_ms_to_loc_ms.wait_cke_mask = wait_cke_mask;
}

// 远端MS传输到本端MS
// locMSId: 本端MSId
// rmtMSId: 远端MSId
// 数据长度: length
// 路径: channelId
void trans_rmt_ms_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t rmt_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type,
    uint16_t length_en)
{
    instr->header = instr_header(trans_type, transrmtmstolocms_code);
    instr->v1.trans_rmt_ms_to_loc_ms.loc_ms_id = loc_ms_id;
    instr->v1.trans_rmt_ms_to_loc_ms.rmt_ms_id = rmt_ms_id;
    instr->v1.trans_rmt_ms_to_loc_ms.length_xn_id = length_xn_id;
    instr->v1.trans_rmt_ms_to_loc_ms.channel_id = channel_id;
    instr->v1.trans_rmt_ms_to_loc_ms.clear_type = clear_type & 0x1;
    instr->v1.trans_rmt_ms_to_loc_ms.length_en = length_en & 0x1;
    instr->v1.trans_rmt_ms_to_loc_ms.set_cke_id = set_cke_id;
    instr->v1.trans_rmt_ms_to_loc_ms.set_cke_mask = set_cke_mask;
    instr->v1.trans_rmt_ms_to_loc_ms.wait_cke_id = wait_cke_id;
    instr->v1.trans_rmt_ms_to_loc_ms.wait_cke_mask = wait_cke_mask;
}

void trans_loc_ms_to_rmt_ms_instr(
    ccu_instr* instr, uint16_t rmt_ms_id, uint16_t loc_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_rmt_cke_id, uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmstormtms_code);
    instr->v1.trans_loc_ms_to_rmt_ms.rmt_ms_id = rmt_ms_id;
    instr->v1.trans_loc_ms_to_rmt_ms.loc_ms_id = loc_ms_id;
    instr->v1.trans_loc_ms_to_rmt_ms.length_xn_id = length_xn_id;
    instr->v1.trans_loc_ms_to_rmt_ms.channel_id = channel_id;
    instr->v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_id = set_rmt_cke_id;
    instr->v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_mask = set_rmt_cke_mask;

    instr->v1.trans_loc_ms_to_rmt_ms.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_ms_to_rmt_ms.length_en = length_en & 0x1;
    instr->v1.trans_loc_ms_to_rmt_ms.set_cke_id = set_cke_id;
    instr->v1.trans_loc_ms_to_rmt_ms.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_ms_to_rmt_ms.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_ms_to_rmt_ms.wait_cke_mask = wait_cke_mask;
}

// 远端Memory传输到本端Memory
// <locGSAId, locXnId>: 本端Memory地址和Token
// <rmtGSAId, rmtXnId>: 远端Memory地址和Token
// 数据长度: length
// 路径: channelId
void trans_rmt_mem_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t rmt_gsa_id, uint16_t rmt_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t reduce_data_type, uint16_t reduce_op_code, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en,
    uint16_t reduce_en)
{
    instr->header = instr_header(trans_type, transrmtmemtolocmem_code);
    instr->v1.trans_rmt_mem_to_loc_mem.loc_gsa_id = loc_gsa_id;
    instr->v1.trans_rmt_mem_to_loc_mem.loc_xn_id = loc_xn_id;
    instr->v1.trans_rmt_mem_to_loc_mem.rmt_gsa_id = rmt_gsa_id;
    instr->v1.trans_rmt_mem_to_loc_mem.rmt_xn_id = rmt_xn_id;
    instr->v1.trans_rmt_mem_to_loc_mem.length_xn_id = length_xn_id;
    instr->v1.trans_rmt_mem_to_loc_mem.channel_id = channel_id;
    instr->v1.trans_rmt_mem_to_loc_mem.udf_type = 0;
    instr->v1.trans_rmt_mem_to_loc_mem.reduce_data_type = reduce_data_type & 0xf;
    instr->v1.trans_rmt_mem_to_loc_mem.reduce_op_code = reduce_op_code & 0xf;
    instr->v1.trans_rmt_mem_to_loc_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_rmt_mem_to_loc_mem.length_en = length_en & 0x1;
    instr->v1.trans_rmt_mem_to_loc_mem.reduce_en = reduce_en & 0x1;
    instr->v1.trans_rmt_mem_to_loc_mem.set_cke_id = set_cke_id;
    instr->v1.trans_rmt_mem_to_loc_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_rmt_mem_to_loc_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_rmt_mem_to_loc_mem.wait_cke_mask = wait_cke_mask;
}

// 本端Memory传输到远端Memory
// <rmtGSAId, rmtXnId>: 远端Memory地址和Token
// <locGSAId, locXnId>: 本端Memory地址和Token
// 数据长度: length
// 路径: channelId
void trans_loc_mem_to_rmt_mem_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t loc_gsa_id, uint16_t loc_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t reduce_data_type, uint16_t reduce_op_code, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en,
    uint16_t reduce_en)
{
    instr->header = instr_header(trans_type, translocmemtormtmem_code);
    instr->v1.trans_loc_mem_to_rmt_mem.rmt_gsa_id = rmt_gsa_id;
    instr->v1.trans_loc_mem_to_rmt_mem.rmt_xn_id = rmt_xn_id;
    instr->v1.trans_loc_mem_to_rmt_mem.loc_gsa_id = loc_gsa_id;
    instr->v1.trans_loc_mem_to_rmt_mem.loc_xn_id = loc_xn_id;
    instr->v1.trans_loc_mem_to_rmt_mem.length_xn_id = length_xn_id;
    instr->v1.trans_loc_mem_to_rmt_mem.channel_id = channel_id;
    instr->v1.trans_loc_mem_to_rmt_mem.udf_type = 0;
    instr->v1.trans_loc_mem_to_rmt_mem.reduce_data_type = reduce_data_type & 0xf;
    instr->v1.trans_loc_mem_to_rmt_mem.reduce_op_code = reduce_op_code & 0xf;
    instr->v1.trans_loc_mem_to_rmt_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_mem_to_rmt_mem.length_en = length_en & 0x1;
    instr->v1.trans_loc_mem_to_rmt_mem.reduce_en = reduce_en & 0x1;
    instr->v1.trans_loc_mem_to_rmt_mem.set_cke_id = set_cke_id;
    instr->v1.trans_loc_mem_to_rmt_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_mem_to_rmt_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_mem_to_rmt_mem.wait_cke_mask = wait_cke_mask;
}

void trans_loc_mem_to_loc_mem_instr(
    ccu_instr* instr, uint16_t dst_gsa_id, uint16_t dst_xn_id, uint16_t src_gsa_id, uint16_t src_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id,
    uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en)
{
    instr->header = instr_header(trans_type, translocmemtolocmem_code);
    instr->v1.trans_loc_mem_to_loc_mem.dst_gsa_id = dst_gsa_id;
    instr->v1.trans_loc_mem_to_loc_mem.dst_xn_id = dst_xn_id;
    instr->v1.trans_loc_mem_to_loc_mem.src_gsa_id = src_gsa_id;
    instr->v1.trans_loc_mem_to_loc_mem.src_xn_id = src_xn_id;
    instr->v1.trans_loc_mem_to_loc_mem.length_xn_id = length_xn_id;
    instr->v1.trans_loc_mem_to_loc_mem.channel_id = channel_id;
    instr->v1.trans_loc_mem_to_loc_mem.clear_type = clear_type & 0x1;
    instr->v1.trans_loc_mem_to_loc_mem.length_en = length_en & 0x1;
    instr->v1.trans_loc_mem_to_loc_mem.set_cke_id = set_cke_id;
    instr->v1.trans_loc_mem_to_loc_mem.set_cke_mask = set_cke_mask;
    instr->v1.trans_loc_mem_to_loc_mem.wait_cke_id = wait_cke_id;
    instr->v1.trans_loc_mem_to_loc_mem.wait_cke_mask = wait_cke_mask;
}

// 本端CKE同步到远端CKE
// rmtCKEId: 远端CKEId
// locCKEId: 本端CKEId
// locCKEMask: mask
// 路径: channelId
void sync_cke_instr(
    ccu_instr* instr, uint16_t rmt_cke_id, uint16_t loc_cke_id, uint16_t loc_cke_mask, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type)
{
    instr->header = instr_header(trans_type, synccke_code);
    instr->v1.sync_cke.rmt_cke_id = rmt_cke_id;
    instr->v1.sync_cke.loc_cke_id = loc_cke_id;
    instr->v1.sync_cke.loc_cke_mask = loc_cke_mask;
    instr->v1.sync_cke.channel_id = channel_id;
    instr->v1.sync_cke.clear_type = clear_type & 0x1;
    instr->v1.sync_cke.set_cke_id = set_cke_id;
    instr->v1.sync_cke.set_cke_mask = set_cke_mask;
    instr->v1.sync_cke.wait_cke_id = wait_cke_id;
    instr->v1.sync_cke.wait_cke_mask = wait_cke_mask;
}

// 本端<locGSAId>同步到远端<rmtGSAId> 同步完成后, 置位远端的<setRmtCKEId, setRmtCKEMask>
void sync_gsa_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t loc_gsa_id, uint16_t channel_id, uint16_t set_rmt_cke_id,
    uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(trans_type, syncgsa_code);
    instr->v1.sync_gsa.rmt_gsa_id = rmt_gsa_id;
    instr->v1.sync_gsa.loc_gsa_id = loc_gsa_id;
    instr->v1.sync_gsa.channel_id = channel_id;
    instr->v1.sync_gsa.set_rmt_cke_id = set_rmt_cke_id;
    instr->v1.sync_gsa.set_rmt_cke_mask = set_rmt_cke_mask;
    instr->v1.sync_gsa.clear_type = clear_type & 0x1;
    instr->v1.sync_gsa.set_cke_id = set_cke_id;
    instr->v1.sync_gsa.set_cke_mask = set_cke_mask;
    instr->v1.sync_gsa.wait_cke_id = wait_cke_id;
    instr->v1.sync_gsa.wait_cke_mask = wait_cke_mask;
}

// 本端<locInputXnId, locOutputXnId>同步到远端<rmtInputXnId, rmtOutputXnId> 同步完成后, 置位远端的<setRmtCKEId,
// setRmtCKEMask>
void sync_xn_instr(
    ccu_instr* instr, uint16_t rmt_xn_id, uint16_t loc_xn_id, uint16_t channel_id, uint16_t set_rmt_cke_id,
    uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type)
{
    instr->header = instr_header(trans_type, syncxn_code);
    instr->v1.sync_xn.rmt_xn_id = rmt_xn_id;
    instr->v1.sync_xn.loc_xn_id = loc_xn_id;
    instr->v1.sync_xn.channel_id = channel_id;
    instr->v1.sync_xn.set_rmt_cke_id = set_rmt_cke_id;
    instr->v1.sync_xn.set_rmt_cke_mask = set_rmt_cke_mask;
    instr->v1.sync_xn.clear_type = clear_type & 0x1;
    instr->v1.sync_xn.set_cke_id = set_cke_id;
    instr->v1.sync_xn.set_cke_mask = set_cke_mask;
    instr->v1.sync_xn.wait_cke_id = wait_cke_id;
    instr->v1.sync_xn.wait_cke_mask = wait_cke_mask;
}

// MSA~MSH Reduce到 MSA
// count: 参与Reduce的MS数目
// castEn: 输出是否截断
// dataType: Reduce数据类型
void add_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t cast_en, uint16_t data_type, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length)
{
    count -= 2; // CCU指令中指定count数为实际参与运算的MS数减2
    instr->header = instr_header(reduce_type, add_code);
    for (uint16_t index = 0; index < ccu_reduce_max_ms; index++) {
        instr->v1.add.ms_id[index] = ms_id[index];
    }
    instr->v1.add.xn_id_length = xn_id_length;
    instr->v1.add.count = count & 0x7;
    instr->v1.add.cast_en = cast_en & 0x3;
    instr->v1.add.data_type = data_type & 0x1f;
    instr->v1.add.clear_type = clear_type & 0x1;
    instr->v1.add.set_cke_id = set_cke_id;
    instr->v1.add.set_cke_mask = set_cke_mask;
    instr->v1.add.wait_cke_id = wait_cke_id;
    instr->v1.add.wait_cke_mask = wait_cke_mask;
}

void max_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length)
{
    count -= 2; // CCU指令中指定count数为实际参与运算的MS数减2
    instr->header = instr_header(reduce_type, max_code);
    for (uint16_t index = 0; index < ccu_reduce_max_ms; index++) {
        instr->v1.add.ms_id[index] = ms_id[index];
    }
    instr->v1.max.xn_id_length = xn_id_length;
    instr->v1.max.count = count & 0x7;
    instr->v1.max.data_type = data_type & 0x1f;
    instr->v1.max.clear_type = clear_type & 0x1;
    instr->v1.max.set_cke_id = set_cke_id;
    instr->v1.max.set_cke_mask = set_cke_mask;
    instr->v1.max.wait_cke_id = wait_cke_id;
    instr->v1.max.wait_cke_mask = wait_cke_mask;
}

void min_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length)
{
    count -= 2; // CCU指令中指定count数为实际参与运算的MS数减2
    instr->header = instr_header(reduce_type, min_code);
    for (uint16_t index = 0; index < ccu_reduce_max_ms; index++) {
        instr->v1.add.ms_id[index] = ms_id[index];
    }
    instr->v1.max.xn_id_length = xn_id_length;
    instr->v1.min.count = count & 0x7;
    instr->v1.min.data_type = data_type & 0x1f;
    instr->v1.min.clear_type = clear_type & 0x1;
    instr->v1.min.set_cke_id = set_cke_id;
    instr->v1.min.set_cke_mask = set_cke_mask;
    instr->v1.min.wait_cke_id = wait_cke_id;
    instr->v1.min.wait_cke_mask = wait_cke_mask;
}

std::string parse_load_sqe_args_to_gsa_instr(const ccu_instr* instr)
{
    uint16_t gsa_id = instr->v1.load_sqe_args_to_gsa.gsa_id;
    uint16_t sqe_args_id = instr->v1.load_sqe_args_to_gsa.sqe_args_id;

    return asc::format_ccu_message("Load SqeArg[%u] to GSA[%u]", sqe_args_id, gsa_id);
}

static std::string parse_load_sqe_args_to_xn_instr(const ccu_instr* instr)
{
    uint16_t xn_id = instr->v1.load_sqe_args_to_xn.xn_id;
    uint16_t sqe_args_id = instr->v1.load_sqe_args_to_xn.sqe_args_id;

    return asc::format_ccu_message("Load SqeArg[%u] to Xn[%u]", sqe_args_id, xn_id);
}

static std::string parse_load_imd_to_gsa_instr(const ccu_instr* instr)
{
    uint16_t gsa_id = instr->v1.load_imd_to_gsa.gsa_id;
    uint64_t immediate = instr->v1.load_imd_to_gsa.immediate;

    return asc::format_ccu_message("Load immediate[%llu] to GSA[%u]", immediate, gsa_id);
}

static std::string parse_load_imd_to_xn_instr(const ccu_instr* instr)
{
    uint16_t xn_id = instr->v1.load_imd_to_xn.xn_id;
    uint64_t immediate = instr->v1.load_imd_to_xn.immediate;

    if (instr->v1.load_imd_to_xn.sec_flag == ccu_load_to_xn_sec_info) {
        return asc::format_ccu_message("Load immediate[tokenInfo] to Xn[%u]", xn_id);
    }

    return asc::format_ccu_message("Load immediate[%llu] to Xn[%u]", immediate, xn_id);
}

static std::string parse_load_gsa_xn_instr(const ccu_instr* instr)
{
    uint16_t gs_ad_id = instr->v1.load_gsa_xn.gs_ad_id;
    uint16_t gs_am_id = instr->v1.load_gsa_xn.gs_am_id;
    uint16_t xn_id = instr->v1.load_gsa_xn.xn_id;

    return asc::format_ccu_message("Load GSA[%u] + Xn[%u] to GSA[%u]", gs_am_id, xn_id, gs_ad_id);
}

static std::string parse_load_gsagsa_instr(const ccu_instr* instr)
{
    uint16_t gs_ad_id = instr->v1.load_gsagsa.gs_ad_id;
    uint16_t gs_am_id = instr->v1.load_gsagsa.gs_am_id;
    uint16_t gs_an_id = instr->v1.load_gsagsa.gs_an_id;

    return asc::format_ccu_message("Load GSA[%u] + GSA[%u] to GSA[%u]", gs_am_id, gs_an_id, gs_ad_id);
}

static std::string parse_load_xx_instr(const ccu_instr* instr)
{
    uint16_t xd_id = instr->v1.load_xx.xd_id;
    uint16_t xm_id = instr->v1.load_xx.xm_id;
    uint16_t xn_id = instr->v1.load_xx.xn_id;

    return asc::format_ccu_message("Load Xn[%u] + Xn[%u] to Xn[%u]", xm_id, xn_id, xd_id);
}

static std::string parse_loop_instr(const ccu_instr* instr)
{
    uint16_t start_instr_id = instr->v1.loop.start_instr_id;
    uint16_t end_instr_id = instr->v1.loop.end_instr_id;
    uint16_t xn_id = instr->v1.loop.xn_id;

    return asc::format_ccu_message(
        "Loop From startInstrId[%u] to endInstrId[%u] with loopXn[%u]", start_instr_id, end_instr_id, xn_id);
}

static std::string parse_loop_group_instr(const ccu_instr* instr)
{
    uint16_t start_loop_instr_id = instr->v1.loop_group.start_loop_instr_id;
    uint16_t xn_id = instr->v1.loop_group.xn_id;
    uint16_t xm_id = instr->v1.loop_group.xm_id;
    uint16_t high_perf_mode_en = instr->v1.loop_group.high_perf_mode_en;

    return asc::format_ccu_message(
        "LoopGroup From startLoopInstrId[%u] with loopGroupXn[%u], offsetXn[%u] and highPerfModeEn[%u]",
        start_loop_instr_id, xn_id, xm_id, high_perf_mode_en);
}

static std::string parse_set_cke_instr(const ccu_instr* instr)
{
    uint16_t clear_type = instr->v1.set_cke.clear_type;
    uint16_t set_cke_id = instr->v1.set_cke.set_cke_id;
    uint16_t set_cke_mask = instr->v1.set_cke.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.set_cke.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.set_cke.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Set CKE[%u:%04x], clearType[%u]", wait_cke_id, wait_cke_mask, set_cke_id, set_cke_mask,
        clear_type);
}

static std::string parse_clear_cke_instr(const ccu_instr* instr)
{
    uint16_t clear_type = instr->v1.clear_cke.clear_type;
    uint16_t clear_cke_id = instr->v1.clear_cke.clear_cke_id;
    uint16_t clear_mask = instr->v1.clear_cke.clear_mask;
    uint16_t wait_cke_id = instr->v1.clear_cke.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.clear_cke.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Clear CKE[%u:%04x], clearType[%u]", wait_cke_id, wait_cke_mask, clear_cke_id, clear_mask,
        clear_type);
}

static std::string parse_jump_instr(const ccu_instr* instr)
{
    uint16_t dst_instr_xn_id = instr->v1.jmp.dst_instr_xn_id;
    uint16_t condition_xn_id = instr->v1.jmp.condition_xn_id;
    uint64_t expect_data = instr->v1.jmp.expect_data;

    return asc::format_ccu_message(
        "When conditionXn[%u] not equal to expectData[%llu], Jump To InstrIdXn[%u]", condition_xn_id, expect_data,
        dst_instr_xn_id);
}

static std::string parse_trans_loc_mem_to_loc_ms_instr(const ccu_instr* instr)
{
    uint16_t loc_ms_id = instr->v1.trans_loc_mem_to_loc_ms.loc_ms_id;
    uint16_t loc_gsa_id = instr->v1.trans_loc_mem_to_loc_ms.loc_gsa_id;
    uint16_t loc_xn_id = instr->v1.trans_loc_mem_to_loc_ms.loc_xn_id;
    uint16_t length_xn_id = instr->v1.trans_loc_mem_to_loc_ms.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_mem_to_loc_ms.channel_id;
    uint16_t clear_type = instr->v1.trans_loc_mem_to_loc_ms.clear_type;
    uint16_t length_en = instr->v1.trans_loc_mem_to_loc_ms.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_mem_to_loc_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_mem_to_loc_ms.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_mem_to_loc_ms.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_mem_to_loc_ms.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMem[%u:%u] To LocMS[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, loc_gsa_id, loc_xn_id, loc_ms_id / 0x8000, loc_ms_id % 0x8000, length_xn_id,
        channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_rmt_mem_to_loc_ms_instr(const ccu_instr* instr)
{
    uint16_t loc_ms_id = instr->v1.trans_rmt_mem_to_loc_ms.loc_ms_id;
    uint16_t rmt_gsa_id = instr->v1.trans_rmt_mem_to_loc_ms.rmt_gsa_id;
    uint16_t rmt_xn_id = instr->v1.trans_rmt_mem_to_loc_ms.rmt_xn_id;
    uint16_t length_xn_id = instr->v1.trans_rmt_mem_to_loc_ms.length_xn_id;
    uint16_t channel_id = instr->v1.trans_rmt_mem_to_loc_ms.channel_id;
    uint16_t clear_type = instr->v1.trans_rmt_mem_to_loc_ms.clear_type;
    uint16_t length_en = instr->v1.trans_rmt_mem_to_loc_ms.length_en;
    uint16_t set_cke_id = instr->v1.trans_rmt_mem_to_loc_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_rmt_mem_to_loc_ms.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_rmt_mem_to_loc_ms.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_rmt_mem_to_loc_ms.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans RmtMem[%u:%u] To LocMS[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, rmt_gsa_id, rmt_xn_id, loc_ms_id / 0x8000, loc_ms_id % 0x8000, length_xn_id,
        channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_loc_ms_to_loc_mem_instr(const ccu_instr* instr)
{
    uint16_t loc_gsa_id = instr->v1.trans_loc_ms_to_loc_mem.loc_gsa_id;
    uint16_t loc_xn_id = instr->v1.trans_loc_ms_to_loc_mem.loc_xn_id;
    uint16_t loc_ms_id = instr->v1.trans_loc_ms_to_loc_mem.loc_ms_id;
    uint16_t length_xn_id = instr->v1.trans_loc_ms_to_loc_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_ms_to_loc_mem.channel_id;
    uint16_t clear_type = instr->v1.trans_loc_ms_to_loc_mem.clear_type;
    uint16_t length_en = instr->v1.trans_loc_ms_to_loc_mem.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_ms_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_ms_to_loc_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_ms_to_loc_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_ms_to_loc_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMS[%u:%u] To LocMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, loc_ms_id / 0x8000, loc_ms_id % 0x8000, loc_gsa_id, loc_xn_id, length_xn_id,
        channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_loc_ms_to_rmt_mem_instr(const ccu_instr* instr)
{
    uint16_t rmt_gsa_id = instr->v1.trans_loc_ms_to_rmt_mem.rmt_gsa_id;
    uint16_t rmt_xn_id = instr->v1.trans_loc_ms_to_rmt_mem.rmt_xn_id;
    uint16_t loc_ms_id = instr->v1.trans_loc_ms_to_rmt_mem.loc_ms_id;
    uint16_t length_xn_id = instr->v1.trans_loc_ms_to_rmt_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_ms_to_rmt_mem.channel_id;
    uint16_t clear_type = instr->v1.trans_loc_ms_to_rmt_mem.clear_type;
    uint16_t length_en = instr->v1.trans_loc_ms_to_rmt_mem.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_ms_to_rmt_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_ms_to_rmt_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_ms_to_rmt_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_ms_to_rmt_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMS[%u:%u] To RmtMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, loc_ms_id / 0x8000, loc_ms_id % 0x8000, rmt_gsa_id, rmt_xn_id, length_xn_id,
        channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_rmt_ms_to_loc_mem_instr(const ccu_instr* instr)
{
    uint16_t loc_gsa_id = instr->v1.trans_rmt_ms_to_loc_mem.loc_gsa_id;
    uint16_t loc_xn_id = instr->v1.trans_rmt_ms_to_loc_mem.loc_xn_id;
    uint16_t rmt_ms_id = instr->v1.trans_rmt_ms_to_loc_mem.rmt_ms_id;
    uint16_t length_xn_id = instr->v1.trans_rmt_ms_to_loc_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_rmt_ms_to_loc_mem.channel_id;
    uint16_t clear_type = instr->v1.trans_rmt_ms_to_loc_mem.clear_type;
    uint16_t length_en = instr->v1.trans_rmt_ms_to_loc_mem.length_en;
    uint16_t set_cke_id = instr->v1.trans_rmt_ms_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_rmt_ms_to_loc_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_rmt_ms_to_loc_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_rmt_ms_to_loc_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans RmtMS[%u:%u] To LocMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, rmt_ms_id / 0x8000, rmt_ms_id % 0x8000, loc_gsa_id, loc_xn_id, length_xn_id,
        channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_loc_ms_to_loc_ms_instr(const ccu_instr* instr)
{
    uint16_t dst_ms_id = instr->v1.trans_loc_ms_to_loc_ms.dst_ms_id;
    uint16_t src_ms_id = instr->v1.trans_loc_ms_to_loc_ms.src_ms_id;
    uint16_t length_xn_id = instr->v1.trans_loc_ms_to_loc_ms.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_ms_to_loc_ms.channel_id;
    uint16_t clear_type = instr->v1.trans_loc_ms_to_loc_ms.clear_type;
    uint16_t length_en = instr->v1.trans_loc_ms_to_loc_ms.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_ms_to_loc_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_ms_to_loc_ms.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_ms_to_loc_ms.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_ms_to_loc_ms.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMS[%u:%u] To LocMS[%u:%u] With LengthXn[%u] Use Channel[%u], "
        "Set CKE[%u:%04x], "
        "clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, src_ms_id / 0x8000, src_ms_id % 0x8000, dst_ms_id / 0x8000, dst_ms_id % 0x8000,
        length_xn_id, channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_rmt_ms_to_loc_ms_instr(const ccu_instr* instr)
{
    uint16_t loc_ms_id = instr->v1.trans_rmt_ms_to_loc_ms.loc_ms_id;
    uint16_t rmt_ms_id = instr->v1.trans_rmt_ms_to_loc_ms.rmt_ms_id;
    uint16_t length_xn_id = instr->v1.trans_rmt_ms_to_loc_ms.length_xn_id;
    uint16_t channel_id = instr->v1.trans_rmt_ms_to_loc_ms.channel_id;
    uint16_t clear_type = instr->v1.trans_rmt_ms_to_loc_ms.clear_type;
    uint16_t length_en = instr->v1.trans_rmt_ms_to_loc_ms.length_en;
    uint16_t set_cke_id = instr->v1.trans_rmt_ms_to_loc_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_rmt_ms_to_loc_ms.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_rmt_ms_to_loc_ms.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_rmt_ms_to_loc_ms.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans RmtMS[%u:%u] To LocMS[%u:%u] With LengthXn[%u] Use Channel[%u], "
        "Set CKE[%u:%04x], "
        "clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, rmt_ms_id / 0x8000, rmt_ms_id % 0x8000, loc_ms_id / 0x8000, loc_ms_id % 0x8000,
        length_xn_id, channel_id, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_loc_ms_to_rmt_ms_instr(const ccu_instr* instr)
{
    uint16_t rmt_ms_id = instr->v1.trans_loc_ms_to_rmt_ms.rmt_ms_id;
    uint16_t loc_ms_id = instr->v1.trans_loc_ms_to_rmt_ms.loc_ms_id;
    uint16_t length_xn_id = instr->v1.trans_loc_ms_to_rmt_ms.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_ms_to_rmt_ms.channel_id;
    uint16_t set_rmt_cke_id = instr->v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_id;
    uint16_t set_rmt_cke_mask = instr->v1.trans_loc_ms_to_rmt_ms.set_rmt_cke_mask;

    uint16_t clear_type = instr->v1.trans_loc_ms_to_rmt_ms.clear_type;
    uint16_t length_en = instr->v1.trans_loc_ms_to_rmt_ms.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_ms_to_rmt_ms.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_ms_to_rmt_ms.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_ms_to_rmt_ms.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_ms_to_rmt_ms.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMS[%u:%u] To RmtMS[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "RmtCKE[%u:%04x], Set CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, loc_ms_id / 0x8000, loc_ms_id % 0x8000, rmt_ms_id / 0x8000, rmt_ms_id % 0x8000,
        length_xn_id, channel_id, set_rmt_cke_id, set_rmt_cke_mask, set_cke_id, set_cke_mask, clear_type, length_en);
}

static std::string parse_trans_rmt_mem_to_loc_mem_instr(const ccu_instr* instr)
{
    uint16_t loc_gsa_id = instr->v1.trans_rmt_mem_to_loc_mem.loc_gsa_id;
    uint16_t loc_xn_id = instr->v1.trans_rmt_mem_to_loc_mem.loc_xn_id;
    uint16_t rmt_gsa_id = instr->v1.trans_rmt_mem_to_loc_mem.rmt_gsa_id;
    uint16_t rmt_xn_id = instr->v1.trans_rmt_mem_to_loc_mem.rmt_xn_id;
    uint16_t length_xn_id = instr->v1.trans_rmt_mem_to_loc_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_rmt_mem_to_loc_mem.channel_id;
    uint16_t reduce_data_type = instr->v1.trans_rmt_mem_to_loc_mem.reduce_data_type;
    uint16_t reduce_op_code = instr->v1.trans_rmt_mem_to_loc_mem.reduce_op_code;
    uint16_t clear_type = instr->v1.trans_rmt_mem_to_loc_mem.clear_type;
    uint16_t length_en = instr->v1.trans_rmt_mem_to_loc_mem.length_en;
    uint16_t reduce_en = instr->v1.trans_rmt_mem_to_loc_mem.reduce_en;
    uint16_t set_cke_id = instr->v1.trans_rmt_mem_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_rmt_mem_to_loc_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_rmt_mem_to_loc_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_rmt_mem_to_loc_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans RmtMem[%u:%u] To LocMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u], DataType[%u], ReduceType[%u] reduceEn[%u]",
        wait_cke_id, wait_cke_mask, rmt_gsa_id, rmt_xn_id, loc_gsa_id, loc_xn_id, length_xn_id, channel_id, set_cke_id,
        set_cke_mask, clear_type, length_en, reduce_data_type, reduce_op_code, reduce_en);
}

static std::string parse_trans_loc_mem_to_rmt_mem_instr(const ccu_instr* instr)
{
    uint16_t rmt_gsa_id = instr->v1.trans_loc_mem_to_rmt_mem.rmt_gsa_id;
    uint16_t rmt_xn_id = instr->v1.trans_loc_mem_to_rmt_mem.rmt_xn_id;
    uint16_t loc_gsa_id = instr->v1.trans_loc_mem_to_rmt_mem.loc_gsa_id;
    uint16_t loc_xn_id = instr->v1.trans_loc_mem_to_rmt_mem.loc_xn_id;
    uint16_t length_xn_id = instr->v1.trans_loc_mem_to_rmt_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_mem_to_rmt_mem.channel_id;
    uint16_t reduce_data_type = instr->v1.trans_loc_mem_to_rmt_mem.reduce_data_type;
    uint16_t reduce_op_code = instr->v1.trans_loc_mem_to_rmt_mem.reduce_op_code;
    uint16_t clear_type = instr->v1.trans_loc_mem_to_rmt_mem.clear_type;
    uint16_t length_en = instr->v1.trans_loc_mem_to_rmt_mem.length_en;
    uint16_t reduce_en = instr->v1.trans_loc_mem_to_rmt_mem.reduce_en;
    uint16_t set_cke_id = instr->v1.trans_loc_mem_to_rmt_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_mem_to_rmt_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_mem_to_rmt_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_mem_to_rmt_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMem[%u:%u] To RmtMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u], DataType[%u], ReduceType[%u] reduceEn[%u]",
        wait_cke_id, wait_cke_mask, loc_gsa_id, loc_xn_id, rmt_gsa_id, rmt_xn_id, length_xn_id, channel_id, set_cke_id,
        set_cke_mask, clear_type, length_en, reduce_data_type, reduce_op_code, reduce_en);
}

static std::string parse_trans_loc_mem_to_loc_mem_instr(const ccu_instr* instr)
{
    uint16_t dst_gsa_id = instr->v1.trans_loc_mem_to_loc_mem.dst_gsa_id;
    uint16_t dst_xn_id = instr->v1.trans_loc_mem_to_loc_mem.dst_xn_id;
    uint16_t src_gsa_id = instr->v1.trans_loc_mem_to_loc_mem.src_gsa_id;
    uint16_t src_xn_id = instr->v1.trans_loc_mem_to_loc_mem.src_xn_id;
    uint16_t length_xn_id = instr->v1.trans_loc_mem_to_loc_mem.length_xn_id;
    uint16_t channel_id = instr->v1.trans_loc_mem_to_loc_mem.channel_id;
    uint16_t clear_type = instr->v1.trans_loc_mem_to_loc_mem.clear_type;
    uint16_t length_en = instr->v1.trans_loc_mem_to_loc_mem.length_en;
    uint16_t set_cke_id = instr->v1.trans_loc_mem_to_loc_mem.set_cke_id;
    uint16_t set_cke_mask = instr->v1.trans_loc_mem_to_loc_mem.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.trans_loc_mem_to_loc_mem.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.trans_loc_mem_to_loc_mem.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Trans LocMem[%u:%u] To LocMem[%u:%u] With LengthXn[%u] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u], lengthEn[%u]",
        wait_cke_id, wait_cke_mask, src_gsa_id, src_xn_id, dst_gsa_id, dst_xn_id, length_xn_id, channel_id, set_cke_id,
        set_cke_mask, clear_type, length_en);
}

static std::string parse_sync_cke_instr(const ccu_instr* instr)
{
    uint16_t rmt_cke_id = instr->v1.sync_cke.rmt_cke_id;
    uint16_t loc_cke_id = instr->v1.sync_cke.loc_cke_id;
    uint16_t loc_cke_mask = instr->v1.sync_cke.loc_cke_mask;
    uint16_t channel_id = instr->v1.sync_cke.channel_id;
    uint16_t clear_type = instr->v1.sync_cke.clear_type;
    uint16_t set_cke_id = instr->v1.sync_cke.set_cke_id;
    uint16_t set_cke_mask = instr->v1.sync_cke.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.sync_cke.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.sync_cke.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Sync LocCKE[%u:%04x] To rmtCKE[%u:%04x] Use Channel[%u], Set "
        "CKE[%u:%04x], clearType[%u]",
        wait_cke_id, wait_cke_mask, loc_cke_id, loc_cke_mask, rmt_cke_id, loc_cke_mask, channel_id, set_cke_id,
        set_cke_mask, clear_type);
}

static std::string parse_sync_gsa_instr(const ccu_instr* instr)
{
    uint16_t rmt_gsa_id = instr->v1.sync_gsa.rmt_gsa_id;
    uint16_t loc_gsa_id = instr->v1.sync_gsa.loc_gsa_id;
    uint16_t channel_id = instr->v1.sync_gsa.channel_id;
    uint16_t set_rmt_cke_id = instr->v1.sync_gsa.set_rmt_cke_id;
    uint16_t set_rmt_cke_mask = instr->v1.sync_gsa.set_rmt_cke_mask;
    uint16_t clear_type = instr->v1.sync_gsa.clear_type;
    uint16_t set_cke_id = instr->v1.sync_gsa.set_cke_id;
    uint16_t set_cke_mask = instr->v1.sync_gsa.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.sync_gsa.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.sync_gsa.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Sync locGSAId[%u] To rmtGSAId[%u] Use Channel[%u], Set rmtCKE[%u:%04x], Set "
        "CKE[%u:%04x], clearType[%u]",
        wait_cke_id, wait_cke_mask, loc_gsa_id, rmt_gsa_id, channel_id, set_rmt_cke_id, set_rmt_cke_mask, set_cke_id,
        set_cke_mask, clear_type);
}

static std::string parse_sync_xn_instr(const ccu_instr* instr)
{
    uint16_t rmt_xn_id = instr->v1.sync_xn.rmt_xn_id;
    uint16_t loc_xn_id = instr->v1.sync_xn.loc_xn_id;
    uint16_t channel_id = instr->v1.sync_xn.channel_id;
    uint16_t set_rmt_cke_id = instr->v1.sync_xn.set_rmt_cke_id;
    uint16_t set_rmt_cke_mask = instr->v1.sync_xn.set_rmt_cke_mask;
    uint16_t clear_type = instr->v1.sync_xn.clear_type;
    uint16_t set_cke_id = instr->v1.sync_xn.set_cke_id;
    uint16_t set_cke_mask = instr->v1.sync_xn.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.sync_xn.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.sync_xn.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Sync locXnId[%u] To rmtXnId[%u] Use Channel[%u], Set rmtCKE[%u:%04x], Set "
        "CKE[%u:%04x], clearType[%u]",
        wait_cke_id, wait_cke_mask, loc_xn_id, rmt_xn_id, channel_id, set_rmt_cke_id, set_rmt_cke_mask, set_cke_id,
        set_cke_mask, clear_type);
}

static std::string parse_ms_list(const ccu_instr* instr)
{
    uint16_t ms_id[ccu_reduce_max_ms];
    uint16_t count = instr->v1.add.count;
    for (uint16_t index = 0; index < ccu_reduce_max_ms; index++) {
        ms_id[index] = instr->v1.add.ms_id[index];
    }

    // CCU指令中指定count数为实际参与运算的MS数减2，因此当count +
    // 2大于CCU_REDUCE_MAX_MS时，说明MS列表中的MS数量超过了CCU指令的最大支持数量，此时无法正确解析MS列表，直接返回"MS[]"
    if (count + 2 > ccu_reduce_max_ms) {
        return "MS[]";
    }

    std::string res = "MS[";
    for (uint16_t i = 0; i < count + 2; i++) { // 循环范围 0~count + 2
        if (i == count + 1) {
            res += std::to_string(ms_id[i] / 0x8000) + ":" + std::to_string(ms_id[i] % 0x8000) + "]";
        } else {
            res += std::to_string(ms_id[i] / 0x8000) + ":" + std::to_string(ms_id[i] % 0x8000) + ", ";
        }
    }
    return res;
}

static std::string parse_add_instr(const ccu_instr* instr)
{
    uint16_t count = instr->v1.add.count;
    uint16_t cast_en = instr->v1.add.cast_en;
    uint16_t data_type = instr->v1.add.data_type;
    uint16_t clear_type = instr->v1.add.clear_type;
    uint16_t set_cke_id = instr->v1.add.set_cke_id;
    uint16_t set_cke_mask = instr->v1.add.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.add.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.add.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Add %s with Count[%u], DataType[%u] and CastEn[%u], Set CKE[%u:%04x], clearType[%u]",
        wait_cke_id, wait_cke_mask, parse_ms_list(instr).c_str(), count, data_type, cast_en, set_cke_id, set_cke_mask,
        clear_type);
}

static std::string parse_max_instr(const ccu_instr* instr)
{
    uint16_t count = instr->v1.max.count;
    uint16_t data_type = instr->v1.max.data_type;
    uint16_t clear_type = instr->v1.max.clear_type;
    uint16_t set_cke_id = instr->v1.max.set_cke_id;
    uint16_t set_cke_mask = instr->v1.max.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.max.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.max.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Max %s with Count[%u], DataType[%u], Set CKE[%u:%04x], clearType[%u]", wait_cke_id,
        wait_cke_mask, parse_ms_list(instr).c_str(), count, data_type, set_cke_id, set_cke_mask, clear_type);
}

static std::string parse_min_instr(const ccu_instr* instr)
{
    uint16_t count = instr->v1.min.count;
    uint16_t data_type = instr->v1.min.data_type;
    uint16_t clear_type = instr->v1.min.clear_type;
    uint16_t set_cke_id = instr->v1.min.set_cke_id;
    uint16_t set_cke_mask = instr->v1.min.set_cke_mask;
    uint16_t wait_cke_id = instr->v1.min.wait_cke_id;
    uint16_t wait_cke_mask = instr->v1.min.wait_cke_mask;

    return asc::format_ccu_message(
        "Wait CKE[%u:%04x], Min %s with Count[%u], DataType[%u], Set CKE[%u:%04x], clearType[%u]", wait_cke_id,
        wait_cke_mask, parse_ms_list(instr).c_str(), count, data_type, set_cke_id, set_cke_mask, clear_type);
}

using parse_instr_func = std::string (*)(const ccu_instr*);

static std::unordered_map<uint16_t, parse_instr_func> g_parse_instr_sqe_map = {
    {instr_header(load_type, loadsqeargstogsa_code).header, &parse_load_sqe_args_to_gsa_instr},
    {instr_header(load_type, loadsqeargstoxn_code).header, &parse_load_sqe_args_to_xn_instr},
    {instr_header(load_type, loadimdtogsa_code).header, &parse_load_imd_to_gsa_instr},
    {instr_header(load_type, loadimdtoxn_code).header, &parse_load_imd_to_xn_instr},
    {instr_header(load_type, loadgsaxn_code).header, &parse_load_gsa_xn_instr},
    {instr_header(load_type, loadgsagsa_code).header, &parse_load_gsagsa_instr},
    {instr_header(load_type, loadxx_code).header, &parse_load_xx_instr},
    {instr_header(ctrl_type, loop_code).header, &parse_loop_instr},
    {instr_header(ctrl_type, loopgroup_code).header, &parse_loop_group_instr},
    {instr_header(ctrl_type, setcke_code).header, &parse_set_cke_instr},
    {instr_header(ctrl_type, clearcke_code).header, &parse_clear_cke_instr},
    {instr_header(ctrl_type, jmp_code).header, &parse_jump_instr},
    {instr_header(trans_type, translocmemtolocms_code).header, &parse_trans_loc_mem_to_loc_ms_instr},
    {instr_header(trans_type, transrmtmemtolocms_code).header, &parse_trans_rmt_mem_to_loc_ms_instr},
    {instr_header(trans_type, translocmstolocmem_code).header, &parse_trans_loc_ms_to_loc_mem_instr},
    {instr_header(trans_type, translocmstormtmem_code).header, &parse_trans_loc_ms_to_rmt_mem_instr},
    {instr_header(trans_type, transrmtmstolocmem_code).header, &parse_trans_rmt_ms_to_loc_mem_instr},
    {instr_header(trans_type, translocmstolocms_code).header, &parse_trans_loc_ms_to_loc_ms_instr},
    {instr_header(trans_type, transrmtmstolocms_code).header, &parse_trans_rmt_ms_to_loc_ms_instr},
    {instr_header(trans_type, translocmstormtms_code).header, &parse_trans_loc_ms_to_rmt_ms_instr},
    {instr_header(trans_type, transrmtmemtolocmem_code).header, &parse_trans_rmt_mem_to_loc_mem_instr},
    {instr_header(trans_type, translocmemtormtmem_code).header, &parse_trans_loc_mem_to_rmt_mem_instr},
    {instr_header(trans_type, translocmemtolocmem_code).header, &parse_trans_loc_mem_to_loc_mem_instr},
    {instr_header(trans_type, synccke_code).header, &parse_sync_cke_instr},
    {instr_header(trans_type, syncgsa_code).header, &parse_sync_gsa_instr},
    {instr_header(trans_type, syncxn_code).header, &parse_sync_xn_instr},
    {instr_header(reduce_type, add_code).header, &parse_add_instr},
    {instr_header(reduce_type, max_code).header, &parse_max_instr},
    {instr_header(reduce_type, min_code).header, &parse_min_instr},
};

std::string parse_instr(const ccu_instr* instr)
{
    if (g_parse_instr_sqe_map.find(instr->header.header) == g_parse_instr_sqe_map.end()) {
        return asc::format_ccu_message("Unsupported instruction with header: 0x%04x", instr->header.header);
    }
    return g_parse_instr_sqe_map[instr->header.header](instr);
}

}; // namespace ccu_rep
}; // namespace asc
