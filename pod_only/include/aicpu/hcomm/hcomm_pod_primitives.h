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

#if defined(__has_include)
#if __has_include("../../../../comm_api/aicpu/hcomm/hcomm_primitives.h")
#include "../../../../comm_api/aicpu/hcomm/hcomm_primitives.h"
#elif __has_include("hcomm_primitives.h")
#include "hcomm_primitives.h"
#elif __has_include("hccl/internal/hcomm/hcomm_primitives.h")
#include "hccl/internal/hcomm/hcomm_primitives.h"
#else
#error "hcomm_primitives.h not found"
#endif
#else
#include "hcomm_primitives.h"
#endif

#if defined(__GNUC__)
#define HCOMM_POD_API __attribute__((visibility("default")))
#else
#define HCOMM_POD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

HCOMM_POD_API int32_t HcommBatchModeStartPod(const char *batchTag);
HCOMM_POD_API int32_t HcommBatchModeEndPod(const char *batchTag);

HCOMM_POD_API int32_t HcommLocalCopyOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t HcommLocalReduceOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t count,
    HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t HcommThreadNotifyRecordOnThreadPod(
    ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx);
HCOMM_POD_API int32_t HcommThreadNotifyWaitOnThreadPod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut);
HCOMM_POD_API int32_t HcommAclrtNotifyRecordOnThreadPod(ThreadHandle thread, uint64_t dstNotifyId);
HCOMM_POD_API int32_t HcommAclrtNotifyWaitOnThreadPod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut);
HCOMM_POD_API int32_t HcommWriteOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t HcommWriteReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t HcommWriteWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t len, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t HcommWriteReduceWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t HcommReadOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t HcommReadReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t HcommBatchTransferOnThreadPod(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum);
HCOMM_POD_API int32_t HcommChannelNotifyRecordOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t HcommChannelNotifyWaitOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut);
HCOMM_POD_API int32_t HcommChannelFenceOnThreadPod(ThreadHandle thread, ChannelHandle channel);
HCOMM_POD_API int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeoutPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx);
HCOMM_POD_API int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeoutPod(ThreadHandle thread, uint32_t notifyIdx);
HCOMM_POD_API int32_t HcommChannelDrainOnThreadPod(ThreadHandle thread, ChannelHandle channel);

#ifdef __cplusplus
}
#endif

#endif // HCOMM_POD_PRIMITIVES_H
