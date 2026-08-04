/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCCL_COMMON_H
#define HCCL_COMMON_H

#include "hccl/base.h"
#include "dtype_common.h"
#include "log/hcomm_log.h"

// NOTE: 以下公共类型和常量被 AICPU 数据面各模块依赖。
using char_t = char;
using HcclRtNotify = void *;
using HcclRtContext = void *;
using HcclDispatcher = void *;

constexpr u32 INVALID_VALUE_RANKID = 0xFFFFFFFF; // rank id非法值
constexpr u32 INVALID_UINT = 0xFFFFFFFF;
constexpr u64 INVALID_U64 = 0xFFFFFFFFFFFFFFFF;
constexpr s32 INVALID_VALUE_STAGE = -1;
constexpr u32 NOTIFY_INVALID_WAIT_TIME = 0xFFFFFFFF; // notifywait时长非法值

namespace hccl {
enum class NotifyLoadType {
    HOST_NOTIFY = 0,
    DEVICE_NOTIFY
};
}  // namespace hccl

struct HcclSignalInfo {
    u64 resId; // 在代表event时为eventid，notify时为notifyid
    u64 addr;
    u32 devId;
    u32 tsId;
    u32 rankId;
    u32 flag;
};

struct HcclComStreamInfo {
    int32_t actualStreamId; // 实际streamid
    int32_t sqId;
    uint32_t sqDepth;
    void *sqBaseAddr;
    u32 logicCqId; // 记录逻辑cqId
};

#endif  // HCCL_COMMON_H
