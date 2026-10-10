/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RESULT_H
#define ASCCOMM_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ASCCOMM_SUCCESS = 0,
    ASCCOMM_E_PARA = 1,
    ASCCOMM_E_PTR = 2,
    ASCCOMM_E_MEMORY = 3,
    ASCCOMM_E_INTERNAL = 4,
    ASCCOMM_E_NOT_SUPPORT = 5,
    ASCCOMM_E_NOT_FOUND = 6,
    ASCCOMM_E_UNAVAIL = 7,
    ASCCOMM_E_SYSCALL = 8,
    ASCCOMM_E_TIMEOUT = 9,
    ASCCOMM_E_OPEN_FILE_FAILURE = 10,
    ASCCOMM_E_TCP_CONNECT = 11,
    ASCCOMM_E_ROCE_CONNECT = 12,
    ASCCOMM_E_TCP_TRANSFER = 13,
    ASCCOMM_E_ROCE_TRANSFER = 14,
    ASCCOMM_E_RUNTIME = 15,
    ASCCOMM_E_DRV = 16,
    ASCCOMM_E_PROFILING = 17,
    ASCCOMM_E_CCE = 18,
    ASCCOMM_E_NETWORK = 19,
    ASCCOMM_E_AGAIN = 20,
    ASCCOMM_E_REMOTE = 21,
    ASCCOMM_E_SUSPENDING = 22,
    ASCCOMM_E_OPRETRY_FAIL = 23,
    ASCCOMM_E_OOM = 24,
    ASCCOMM_E_IN_STATUS = 1041,
    ASCCOMM_E_RESERVED
} AsccommResult;

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_RESULT_H
