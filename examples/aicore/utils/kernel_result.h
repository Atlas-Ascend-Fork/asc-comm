/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASC_COMM_EXAMPLES_UTILS_KERNEL_RESULT_H
#define ASC_COMM_EXAMPLES_UTILS_KERNEL_RESULT_H

#include <cstdint>

// kernel侧执行结果，随host下发的context在host与kernel间传递；各样例按需取用其中子集。
enum kernel_result : uint32_t {
    KERNEL_SUCCESS = 0U,
    KERNEL_INIT_FAILED,        // asccomm工作区初始化失败
    KERNEL_MAKE_HANDLE_FAILED, // 批量句柄创建失败
    KERNEL_WRITE_FAILED,       // asccomm_write_nbi失败
    KERNEL_READ_FAILED,        // asccomm_read_nbi失败
    KERNEL_COMMIT_FAILED,      // asccomm_commit/asccomm_batch_commit失败
    KERNEL_DRAIN_FAILED,       // asccomm_drain失败
    KERNEL_LOCK_FAILED,        // 通道锁获取失败
    KERNEL_UNLOCK_FAILED,      // 通道锁释放失败
};

static_assert(sizeof(kernel_result) == sizeof(uint32_t), "kernel_result must remain uint32_t-sized");

#endif // ASC_COMM_EXAMPLES_UTILS_KERNEL_RESULT_H
