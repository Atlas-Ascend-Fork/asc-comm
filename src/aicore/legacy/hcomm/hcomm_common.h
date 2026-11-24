/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H
#define INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H

// Deprecation attribute for the legacy Hcomm interfaces.
#ifndef ASC_DEPRECATED
#define ASC_DEPRECATED(version, removal_date, replacement)                                                  \
    [[deprecated("Deprecated since " #version ", Will be removed after " removal_date ", Use " #replacement \
                 " instead.")]]
#endif

#if __has_include("../asccomm/asccomm_common.h")
#include "../asccomm/asccomm_common.h"
#else
#include "../../comm_api/aicore/asccomm/asccomm_common.h"
#endif

namespace AscendC {

// ===================== legacy gen1 public types (remove after 2027/12/31) =====================
// Pre-rename public spellings; canonical definitions live in asc (asccomm_common.h /
// asccomm_inner_def.h).

using ChannelHandle = asc::ChannelHandle;
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

// Real struct (not alias): consumers forward-declare it as struct AscendC::WqeEntry.
#pragma pack(push, 1)
struct ASC_DEPRECATED(9.3.0, "2027/12/31", asc::wqe_entry) WqeEntry {
    uint32_t odr : 3;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
} __attribute__((packed));
#pragma pack(pop)

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static_assert(sizeof(WqeEntry) == sizeof(asc::wqe_entry), "wqe_entry layout must stay identical to WqeEntry");
static_assert(sizeof(WqeEntry) == 1, "WqeEntry must stay a packed 1-byte bitfield struct");
#pragma GCC diagnostic pop

// Real struct (not alias): consumers forward-declare it as struct AscendC::UrmaWqeEntry.
struct __attribute__((packed)) UrmaWqeEntry {
    uint32_t odr : 3;
    uint32_t fence : 1;
    uint32_t se : 1;
    uint32_t cqe : 1;
    uint32_t inlineEn : 1;
};
static_assert(sizeof(UrmaWqeEntry) == sizeof(asc::wqe_entry), "UrmaWqeEntry layout must stay identical to wqe_entry");

using HcommUrmaReduceOp = asc::asccomm_reduce_op;

} // namespace AscendC

#endif // INCLUDE_LEGACY_HCOMM_HCOMM_COMMON_H
