/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_CPU_PRIMITIVES_H
#define ASCCOMM_CPU_PRIMITIVES_H

#include <stdint.h>

#if defined(__GNUC__)
#define ASCCOMM_CPU_API __attribute__((visibility("default")))
#else
#define ASCCOMM_CPU_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

ASCCOMM_CPU_API int32_t
asccomm_cpu_thread_notify_record_on_thread(uint64_t thread, uint64_t dstThread, uint32_t dstNotifyIdx);
ASCCOMM_CPU_API int32_t asccomm_cpu_thread_notify_wait_on_thread(uint64_t thread, uint32_t notifyIdx, uint32_t timeOut);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_CPU_PRIMITIVES_H
