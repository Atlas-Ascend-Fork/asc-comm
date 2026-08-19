/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef CHANNEL_PARAM_H
#define CHANNEL_PARAM_H

#include <cstdint>

using u32 = uint32_t;
using s32 = int32_t;

constexpr u32 HCCL_AICPU_CHANNEL_HCOMID_MAX_LENGTH = 256;

// NOTE: 以下参数被 coll_comm_aicpu 依赖。
struct DevAicpuChannelConfig {
    // 如要新增配置类字段，在此处添加
};

struct HcclChannelUrmaRes {
    char  hcomId[HCCL_AICPU_CHANNEL_HCOMID_MAX_LENGTH]; // 通信域ID 最大长度待修改
    void* channelList;               // 反序列后返回给host侧的device侧handle地址
    u32   listNum = 0;               // 建链channel的总数量
    void* uniqueIdAddr;              // 序列化后device侧地址
    u32   uniqueIdSize{0};           // 序列化后总地址长度
    void* channelSizeAddr;            // 存放序列化后deivce channel size的指针
    u32*  remoteRankList;            // 序列化后返回给host侧的device侧rankList地址
    u32*  remoteRankId;              // 记录每个channel的对端rank
    s32   deviceLogicId{0};          // 基础通信使用
    u32   deviceType{0};             // 基础通信使用
    DevAicpuChannelConfig channelConfig; // 收编channel配置类变量
    void* ctxList{nullptr};          // device侧ctx指针数组，非null时kernel写ctx->deviceChannel
};

#endif
