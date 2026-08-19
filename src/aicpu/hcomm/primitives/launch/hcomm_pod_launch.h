/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_POD_LAUNCH_H
#define HCOMM_POD_LAUNCH_H

#include "hcomm/hcomm_pod_primitives.h"
#include "hccl/hccl_types.h"

void hcomm_pod_register_thread(ThreadHandle thread);
bool hcomm_pod_is_batch_launch_mode();
uint32_t hcomm_pod_get_notify_wait_timeout();
void hcomm_pod_set_notify_wait_timeout(uint32_t timeout);
uint32_t hcomm_pod_get_sq_full_timeout();
void hcomm_pod_set_sq_full_timeout(uint32_t timeout);
void hcomm_pod_set_batch_launch_mode(bool batchMode);
HcclResult hcomm_pod_launch_registered_threads();
HcclResult hcomm_pod_try_launch_registered_threads();
void hcomm_pod_clear_registered_threads();

#endif // HCOMM_POD_LAUNCH_H
