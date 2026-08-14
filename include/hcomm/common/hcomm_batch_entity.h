/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#ifndef INCLUDE_HCOMM_COMMON_HCOMM_BATCH_ENTITY_H
#define INCLUDE_HCOMM_COMMON_HCOMM_BATCH_ENTITY_H

#include <stdint.h>
#include "hcomm/hcomm_res_entity_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HCOMM_MULTI_CHANNEL_VERSION 1U
#define HCOMM_MULTI_CHANNEL_MAGIC_WORD 0x484D4348U

/** Per-channel transport information and a reference to its remote MR table. */
typedef struct {
    uint64_t remoteBufferAddr;
    uint32_t remoteBufferNum;
    uint32_t tpID;
    uint64_t rmtEidL;
    uint64_t rmtEidH;
} HcommBatchRemoteInfo;

/**
 * Shared SQ/CQ contexts followed immediately by HcommBatchRemoteInfo[channelNum].
 * The SQ transport fields belong to channel 0 and are not used by the batch Device path.
 */
typedef struct {
    CommAbiHeader abiHeader;
    SqContext sqContext;
    CqContext cqContext;
    uint32_t channelNum;
    uint32_t reserved;
} HcommMultiChannelEntity;

/** Device address of one HcommMultiChannelEntity allocation. */
typedef uint64_t MultiChannelHandle;

#ifdef __cplusplus
}

static_assert(sizeof(CommAbiHeader) == 16U, "CommAbiHeader ABI must be 16 bytes");
static_assert(sizeof(SqContext) == 128U, "SqContext ABI must be 128 bytes");
static_assert(sizeof(CqContext) == 128U, "CqContext ABI must be 128 bytes");
static_assert(sizeof(HcommBatchRemoteInfo) == 32U, "HcommBatchRemoteInfo ABI must be 32 bytes");
static_assert(sizeof(HcommMultiChannelEntity) == 280U, "HcommMultiChannelEntity ABI must be 280 bytes");
#endif

#endif // INCLUDE_HCOMM_COMMON_HCOMM_BATCH_ENTITY_H
