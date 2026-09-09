/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef HCOMM_CCU_CHANNEL_H
#define HCOMM_CCU_CHANNEL_H

#include <stddef.h>
#include <stdint.h>

#include "hcomm/hcomm_ccu_abi.h"

// 旧版两阶段查询使用的魔数与版本（保留以兼容既有调用方）
static const uint32_t HCOMM_CCU_CHANNEL_MAGIC_WORD = 0x0fcf0f0fU;
static const uint32_t HCOMM_CCU_CHANNEL_VERSION = 3U;

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
    HCOMM_CCU_CHANNEL_ABI_VERSION = 3,              // 当前 POD ABI 版本
    HCOMM_CCU_CHANNEL_POD_MAGIC_WORD = 0x43435543U, // "CCUC"，标识 Channel POD 类型
    HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY = 4,        // 每个 channel 固定持有的 XN/CKE 资源槽数量
};

/**
 * @brief CCU Channel 单次查询快照 POD（channel pop 优化后的定长结构）。
 *
 * 相比旧 ChannelEntity 的两阶段查询（先查数量再传缓冲区），本 POD 把
 * channel 的全部资源信息压缩为定长字段：本地/远端 XN、CKE id 各固定 4 槽，
 * 外加远端 CCU buffer 的基址/大小/token。调用方无需准备变长缓冲区，
 * hcomm 在持锁期间一次性填满快照，之后数据面只读该 POD。
 */
typedef struct {
    HcommCcuAbiHeaderPod header; // ABI 头：version/magic/size 校验

    int32_t deviceLogicId; // 所在设备逻辑 id
    uint32_t ccuVersion;   // CCU 版本（V1/V2）
    uint32_t dieId;        // die id
    uint32_t channelId;    // channel id（DFX 记录用）

    uint32_t localXnIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];   // 本端 XN id 槽
    uint32_t remoteXnIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 远端 XN id 槽
    uint32_t localCkeIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY];  // 本端 CKE id 槽
    uint32_t remoteCkeIds[HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY]; // 远端 CKE id 槽

    uint64_t rmtCcuBufAddr;       // 远端 CCU 资源 buffer 基址
    uint32_t rmtCcuBufSize;       // 远端 CCU 资源 buffer 大小
    uint32_t rmtCcuBufTokenId;    // 远端 CCU buffer 保护 token id
    uint32_t rmtCcuBufTokenValue; // 远端 CCU buffer 保护 token value
    uint32_t reserved[3];         // 预留扩展空间，当前必须清零
} HcommCcuChannelPod;

#ifdef __cplusplus
}

static_assert(std::is_standard_layout<HcommCcuChannelPod>::value, "HcommCcuChannelPod must be standard-layout");
static_assert(std::is_trivially_copyable<HcommCcuChannelPod>::value, "HcommCcuChannelPod must be trivially copyable");
static_assert(sizeof(HcommCcuChannelPod) == 128, "HcommCcuChannelPod ABI size changed");
static_assert(offsetof(HcommCcuChannelPod, header) == 0, "HcommCcuChannelPod.header ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, localXnIds) == 32, "HcommCcuChannelPod.localXnIds ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, remoteXnIds) == 48, "HcommCcuChannelPod.remoteXnIds ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, localCkeIds) == 64, "HcommCcuChannelPod.localCkeIds ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, remoteCkeIds) == 80, "HcommCcuChannelPod.remoteCkeIds ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, rmtCcuBufAddr) == 96, "HcommCcuChannelPod.rmtCcuBufAddr ABI offset changed");
static_assert(offsetof(HcommCcuChannelPod, reserved) == 116, "HcommCcuChannelPod.reserved ABI offset changed");
#endif

#endif // HCOMM_CCU_CHANNEL_H
