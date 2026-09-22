/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_CCU_UT_LEGACY_COMPAT_H
#define ASCCOMM_CCU_UT_LEGACY_COMPAT_H

#include <stdint.h>

#include "ccu_api_exception.h"
#include "const_val.h"
#include "exception_util.h"
#include "log.h"

using u32 = uint32_t;

#define CHK_PRT_THROW(condition_, log_action, exception, message) \
    do {                                                          \
        if (condition_) {                                         \
            log_action;                                           \
            throw exception(message);                             \
        }                                                         \
    } while (0)

#define CHK_PRT_RET(condition_, log_action, result) \
    do {                                            \
        if (condition_) {                           \
            log_action;                             \
            return result;                          \
        }                                           \
    } while (0)

#endif
