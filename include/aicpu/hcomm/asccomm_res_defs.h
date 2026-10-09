/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RES_DEFS_H
#define ASCCOMM_RES_DEFS_H

#include <stdint.h>

#include "hcomm_res_defs.h"
#include "hcomm_res_entity_defs.h"

// ASCCOMM AICPU公共类型定义；基础ABI类型由CANN包pkg_inc/hcomm/hcomm_res_defs.h提供。
#ifdef __cplusplus
extern "C" {
#endif

#ifndef ASCCOMM_REDUCE_OP_DEFINED
#define ASCCOMM_REDUCE_OP_DEFINED
typedef enum {
    ASCCOMM_REDUCE_SUM = 0,
    ASCCOMM_REDUCE_PROD = 1,
    ASCCOMM_REDUCE_MAX = 2,
    ASCCOMM_REDUCE_MIN = 3,
    ASCCOMM_REDUCE_RESERVED = 255
} AsccommReduceOp;
#endif

#ifndef ASCCOMM_DATA_TYPE_DEFINED
#define ASCCOMM_DATA_TYPE_DEFINED
typedef enum {
    ASCCOMM_DATA_TYPE_INT8 = 0,
    ASCCOMM_DATA_TYPE_INT16 = 1,
    ASCCOMM_DATA_TYPE_INT32 = 2,
    ASCCOMM_DATA_TYPE_FP16 = 3,
    ASCCOMM_DATA_TYPE_FP32 = 4,
    ASCCOMM_DATA_TYPE_INT64 = 5,
    ASCCOMM_DATA_TYPE_UINT64 = 6,
    ASCCOMM_DATA_TYPE_UINT8 = 7,
    ASCCOMM_DATA_TYPE_UINT16 = 8,
    ASCCOMM_DATA_TYPE_UINT32 = 9,
    ASCCOMM_DATA_TYPE_FP64 = 10,
    ASCCOMM_DATA_TYPE_BFP16 = 11,
    ASCCOMM_DATA_TYPE_INT128 = 12,
    ASCCOMM_DATA_TYPE_HIF8 = 14,
    ASCCOMM_DATA_TYPE_FP8E4M3 = 15,
    ASCCOMM_DATA_TYPE_FP8E5M2 = 16,
    ASCCOMM_DATA_TYPE_FP8E8M0 = 17,
    ASCCOMM_DATA_TYPE_RESERVED = 255
} AsccommDataType;
#endif

#ifndef ASCCOMM_TRANSFER_TYPE_DEFINED
#define ASCCOMM_TRANSFER_TYPE_DEFINED
typedef enum {
    ASCCOMM_TRANSFER_TYPE_INVALID = -1,
    ASCCOMM_TRANSFER_TYPE_WRITE = 0,
    ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE = 1,
    ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY = 2,
    ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY = 3,
    ASCCOMM_TRANSFER_TYPE_READ = 4,
    ASCCOMM_TRANSFER_TYPE_READ_REDUCE = 5,
    ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD = 6,
} AsccommTransferType;
#endif

#ifndef ASCCOMM_BATCH_TRANSFER_DESC_DEFINED
#define ASCCOMM_BATCH_TRANSFER_DESC_DEFINED
typedef struct {
    AsccommTransferType transType;
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
            AsccommReduceOp reduceOp;
            AsccommDataType dataType;
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
            AsccommReduceOp reduceOp;
            AsccommDataType dataType;
            uint32_t notifyIdx;
        } writeReduceWithNotify;
    } transferInfo;
} AsccommBatchTransferDesc;
#endif

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_RES_DEFS_H
