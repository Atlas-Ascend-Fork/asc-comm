/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "acl/acl.h"
#include "kernel_operator.h"

#include <c_api/sync/sync.h>
#include <c_api/vector_datamove/vector_datamove.h>

namespace {
// Use a small 4 KiB working tile for this sample; this is not the UB capacity limit.
constexpr uint32_t TILE_BYTES = 4U * 1024U;
constexpr uint32_t TILE_ELEMENTS = TILE_BYTES / sizeof(uint32_t);
} // namespace

// UB数据搬运kernel：GM->UB->GM单block串行拷贝，用于one path/multi path的通路验证。
__global__ __vector__ void ubmem_data_copy_kernel(__gm__ uint8_t* dst, __gm__ uint8_t* src, uint32_t elem_count)
{
    AscendC::InitSocState();

    __gm__ uint32_t* dst_global = reinterpret_cast<__gm__ uint32_t*>(dst);
    __gm__ uint32_t* src_global = reinterpret_cast<__gm__ uint32_t*>(src);

    __ubuf__ uint32_t local[TILE_ELEMENTS];
    for (uint32_t offset = 0; offset < elem_count; offset += TILE_ELEMENTS) {
        uint32_t count = elem_count - offset;
        if (count > TILE_ELEMENTS) {
            count = TILE_ELEMENTS;
        }

        asc_copy_gm2ub_align(local, src_global + offset, count * sizeof(uint32_t));
        // MTE2 -> MTE3: finish loading this tile before writing it back from the same local buffer.
        asc_sync_mte2(0);
        asc_copy_ub2gm_align(dst_global + offset, local, count * sizeof(uint32_t));
        // MTE3 -> MTE2: finish this writeback before the next tile reuses the local buffer.
        asc_sync_mte3(0);
    }
    asc_sync_pipe(PIPE_ALL);
}

// 下发数据搬运kernel。
extern "C" aclError launch_ubmem_data_copy(void* dst, void* src, uint32_t byte_size, aclrtStream stream)
{
    if (dst == nullptr || src == nullptr || stream == nullptr || byte_size == 0 || byte_size % sizeof(uint32_t) != 0) {
        return ACL_ERROR_INVALID_PARAM;
    }

    constexpr uint32_t block_num = 1;
    ubmem_data_copy_kernel<<<block_num, 0, stream>>>(
        static_cast<uint8_t*>(dst), static_cast<uint8_t*>(src), byte_size / sizeof(uint32_t));
    return ACL_SUCCESS;
}
