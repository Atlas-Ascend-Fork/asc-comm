/**
 * Copyright (c) 2025 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef AICPU_TS_CHANNEL_HELPER_H
#define AICPU_TS_CHANNEL_HELPER_H

#include "hccl/hccl_types.h"
#include "hcomm_res_defs.h"

// NOTE: 以下通道上下文和解包接口被 aicpu_ts_primitives_c_adpt 依赖。
static const uint32_t HCOMM_AICPU_CHANNEL_CTX_MAGIC_WORD = 0x0fcf0f4fU;
static const uint32_t HCOMM_AICPU_CHANNEL_CTX_VERSION = 0U;

typedef struct {
    CommAbiHeader abiHeader{HCOMM_AICPU_CHANNEL_CTX_VERSION, HCOMM_AICPU_CHANNEL_CTX_MAGIC_WORD, 0, 0};
    CommEngine engine{COMM_ENGINE_RESERVED};
    CommProtocol protocol{COMM_PROTOCOL_RESERVED};
    void *deviceChannel{nullptr};
} HcommAicpuChannelCtx;

inline HcclResult UnwrapChannelHandle(ChannelHandle &handle)
{
    if (handle == 0) {
        HCCL_ERROR("[%s] handle is 0.", __func__);
        return HCCL_E_PTR;
    }
    const auto *ctx = reinterpret_cast<const HcommAicpuChannelCtx *>(static_cast<uintptr_t>(handle));
    if (ctx->abiHeader.version == HCOMM_AICPU_CHANNEL_CTX_VERSION
        && ctx->abiHeader.magicWord == HCOMM_AICPU_CHANNEL_CTX_MAGIC_WORD) {
        if (ctx->deviceChannel == nullptr) {
            HCCL_ERROR("[%s] ctx deviceChannel is null.", __func__);
            return HCCL_E_INTERNAL;
        }
        handle = reinterpret_cast<ChannelHandle>(ctx->deviceChannel);
    }
    return HCCL_SUCCESS;
}

#endif // AICPU_TS_CHANNEL_HELPER_H
