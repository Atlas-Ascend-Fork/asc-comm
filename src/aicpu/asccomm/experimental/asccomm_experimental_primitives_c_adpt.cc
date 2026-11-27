/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "asccomm/experimental/asccomm_experimental_primitives.h"
#include "asccomm_adpt_internal.h"
#include "asccomm_transport.h"

namespace {

using asccomm_adpt::CommContext;
using asccomm_adpt::execute_api;
using asccomm_adpt::get_context;

constexpr uint64_t ASCCOMM_SEND_LEN_MAX = 64ULL * 1024ULL;

} // namespace

int32_t asccomm_send_on_thread(ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len)
{
    // The receive-side SEND contract cannot be captured by binary replay.
    AsccommBinaryCapture::Invalidate();
    return execute_api([&]() {
        CHK_PTR_NULL(src);
        CHK_PRT_RET(
            len > ASCCOMM_SEND_LEN_MAX,
            ASCCOMM_ERROR(
                "[%s] send length[%llu] exceeds max limit[%llu].", __func__, static_cast<unsigned long long>(len),
                static_cast<unsigned long long>(ASCCOMM_SEND_LEN_MAX)),
            ASCCOMM_E_PARA);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_send(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), len);
    });
}

int32_t asccomm_send_with_imm_on_thread(
    ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len, uint64_t immData)
{
    // The receive-side SEND contract cannot be captured by binary replay.
    AsccommBinaryCapture::Invalidate();
    return execute_api([&]() {
        CHK_PTR_NULL(src);
        CHK_PRT_RET(
            len > ASCCOMM_SEND_LEN_MAX,
            ASCCOMM_ERROR(
                "[%s] send-with-imm length[%llu] exceeds max limit[%llu].", __func__,
                static_cast<unsigned long long>(len), static_cast<unsigned long long>(ASCCOMM_SEND_LEN_MAX)),
            ASCCOMM_E_PARA);
        CommContext context{};
        CHK_RET(get_context(thread, channel, true, context));
        return asccomm_send_with_imm(*context.thread, *context.channel, reinterpret_cast<uintptr_t>(src), len, immData);
    });
}
