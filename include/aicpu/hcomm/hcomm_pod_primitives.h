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

#include <stdint.h>
#include "hcomm_primitives.h"


#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start batching POD primitives on the current execution thread.
 * @param[in] batchTag Reserved for compatibility; POD batching currently does not group threads by tag.
 */
extern int32_t HcommBatchModeStartPod(const char *batchTag);

/**
 * @brief Submit all POD threads registered since HcommBatchModeStartPod and leave batch mode.
 * @param[in] batchTag Reserved for compatibility; POD batching currently does not group threads by tag.
 */
extern int32_t HcommBatchModeEndPod(const char *batchTag);

extern int32_t HcommLocalCopyOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t len);
extern int32_t HcommLocalReduceOnThreadPod(ThreadHandle thread, void *dst, const void *src, uint64_t count,
    HcommDataType dataType, HcommReduceOp reduceOp);
extern int32_t HcommThreadNotifyRecordOnThreadPod(
    ThreadHandle thread, ThreadHandle dstThread, uint32_t dstNotifyIdx);
extern int32_t HcommThreadNotifyWaitOnThreadPod(ThreadHandle thread, uint32_t notifyIdx, uint32_t timeOut);
extern int32_t HcommAclrtNotifyRecordOnThreadPod(ThreadHandle thread, uint64_t dstNotifyId);
extern int32_t HcommAclrtNotifyWaitOnThreadPod(ThreadHandle thread, uint64_t notifyId, uint32_t timeOut);
extern int32_t HcommWriteOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
extern int32_t HcommWriteReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
extern int32_t HcommWriteWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t len, uint32_t remoteNotifyIdx);
extern int32_t HcommWriteReduceWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx);
extern int32_t HcommReadOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
extern int32_t HcommReadReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
extern int32_t HcommBatchTransferOnThreadPod(ThreadHandle thread, ChannelHandle channel,
    const HcommBatchTransferDesc *transferDescs, uint32_t transferDescNum);
extern int32_t HcommChannelNotifyRecordOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t remoteNotifyIdx);
extern int32_t HcommChannelNotifyWaitOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx, uint32_t timeOut);
extern int32_t HcommChannelFenceOnThreadPod(ThreadHandle thread, ChannelHandle channel);
extern int32_t HcommChannelNotifyWaitOnThreadWithDefaultTimeoutPod(
    ThreadHandle thread, ChannelHandle channel, uint32_t localNotifyIdx);
extern int32_t HcommThreadNotifyWaitOnThreadWithDefaultTimeoutPod(ThreadHandle thread, uint32_t notifyIdx);
extern int32_t HcommChannelDrainOnThreadPod(ThreadHandle thread, ChannelHandle channel);

#ifdef __cplusplus
}
#endif

#endif // HCOMM_POD_PRIMITIVES_H
