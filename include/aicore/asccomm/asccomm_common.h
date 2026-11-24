/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_COMMON_H
#define INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_COMMON_H

#include <cstdint>

#include "kernel_basic_intf.h"

// Deprecation attribute for legacy public interfaces. Definition mirrors the devkit
// c_api/defs/macro.h so headers can coexist in one translation unit.
#ifndef ASC_DEPRECATED
#define ASC_DEPRECATED(version, removal_date, replacement)                                                  \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif

// Member-level variant of ASC_DEPRECATED for non-static data members (bitfields included; the
// attribute sits between the member name and the bitfield width).
#ifndef ASC_MEMBER_DEPRECATED
#define ASC_MEMBER_DEPRECATED(version, removal_date, replacement)                                           \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif

// ===================== canonical (namespace asc) =====================
namespace asc {
// Aliases ::ChannelHandle when a CANN hcomm public header was included first (guard macro), so
// unqualified ChannelHandle always resolves to one declaration. CANN hcomm headers come first.
#ifdef CHANNEL_HANDLE_DEFINED
using ChannelHandle = ::ChannelHandle;
#else
using ChannelHandle = uint64_t;
#endif

/** @brief Describes one local GM buffer segment for scatter/gather operations. */
struct buf_desc {
    GM_ADDR addr; ///< Start address of the local GM buffer segment.
    uint32_t len; ///< Length of the buffer segment in bytes.
};

#pragma pack(push, 1)
/**
 * @brief WQE control configuration, snake_case members. Layout-identical to the legacy WqeEntry
 *        (legacy/hcomm/hcomm_common.h); interchangeable as template config parameters.
 */
struct wqe_entry {
    uint32_t odr : 3; ///< Ordering attribute. bit[1:0]: execution order (00=no order, 01=relax order, 10=strong order);
                      ///< bit[2]: completion order (0=no order, 1=in order).
    uint32_t fence : 1;     ///< Set to 1 to make this WQE wait for completion of all previous WQEs.
    uint32_t se : 1;        ///< Set to 1 to trigger remote event notification on completion.
    uint32_t cqe : 1;       ///< Set to 1 to generate a completion queue entry on completion.
    uint32_t inline_en : 1; ///< Set to 1 to carry data inline in the WQE instead of an SGE reference.
} __attribute__((packed));

// The packed 7-bit WQE config must stay 1 byte.
static_assert(sizeof(wqe_entry) == 1, "wqe_entry must stay a packed 1-byte bitfield struct");
#pragma pack(pop)

/**
 * @brief 通信协议类型枚举
 */
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,                  ///< 保留协议类型
    COMM_PROTOCOL_HCCS = 0,                       ///< HCCS协议
    COMM_PROTOCOL_ROCE = 1,                       ///< RDMA over Converged Ethernet
    COMM_PROTOCOL_PCIE = 2,                       ///< PCIE协议
    COMM_PROTOCOL_SIO = 3,                        ///< SIO协议
    COMM_PROTOCOL_UB_CTP = 4,                     ///< 华为统一总线UB_CTP
    COMM_PROTOCOL_UBC_CTP = COMM_PROTOCOL_UB_CTP, ///< 兼容旧协议名UBC_CTP
    COMM_PROTOCOL_UBC_TP = 5,                     ///< 华为统一总线UBC_TP
    COMM_PROTOCOL_UB_MEM = 6,                     ///< UB_MEM
    COMM_PROTOCOL_UBOE = 7,                       ///< UBoE
    COMM_PROTOCOL_HCCS_ONLY = 8,                  ///< 一卡双DIE使用HCCS
    COMM_PROTOCOL_UB_RTP = 9,                     ///< UB_RTP
    COMM_PROTOCOL_UBG = COMM_PROTOCOL_UB_RTP,     ///< 兼容旧协议名UBG
} CommProtocol;

// Shared enumerators must stay value-identical to CANN hcomm/hcomm_res_defs.h.
static_assert(
    COMM_PROTOCOL_RESERVED == -1 && COMM_PROTOCOL_HCCS == 0 && COMM_PROTOCOL_ROCE == 1 && COMM_PROTOCOL_PCIE == 2 &&
        COMM_PROTOCOL_SIO == 3 && COMM_PROTOCOL_UB_CTP == 4 && COMM_PROTOCOL_UBC_TP == 5 && COMM_PROTOCOL_UB_MEM == 6,
    "asc::CommProtocol shared values must stay aligned with CANN hcomm_res_defs.h");

enum class asccomm_reduce_op : uint32_t {
    MAX = 0x8U,
    MIN = 0x9U,
    SUM = 0xAU,
};

static constexpr wqe_entry ASCCOMM_DEFAULT_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 0,
};

static constexpr wqe_entry ASCCOMM_INLINE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 1,
    .inline_en = 1,
};

} // namespace asc

#endif // INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_COMMON_H
