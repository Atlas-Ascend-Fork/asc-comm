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
 * \file asccomm_simt_inner_def.h
 * \brief asccomm SIMT inner definition
 */

#if !defined(ASCCOMM_INCLUDE_INTERNAL_HEADERS)
#pragma message("This is an internal asccomm header. Please include public asccomm headers instead.")
#define ASCCOMM_INCLUDE_INTERNAL_HEADERS
#define ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H
#endif

#ifndef IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H
#define IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H

#include <cstdint>

// SIMT intrinsics used by UB context initialization and doorbell ordering.
#include "simt_api/asc_simt.h"
#include "simt_api/device_atomic_functions.h"
#include "simt_api/device_sync_functions.h"

#include "hcomm/hcomm_common.h"

#include "hcomm_inner_def.h"

namespace AscendC::simt {

// head_addr stores cur_head in low 32 bits and wqe_cnt in high 32 bits.
// Concurrent deferred posts reserve it atomically; publication and drain are single-lane.
constexpr uint64_t asccomm_simt_wqe_cnt_mask = 0xFFFFFFFFULL;

// SIMT view of channel_entity: the shared definition stores typed host pointers, which the SIMT
// path cannot name, so every pointer member is viewed as a uint64_t here. The static_assert keeps
// this view locked to the shared layout.
struct asccomm_simt_channel_entity {
    comm_abi_header abi_header;
    comm_engine engine;
    int32_t protocol;
    uint32_t local_notify_num;
    uint32_t remote_notify_num;
    uint32_t local_buffer_num;
    uint32_t remote_buffer_num;
    uint32_t sq_num;
    uint32_t cq_num;
    uint64_t local_notify_addr;
    uint64_t remote_notify_addr;
    uint64_t local_buffer_addr;
    uint64_t remote_buffer_addr;
    uint64_t sq_context_addr;
    uint64_t cq_context_addr;
    // The SIMD path tracks its queue state in these four counters. SIMT does not use them: it packs
    // cur_head and wqe_cnt into the single u64 at sq_context::ub_jfs::head_addr. Posting interfaces for
    // one channel are single-lane; the packed update keeps both counters consistent in one store.
    // These fields are listed here only to keep this view
    // field-for-field identical to channel_entity.
    uint32_t sq_head;
    uint32_t sq_tail;
    uint32_t cq_head;
    uint32_t cq_tail;
    uint8_t reserve[144];
};

static_assert(sizeof(asccomm_simt_channel_entity) == sizeof(channel_entity), "SIMT channel_entity view size mismatch");

} // namespace AscendC::simt

#endif // IMPL_ADV_API_DETAIL_HCOMM_COMMON_HCOMM_SIMT_INNER_DEF_H
#if defined(ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H)
#undef ASCCOMM_INCLUDE_INTERNAL_HEADERS
#undef ASCCOMM_UNDEF_INCLUDE_INTERNAL_HEADERS_HCOMM_SIMT_INNER_DEF_H
#endif
