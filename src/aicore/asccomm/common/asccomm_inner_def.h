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
 * \brief Hcomm inner definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal Hcomm header. Please include public Hcomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_INNER_DEF_H
#endif

#ifndef IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_INNER_DEF_H
#define IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_INNER_DEF_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The shared communication types are provided by the including public header chain (either
// asccomm/asccomm_common.h or the legacy hcomm_common.h forwarder resolves first). Best-effort
// self-sufficiency for internal-only inclusion paths:
#if __has_include("asccomm/asccomm_common.h")
#include "asccomm/asccomm_common.h"
#elif __has_include("hcomm/hcomm_common.h")
#include "hcomm/hcomm_common.h"
#endif

namespace asc {
using namespace AscendC;

enum class multi_channel_handle : uint64_t {};

constexpr int32_t ASCCOMM_FAILED = -1;
constexpr int32_t ASCCOMM_SUCCESS = 0;
// Deprecated pre-rename spellings, kept for source compatibility of 9.2.0-era code;
// removed after 2027/12/31.
constexpr int32_t HCOMM_FAILED = ASCCOMM_FAILED;
constexpr int32_t HCOMM_SUCCESS = ASCCOMM_SUCCESS;
constexpr uint32_t ASCCOMM_LOCK_FREE = 0U;
// Deprecated pre-rename spellings, kept for source compatibility of 9.2.0-era code; removed after 2027/12/31.
constexpr auto HCOMM_LOCK_FREE = ASCCOMM_LOCK_FREE;
constexpr uint32_t ASCCOMM_LOCK_HELD = 1U;
constexpr auto HCOMM_LOCK_HELD = ASCCOMM_LOCK_HELD;
constexpr uint32_t ASCCOMM_UB_BUF_SIZE = 512;
constexpr uint32_t HCOMM_UB_BUF_SIZE = ASCCOMM_UB_BUF_SIZE;
constexpr uint32_t ASCCOMM_POLLCQ_MAX_RETRY_TIMES = 1000000;
constexpr uint32_t ASCCOMM_POLL_CQ_THRESHOLD = 10;
constexpr auto HCOMM_POLL_CQ_THRESHOLD = ASCCOMM_POLL_CQ_THRESHOLD;
constexpr uint32_t ASCCOMM_NUM_CQE_PER_POLL_CQ = 100;
// HCCL guarantees buffer 0 covers all URMA buffers and shares their protection metadata.
constexpr uint32_t ASCCOMM_URMA_DEFAULT_BUFFER_IDX = 0U;

// config_type types accepted by the template config parameter: WqeEntry/UdmaWqeEntry-style legacy layouts carry
// camelCase inlineEn, while wqe_entry uses snake_case inline_en. Every other member name is identical,
// so only the inline flag needs a dual-name read. Compile-time only: config objects are constexpr.
template <typename config_type, typename = void>
struct has_snake_inline_en : std::false_type {};

template <typename config_type>
struct has_snake_inline_en<config_type, std::void_t<decltype(std::declval<const config_type&>().inline_en)>>
    : std::true_type {};

template <typename config_type>
__aicore__ constexpr uint32_t asccomm_config_inline_en(const config_type& config)
{
    if constexpr (has_snake_inline_en<config_type>::value) {
        return config.inline_en;
    } else {
        return config.inlineEn;
    }
}

static constexpr wqe_entry URMA_DEFAULT_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 0,
};

static constexpr wqe_entry URMA_INLINE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 1,
};

static constexpr wqe_entry ROCE_DEFAULT_CFG = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 0,
};

typedef enum {
    COMM_ENGINE_RESERVED = -1, ///< 保留的通信引擎
    COMM_ENGINE_CPU = 0,       ///< HOST CPU引擎
    COMM_ENGINE_CPU_TS = 1,    ///< HOST CPU TS引擎
    COMM_ENGINE_AICPU = 2,     ///< AICPU引擎
    COMM_ENGINE_AICPU_TS = 3,  ///< AICPU TS引擎
    COMM_ENGINE_AIV = 4,       ///< AIV引擎
    COMM_ENGINE_CCU = 5,       ///< CCU引擎
} CommEngine;

