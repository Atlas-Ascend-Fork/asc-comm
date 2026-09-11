/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef CCU_MICROCODE_H
#define CCU_MICROCODE_H

#include <cstdint>
#include <string>
#include <unordered_map>
namespace asc {
namespace ccu_rep {

constexpr uint16_t ccu_reduce_sum = 0;
constexpr uint16_t ccu_reduce_max = 1;
constexpr uint16_t ccu_reduce_min = 2;

constexpr uint16_t ccu_reduce_min_ms = 2;
constexpr uint16_t ccu_reduce_max_ms = 8;

constexpr uint64_t ccu_ms_size = 4096;
constexpr uint64_t ccu_ms_interleave = 8;
constexpr uint64_t ccu_ms_default_loop_count = 128;

constexpr uint16_t ccu_load_to_xn_sec_info = 1;

// V2 CKE 写后读需要的最坏延迟，调度补 NOP 与指令空间预留必须共用该值。
constexpr uint32_t ccu_cke_raw_latency = 14;

#pragma pack(push, 1)
// instr common header
union ccu_instr_header {
    struct {
        uint16_t code : 11;
        uint16_t type : 4;
        uint16_t reserved : 1;
    };

    uint16_t header;
};
#pragma pack(pop)

inline ccu_instr_header instr_header(uint16_t type, uint16_t code)
{
    ccu_instr_header header = {};
    header.type = type;
    header.code = code;
    return header;
}

namespace ccu_v1 {

#pragma pack(push, 1)
// load instruction
struct ccu_instr_load_sqe_args_to_gsa {
    uint16_t gsa_id;
    uint16_t sqe_args_id;
    uint16_t reserved[13];
};

struct ccu_instr_load_sqe_args_to_xn {
    uint16_t xn_id;
    uint16_t sqe_args_id;
    uint16_t reserved[13];
};

struct ccu_instr_load_imd_to_gsa {
    uint16_t gsa_id;
    uint64_t immediate;
    uint16_t reserved[10];
};

struct ccu_instr_load_imd_to_xn {
    uint16_t xn_id;
    uint64_t immediate;
    uint16_t sec_flag;
    uint16_t reserved[9];
};

struct ccu_instr_load_gsa_xn {
    uint16_t gs_ad_id;
    uint16_t gs_am_id;
    uint16_t xn_id;
    uint16_t reserved[12];
};

struct ccu_instr_load_gsagsa {
    uint16_t gs_ad_id;
    uint16_t gs_am_id;
    uint16_t gs_an_id;
    uint16_t reserved[12];
};

struct ccu_instr_load_xx {
    uint16_t xd_id;
    uint16_t xm_id;
    uint16_t xn_id;
    uint16_t reserved[12];
};

// loop control instruction
struct ccu_instr_loop {
    uint16_t start_instr_id; // 开始指令地址Id
    uint16_t end_instr_id;   // 结束指令地址Id
    uint16_t xn_id;          // LoopCtxId[52:45], Offset[44:13], IterNum[12:0]
    uint16_t reserved[12];
};

struct ccu_instr_loop_group {
    uint16_t start_loop_instr_id; // 开始loop地址Id
    uint16_t xn_id;               // LoopNum[61:55], RepeatNum[54:48], NoRepeatNum[47:41]
    uint16_t xm_id;               // gsaOffset[52:21], MSOffset[20:10], ckeOffset[9:0]
    uint16_t high_perf_mode_en : 1;
    uint16_t reserved1 : 15;
    uint16_t reserved[11];
};

struct ccu_instr_set_cke {
    uint16_t clear_type : 1;
    uint16_t reserved1 : 15;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
    uint16_t reserved[10];
};

struct ccu_instr_clear_cke {
    uint16_t clear_type : 1;
    uint16_t reserved1 : 15;
    uint16_t clear_cke_id;
    uint16_t clear_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
    uint16_t reserved[10];
};

struct ccu_instr_jmp {
    uint16_t dst_instr_xn_id;
    uint16_t condition_xn_id;
    uint64_t expect_data;
};

// data transfer instruction
struct ccu_instr_trans_loc_mem_to_loc_ms {
    uint16_t loc_ms_id;
    uint16_t loc_gsa_id;
    uint16_t loc_xn_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[5];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_rmt_mem_to_loc_ms {
    uint16_t loc_ms_id;
    uint16_t rmt_gsa_id;
    uint16_t rmt_xn_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[5];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_loc_mem {
    uint16_t loc_gsa_id;
    uint16_t loc_xn_id;
    uint16_t loc_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[5];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_rmt_mem {
    uint16_t rmt_gsa_id;
    uint16_t rmt_xn_id;
    uint16_t loc_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[5];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_rmt_ms_to_loc_mem {
    uint16_t loc_gsa_id;
    uint16_t loc_xn_id;
    uint16_t rmt_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[5];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_loc_ms {
    uint16_t dst_ms_id;
    uint16_t src_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[6];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_rmt_ms_to_loc_ms {
    uint16_t loc_ms_id;
    uint16_t rmt_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[6];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_rmt_ms {
    uint16_t rmt_ms_id;
    uint16_t loc_ms_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t set_rmt_cke_id;
    uint16_t set_rmt_cke_mask;
    uint16_t reserved1[4];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_rmt_mem_to_loc_mem {
    uint16_t loc_gsa_id;
    uint16_t loc_xn_id;
    uint16_t rmt_gsa_id;
    uint16_t rmt_xn_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t udf_type : 8;
    uint16_t reduce_data_type : 4;
    uint16_t reduce_op_code : 4;
    uint16_t reserved[3];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reduce_en : 1;
    uint16_t reserved1 : 13;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_mem_to_rmt_mem {
    uint16_t rmt_gsa_id;
    uint16_t rmt_xn_id;
    uint16_t loc_gsa_id;
    uint16_t loc_xn_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t udf_type : 8;
    uint16_t reduce_data_type : 4;
    uint16_t reduce_op_code : 4;
    uint16_t reserved[3];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reduce_en : 1;
    uint16_t reserved1 : 13;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_trans_loc_mem_to_loc_mem {
    uint16_t dst_gsa_id;
    uint16_t dst_xn_id;
    uint16_t src_gsa_id;
    uint16_t src_xn_id;
    uint16_t length_xn_id;
    uint16_t channel_id;
    uint16_t reserved1[4];
    uint16_t clear_type : 1;
    uint16_t length_en : 1;
    uint16_t reserved : 14;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_sync_cke {
    uint16_t rmt_cke_id;
    uint16_t loc_cke_id;
    uint16_t loc_cke_mask;
    uint16_t channel_id;
    uint16_t reserved[6];
    uint16_t clear_type : 1;
    uint16_t reserved1 : 15;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_sync_gsa {
    uint16_t rmt_gsa_id;
    uint16_t loc_gsa_id;
    uint16_t reserved2;
    uint16_t channel_id;
    uint16_t set_rmt_cke_id;
    uint16_t set_rmt_cke_mask;
    uint16_t reserved[4];
    uint16_t clear_type : 1;
    uint16_t reserved1 : 15;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_sync_xn {
    uint16_t rmt_xn_id;
    uint16_t loc_xn_id;
    uint16_t reserved2;
    uint16_t channel_id;
    uint16_t set_rmt_cke_id;
    uint16_t set_rmt_cke_mask;
    uint16_t reserved[4];
    uint16_t clear_type : 1;
    uint16_t reserved1 : 15;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_add {
    uint16_t ms_id[ccu_reduce_max_ms];
    uint16_t xn_id_length;
    uint16_t reserved;
    uint16_t clear_type : 1;
    uint16_t count : 3;
    uint16_t cast_en : 2;
    uint16_t reserved1 : 5;
    uint16_t data_type : 5;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_max {
    uint16_t ms_id[ccu_reduce_max_ms];
    uint16_t xn_id_length;
    uint16_t reserved;
    uint16_t clear_type : 1;
    uint16_t count : 3;
    uint16_t reserved1 : 7;
    uint16_t data_type : 5;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};

struct ccu_instr_min {
    uint16_t ms_id[ccu_reduce_max_ms];
    uint16_t xn_id_length;
    uint16_t reserved;
    uint16_t clear_type : 1;
    uint16_t count : 3;
    uint16_t reserved1 : 7;
    uint16_t data_type : 5;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
};
#pragma pack(pop)

union ccu_micro_code_v1 {
    ccu_v1::ccu_instr_load_sqe_args_to_gsa load_sqe_args_to_gsa;
    ccu_v1::ccu_instr_load_sqe_args_to_xn load_sqe_args_to_xn;
    ccu_v1::ccu_instr_load_imd_to_gsa load_imd_to_gsa;
    ccu_v1::ccu_instr_load_imd_to_xn load_imd_to_xn;
    ccu_v1::ccu_instr_load_gsa_xn load_gsa_xn;
    ccu_v1::ccu_instr_load_gsagsa load_gsagsa;
    ccu_v1::ccu_instr_load_xx load_xx;

    ccu_v1::ccu_instr_loop loop;
    ccu_v1::ccu_instr_loop_group loop_group;
    ccu_v1::ccu_instr_set_cke set_cke;
    ccu_v1::ccu_instr_clear_cke clear_cke;
    ccu_v1::ccu_instr_jmp jmp;

    ccu_v1::ccu_instr_trans_loc_mem_to_loc_ms trans_loc_mem_to_loc_ms;
    ccu_v1::ccu_instr_trans_rmt_mem_to_loc_ms trans_rmt_mem_to_loc_ms;
    ccu_v1::ccu_instr_trans_loc_ms_to_loc_mem trans_loc_ms_to_loc_mem;
    ccu_v1::ccu_instr_trans_loc_ms_to_rmt_mem trans_loc_ms_to_rmt_mem;
    ccu_v1::ccu_instr_trans_rmt_ms_to_loc_mem trans_rmt_ms_to_loc_mem;

    ccu_v1::ccu_instr_trans_loc_ms_to_loc_ms trans_loc_ms_to_loc_ms;
    ccu_v1::ccu_instr_trans_rmt_ms_to_loc_ms trans_rmt_ms_to_loc_ms;
    ccu_v1::ccu_instr_trans_loc_ms_to_rmt_ms trans_loc_ms_to_rmt_ms;

    ccu_v1::ccu_instr_trans_rmt_mem_to_loc_mem trans_rmt_mem_to_loc_mem;
    ccu_v1::ccu_instr_trans_loc_mem_to_rmt_mem trans_loc_mem_to_rmt_mem;
    ccu_v1::ccu_instr_trans_loc_mem_to_loc_mem trans_loc_mem_to_loc_mem;

    ccu_v1::ccu_instr_sync_cke sync_cke;
    ccu_v1::ccu_instr_sync_gsa sync_gsa;
    ccu_v1::ccu_instr_sync_xn sync_xn;

    ccu_v1::ccu_instr_add add;
    ccu_v1::ccu_instr_max max;
    ccu_v1::ccu_instr_min min;
};

}; // namespace ccu_v1

namespace ccu_v2 {

struct cache_config {
    uint16_t alloc_hint;
    uint16_t victim_hint;
};

struct trans_mem_notify_info {
    uint16_t xn_id;
    uint16_t xnt_id;
    uint32_t value;
};

struct trans_mem_reduce_info {
    uint16_t udf_type;
    uint16_t reduce_data_type;
    uint16_t reduce_op_code;
};

struct trans_mem_config {
    uint16_t dma_op_code;
    uint16_t order;
    uint16_t fence;
    uint16_t cqe;
    uint16_t nf;
    uint16_t udf_enable;
    uint16_t split_mode;
    uint16_t se;
    uint16_t rmt_jetty_type;
    uint16_t src_mode;
    uint16_t dst_mode;
    uint16_t ms_id_mode;
};

#pragma pack(push, 1)

// load instruction
struct ccu_instr_load_sqe_args_to_x {
    uint16_t xn_id;
    uint16_t sqe_args_id;
    uint16_t reserved[11];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_load_imd_to_x {
    uint16_t xn_id;
    uint64_t immediate;
    uint16_t reserved[8];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_load_store_x {
    uint16_t xd_id;
    uint16_t xs_id;
    uint16_t xso_id;
    uint16_t xdo_id;
    uint16_t o_mode : 1;
    uint16_t reserved : 15;
    uint16_t reserved1[8];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_clear_x {
    uint16_t xn_id : 15;
    uint16_t xn_id_mode : 1;
    uint16_t xm_id : 15;
    uint16_t xm_id_mode : 1;
    uint16_t reserved[11];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_nop {
    uint16_t reserved[15];
};

struct ccu_instr_load {
    uint16_t xd_id;
    uint16_t xs_id;
    uint16_t xst_id;
    uint16_t xl_id;
    uint16_t reserved[6];
    uint16_t dst_type : 4;
    uint16_t alloc_hint : 2;
    uint16_t victim_hint : 2;
    uint16_t reserved1 : 8;
    uint16_t reserved2[2];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_store {
    uint16_t xd_id;
    uint16_t xdt_id;
    uint16_t xs_id;
    uint16_t xl_id;
    uint16_t xh_id;
    uint16_t reserved[5];
    uint16_t src_type : 4;
    uint16_t alloc_hint : 2;
    uint16_t victim_hint : 2;
    uint16_t store_type : 1;
    uint16_t hscb_type : 1;
    uint16_t hscb_broad_cast_dst_type : 6;
    uint16_t reserved1[2];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

// 运算符复用相同的结构体
// 左移右移：需要指定shiftType
// A = B@C类算子，需要指定xd, xn, xm
// 按位非，需要指定xd, xn
// popcnt，需要指定xd, xn
struct ccu_instr_operator {
    uint16_t xd_id;
    uint16_t xn_id;
    uint16_t xm_id;
    uint16_t par_mode : 1;
    uint16_t reserved : 15;
    uint16_t reserved1[6];
    uint16_t shift_type : 1;
    uint16_t reserved2 : 15;
    uint16_t reserved3[2];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

// loop control instruction
struct ccu_instr_loop {
    uint16_t start_instr_id; // 开始指令地址Id
    uint16_t end_instr_id;   // 结束指令地址Id
    uint16_t xm_id;          // IterNum[12:0]
    uint16_t xn_id;          // Offset[31:0]
    uint16_t xp_id;          // LoopCtxId[8:0]
    uint16_t wish_cke_bit;
    uint16_t mode : 1;
    uint16_t reserved : 15;
    uint16_t reserved1[8];
};

struct ccu_instr_loop_group {
    uint16_t start_loop_instr_id; // 开始loop地址Id
    uint16_t xn_id;               // ExtendNum[22:16], RepeatLoopIndex[15:9], LoopNum[8:0]
    uint16_t xm_id;               // gsaOffset[52:21], MSOffset[20:10], ckeOffset[9:0]
    uint16_t xp_id;               // xnOffset[31:0]
    uint16_t reserved[11];
};

struct ccu_instr_set_cke {
    uint16_t clear_type : 1;
    uint16_t reserved : 15;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
    uint64_t user_data;
    uint16_t reserved1[6];
};

struct ccu_instr_clear_cke {
    uint16_t clear_type : 1;
    uint16_t reserved : 15;
    uint16_t clear_cke_id;
    uint16_t clear_mask;
    uint16_t wait_cke_id;
    uint16_t wait_cke_mask;
    uint16_t reserved1[10];
};

struct ccu_instr_jmp {
    uint16_t expected_xn_id;
    uint16_t condition_xn_id;
    uint16_t rel_tar_instr_xn_id;
    uint16_t condition_type : 4;
    uint16_t jump_mode : 1;
    uint16_t reserved : 11;
    uint16_t reserved1[11];
};

struct ccu_instr_wait {
    uint16_t condition_xn_id; // Xn
    uint16_t expected_xn_id;  // Xm
    uint16_t condition_type : 4;
    uint16_t reserved : 12;
    uint16_t reserved1[12];
};

struct ccu_instr_fence {
    uint16_t reserved[15];
};

// data transfer instruction
struct ccu_instr_trans_loc_mem_to_loc_ms {
    uint16_t ms_id;
    uint16_t xs_id;
    uint16_t xst_id;
    uint16_t xl_id;
    uint16_t xo_id;
    uint16_t alloc_hint : 2;
    uint16_t victim_hint : 2;
    uint16_t reserved : 12;
    uint16_t reserved1[7];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_loc_mem {
    uint16_t xd_id;
    uint16_t xdt_id;
    uint16_t ms_id;
    uint16_t xl_id;
    uint16_t xo_id;
    uint16_t alloc_hint : 2;
    uint16_t victim_hint : 2;
    uint16_t reserved : 12;
    uint16_t reserved1[7];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_trans_loc_ms_to_loc_ms {
    uint16_t msd_id;
    uint16_t mss_id;
    uint16_t xl_id;
    uint16_t xo_id;
    uint16_t reserved[9];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_trans_loc_mem_to_loc_mem {
    uint16_t xd_id;
    uint16_t xdt_id;
    uint16_t xs_id;
    uint16_t xst_id;
    uint16_t xl_id;
    uint16_t used_ms_id;
    uint16_t src_alloc_hint : 2;
    uint16_t src_victim_hint : 2;
    uint16_t dst_alloc_hint : 2;
    uint16_t dst_victim_hint : 2;
    uint16_t ms_num : 8;
    uint16_t reserved1[6];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

struct ccu_instr_trans_mem {
    uint16_t xd_id;  // 存储目的内存地址
    uint16_t xdt_id; // 存储目的内存Token
    uint16_t xs_id;  // 存储源内存地址
    uint16_t xst_id; // 存储源内存Token
    uint16_t xl_id;  // 存储要搬运的数据长度
    uint16_t xc_id;  // 存储使用的channelId
    uint16_t xn_id;  // 存储notify/atomic的目的地址
    uint16_t xnt_id; // 存储notify/atomic的目的Token
    uint32_t value;  // notify value, atomic store add value, immediate data, 视不同的opCode确定, 只支持32bit
    uint16_t udf_type : 8;
    uint16_t reduce_data_type : 4;
    uint16_t reduce_op_code : 4;
    uint16_t dma_op_code : 8; // wqe中的opcode，支持0x0: send，0x1: send with immediate，0x3: Write，0x5: Write
                              // with Notify，0x6: Read，0x70: Write with atomic store add
    uint16_t order : 3;
    uint16_t fence : 1;
    uint16_t cqe : 1;
    uint16_t nf : 1; // No Fragment, 不允许分片
    uint16_t udf_enable : 1;
    uint16_t split_mode : 1;
    uint16_t se : 1;             // Solicited Event，Responder基于se来判断生成CQE时是否产生Completion Event
    uint16_t rmt_jetty_type : 2; // 00: JFR, 01: Jetty，10: JettyGroup，11: reserved
    uint16_t src_mode : 1;
    uint16_t dst_mode : 1;
    uint16_t ms_id_mode : 1; // 标记是否使用transmem的msId模式
    uint16_t reserved : 2;
    uint16_t target_hint : 8;
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

// SyncWtX指令固定将一个指定Xn的信息同步，固定8B
struct ccu_instr_sync_wt_x {
    uint16_t xd_id;  // 存储目的Xn寄存器的地址
    uint16_t xdt_id; // 存储目的Xn寄存器Token
    uint16_t xs_id;  // 存储源Xn寄存器Id
    uint16_t xc_id;  // 存储使用的channelId
    uint16_t xn_id;  // 存储notify/atomic的目的地址
    uint16_t xnt_id; // 存储notify/atomic的目的Token
    uint32_t value;  // notify value, 只支持32bit
    uint16_t notify_valid : 1;
    uint16_t par_mode : 1;
    uint16_t reserved : 14;
    uint16_t reserved1[4];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

// SyncAtX指令以write with atomic store add发往对端，其中write的数据长度为0，本端Xn的值是写在atomic
// value中进行发送
struct ccu_instr_sync_at_x {
    uint16_t xd_id;  // 存储目的Xn寄存器的地址
    uint16_t xdt_id; // 存储目的Xn寄存器Token
    uint16_t xs_id;  // 存储源Xn寄存器Id
    uint16_t xc_id;  // 存储使用的channelId
    uint16_t reserved : 1;
    uint16_t par_mode : 1;
    uint16_t reserved1 : 14;
    uint16_t reserved2[8];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

// reduce instruction
// add: 配置castEn
// max和min: castEn保持默认为0
struct ccu_instr_reduce {
    uint16_t ms_id[ccu_reduce_max_ms];
    uint16_t xn_id_length;
    uint16_t reserved;
    uint16_t reserved1 : 1;
    uint16_t count : 3;
    uint16_t cast_en : 2;
    uint16_t reserved2 : 5;
    uint16_t data_type : 5;
    uint16_t reserved3[2];
    uint16_t set_cke_id;
    uint16_t set_cke_mask;
};

#pragma pack(pop)

union ccu_micro_code_v2 {
    ccu_v2::ccu_instr_load_sqe_args_to_x load_sqe_args_to_x;
    ccu_v2::ccu_instr_load_imd_to_x load_imd_to_x;
    ccu_v2::ccu_instr_load_store_x load_store_x;
    ccu_v2::ccu_instr_clear_x clear_x;
    ccu_v2::ccu_instr_nop nop;
    ccu_v2::ccu_instr_operator operate;
    ccu_v2::ccu_instr_load load;
    ccu_v2::ccu_instr_store store;

    ccu_v2::ccu_instr_loop loop;
    ccu_v2::ccu_instr_loop_group loop_group;
    ccu_v2::ccu_instr_set_cke set_cke;
    ccu_v2::ccu_instr_clear_cke clear_cke;
    ccu_v2::ccu_instr_jmp jmp;
    ccu_v2::ccu_instr_wait wait;
    ccu_v2::ccu_instr_fence fence;

    ccu_v2::ccu_instr_trans_loc_mem_to_loc_ms trans_loc_mem_to_loc_ms;
    ccu_v2::ccu_instr_trans_loc_ms_to_loc_mem trans_loc_ms_to_loc_mem;
    ccu_v2::ccu_instr_trans_loc_ms_to_loc_ms trans_loc_ms_to_loc_ms;
    ccu_v2::ccu_instr_trans_loc_mem_to_loc_mem trans_loc_mem_to_loc_mem;
    ccu_v2::ccu_instr_trans_mem trans_mem;
    ccu_v2::ccu_instr_sync_wt_x sync_wt_x;
    ccu_v2::ccu_instr_sync_at_x sync_at_x;

    ccu_v2::ccu_instr_reduce reduce;
};
}; // namespace ccu_v2

struct ccu_instr {
    ccu_instr_header header;
    union {
        ccu_v1::ccu_micro_code_v1 v1;
        ccu_v2::ccu_micro_code_v2 v2;
    };
};

std::string parse_instr(const ccu_instr* instr);

void load_sqe_args_to_gsa_instr(ccu_instr* instr, uint16_t gsa_id, uint16_t sqe_args_id);
void load_sqe_args_to_xn_instr(ccu_instr* instr, uint16_t xn_id, uint16_t sqe_args_id);
void load_imd_to_gsa_instr(ccu_instr* instr, uint16_t gsa_id, uint64_t immediate);
void load_imd_to_xn_instr(ccu_instr* instr, uint16_t xn_id, uint64_t immediate, uint16_t sec_flag = 0);
void load_gsa_xn_instr(ccu_instr* instr, uint16_t gs_ad_id, uint16_t gs_am_id, uint16_t xn_id);
void load_gsagsa_instr(ccu_instr* instr, uint16_t gs_ad_id, uint16_t gs_am_id, uint16_t gs_an_id);
void load_xx_instr(ccu_instr* instr, uint16_t xd_id, uint16_t xm_id, uint16_t xn_id);

void loop_instr(ccu_instr* instr, uint16_t start_instr_id, uint16_t end_instr_id, uint16_t xn_id);
void loop_group_instr(
    ccu_instr* instr, uint16_t start_loop_instr_id, uint16_t xn_id, uint16_t xm_id, uint16_t high_perf_mode_en);
void jump_instr(ccu_instr* instr, uint16_t dst_instr_xn_id, uint16_t condition_xn_id, uint64_t expect_data);
void set_cke_instr(
    ccu_instr* instr, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);
void clear_cke_instr(
    ccu_instr* instr, uint16_t clear_cke_id, uint16_t clear_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);

void trans_loc_mem_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en);
void trans_rmt_mem_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en);
void trans_loc_ms_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t loc_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en);
void trans_loc_ms_to_rmt_mem_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t loc_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en);
void trans_rmt_ms_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t rmt_ms_id, uint16_t length_xn_id,
    uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type, uint16_t length_en);

void trans_loc_ms_to_loc_ms_instr(
    ccu_instr* instr, uint16_t dst_ms_id, uint16_t src_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type,
    uint16_t length_en);
void trans_rmt_ms_to_loc_ms_instr(
    ccu_instr* instr, uint16_t loc_ms_id, uint16_t rmt_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type,
    uint16_t length_en);
void trans_loc_ms_to_rmt_ms_instr(
    ccu_instr* instr, uint16_t rmt_ms_id, uint16_t loc_ms_id, uint16_t length_xn_id, uint16_t channel_id,
    uint16_t set_rmt_cke_id, uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en);

void trans_rmt_mem_to_loc_mem_instr(
    ccu_instr* instr, uint16_t loc_gsa_id, uint16_t loc_xn_id, uint16_t rmt_gsa_id, uint16_t rmt_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t reduce_data_type, uint16_t reduce_op_code, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en,
    uint16_t reduce_en);
void trans_loc_mem_to_rmt_mem_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t rmt_xn_id, uint16_t loc_gsa_id, uint16_t loc_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t reduce_data_type, uint16_t reduce_op_code, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en,
    uint16_t reduce_en);
void trans_loc_mem_to_loc_mem_instr(
    ccu_instr* instr, uint16_t dst_gsa_id, uint16_t dst_xn_id, uint16_t src_gsa_id, uint16_t src_xn_id,
    uint16_t length_xn_id, uint16_t channel_id, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id,
    uint16_t wait_cke_mask, uint16_t clear_type, uint16_t length_en);

void sync_cke_instr(
    ccu_instr* instr, uint16_t rmt_cke_id, uint16_t loc_cke_id, uint16_t loc_cke_mask, uint16_t channel_id,
    uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type);
void sync_gsa_instr(
    ccu_instr* instr, uint16_t rmt_gsa_id, uint16_t loc_gsa_id, uint16_t channel_id, uint16_t set_rmt_cke_id,
    uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);
void sync_xn_instr(
    ccu_instr* instr, uint16_t rmt_xn_id, uint16_t loc_xn_id, uint16_t channel_id, uint16_t set_rmt_cke_id,
    uint16_t set_rmt_cke_mask, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);

void add_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t cast_en, uint16_t data_type, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length);
void max_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length);
void min_instr(
    ccu_instr* instr, uint16_t* ms_id, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t wait_cke_id, uint16_t wait_cke_mask, uint16_t clear_type, uint16_t xn_id_length);

namespace ccu_v2 {
void nop(ccu_instr* instr);

void load_sqe_args_to_x(
    ccu_instr* instr, uint16_t xn_id, uint16_t sqe_args_id, uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);
void load_imd_to_xn(
    ccu_instr* instr, uint16_t xn_id, uint64_t immediate, uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);
void clear_x(
    ccu_instr* instr, uint16_t xn_id, uint16_t xm_id, uint16_t xn_id_mode = 0, uint16_t xm_id_mode = 0,
    uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);
void load_x_from_mem(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t src_token, uint16_t len, const cache_config& cache_config,
    uint16_t set_cke_id, uint16_t set_cke_mask);
void store_x_to_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t len, const cache_config& cache_config,
    uint16_t set_cke_id, uint16_t set_cke_mask);
void hscb_store_x_to_mem(
    ccu_instr* instr, uint16_t dst, uint16_t src, uint16_t len, const cache_config& cache_config, uint16_t set_cke_id,
    uint16_t set_cke_mask);

void assign(ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);
void assign_i(ccu_instr* instr, uint16_t result, uint64_t operand, uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);

void add(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void add_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void sub(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void sub_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void mul(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void mul_i(
    ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t imm, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);

void and_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void or_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void xor_op(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void not_op(ccu_instr* instr, uint16_t result, uint16_t operand, uint16_t set_cke_id = 0, uint16_t set_cke_mask = 0);

void sll(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void srl(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void sla(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);
void sra(
    ccu_instr* instr, uint16_t result, uint16_t operand1, uint16_t operand2, uint16_t set_cke_id = 0,
    uint16_t set_cke_mask = 0);

void loop(
    ccu_instr* instr, uint16_t start_instr_id, uint16_t end_instr_id, uint16_t iter_num, uint16_t offset,
    uint16_t context_id);
void loop_group(
    ccu_instr* instr, uint16_t start_loop_instr_id, uint16_t loop_group_config, uint16_t res_offset,
    uint16_t xn_offset);
void jump(
    ccu_instr* instr, uint16_t rel_tar_instr_xn_id, uint16_t condition_xn_id, uint16_t expected_xn_id,
    uint16_t condition_type);
void wait(ccu_instr* instr, uint16_t condition_xn_id, uint16_t expected_xn_id, uint16_t condition_type);
void fence(ccu_instr* instr);
void set_cke(
    ccu_instr* instr, uint16_t set_cke_id, uint16_t set_cke_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);
void clear_cke(
    ccu_instr* instr, uint16_t clear_cke_id, uint16_t clear_mask, uint16_t wait_cke_id, uint16_t wait_cke_mask,
    uint16_t clear_type);

void trans_loc_mem_to_loc_ms(
    ccu_instr* instr, uint16_t ms, uint16_t src, uint16_t src_token, uint16_t len, uint16_t offset, uint16_t set_cke_id,
    uint16_t set_cke_mask, const cache_config& cache_config);
void trans_loc_ms_to_loc_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t ms, uint16_t len, uint16_t offset, uint16_t set_cke_id,
    uint16_t set_cke_mask, const cache_config& cache_config);
void trans_loc_mem_to_loc_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t src_token, uint16_t len,
    uint16_t used_ms_id, uint16_t used_ms_num, uint16_t set_cke_id, uint16_t set_cke_mask,
    const cache_config& src_cache_config, const cache_config& dstcache_config);
void trans_mem(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t src, uint16_t src_token, uint16_t len,
    uint16_t channel, const trans_mem_notify_info& notify, const trans_mem_reduce_info& reduce,
    const trans_mem_config& config, uint16_t set_cke_id, uint16_t set_cke_mask);
void sync_wt_x(
    ccu_instr* instr, const trans_mem_notify_info& notify, uint16_t channel_id, uint16_t set_cke_id,
    uint16_t set_cke_mask);
void sync_wt_x(
    ccu_instr* instr, uint16_t dst, uint16_t dst_token, uint16_t xn, uint16_t channel_id,
    const trans_mem_notify_info& notify, uint16_t set_cke_id, uint16_t set_cke_mask);
void sync_at_x(
    ccu_instr* instr, uint16_t dst_addr, uint16_t dst_token, uint16_t src_id, uint16_t channel_id, uint16_t set_cke_id,
    uint16_t set_cke_mask);
void reduce_add(
    ccu_instr* instr, uint16_t* ms, uint16_t count, uint16_t cast_en, uint16_t data_type, uint16_t set_cke_id,
    uint16_t set_cke_mask, uint16_t xn_id_length);
void reduce_max(
    ccu_instr* instr, uint16_t* ms, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t xn_id_length);
void reduce_min(
    ccu_instr* instr, uint16_t* ms, uint16_t count, uint16_t data_type, uint16_t set_cke_id, uint16_t set_cke_mask,
    uint16_t xn_id_length);
void rel_jmp(ccu_instr* instr, uint16_t target_xn_id, uint32_t jmp_instr_id, uint16_t xn0, uint16_t xn1);
std::string parse_instr_v2(const ccu_instr* instr);
}; // namespace ccu_v2

}; // namespace ccu_rep
}; // namespace asc

#endif // CCU_MICROCODE_H
