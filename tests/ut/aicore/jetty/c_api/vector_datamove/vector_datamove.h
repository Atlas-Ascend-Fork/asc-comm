/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef INCLUDE_C_API_VECTOR_DATAMOVE_VECTOR_DATAMOVE_H
#define INCLUDE_C_API_VECTOR_DATAMOVE_VECTOR_DATAMOVE_H

#if !defined(ASCENDC_CPU_DEBUG) || !ASCENDC_CPU_DEBUG
#error "This byte-copy shim is only for Jetty CPU-debug unit tests."
#endif

#include <cstdint>
#include <cstring>

#include "c_api/defs/defs.h"

// Avoid the unsupported 3510 vector helpers pulled in by the CANN 9.2.0
// datamove header. Only the byte-copy overload used by Jetty is replaced.
__aicore__ inline void asc_copy_ub2gm_align(
    __gm__ uint8_t* dst, __ubuf__ uint8_t* src, uint32_t burst_count, uint32_t burst_len, asc_store_l2_cache_mode,
    uint64_t dst_stride, uint32_t src_stride)
{
    // 3510 strides are byte offsets between the starts of consecutive bursts.
    for (uint32_t burst = 0; burst < burst_count; ++burst) {
        std::memcpy(
            dst + static_cast<uint64_t>(burst) * dst_stride, src + static_cast<uint64_t>(burst) * src_stride,
            burst_len);
    }
}

#endif
