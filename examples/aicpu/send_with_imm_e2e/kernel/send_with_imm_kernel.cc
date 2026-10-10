/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "asccomm_primitives.h"

#include <cstdint>

namespace {

struct KernelArgs {
    ThreadHandle thread{0};
    ChannelHandle channel{0};
    uintptr_t src{0};
    uint64_t len{0};
    uint32_t immData{0};
    int32_t ret{-1};
};

} // namespace

extern "C" void AsccommSendWithImmKernel(KernelArgs* args)
{
    if (args == nullptr)
        return;
    args->ret = asccomm_send_with_imm_on_thread(
        args->thread, args->channel, reinterpret_cast<const void*>(args->src), args->len, args->immData);
}
