/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef AICPU_LAUNCH_MANAGER_H
#define AICPU_LAUNCH_MANAGER_H

#include "thread.h"
#include "aicpu_init_param.h"

// NOTE: 以下参数被 aicpu_indop_process 和 coll_comm_aicpu 依赖。
constexpr uint32_t THREAD_UNIQUE_ID_MAX_SIZE = 6000;
constexpr uint32_t NOTIFY_UNIQUE_ID_MAX_SIZE = THREAD_UNIQUE_ID_MAX_SIZE * hccl::HCCL_THREAD_NOTIFY_MAX_NUM;
constexpr uint32_t SIGNAL_DEV_STREAM_MAX_NUM = 200;
struct DevAicpuThreadConfig {
    // 如要新增配置类字段，在此处添加
};

struct ThreadMgrAicpuParam {
    u32 threadNum;
    char hcomId[HCOMID_MAX_SIZE];
    char threadParam[SIGNAL_DEV_STREAM_MAX_NUM][THREAD_UNIQUE_ID_MAX_SIZE]; // 含序列化后thread信息，约40KB
    void* deviceHandle;
    s32 deviceLogicId{-1}; // 基础通信使用
    u32 deviceType{0}; // 基础通信使用
    DevAicpuThreadConfig threadConfig; // 收编thread配置类变量
};

struct DevAicpuNotifyConfig {
    // 如要新增配置类字段，在此处添加
};

struct NotifyMgrAicpuParam {
    u32 notifyNum;
    char hcomId[HCOMID_MAX_SIZE];
    char notifyParam[NOTIFY_UNIQUE_ID_MAX_SIZE]; // 含序列化后notify信息
    void* deviceHandle;
    bool freeFlag;
    DevAicpuNotifyConfig notifyConfig; // 收编notify配置类变量
};
#endif
