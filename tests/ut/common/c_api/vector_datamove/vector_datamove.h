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
#error "This datamove shim is only for CPU-debug unit tests."
#endif

#include <cstdint>
#include <cstring>

// Avoid pulling in the CANN datamove header on CPU-debug UT paths: the 9.2.0/9.3.0
// npu_arch_3510 header drags in unsupported vector helpers and its internal
// copy_gm_to_ubuf_align_v2 calls do not match the tikicpulib stub signatures.
// Only the byte-copy primitives used by the UTs are provided here, with plain
// host-memory semantics. The asc_store_l2_cache_mode enum always comes from the
// SDK (c_api/defs/enum.h when available, matching the enum.h chain that the
// implementation headers pull in anyway); a local uint8_t fallback keeps the
// shim self-contained on older SDK versions that lack the defs headers.
#if __has_include(<c_api/defs/enum.h>)
#include <c_api/defs/enum.h>
#else
enum class asc_store_l2_cache_mode : uint8_t { NOTALLOC_CLEAN, CLEAN, NOTALLOC };
#endif

__aicore__ inline void asc_copy_ub2gm_align(
    uint8_t* dst, uint8_t* src, uint32_t burst_count, uint32_t burst_len, asc_store_l2_cache_mode, uint64_t dst_stride,
    uint32_t src_stride)
{
    for (uint32_t burst = 0; burst < burst_count; ++burst) {
        std::memcpy(
            dst + static_cast<uint64_t>(burst) * dst_stride, src + static_cast<uint64_t>(burst) * src_stride,
            burst_len);
    }
}

__aicore__ inline void asc_copy_gm2ub_align(
    uint8_t* dst, uint8_t* src, uint32_t burst_count, uint32_t burst_len, asc_store_l2_cache_mode, uint64_t dst_stride,
    uint32_t src_stride)
{
    for (uint32_t burst = 0; burst < burst_count; ++burst) {
        std::memcpy(
            dst + static_cast<uint64_t>(burst) * dst_stride, src + static_cast<uint64_t>(burst) * src_stride,
            burst_len);
    }
}

#endif // INCLUDE_C_API_VECTOR_DATAMOVE_VECTOR_DATAMOVE_H
