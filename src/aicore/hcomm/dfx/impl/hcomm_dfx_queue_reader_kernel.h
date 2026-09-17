/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_SRC_AICORE_HCOMM_DFX_IMPL_HCOMM_DFX_QUEUE_READER_KERNEL_H
#define ASCCOMM_SRC_AICORE_HCOMM_DFX_IMPL_HCOMM_DFX_QUEUE_READER_KERNEL_H

#include "basic_api/kernel_common.h"
#include "../common/hcomm_dfx_impl.h"

namespace AscendC {
namespace HcommDfx {
constexpr uint64_t HCOMM_DFX_CHANNEL_CURSOR_OFFSET = 96U;
constexpr uint64_t HCOMM_DFX_CHANNEL_CURSOR_BYTES = 4U * sizeof(uint32_t);
constexpr uint64_t HCOMM_DFX_CACHE_LINE_SIZE = 64U;
constexpr uint32_t HCOMM_DFX_URMA_MUTEX_ID = 29U;

__aicore__ inline void FlushRange(uint64_t addr, uint64_t length)
{
    if (addr == 0U || length == 0U) {
        return;
    }
    __gm__ uint8_t *start =
        reinterpret_cast<__gm__ uint8_t *>(addr / HCOMM_DFX_CACHE_LINE_SIZE * HCOMM_DFX_CACHE_LINE_SIZE);
    __gm__ uint8_t *end = reinterpret_cast<__gm__ uint8_t *>(
        (addr + length) / HCOMM_DFX_CACHE_LINE_SIZE * HCOMM_DFX_CACHE_LINE_SIZE);
    GlobalTensor<uint8_t> global;
    global.SetGlobalBuffer(start);
    for (uint32_t offset = 0; offset <= static_cast<uint32_t>(end - start); offset += HCOMM_DFX_CACHE_LINE_SIZE) {
        DataCacheCleanAndInvalid<uint8_t, CacheLine::SINGLE_CACHE_LINE, DcciDst::CACHELINE_OUT>(global[offset]);
    }
}
} // namespace HcommDfx
} // namespace AscendC

#define HCOMM_DFX_EXPORT_AIV_META_INFO(kernel_name) \
static const struct FunLevelKType kernel_name##_kernel_type_section __attribute__ \
((used, section (".ascend.meta." #kernel_name))) \
= {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}}

extern "C" __vector__ __global__ __aicore__ void hcomm_dfx_queue_read_kernel(AscendC::HcommDfxQueueReadArgs args)
{
    if (AscendC::GetBlockIdx() != 0) {
        return;
    }

    __gm__ AscendC::HcommDfxQueueReadResult *result =
        reinterpret_cast<__gm__ AscendC::HcommDfxQueueReadResult *>(args.resultAddr);
    if (result == nullptr) {
        return;
    }

    result->ret = AscendC::HCOMM_DFX_QUEUE_READ_INVALID_PARAM;
    result->size = 0;
    if (args.srcAddr == 0 || args.size == 0 || args.size > AscendC::HCOMM_DFX_QUEUE_ENTRY_BYTES) {
        return;
    }

    if ((args.flags & AscendC::HCOMM_DFX_QUEUE_READ_FLAG_FLUSH_CHANNEL_CURSORS) != 0U) {
        AscendC::Mutex::Lock<PIPE_MTE3>(AscendC::HcommDfx::HCOMM_DFX_URMA_MUTEX_ID);
        AscendC::HcommDfx::FlushRange(args.srcAddr + AscendC::HcommDfx::HCOMM_DFX_CHANNEL_CURSOR_OFFSET,
            AscendC::HcommDfx::HCOMM_DFX_CHANNEL_CURSOR_BYTES);
        AscendC::Mutex::Unlock<PIPE_MTE3>(AscendC::HcommDfx::HCOMM_DFX_URMA_MUTEX_ID);
    }

    __gm__ uint8_t *src = reinterpret_cast<__gm__ uint8_t *>(args.srcAddr);
    for (uint32_t idx = 0; idx < args.size; ++idx) {
        result->data[idx] = src[idx];
    }
    result->size = args.size;
    result->ret = AscendC::HCOMM_DFX_QUEUE_READ_SUCCESS;
}

HCOMM_DFX_EXPORT_AIV_META_INFO(hcomm_dfx_queue_read_kernel);

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_IMPL_HCOMM_DFX_QUEUE_READER_KERNEL_H