typedef struct {
    uint32_t version;
    uint32_t magicWord;
    uint32_t size;
    uint32_t reserved;
} CommAbiHeader;

typedef enum {
    PROTECTION_TYPE_INVALID = -1,
    PROTECTION_TYPE_ROCE = 0,
    PROTECTION_TYPE_UB = 1,
} ProtectionType;

typedef enum {
    SQ_CONTEXT_TYPE_INVALID = -1,
    SQ_CONTEXT_TYPE_UB_JFS = 0,
    SQ_CONTEXT_TYPE_ROCE = 1,
} SqContextType;

typedef enum {
    CQ_CONTEXT_TYPE_INVALID = -1,
    CQ_CONTEXT_TYPE_UB_JFC = 0,
    CQ_CONTEXT_TYPE_ROCE = 1,
} CqContextType;

typedef enum {
    REGED_NOTIFY_INVALID = -1,
    REGED_NOTIFY_IPC_RT = 0,
    REGED_NOTIFY_IPC_MEM = 1,
    REGED_NOTIFY_RMA_RT = 2,
    REGED_NOTIFY_RMA_MEM = 3,
} RegedNotifyType;

typedef enum {
    REGED_BUFFER_INVALID = -1,
    REGED_BUFFER_IPC = 0,
    REGED_BUFFER_RMA = 1,
} RegedBufferType;

typedef struct {
    ProtectionType type;
    union {
        struct {
            uint32_t lkey;
            uint32_t rkey;
        } roce;
        struct {
            uint32_t tokenId;
            uint32_t tokenValue;
        } ub;
        uint8_t raws[24];
    } memInfo;
} ProtectionInfo; // 32B

typedef struct {
    RegedBufferType type;
    union {
        struct {
            uint64_t addr;
            uint64_t size;
        } ipc;
        struct {
            uint64_t addr;
            uint64_t size;
            ProtectionInfo protectionInfo;
        } rma;
        uint8_t raws[56];
    } bufferInfo;
} RegedBufferEntity; // 64B

typedef struct {
    RegedNotifyType type;
    union {
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
        } ipcRt;
        struct {
            uint64_t addr;
            uint32_t size;
        } ipcMem;
        struct {
            uint64_t addr;
            uint32_t size;
            int32_t notifyId;
            ProtectionInfo protectionInfo;
        } rmaRt;
        struct {
            uint64_t addr;
            uint32_t size;
            ProtectionInfo protectionInfo;
        } rmaMem;
        uint8_t raws[56];
    } notifyInfo;
} RegedNotifyEntity; // 64B

typedef struct {
    SqContextType type;
    union {
        struct {
            uint64_t sqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
            uint32_t jfsID;
            uint32_t wqeSize;
            uint32_t sqDepth;
            uint32_t tpID;
            uint8_t remoteEID[16];
        } ubJfs;
        struct {
            uint64_t sqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t qpn;
            uint32_t wqeSize;
            uint32_t depth;
            uint8_t sl;
            uint64_t dbVendorSpecified;
        } roceSq;
        uint8_t raws[120];
    } contextInfo;
} SqContext;

typedef struct {
    CqContextType type;
    union {
        struct {
            uint64_t scqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbVa;
            uint32_t jfcID;
            uint32_t cqeSize;
            uint32_t cqDepth;
        } ubJfc;
        struct {
            uint64_t cqVa;
            uint64_t headAddr;
            uint64_t tailAddr;
            uint64_t dbHwVa;
            uint64_t dbSwVa;
            uint32_t cqn;
            uint32_t cqeSize;
            uint32_t cqDepth;
            uint64_t dbVendorSpecified;
        } roceCq;
        uint8_t raws[120];
    } contextInfo;
} CqContext;

