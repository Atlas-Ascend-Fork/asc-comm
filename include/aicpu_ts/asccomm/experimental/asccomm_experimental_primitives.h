/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_EXPERIMENTAL_PRIMITIVES_H
#define ASCCOMM_EXPERIMENTAL_PRIMITIVES_H

#include "../asccomm_res_defs.h"

#if defined(__GNUC__)
#define ASCCOMM_EXPERIMENTAL_API __attribute__((visibility("default")))
#else
#define ASCCOMM_EXPERIMENTAL_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * WARNING: experimental API, No compatibility is currently guaranteed for this API
 */
ASCCOMM_EXPERIMENTAL_API int32_t
asccomm_send_on_thread(ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len);
/**
 * WARNING: experimental API, No compatibility is currently guaranteed for this API
 */
ASCCOMM_EXPERIMENTAL_API int32_t asccomm_send_with_imm_on_thread(
    ThreadHandle thread, ChannelHandle channel, const void* src, uint64_t len, uint64_t immData);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_EXPERIMENTAL_PRIMITIVES_H
