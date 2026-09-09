/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the License).
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_CHANNEL_H
#define HCOMM_CCU_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "hcomm/hcomm_ccu_abi.h"
#include "hcomm/hcomm_res_entity_defs.h"

#ifndef CHANNEL_HANDLE_DEFINED
#define CHANNEL_HANDLE_DEFINED
// 通道句柄：不透明唯一标识，跨 SO 以整型值传递
typedef uint64_t ChannelHandle;
#endif

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

enum {
    HCOMM_CCU_CHANNEL_ABI_VERSION = 4,               // 当前 POD ABI 版本（定档重构）
    HCOMM_CCU_CHANNEL_POD_MAGIC_WORD = 0x43435543U,  // "CCUC"，标识 Channel POD 类型
    HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY = 16,        // 每个 channel 持有的 XN/CKE 资源槽位数（定档）
};

/**
 * @brief CCU Channel 单次查询快照 POD（定档口径：16 槽 + 计数 + 事件掩码 + 远端 buffer 数组）。
 *
 * localVarIds/remoteVarIds 为本/远端 XN 槽，localEventIds/remoteEventIds 为本/远端 CKE 槽，
 * 实际有效数量由 localVarNum/remoteVarNum/localEventNum/remoteEventNum 给出（<= 容量）。
 * localEventMasks/remoteEventMasks 为定档预留槽位：当前 mask 是 kernel 构建期的按次参数，
 * 非 channel 属性，hcomm 填 0，语义待后续定档补充。
 * rmtCcuResBuffer 为对端 CCU 资源空间（RegedBufferEntity 口径，取代旧散字段）。
 * remoteBuffers 为 hcomm channel 内部远端 buffer 列表的借用指针，channel 销毁后失效；
 * 本批数据面未消费（翻译器仍按单 CCU 资源空间假设），仅为定档形状预留。
 * 调用方提供 POD 存储并填好 ABI 头，hcomm 持锁一次填满快照，之后数据面只读该 POD。
 */
typedef struct {
    HcommCcuAbiHeaderPod header;  // ABI 头：version/magic/size 校验

    int32_t deviceLogicId;        // 所在设备逻辑 id
    uint32_t ccuVersion;          // CCU 版本（V1/V2）
    uint32_t dieId;               // die id
    uint32_t channelId;           // channel id（DFX 记录用）

    RegedBufferEntity rmtCcuResBuffer; // 对端 CCU 资源空间（基址/大小/token 折叠为 buffer 实体）

    uint32_t localVarNum;             // 本端 XN 有效数量（<= HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY）
    uint32_t remoteVarNum;            // 远端 XN 有效数量
    uint32_t localEventNum;           // 本端 CKE 有效数量
    uint32_t remoteEventNum;          // 远端 CKE 有效数量
    uint32_t remoteBufferNum;         // 远端 buffer 数量（remoteBuffers 数组长度）
    uint32_t reserved0;               // 对齐字段，当前必须为 0
    const RegedBufferEntity *remoteBuffers; // 远端 buffer 数组借用指针（hcomm 持有，本批数据面不消费）

    uint32_t localVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 本端 XN id 槽
    uint32_t remoteVarIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 远端 XN id 槽
    uint32_t localEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 本端 CKE id 槽
    uint32_t remoteEventIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE id 槽
    uint32_t localEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 本端 CKE 掩码槽（预留，填 0）
    uint32_t remoteEventMasks[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE 掩码槽（预留，填 0）

    uint64_t reserved[4];  // 预留扩展空间，当前必须清零
} HcommCcuChannelPod;

#ifdef __cplusplus
} // extern "C"

static_assert(std::is_standard_layout<HcommCcuChannelPod>::value,
    "HcommCcuChannelPod must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuChannelPod>::value,
    "HcommCcuChannelPod must be trivially copyable");
static_assert(sizeof(HcommCcuChannelPod) == 544, "HcommCcuChannelPod ABI size changed");
#endif

#endif // HCOMM_CCU_CHANNEL_H
