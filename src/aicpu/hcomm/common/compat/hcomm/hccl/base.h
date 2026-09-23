/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_BASE_H
#define HCCL_BASE_H
#include <hccl/hccl_types.h>

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

// NOTE: 基础整数别名被 dtype_common 及 AICPU 数据面实现依赖；rtStream_t 被 AicpuTsThread 和 Stream 依赖；
// hccl_types.h 被 dtype_common、task_param、mem_device_pub、log、ip_address 和 aicpu_hccl_sqcq 间接依赖。
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

/**
* @brief stream handle.
*/
typedef void *rtStream_t;

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // HCCL_BASE_H