typedef struct {
    CommAbiHeader abiHeader;
    CommEngine engine;
    CommProtocol protocol;
    uint32_t localNotifyNum;
    uint32_t remoteNotifyNum;
    uint32_t localBufferNum;
    uint32_t remoteBufferNum;
    uint32_t sqNum;
    uint32_t cqNum;
    RegedNotifyEntity* localNotifyAddr;
    RegedNotifyEntity* remoteNotifyAddr;
    RegedBufferEntity* localBufferAddr;
    RegedBufferEntity* remoteBufferAddr;
    SqContext* sqContextAddr;
    CqContext* cqContextAddr;
    uint8_t resv1[48]; // for other data seg in aicpu
    // RoCE and URMA both use this field as the SQ producer WQEBB index.
    uint32_t sqHead;
    // Completed SQ consumer WQEBB index.
    uint32_t sqTail;
    uint32_t cqHead; // Number of expected CQEs
    uint32_t cqTail; // Number of consumed CQEs
    uint8_t reserve[96];
} ChannelEntity;

static_assert(sizeof(ChannelEntity) == 256, "ChannelEntity size must keep aligned with hcomm");

typedef struct {
    uint32_t token_id;
    uint32_t token_value;
    uint64_t remote_eid_low;
    uint64_t remote_eid_high;
    uint32_t tp_id;
    uint32_t reserved;
} batch_remote_info;

static_assert(sizeof(batch_remote_info) == 32U, "batch_remote_info layout must not change");

typedef struct {
    uint64_t remote_buffer_addr;
    uint32_t remote_buffer_num;
    uint32_t tp_id;
    uint64_t remote_eid_low;
    uint64_t remote_eid_high;
} multi_channel_remote_info;

static_assert(sizeof(multi_channel_remote_info) == 32U, "multi_channel_remote_info layout must not change");

typedef struct {
    uint32_t sq_head;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint32_t pre_sq_cnt;
} batch_queue_cursor;

static_assert(sizeof(batch_queue_cursor) == 20U, "batch_queue_cursor layout must not change");

typedef struct {
    ChannelHandle channel_handle;
    uint32_t channel_num;
    uint32_t reserved;
    uint64_t remote_info_addr;
} multi_channel_entity;

static_assert(sizeof(multi_channel_entity) == 24U, "multi_channel_entity layout must not change");

typedef struct {
    uint32_t l_key;
    uint32_t r_key;
    uint64_t key_word;
    uint64_t data_key_high_word;
} roce_mr_key;

static_assert(sizeof(roce_mr_key) <= sizeof(batch_remote_info), "RoCE MR key cache must not grow ubc_batch_handle");

typedef struct {
    LocalTensor<uint32_t> buffer;
    __ubuf__ uint32_t* raw_buffer;
    uint32_t buffer_capacity;
} batch_buffer;

typedef struct {
    union {
        ChannelHandle channel_handle;
        ChannelHandle channelHandle; // legacy alias: channelHandle; exposed to users for the zero-handle validity check
    };
    SqContext sq_context;
    CqContext cq_context;
    batch_queue_cursor cursor;
    batch_remote_info remote_info;
    roce_mr_key mr_key;
    batch_buffer buffer;
} ubc_batch_handle;

// RoCE uses the same batch-handle ABI as UB/URMA. This alias keeps the RoCE implementation
// protocol-specific in source without changing the public batch_handle mapping.
using roce_batch_handle = ubc_batch_handle;

typedef struct {
    ubc_batch_handle handle;
    union {
        ChannelHandle channel_handle;
        ChannelHandle channelHandle; // legacy alias: channelHandle
    };
    union {
        uint32_t channel_num;
        uint32_t channelNum; // legacy alias: channelNum
    };
    uint32_t reserved;
    uint64_t remote_info_addr;
} ubc_multi_batch_handle;

