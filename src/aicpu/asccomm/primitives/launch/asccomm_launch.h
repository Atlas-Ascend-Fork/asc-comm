/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef ASCCOMM_LAUNCH_H
#define ASCCOMM_LAUNCH_H

#include "asccomm_primitives.h"
#include "../../common/utils/asccomm_result.h"

// 登记batch模式批量launch的ThreadHandle；句柄值为控制面提供的资源实体地址。
void asccomm_register_thread(ThreadHandle thread);
bool asccomm_is_batch_launch_mode();
uint32_t asccomm_get_notify_wait_timeout();
void asccomm_set_notify_wait_timeout(uint32_t timeout);
uint32_t asccomm_get_sq_full_timeout();
void asccomm_set_sq_full_timeout(uint32_t timeout);
void asccomm_set_batch_launch_mode(bool batchMode);
AsccommResult asccomm_launch_registered_threads();
AsccommResult asccomm_try_launch_registered_threads();
void asccomm_clear_registered_threads();

#endif // ASCCOMM_LAUNCH_H
