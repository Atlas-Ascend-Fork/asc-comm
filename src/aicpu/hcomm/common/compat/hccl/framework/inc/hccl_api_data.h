/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_API_DATA_H
#define HCCL_API_DATA_H

// NOTE: HcommLaunchMode 被 launch_context 和 aicpu_ts_primitives_c_adpt 依赖。
typedef enum {
    HCOMM_LAUNCH_MODE_RESERVED = -1,
    HCOMM_LAUNCH_MODE_EAGER = 0,
    HCOMM_LAUNCH_MODE_BATCH
} HcommLaunchMode;

#endif  // HCCL_API_DATA_H
