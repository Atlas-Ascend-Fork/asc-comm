/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_DEVICE_H
#define ASCCOMM_DEVICE_H

#include <stdbool.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool asccomm_is_sqe_profiling_enabled(void);
void asccomm_set_sqe_profiling(bool enabled);
int32_t asccomm_handle_profiling_command(uint32_t type, void* data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_DEVICE_H
