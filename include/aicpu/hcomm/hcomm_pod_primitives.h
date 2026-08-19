/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_PRIMITIVES_H
#define HCOMM_POD_PRIMITIVES_H

#ifdef HCOMM_POD_SOURCE_BUILD
#include "hcomm_primitives.h"
#else
#include <hccl/hcomm_primitives.h>
#endif

#if defined(__GNUC__)
#define HCOMM_POD_API __attribute__((visibility("default")))
#else
#define HCOMM_POD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

HCOMM_POD_API int32_t hcomm_batch_mode_start_pod(const char *batchTag);
HCOMM_POD_API int32_t hcomm_batch_mode_end_pod(const char *batchTag);

HCOMM_POD_API int32_t hcomm_local_copy_on_thread_pod(ThreadHandle thread, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t hcomm_local_reduce_on_thread_pod(ThreadHandle thread, void *dst, const void *src, uint64_t count,
    HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t hcomm_thread_notify_record_on_thread_pod(
    ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx);
HCOMM_POD_API int32_t hcomm_thread_notify_wait_on_thread_pod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut);
HCOMM_POD_API int32_t hcomm_aclrt_notify_record_on_thread_pod(ThreadHandle thread, uint64_t dstNotifyId);
HCOMM_POD_API int32_t hcomm_aclrt_notify_wait_on_thread_pod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut);
HCOMM_POD_API int32_t hcomm_write_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t hcomm_write_reduce_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t hcomm_write_with_notify_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t len, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t hcomm_write_reduce_with_notify_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t hcomm_read_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t hcomm_read_reduce_on_thread_pod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t hcomm_batch_transfer_on_thread_pod(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum);
HCOMM_POD_API int32_t hcomm_channel_notify_record_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t hcomm_channel_notify_wait_on_thread_pod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut);
HCOMM_POD_API int32_t hcomm_channel_fence_on_thread_pod(ThreadHandle thread, ChannelHandle channel);
HCOMM_POD_API int32_t hcomm_channel_notify_wait_on_thread_with_default_timeout_pod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx);
HCOMM_POD_API int32_t hcomm_thread_notify_wait_on_thread_with_default_timeout_pod(ThreadHandle thread, uint32_t notifyIdx);
HCOMM_POD_API int32_t hcomm_set_notify_wait_timeout_pod(float timeOut);
HCOMM_POD_API int32_t hcomm_thread_res_acquire_timeout_pod(float timeOut);
HCOMM_POD_API int32_t hcomm_channel_drain_on_thread_pod(ThreadHandle thread, ChannelHandle channel);

#ifdef __cplusplus
}
#endif

#endif // HCOMM_POD_PRIMITIVES_H
