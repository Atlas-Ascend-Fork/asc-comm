/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_AICPU_RES_DEFS_H
#define ASCCOMM_AICPU_RES_DEFS_H

#include <stdint.h>

// 数据面随仓副本：hcomm侧的hcomm_res_defs.h复用include/hcomm_res_defs.h
// 权威定义(带guard宏)。数据面编译环境无该头，此副本提供同构定义。
// 字段/枚举值必须与hcomm_res_defs.h保持一致。该副本使用独立guard，避免屏蔽控制面完整定义。
#ifdef __cplusplus
extern "C" {
#endif

#ifndef CHANNEL_HANDLE_DEFINED
#define CHANNEL_HANDLE_DEFINED
typedef uint64_t ChannelHandle;
#endif

#ifndef THREAD_HANDLE_DEFINED
#define THREAD_HANDLE_DEFINED
typedef uint64_t ThreadHandle;
#endif

#ifndef COMM_ABI_HEADER_DEFINED
#define COMM_ABI_HEADER_DEFINED
typedef struct {
    uint32_t version;
    uint32_t magicWord;
    uint32_t size;
    uint32_t reserved;
} CommAbiHeader;
#endif

#ifndef COMM_ENGINE_DEFINED
#define COMM_ENGINE_DEFINED
typedef enum {
    COMM_ENGINE_RESERVED = -1,
    COMM_ENGINE_CPU = 0,
    COMM_ENGINE_CPU_TS = 1,
    COMM_ENGINE_AICPU = 2,
    COMM_ENGINE_AICPU_TS = 3,
    COMM_ENGINE_AIV = 4,
    COMM_ENGINE_CCU = 5,
} CommEngine;
#endif

#ifndef COMM_PROTOCOL_DEFINED
#define COMM_PROTOCOL_DEFINED
typedef enum {
    COMM_PROTOCOL_RESERVED = -1,
    COMM_PROTOCOL_HCCS = 0,
    COMM_PROTOCOL_ROCE = 1,
    COMM_PROTOCOL_PCIE = 2,
    COMM_PROTOCOL_SIO = 3,
    COMM_PROTOCOL_UB_CTP = 4,
    COMM_PROTOCOL_UBC_CTP = COMM_PROTOCOL_UB_CTP,
    COMM_PROTOCOL_UBC_TP = 5,
    COMM_PROTOCOL_UB_MEM = 6,
    COMM_PROTOCOL_UBOE = 7,
    COMM_PROTOCOL_HCCS_ONLY = 8,
    COMM_PROTOCOL_UB_RTP = 9,
    COMM_PROTOCOL_UBG = COMM_PROTOCOL_UB_RTP,
} CommProtocol;
#endif

#ifndef HCOMM_REDUCE_OP_DEFINED
#define HCOMM_REDUCE_OP_DEFINED
typedef enum {
    HCOMM_REDUCE_SUM = 0,
    HCOMM_REDUCE_PROD = 1,
    HCOMM_REDUCE_MAX = 2,
    HCOMM_REDUCE_MIN = 3,
    HCOMM_REDUCE_RESERVED = 255
} HcommReduceOp;
#endif

#ifndef HCOMM_DATA_TYPE_DEFINED
#define HCOMM_DATA_TYPE_DEFINED
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

#ifndef HCOMM_TRANSFER_TYPE_DEFINED
#define HCOMM_TRANSFER_TYPE_DEFINED
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
#endif

#ifndef HCOMM_BATCH_TRANSFER_DESC_DEFINED
#define HCOMM_BATCH_TRANSFER_DESC_DEFINED
typedef struct {
    HcommTransferType transType;
    uint8_t reserved[4];
    union {
        uint8_t raws[56];
        struct {
            uint64_t len;
            void* dst;
            void* src;
        } write;
        struct {
            uint64_t len;
            void* dst;
            void* src;
        } read;
        struct {
            uint64_t count;
            void* dst;
            void* src;
            HcommReduceOp reduceOp;
            HcommDataType dataType;
        } reduce;
        struct {
            uint32_t notifyIdx;
        } notifyRecord;
        struct {
            uint64_t len;
            void* dst;
            void* src;
            uint32_t notifyIdx;
        } writeWithNotify;
        struct {
            uint64_t count;
            void* dst;
            void* src;
            HcommReduceOp reduceOp;
            HcommDataType dataType;
            uint32_t notifyIdx;
        } writeReduceWithNotify;
    } transferInfo;
} HcommBatchTransferDesc;
#endif

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_AICPU_RES_DEFS_H
