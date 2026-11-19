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

namespace asc {
using ChannelHandle = uint64_t;

/** @brief Describes one local GM buffer segment for scatter/gather operations. */
struct buf_desc {
    GM_ADDR addr; ///< Start address of the local GM buffer segment.
    uint32_t len; ///< Length of the segment in bytes.
};

// Compatibility alias for the pre-rename spelling.
using BufDesc = buf_desc;

#pragma pack(push, 1)
/**
 * @brief Legacy WQE control configuration kept for compatibility transition, will be removed after
 *        2027/12/31. Field names (e.g. inlineEn) stay camelCase for source compatibility of
 *        already-released code; superseded by wqe_entry, which uses snake_case members. RoCE uses
 *        cqe and fence; the other fields are ignored.
 */
struct ASC_DEPRECATED(9.3.0, "2027/12/31", wqe_entry) WqeEntry {
    uint32_t odr : 3; ///< Ordering attribute. bit[1:0]: execution order (00=no order, 01=relax order, 10=strong order);
                      ///< bit[2]: completion order (0=no order, 1=in order).
    uint32_t fence : 1;    ///< Set to 1 to make this WQE wait for completion of all previous WQEs.
    uint32_t se : 1;       ///< Set to 1 to trigger remote event notification on completion.
    uint32_t cqe : 1;      ///< Set to 1 to generate a completion queue entry on completion.
    uint32_t inlineEn : 1; ///< Set to 1 to carry data inline in the WQE instead of an SGE reference.
} __attribute__((packed));

/**
 * @brief WQE control configuration with snake_case members. Layout is identical to WqeEntry; both types are
 *        interchangeable wherever a config is passed by template parameter. New code should use this type.
 *        RoCE uses cqe and fence; the other fields are ignored.
 */
struct wqe_entry {
    uint32_t odr : 3; ///< Ordering attribute. bit[1:0]: execution order (00=no order, 01=relax order, 10=strong order);
                      ///< bit[2]: completion order (0=no order, 1=in order).
    uint32_t fence : 1;     ///< Set to 1 to make this WQE wait for completion of all previous WQEs.
    uint32_t se : 1;        ///< Set to 1 to trigger remote event notification on completion.
    uint32_t cqe : 1;       ///< Set to 1 to generate a completion queue entry on completion.
    uint32_t inline_en : 1; ///< Set to 1 to carry data inline in the WQE instead of an SGE reference.
} __attribute__((packed));

// Layout lock between the deprecated legacy type and the replacement; the diagnostic suppression keeps
// internal builds warning-free while referencing the deprecated type.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static_assert(sizeof(WqeEntry) == sizeof(wqe_entry), "wqe_entry layout must stay identical to WqeEntry");
#pragma GCC diagnostic pop
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

enum class asccomm_urma_reduce_op : uint32_t {
    MAX = 0x8U,
    MIN = 0x9U,
    SUM = 0xAU,
};

} // namespace asc

#endif // INCLUDE_COMM_API_AICORE_ASCCOMM_ASCCOMM_COMMON_H
