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

#include "kernel_operator.h"
#include "hcomm_dfx_impl.h"

using namespace AscendC;

#define HCOMM_DFX_EXPORT_AIV_META_INFO(kernel_name) \
static const struct FunLevelKType kernel_name##_kernel_type_section __attribute__ \
((used, section (".ascend.meta." #kernel_name))) \
= {{F_TYPE_KTYPE, sizeof(unsigned int), K_TYPE_AIV}}

extern "C" __vector__ __global__ __aicore__ void hcomm_dfx_queue_read_kernel(HcommDfxQueueReadArgs args)
{
    if (GetBlockIdx() != 0) {
        return;
    }

    __gm__ HcommDfxQueueReadResult *result =
        reinterpret_cast<__gm__ HcommDfxQueueReadResult *>(args.resultAddr);
    if (result == nullptr) {
        return;
    }

    result->ret = HCOMM_DFX_QUEUE_READ_INVALID_PARAM;
    result->size = 0;
    if (args.srcAddr == 0 || args.size == 0 || args.size > HCOMM_DFX_QUEUE_ENTRY_BYTES) {
        return;
    }

    __gm__ uint8_t *src = reinterpret_cast<__gm__ uint8_t *>(args.srcAddr);
    for (uint32_t idx = 0; idx < args.size; ++idx) {
        result->data[idx] = src[idx];
    }
    result->size = args.size;
    result->ret = HCOMM_DFX_QUEUE_READ_SUCCESS;
}

HCOMM_DFX_EXPORT_AIV_META_INFO(hcomm_dfx_queue_read_kernel);

#endif // ASCCOMM_SRC_AICORE_HCOMM_DFX_IMPL_HCOMM_DFX_QUEUE_READER_KERNEL_H
