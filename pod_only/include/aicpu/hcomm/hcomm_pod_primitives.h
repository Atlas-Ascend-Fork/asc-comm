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
#include "hcomm_pod_types.h"

#if defined(__GNUC__)
#define HCOMM_POD_API __attribute__((visibility("default")))
#else
#define HCOMM_POD_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef HCOMM_REDUCE_TYPES_DEFINED
#define HCOMM_REDUCE_TYPES_DEFINED
typedef enum {
    HCOMM_REDUCE_SUM = 0,
    HCOMM_REDUCE_PROD = 1,
    HCOMM_REDUCE_MAX = 2,
    HCOMM_REDUCE_MIN = 3,
    HCOMM_REDUCE_RESERVED = 255
} HcommReduceOp;

typedef enum {
    HCOMM_DATA_TYPE_INT8 = 0,
    HCOMM_DATA_TYPE_INT16 = 1,
    HCOMM_DATA_TYPE_INT32 = 2,
    HCOMM_DATA_TYPE_FP16 = 3,
    HCOMM_DATA_TYPE_FP32 = 4,
    HCOMM_DATA_TYPE_INT64 = 5,
    HCOMM_DATA_TYPE_UINT64 = 6,
    HCOMM_DATA_TYPE_UINT8 = 7,
    HCOMM_DATA_TYPE_UINT16 = 8,
    HCOMM_DATA_TYPE_UINT32 = 9,
    HCOMM_DATA_TYPE_FP64 = 10,
    HCOMM_DATA_TYPE_BFP16 = 11,
    HCOMM_DATA_TYPE_INT128 = 12,
    HCOMM_DATA_TYPE_HIF8 = 14,
    HCOMM_DATA_TYPE_FP8E4M3 = 15,
    HCOMM_DATA_TYPE_FP8E5M2 = 16,
    HCOMM_DATA_TYPE_FP8E8M0 = 17,
    HCOMM_DATA_TYPE_RESERVED = 255
} HcommDataType;
#endif

#ifndef HCOMM_BATCH_TRANSFER_TYPES_DEFINED
#define HCOMM_BATCH_TRANSFER_TYPES_DEFINED
typedef enum {
    HCOMM_TRANSFER_TYPE_INVALID = -1,
    HCOMM_TRANSFER_TYPE_WRITE = 0,
    HCOMM_TRANSFER_TYPE_WRITE_REDUCE = 1,
    HCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY = 2,
    HCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY = 3,
    HCOMM_TRANSFER_TYPE_READ = 4,
    HCOMM_TRANSFER_TYPE_READ_REDUCE = 5,
    HCOMM_TRANSFER_TYPE_NOTIFY_RECORD = 6,
} HcommTransferType;

typedef struct {
    HcommTransferType transType;
    uint8_t reserved[4];
    union {
        uint8_t raws[56];
        struct {
            uint64_t len;
            void *dst;
            void *src;
        } write;
        struct {
            uint64_t len;
            void *dst;
            void *src;
        } read;
        struct {
            uint64_t count;
            void *dst;
            void *src;
            HcommReduceOp reduceOp;
            HcommDataType dataType;
        } reduce;
        struct {
            uint32_t notifyIdx;
        } notifyRecord;
        struct {
            uint64_t len;
            void *dst;
            void *src;
            uint32_t notifyIdx;
        } writeWithNotify;
        struct {
            uint64_t count;
            void *dst;
            void *src;
            HcommReduceOp reduceOp;
            HcommDataType dataType;
            uint32_t notifyIdx;
        } writeReduceWithNotify;
    } transferInfo;
} HcommBatchTransferDesc;
#endif

/**
 * @brief Start batching POD primitives on the current execution thread.
 * @param[in] batchTag Reserved for compatibility; POD batching currently does not group threads by tag.
 */
HCOMM_POD_API int32_t HcommBatchModeStartPod(const char *batchTag);

/**
 * @brief Submit all POD threads registered since HcommBatchModeStartPod and leave batch mode.
 * @param[in] batchTag Reserved for compatibility; POD batching currently does not group threads by tag.
 */
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
HCOMM_POD_API int32_t HcommWriteReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
HCOMM_POD_API int32_t HcommWriteWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t len, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t HcommWriteReduceWithNotifyOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst,
    const void *src, uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp, uint32_t remoteNotifyIdx);
HCOMM_POD_API int32_t HcommReadOnThreadPod(
    ThreadHandle thread, ChannelHandle channel, void *dst, const void *src, uint64_t len);
HCOMM_POD_API int32_t HcommReadReduceOnThreadPod(ThreadHandle thread, ChannelHandle channel, void *dst, const void *src,
    uint64_t count, HcommDataType dataType, HcommReduceOp reduceOp);
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
