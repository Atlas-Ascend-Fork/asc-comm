/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_PRIMITIVES_H
#define ASCCOMM_PRIMITIVES_H

#include "asccomm_res_defs.h"

#if defined(__GNUC__)
#define ASCCOMM_API __attribute__((visibility("default")))
#else
#define ASCCOMM_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

ASCCOMM_API int32_t asccomm_batch_mode_start(const char* batchTag);
ASCCOMM_API int32_t asccomm_batch_mode_end(const char* batchTag);

ASCCOMM_API int32_t asccomm_local_copy_on_thread(ThreadHandle thread, void* dst, const void* src, uint64_t len);
ASCCOMM_API int32_t asccomm_local_reduce_on_thread(
    ThreadHandle thread, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp);
ASCCOMM_API int32_t
asccomm_thread_notify_record_on_thread(ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx);
ASCCOMM_API int32_t asccomm_thread_notify_wait_on_thread(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut);
ASCCOMM_API int32_t asccomm_aclrt_notify_record_on_thread(ThreadHandle thread, uint64_t dstNotifyId);
ASCCOMM_API int32_t asccomm_aclrt_notify_wait_on_thread(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut);
ASCCOMM_API int32_t
asccomm_write_on_thread(ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len);
ASCCOMM_API int32_t asccomm_write_reduce_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp);
ASCCOMM_API int32_t asccomm_write_with_notify_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len, uint32_t remoteNotifyIdx);
ASCCOMM_API int32_t asccomm_write_reduce_with_notify_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp, uint32_t remoteNotifyIdx);
ASCCOMM_API int32_t
asccomm_read_on_thread(ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t len);
ASCCOMM_API int32_t asccomm_read_reduce_on_thread(
    ThreadHandle thread, ChannelHandle channel, void* dst, const void* src, uint64_t count, AsccommDataType dataType,
    AsccommReduceOp reduceOp);
ASCCOMM_API int32_t asccomm_batch_transfer_on_thread(
    ThreadHandle thread, ChannelHandle channel, const AsccommBatchTransferDesc* transferDescs,
    uint32_t transferDescNum);
ASCCOMM_API int32_t
asccomm_channel_notify_record_on_thread(ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx);
ASCCOMM_API int32_t asccomm_channel_notify_wait_on_thread(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut);
ASCCOMM_API int32_t asccomm_channel_fence_on_thread(ThreadHandle thread, ChannelHandle channel);
ASCCOMM_API int32_t asccomm_channel_notify_wait_on_thread_with_default_timeout(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx);
ASCCOMM_API int32_t asccomm_thread_notify_wait_on_thread_with_default_timeout(ThreadHandle thread, uint32_t notifyIdx);
ASCCOMM_API int32_t asccomm_set_notify_wait_timeout(float timeOut);
ASCCOMM_API int32_t asccomm_thread_res_acquire_timeout(float timeOut);
ASCCOMM_API int32_t asccomm_channel_drain_on_thread(ThreadHandle thread, ChannelHandle channel);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_PRIMITIVES_H