template <typename T>
struct handle_traits;

// For channel types, BatchHandleType is the asccomm_make_batch_handle result; for batch types, it is the execution handle.
template <>
struct handle_traits<ChannelHandle> {
    using BatchHandleType = ubc_batch_handle;
};

template <>
struct handle_traits<multi_channel_handle> {
    using BatchHandleType = ubc_multi_batch_handle;
};

template <typename T>
using batch_handle = typename handle_traits<T>::BatchHandleType;

template <>
struct handle_traits<ubc_batch_handle> {
    using BatchHandleType = ubc_batch_handle;
    using ChannelType = ChannelHandle;
};

template <>
struct handle_traits<ubc_multi_batch_handle> {
    using BatchHandleType = ubc_batch_handle;
    using ChannelType = multi_channel_handle;
};

// RoCE WQE, CQE, DB struct
typedef struct {
    uint8_t owner_sl; // dw0[31:24]: owner(1) + ctrl_section_length(2) + csl(2) + difsl(3)
    uint8_t df_tsl;   // dw0[23:16]: cr(1) + df(1) + va(1) + tsl(5); tsl = task section length / 8B
    uint16_t wf_bdsl; // dw0[15:0]: cf(1) + wf(1) + wqe_ssn(2) + fde(1) + fast(1) + drv_sl(2) + bdsl(8)
    uint32_t cl_pi;   // dw1: cl(4) + reserved(8) + mask_pi(20); mask_pi only used by direct WQE
    uint64_t db;      // dw2-dw3: doorbell segment, not used by this path
} roce_wqe_ctrl_seg;

typedef union {
    struct {
        uint32_t xrc_srqn : 18; // [17:0]  XRC SRQN, RC mode keeps 0
        uint32_t ext : 1;       // [18]    CMD64 extend, unused
        uint32_t dif : 1;       // [19]    reserved for RoCE
        uint32_t rsvd0 : 3;     // [22:20]
        uint32_t so : 1;        // [23]    strong ordering
        uint32_t op_type : 5;   // [28:24] 0x04 = RDMA WRITE, 0x08 = RDMA READ
        uint32_t signal : 1;    // [29]    request a CQE on completion
        uint32_t fence : 1;     // [30]    fence / ordering
        uint32_t se : 1;        // [31]    solicited event
    } dw0;
    uint32_t value;
} roce_wqe_task_com_seg;

