/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_COMPAT_TYPES_H
#define ASCCOMM_COMPAT_TYPES_H

#include <stdint.h>

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

#endif // ASCCOMM_COMPAT_TYPES_H
