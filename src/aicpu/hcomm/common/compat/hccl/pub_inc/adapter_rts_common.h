/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_INC_ADAPTER_RTS_COMMON_H
#define HCCL_INC_ADAPTER_RTS_COMMON_H

#include "dtype_common.h"

// NOTE: HcclRtMemcpyKind 被 task_logic_info 依赖。
enum class HcclRtMemcpyKind {
    HCCL_RT_MEMCPY_KIND_HOST_TO_HOST = 0, /**< host to host */
    HCCL_RT_MEMCPY_KIND_HOST_TO_DEVICE,   /**< host to device */
    HCCL_RT_MEMCPY_KIND_DEVICE_TO_HOST,   /**< device to host */
    HCCL_RT_MEMCPY_KIND_DEVICE_TO_DEVICE, /**< device to device */
    HCCL_RT_MEMCPY_ADDR_DEVICE_TO_DEVICE, /**< Level-2 address copy, device to device */
    HCCL_RT_MEMCPY_KIND_RESERVED,
};

#ifdef __cplusplus
extern "C" {
#endif
// NOTE: 以下运行时接口被 AicpuTsThread、CollCommAicpu 和 Thread 依赖。
HcclResult hrtGetDevice(s32 *deviceLogicId);
HcclResult hrtSetlocalDeviceType(DevType devType);
HcclResult hrtSetlocalDevice(s32 deviceLogicId);
HcclResult hrtSetWorkModeAicpu(bool workModeAicpu);
HcclResult hrtGetDevicePhyIdByIndex(u32 deviceLogicId, u32 &devicePhyId, bool isRefresh = false);
HcclResult hrtGetTaskIdAndStreamID(u32 &taskId, u32 &streamId);
#ifdef __cplusplus
}  // extern "C"
#endif
#endif  // HCCL_INC_ADAPTER_RTS_COMMON_H