typedef struct {
    roce_wqe_task_com_seg com_task;
    uint32_t data_len; // total message length in bytes
    uint32_t imm_data; // immediate data (0 for plain WRITE/READ)
    union {
        struct {
            uint32_t last_ext_len : 8; // [7:0] READ extension length; WRITE keeps 0
            uint32_t cmd_len : 8;      // [15:8] command length, unused for this WQE
            uint32_t pi : 16;          // [31:16] producer index, unused for this WQE
        } bs;
        uint32_t feth;
        uint32_t value; // dw3 is filled in host order, then byte-swapped before the NIC consumes it
    } dw3;
    uint64_t va_remote; // remote virtual address
    uint32_t r_key;     // remote memory key
    uint32_t ulp;       // upper-layer field; low 16 bits carry the local lkey
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
    uint32_t
        op_sr_wqebb;   // dw2: opType(31:27) + s_r(26) + inline(25) + merge(24) + fake(23) + completed_wqebb_index(19:0)
    uint32_t byte_cnt; // dw3: transferred byte count
    uint32_t imm_data; // dw4: immediate data / invalidate key (receive side)
    union {
        struct {
            uint8_t smac[2];
            uint16_t vlan_id_pri;
        } mac_vlan_id;
    } dw4;
    union {
        uint8_t smac[4];
        uint32_t wqe_num;
    } dw5;
    uint32_t vlan_queue_index; // dw7: srqn_rqpn (RC = SRQN, not read by SHMEM)
    uint8_t syndrome;          // dw8[7:0]: error syndrome, valid only when opType = error(0x1e)
    uint8_t rsvd;              // dw8[15:8]
    uint16_t wqe_counter;      // dw8[31:16]: SQ WQE sequence number
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
constexpr uint32_t ASCCOMM_URMA_DEFAULT_QP_IDX = 0U;
constexpr auto HCOMM_URMA_DEFAULT_QP_IDX = ASCCOMM_URMA_DEFAULT_QP_IDX;

enum class asccomm_urma_op_code : uint32_t {
    SEND = 0U,
    SEND_WITH_IMM,
    SEND_WITH_INV,
    WRITE,
    WRITE_WITH_IMM,
    WRITE_WITH_NOTIFY,
    READ,
    CAS,
    ATOMIC_SWAP,
    ATOMIC_STORE,
    ATOMIC_LOAD,
    FAA = 0xBU,
    WRITE_WITH_REDUCE = 0x10U,
    NOP = 0x11U,
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

// asccomm_urma_sqe_ctx is the URMA SQE wire layout: its size and member offsets are part of the
// hardware-facing contract. The legacy-member unions above must keep the packing unchanged.
static_assert(sizeof(asccomm_urma_sqe_ctx) == 48U, "asccomm_urma_sqe_ctx wire layout must not change");
static_assert(offsetof(asccomm_urma_sqe_ctx, rmt_eid_l) == 16U, "asccomm_urma_sqe_ctx wire layout must not change");
static_assert(
    offsetof(asccomm_urma_sqe_ctx, rmt_token_value) == 32U, "asccomm_urma_sqe_ctx wire layout must not change");
static_assert(
    offsetof(asccomm_urma_sqe_ctx, rmt_addr_l_or_token_id) == 40U, "asccomm_urma_sqe_ctx wire layout must not change");

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

static_assert(sizeof(asccomm_urma_notify_ctx) == 32U, "asccomm_urma_notify_ctx wire layout must not change");
static_assert(
    offsetof(asccomm_urma_notify_ctx, notify_token_value) == 4U, "asccomm_urma_notify_ctx wire layout must not change");

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

} // namespace asc

// Compatibility bridge: the legacy Hcomm public headers (src/aicore/legacy/hcomm) and the other
// AscendC-surface modules (ain, Jetty) resolve the shared communication entities through these
// aliases. The single source of truth is namespace asc; the aliases are removed together
// with the legacy API.
namespace AscendC {
using ChannelHandle = asc::ChannelHandle;
using MultiChannelHandle = asc::multi_channel_handle;
using BatchRemoteInfo = asc::batch_remote_info;
using MultiChannelRemoteInfo = asc::multi_channel_remote_info;
using BatchQueueCursor = asc::batch_queue_cursor;
using MultiChannelEntity = asc::multi_channel_entity;
using RoceMrKey = asc::roce_mr_key;
using BatchBuffer = asc::batch_buffer;
using UbcBatchHandle = asc::ubc_batch_handle;
using RoceBatchHandle = asc::roce_batch_handle;
using UbcMultiBatchHandle = asc::ubc_multi_batch_handle;
template <typename T>
using HandleTraits = asc::handle_traits<T>;
template <typename T>
using BatchHandle = asc::batch_handle<T>;
using RoceWqeCtrlSeg = asc::roce_wqe_ctrl_seg;
using RoceWqeTaskComSeg = asc::roce_wqe_task_com_seg;
using RoceWqeTaskSeg = asc::roce_wqe_task_seg;
using RoceWqeDataSeg = asc::roce_wqe_data_seg;
using RoceWqeEntry = asc::roce_wqe_entry;
using RoceCqeEntry = asc::roce_cqe_entry;
using RoceDbEntry = asc::roce_db_entry;
using CommProtocol = asc::CommProtocol;
using asc::COMM_PROTOCOL_HCCS;
using asc::COMM_PROTOCOL_HCCS_ONLY;
using asc::COMM_PROTOCOL_PCIE;
using asc::COMM_PROTOCOL_RESERVED;
using asc::COMM_PROTOCOL_ROCE;
using asc::COMM_PROTOCOL_SIO;
using asc::COMM_PROTOCOL_UB_CTP;
using asc::COMM_PROTOCOL_UB_MEM;
using asc::COMM_PROTOCOL_UB_RTP;
using asc::COMM_PROTOCOL_UBC_CTP;
using asc::COMM_PROTOCOL_UBC_TP;
using asc::COMM_PROTOCOL_UBG;
using asc::COMM_PROTOCOL_UBOE;
using BufDesc = asc::buf_desc;
// Compatibility alias for the deprecated legacy type; kept so legacy user code keeps compiling while
// referencing the deprecated real struct. The diagnostic suppression only covers the alias declaration.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
using WqeEntry = asc::WqeEntry;
#pragma GCC diagnostic pop
// 兼容旧类型名：外部组件以 struct AscendC::UrmaWqeEntry 书写配置（需真实类名，
// 别名无法满足 elaborated-type-specifier）。布局与 WqeEntry/wqe_entry 一致，config 经
// 模板 auto 透传，三者可互换（9.3.0 起对外统一，新增代码建议使用 wqe_entry）。
struct __attribute__((packed)) UrmaWqeEntry {
    uint32_t odr : 3;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
};
static_assert(sizeof(UrmaWqeEntry) == sizeof(asc::wqe_entry), "UrmaWqeEntry layout must stay identical to wqe_entry");
using CommEngine = asc::CommEngine;
using ChannelEntity = asc::ChannelEntity;
using MultiChannelEntity = asc::multi_channel_entity;
using SqContext = asc::SqContext;
using CqContext = asc::CqContext;
using RegedBufferEntity = asc::RegedBufferEntity;
template <typename T>
using HandleTraits = asc::handle_traits<T>;
using HcommUrmaReduceOp = asc::asccomm_urma_reduce_op;
template <typename T>
using BatchHandle = asc::batch_handle<T>;
using asc::ASCCOMM_FAILED;
using asc::ASCCOMM_SUCCESS;
using asc::ASCCOMM_UB_BUF_SIZE;
// Deprecated pre-rename spellings, kept for source compatibility of 9.2.0-era code;
// removed after 2027/12/31.
using asc::HCOMM_FAILED;
using asc::HCOMM_SUCCESS;
using asc::HCOMM_UB_BUF_SIZE;
using asc::ROCE_DEFAULT_CFG;
using asc::URMA_DEFAULT_CFG;
using asc::URMA_INLINE_CFG;
// Deprecated pre-rename spellings, kept for source compatibility of 9.2.0-era code; removed after 2027/12/31.
using asc::HCOMM_LOCK_FREE;
using asc::HCOMM_LOCK_HELD;
using asc::HCOMM_POLL_CQ_THRESHOLD;
using HcommUrmaOpCode = asc::asccomm_urma_op_code;
using HcommUrmaSqeCtx = asc::asccomm_urma_sqe_ctx;
using HcommUrmaSgeCtx = asc::asccomm_urma_sge_ctx;
using HcommUrmaNotifyCtx = asc::asccomm_urma_notify_ctx;
using RegedBufferType = asc::RegedBufferType;
using asc::CQ_CONTEXT_TYPE_UB_JFC;
using asc::HCOMM_URMA_DEFAULT_QP_IDX;
using asc::PROTECTION_TYPE_UB;
using asc::REGED_BUFFER_RMA;
using asc::SQ_CONTEXT_TYPE_UB_JFS;
} // namespace AscendC
#endif // IMPL_COMM_API_AICORE_ASCCOMM_COMMON_ASCCOMM_INNER_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_INNER_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_ASCCOMM_INNER_DEF_H
#endif
