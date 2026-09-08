/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

/**
 * @file hcomm_types.h
 * @brief hcomm 包内公共基础类型定义（HcclResult / HcclComm / HcclReduceOp / HcclDataType）。
 *
 * 控制面/数据面解耦后，跨 SO（hcomm ↔ asc-comm）共享的基础类型统一由本头提供，
 * 替代原先散落在 hccl/hccl_types.h 中的定义，保证两仓按同一份 POD ABI 演进，
 * 且 hcomm 不再反向依赖 hccl 内部的类型头。
 */
#ifndef HCOMM_TYPES_H
#define HCOMM_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

/** HCCL 接口返回码：0 成功，其余为错误码 */
typedef enum {
    HCCL_SUCCESS = 0,
    HCCL_E_PARA = 1,
    HCCL_E_PTR = 2,
    HCCL_E_MEMORY = 3,
    HCCL_E_INTERNAL = 4,
    HCCL_E_NOT_SUPPORT = 5,
    HCCL_E_NOT_FOUND = 6,
    HCCL_E_UNAVAIL = 7,
    HCCL_E_SYSCALL = 8,
    HCCL_E_TIMEOUT = 9,
    HCCL_E_OPEN_FILE_FAILURE = 10,
    HCCL_E_TCP_CONNECT = 11,
    HCCL_E_ROCE_CONNECT = 12,
    HCCL_E_TCP_TRANSFER = 13,
    HCCL_E_ROCE_TRANSFER = 14,
    HCCL_E_RUNTIME = 15,
    HCCL_E_DRV = 16,
    HCCL_E_PROFILING = 17,
    HCCL_E_CCE = 18,
    HCCL_E_NETWORK = 19,
    HCCL_E_AGAIN = 20,
    HCCL_E_REMOTE = 21,
    HCCL_E_SUSPENDING = 22,
    HCCL_E_OPRETRY_FAIL = 23,
    HCCL_E_OOM = 24,
    HCCL_E_IN_STATUS = 1041,
    HCCL_E_RESERVED
} HcclResult;

/** HCCL 通信域句柄（不透明指针） */
typedef void* HcclComm;

/** HCCL 归约操作类型 */
typedef enum {
    HCCL_REDUCE_SUM = 0,
    HCCL_REDUCE_PROD = 1,
    HCCL_REDUCE_MAX = 2,
    HCCL_REDUCE_MIN = 3,
    HCCL_REDUCE_RESERVED = 255
} HcclReduceOp;

/** HCCL 数据类型枚举 */
typedef enum {
    HCCL_DATA_TYPE_INT8 = 0,
    HCCL_DATA_TYPE_INT16 = 1,
    HCCL_DATA_TYPE_INT32 = 2,
    HCCL_DATA_TYPE_FP16 = 3,
    HCCL_DATA_TYPE_FP32 = 4,
    HCCL_DATA_TYPE_INT64 = 5,
    HCCL_DATA_TYPE_UINT64 = 6,
    HCCL_DATA_TYPE_UINT8 = 7,
    HCCL_DATA_TYPE_UINT16 = 8,
    HCCL_DATA_TYPE_UINT32 = 9,
    HCCL_DATA_TYPE_FP64 = 10,
    HCCL_DATA_TYPE_BFP16 = 11,
    HCCL_DATA_TYPE_INT128 = 12,
    HCCL_DATA_TYPE_HIF8 = 14,
    HCCL_DATA_TYPE_FP8E4M3 = 15,
    HCCL_DATA_TYPE_FP8E5M2 = 16,
    HCCL_DATA_TYPE_FP8E8M0 = 17,
    HCCL_DATA_TYPE_RESERVED = 255
} HcclDataType;

#ifdef __cplusplus
}
#endif

#endif // HCOMM_TYPES_H
