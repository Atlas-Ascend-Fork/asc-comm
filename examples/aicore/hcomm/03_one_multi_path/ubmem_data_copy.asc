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

namespace {
// Use a small 4 KiB working tile for this sample; this is not the UB capacity limit.
constexpr uint32_t TILE_BYTES = 4U * 1024U;
constexpr uint32_t TILE_ELEMENTS = TILE_BYTES / sizeof(uint32_t);
} // namespace

__global__ __vector__ void ubmem_data_copy_kernel(__gm__ uint8_t* dst, __gm__ uint8_t* src, uint32_t elemCount)
{
    AscendC::InitSocState();

    AscendC::GlobalTensor<uint32_t> dstGlobal;
    AscendC::GlobalTensor<uint32_t> srcGlobal;
    dstGlobal.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(dst), elemCount);
    srcGlobal.SetGlobalBuffer(reinterpret_cast<__gm__ uint32_t*>(src), elemCount);

    AscendC::LocalTensor<uint32_t> local = AscendC::LocalTensor<uint32_t>(AscendC::TPosition::VECIN, 0, TILE_ELEMENTS);
    for (uint32_t offset = 0; offset < elemCount; offset += TILE_ELEMENTS) {
        uint32_t count = elemCount - offset;
        if (count > TILE_ELEMENTS) {
            count = TILE_ELEMENTS;
        }

        AscendC::DataCopy(local, srcGlobal[offset], count);
        // MTE2 -> MTE3: finish loading this tile before writing it back from the same local buffer.
        AscendC::SetFlag<AscendC::HardEvent::MTE2_MTE3>(EVENT_ID0);
        AscendC::WaitFlag<AscendC::HardEvent::MTE2_MTE3>(EVENT_ID0);
        AscendC::DataCopy(dstGlobal[offset], local, count);
        // MTE3 -> MTE2: finish this writeback before the next tile reuses the local buffer.
        AscendC::SetFlag<AscendC::HardEvent::MTE3_MTE2>(EVENT_ID0);
        AscendC::WaitFlag<AscendC::HardEvent::MTE3_MTE2>(EVENT_ID0);
    }
    AscendC::PipeBarrier<PIPE_ALL>();
}

extern "C" aclError LaunchUbmemDataCopy(void* dst, void* src, uint32_t byteSize, aclrtStream stream)
{
    if (dst == nullptr || src == nullptr || stream == nullptr || byteSize == 0 || byteSize % sizeof(uint32_t) != 0) {
        return ACL_ERROR_INVALID_PARAM;
    }

    constexpr uint32_t blockNum = 1;
    ubmem_data_copy_kernel<<<blockNum, 0, stream>>>(
        static_cast<uint8_t*>(dst), static_cast<uint8_t*>(src), byteSize / sizeof(uint32_t));
    return ACL_SUCCESS;
}
