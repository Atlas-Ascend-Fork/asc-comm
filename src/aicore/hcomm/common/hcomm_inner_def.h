/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/*!
 * \file asccomm_inner_def.h
 * \brief asccomm inner definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_INNER_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_INNER_DEF_H

#include <cstdint>

namespace AscendC {

constexpr int32_t asccomm_failed = -1;
constexpr int32_t asccomm_success = 0;
constexpr uint32_t asccomm_ub_buf_size = 512;
constexpr uint32_t asccomm_pollcq_max_retry_times = 1000000;

#pragma pack(push, 1)
struct urma_wqe_entry {
    uint32_t odr : 3;      // ordering, request ordering strength (0=unordered, 5=strongly ordered)
    uint32_t fence : 1;    // fence, 1=current WQE waits for all prior WQEs to complete
    uint32_t se : 1;       // solicited event, 1=trigger remote event notification on completion
    uint32_t cqe : 1;      // completion queue entry, 1=generate CQE on completion
    uint32_t inline_en : 1; // inline enable, 1=data inlined in WQE instead of SGE reference
} __attribute__((packed));
#pragma pack(pop)

static constexpr urma_wqe_entry urma_default_cfg = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 0,
};

static constexpr urma_wqe_entry urma_inline_cfg = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 1,
};

typedef enum {
    comm_engine_reserved = -1, ///< 保留的通信引擎
    comm_engine_cpu = 0,       ///< HOST CPU引擎
    comm_engine_cpu_ts = 1,    ///< HOST CPU TS引擎
    comm_engine_aicpu = 2,     ///< AICPU引擎
    comm_engine_aicpu_ts = 3,  ///< AICPU TS引擎
    comm_engine_aiv = 4,       ///< AIV引擎
    comm_engine_ccu = 5,       ///< CCU引擎
} comm_engine;

typedef struct {
    uint32_t version;
    uint32_t magic_word;
    uint32_t size;
    uint32_t reserved;
} comm_abi_header;

typedef enum {
    protection_type_invalid = -1,
    protection_type_roce = 0,
    protection_type_ub = 1,
} protection_type;

typedef enum {
    sq_context_type_invalid = -1,
    sq_context_type_ub_jfs = 0,
    sq_context_type_roce = 1,
} sq_context_type;

typedef enum {
    cq_context_type_invalid = -1,
    cq_context_type_ub_jfc = 0,
    cq_context_type_roce = 1,
} cq_context_type;

typedef enum {
    reged_notify_invalid = -1,
    reged_notify_ipc_rt = 0,
    reged_notify_ipc_mem = 1,
    reged_notify_rma_rt = 2,
    reged_notify_rma_mem = 3,
} reged_notify_type;

typedef enum {
    reged_buffer_invalid = -1,
    reged_buffer_ipc = 0,
    reged_buffer_rma = 1,
} reged_buffer_type;

typedef struct {
    protection_type type;
    union {
        struct {
            uint32_t lkey;
            uint32_t rkey;
        } roce;
        struct {
            uint32_t token_id;
            uint32_t token_value;
        } ub;
        uint8_t raws[24];
    } mem_info;
} protection_info; // 32B

typedef struct {
    reged_buffer_type type;
    union {
        struct {
            uint64_t addr;
            uint64_t size;
        } ipc;
        struct {
            uint64_t addr;
            uint64_t size;
            protection_info protection_info_value;
        } rma;
        uint8_t raws[56];
    } buffer_info;
} reged_buffer_entity; // 64B

typedef struct {
    reged_notify_type type;
    union {
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notify_id;
        } ipc_rt;
        struct {
            uint64_t addr;
            uint32_t size;
        } ipc_mem;
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notify_id;
            protection_info protection_info_value;
        } rma_rt;
        struct {
            uint64_t addr;
            uint32_t size;
            protection_info protection_info_value;
        } rma_mem;
        uint8_t raws[56];
    } notify_info;
} reged_notify_entity; // 64B

typedef struct {
    sq_context_type type;
    union {
        struct {
            uint64_t sq_va;
            uint64_t head_addr;
            uint64_t tail_addr;
            uint64_t db_va;
            uint32_t jfs_id;
            uint32_t wqe_size;
            uint32_t sq_depth;
            uint32_t tp_id;
            uint8_t remote_eid[16];
        } ub_jfs;
        struct {
            uint64_t sq_va;
            uint64_t head_addr;
            uint64_t tail_addr;
            uint64_t db_hw_va;
            uint64_t db_sw_va;
            uint32_t qpn;
            uint32_t wqe_size;
            uint32_t depth;
            uint8_t sl;
            uint8_t mtu_shift;
        } roce_sq;
        uint8_t raws[120];
    } context_info;
} sq_context;

typedef struct {
    cq_context_type type;
    union {
        struct {
            uint64_t scq_va;
            uint64_t head_addr;
            uint64_t tail_addr;
            uint64_t db_va;
            uint32_t jfc_id;
            uint32_t cqe_size;
            uint32_t cq_depth;
        } ub_jfc;
        struct {
            uint64_t cq_va;
            uint64_t head_addr;
            uint64_t tail_addr;
            uint64_t db_hw_va;
            uint64_t db_sw_va;
            uint32_t cqn;
            uint32_t cqe_size;
            uint32_t cq_depth;
        } roce_cq;
        uint8_t raws[120];
    } context_info;
} cq_context;

typedef struct {
    comm_abi_header abi_header;
    comm_engine engine;
    comm_protocol protocol;
    uint32_t local_notify_num;
    uint32_t remote_notify_num;
    uint32_t local_buffer_num;
    uint32_t remote_buffer_num;
    uint32_t sq_num;
    uint32_t cq_num;
    reged_notify_entity* local_notify_addr;
    reged_notify_entity* remote_notify_addr;
    reged_buffer_entity* local_buffer_addr;
    reged_buffer_entity* remote_buffer_addr;
    sq_context* sq_context_addr;
    cq_context* cq_context_addr;
    uint32_t sq_head; // SQ producer index in WQE BBs
    uint32_t sq_tail; // Submitted WQE count, reserved for future use
    uint32_t cq_head; // Number of expected CQEs
    uint32_t cq_tail; // Number of consumed CQEs
    uint8_t reserve[144];
} channel_entity;

static_assert(sizeof(channel_entity) == 256, "channel_entity size must keep aligned with asccomm");

// RoCE WQE, CQE, DB struct
typedef struct {
    uint8_t owner_sl; // dw0[31:24]: owner(1) + ctrl_section_length(2) + csl(2) + difsl(3)
    uint8_t df_tsl;   // dw0[23:16]: cr(1) + df(1) + va(1) + tsl(5); tsl = task section length / 8B
    uint16_t wf_bdsl; // dw0[15:0]: cf(1) + wf(1) + wqe_msn(2) + fde(1) + fast(1) + drv_sl(2) + bdsl(8)
    uint32_t cl_pi;   // dw1: cl(4) + reserved(8) + mask_pi(20); mask_pi only used by direct WQE
    uint64_t db;     // dw2-dw3: doorbell segment, not used by this path
} roce_wqe_ctrl_seg;

typedef union {
    struct {
        uint32_t xrc_srqn : 18; // [17:0]  XRC SRQN, RC mode keeps 0
        uint32_t ext : 1;      // [18]    CMD64 extend, unused
        uint32_t dif : 1;      // [19]    reserved for RoCE
        uint32_t rsvd0 : 3;    // [22:20]
        uint32_t so : 1;       // [23]    strong ordering
        uint32_t op_type : 5;   // [28:24] 0x04 = RDMA write, 0x08 = RDMA read
        uint32_t signal : 1;   // [29]    request a CQE on completion
        uint32_t fence : 1;    // [30]    fence / ordering
        uint32_t se : 1;       // [31]    solicited event
    } bs;
    uint32_t value;
} roce_wqe_task_com_seg;

typedef struct {
    roce_wqe_task_com_seg com_task;
    uint32_t data_len; // total message length in bytes
    uint32_t imm_data; // immediate data (0 for plain write/read)
    union {
        struct {
            uint32_t last_ext_len : 8; // [7:0] read extension length; write keeps 0
            uint32_t cmd_len : 8;     // [15:8] command length, unused for this WQE
            uint32_t pi : 16;        // [31:16] producer index, unused for this WQE
        } bs;
        uint32_t value; // dw3 is filled in host order, then byte-swapped before the NIC consumes it
    } dw3;
    uint64_t va_remote; // remote virtual address
    uint32_t r_key;     // remote memory key
    uint32_t ulp;      // upper-layer field; low 16 bits carry the local lkey
} roce_wqe_task_seg;

typedef struct {
    uint64_t va_local;
    uint32_t r_len;
    uint32_t le_key;
} roce_wqe_data_seg;

typedef struct {
    roce_wqe_ctrl_seg ctrl;
    roce_wqe_task_seg task;
    roce_wqe_data_seg data;
} roce_wqe_entry;

typedef struct {
    uint32_t owner_id_qpn; // dw1: owner(31) + cqe_size(30:29) + dif_en(28) + wq_id(27:24) + err_code(23:20) + qpn(19:0)
    uint32_t op_sr_wqebb;  // dw2: op_type(31:27) + s_r(26) + inline(25) + merge(24) + fake(23) + wqebb_cnt(19:0)
    uint32_t byte_cnt;    // dw3: transferred byte count
    uint32_t imm_data;    // dw4: immediate data / invalidate key (receive side)
    uint32_t rsvd_dw5;    // dw5: reserved for RC
    uint32_t wqe_num;     // dw6: merged wr count (RQ merge only)
    uint32_t vlan_queue_index; // dw7: srqn_rqpn (RC = SRQN, not read by SHMEM)
    uint8_t syndrome;        // dw8[7:0]: error syndrome, valid only when op_type = error(0x1e)
    uint8_t rsvd;            // dw8[15:8]
    uint16_t wqe_counter;     // dw8[31:16]: SQ WQE sequence number
} roce_cqe_entry;

typedef struct {
    union {
        struct {
            uint64_t qpn : 20;
            uint64_t ctx_size : 2;
            uint64_t r : 1;
            uint64_t c : 1;
            uint64_t cos : 3;
            uint64_t type : 5;

            uint64_t pi : 8;
            uint64_t resv : 8;
            uint64_t xrc_vld : 1;
            uint64_t rsvd : 1;
            uint64_t mtu_shift : 3;
            uint64_t sgid_idx : 7;
            uint64_t sub_type : 4;
        } bs;
        uint64_t value;
    } dw0;
} roce_db_entry;

// URMA struct
constexpr uint32_t asccomm_urma_default_qp_idx = 0U;

enum class asccomm_urma_op_code : uint32_t {
    send = 0U,
    send_with_imm,
    send_with_inv,
    write,
    write_with_imm,
    write_with_notify,
    read,
    cas,
    atomic_swap,
    atomic_store,
    atomic_load,
    faa = 0xBU,
    write_with_reduce = 0x10U,
    nop = 0x11U,
};

typedef struct {
    uint32_t sqe_bb_idx : 16;
    uint32_t flag : 8;
    uint32_t rsv0 : 3;
    uint32_t nf : 1;
    uint32_t token_en : 1;
    uint32_t rmt_jetty_type : 2;
    uint32_t owner : 1;
    uint32_t target_hint : 8;
    uint32_t opcode : 8;
    uint32_t rsv1 : 6;
    uint32_t inline_msg_len : 10;
    uint32_t tp_id : 24;
    uint32_t sge_num : 8;
    uint32_t rmt_jetty_or_seg_id : 20;
    uint32_t rsv2 : 12;
    uint64_t rmt_eid_l;
    uint64_t rmt_eid_h;
    uint32_t rmt_token_value;
    uint32_t udf_type : 8;
    uint32_t reduce_data_type : 4;
    uint32_t reduce_opcode : 4;
    uint32_t rsv3 : 16;
    uint32_t rmt_addr_l_or_token_id;
    uint32_t rmt_addr_h_or_token_value;
} asccomm_urma_sqe_ctx;

typedef struct {
    uint32_t len;
    uint32_t token_id;
    uint64_t va;
} asccomm_urma_sge_ctx;

typedef struct {
    uint32_t notify_token_id : 20;
    uint32_t rsv : 12;
    uint32_t notify_token_value;
    uint32_t notify_addr_l;
    uint32_t notify_addr_h;
    uint32_t notify_data_l;
    uint32_t notify_data_h;
    uint32_t rsv2[2];
} asccomm_urma_notify_ctx;

typedef struct {
    uint32_t s_r : 1;
    uint32_t is_jetty : 1;
    uint32_t owner : 1;
    uint32_t inline_en : 1;
    uint32_t opcode : 3;
    uint32_t fd : 1;
    uint32_t rsv : 8;
    uint32_t substatus : 8;
    uint32_t status : 8;
    uint32_t entry_idx : 16;
    uint32_t local_num_l : 16;
    uint32_t local_num_h : 4;
    uint32_t rmt_idx : 20;
    uint32_t rsv1 : 8;
    uint32_t tpn : 24;
    uint32_t rsv2 : 8;
    uint32_t byte_cnt;
    uint32_t user_data_l;
    uint32_t user_data_h;
    uint32_t rmt_eid[4];
    uint32_t data_l;
    uint32_t data_h;
    uint32_t inline_data[3];
} asccomm_urma_jfc_cqe_ctx;

} // namespace AscendC
#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_INNER_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_INNER_DEF_H
#endif
