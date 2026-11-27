/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_PRIMITIVES_INTERNAL_H
#define ASCCOMM_PRIMITIVES_INTERNAL_H

// 内部编排原语（地址语义 notify），仅框架组件（mc2_server）消费；刻意不进公共头契约，
// 调用方需 ASCCOMM_ENABLE_INTERNAL_APIS 显式开启。转正路径：发布本头或撤掉宏门控。
#include "asccomm_primitives.h" // ThreadHandle and the remaining primitives stay in the public header

#ifdef ASCCOMM_ENABLE_INTERNAL_APIS

#ifdef __cplusplus
extern "C" {
#endif

ASCCOMM_API int32_t
asccomm_addr_notify_wait_on_thread(ThreadHandle thread, uint64_t waitAddr, uint64_t valueAddr, uint8_t last);
ASCCOMM_API int32_t asccomm_addr_notify_record_on_thread(ThreadHandle thread, uint64_t recordAddr, uint64_t valueAddr);

#ifdef __cplusplus
}
#endif

#endif // ASCCOMM_ENABLE_INTERNAL_APIS

#endif // ASCCOMM_PRIMITIVES_INTERNAL_H
