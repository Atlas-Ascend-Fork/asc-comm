/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_RESULT_CHECK_H
#define ASCCOMM_RESULT_CHECK_H

#ifndef LIKELY
#define LIKELY(condition) (static_cast<bool>(__builtin_expect(static_cast<bool>(condition), 1)))
#define UNLIKELY(condition) (static_cast<bool>(__builtin_expect(static_cast<bool>(condition), 0)))
#endif

#define CHK_PTR_NULL(ptr)       \
    do {                        \
        if ((ptr) == nullptr) { \
            return HCCL_E_PTR;  \
        }                       \
    } while (0)

#define CHK_PRT_RET(result, retCode) \
    do {                             \
        if (result) {                \
            return retCode;          \
        }                            \
    } while (0)

#define CHK_RET(call)                     \
    do {                                  \
        const HcclResult result = (call); \
        if (result != HCCL_SUCCESS) {     \
            return result;                \
        }                                 \
    } while (0)

#endif // ASCCOMM_RESULT_CHECK_H
