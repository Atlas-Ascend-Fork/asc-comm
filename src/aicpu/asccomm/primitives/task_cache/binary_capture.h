/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_BINARY_CAPTURE_H
#define ASCCOMM_BINARY_CAPTURE_H
#include <cstdint>
namespace AsccommBinaryCapture {
enum class Op : uint32_t {
    asccomm_batch_mode_start,
    asccomm_batch_mode_end,
    asccomm_local_copy_on_thread,
    asccomm_local_reduce_on_thread,
    asccomm_thread_notify_record_on_thread,
    asccomm_thread_notify_wait_on_thread,
    asccomm_addr_notify_wait_on_thread,
    asccomm_addr_notify_record_on_thread,
    asccomm_write_on_thread,
    asccomm_write_reduce_on_thread,
    asccomm_write_with_notify_on_thread,
    asccomm_write_reduce_with_notify_on_thread,
    asccomm_read_on_thread,
    asccomm_read_reduce_on_thread,
    asccomm_batch_transfer_on_thread,
    asccomm_channel_notify_record_on_thread,
    asccomm_channel_notify_wait_on_thread,
    asccomm_channel_fence_on_thread,
    asccomm_channel_notify_wait_on_thread_with_default_timeout,
    asccomm_thread_notify_wait_on_thread_with_default_timeout,
    asccomm_set_notify_wait_timeout,
    asccomm_thread_res_acquire_timeout,
    asccomm_channel_drain_on_thread
};
void Record(Op op) noexcept;
void Invalidate() noexcept;
} // namespace AsccommBinaryCapture
#endif
