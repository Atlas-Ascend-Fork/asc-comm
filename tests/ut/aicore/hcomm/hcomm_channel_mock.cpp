/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <hcomm/hcomm_res_defs.h>
#include <hccl/hccl_types.h>

// The UT mock only needs the opaque channel API types. Keep these declarations
// local so the mock uses the packaged resource ABI without depending on the
// channel API header, which is not part of this repository.
typedef void* HcommChannelConfig;
typedef enum {
    HCOMM_CHANNEL_CONFIG_TYPE_INVALID = -1,
    HCOMM_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE = 0,
} HcommChannelConfigType;
typedef struct HcommChannelDesc HcommChannelDesc;

namespace {
HcommResult configDestroyResult = HCCL_SUCCESS;
HcommResult channelDestroyResult = HCCL_SUCCESS;
uint32_t channelDestroyCalls = 0;
uint32_t lastDestroyedChannelCount = 0;
} // namespace

extern "C" void HcommChannelMockReset(HcommResult configResult, HcommResult channelResult)
{
    configDestroyResult = configResult;
    channelDestroyResult = channelResult;
    channelDestroyCalls = 0;
    lastDestroyedChannelCount = 0;
}

extern "C" uint32_t HcommChannelMockDestroyCalls() { return channelDestroyCalls; }

extern "C" uint32_t HcommChannelMockLastDestroyedChannelCount() { return lastDestroyedChannelCount; }

extern "C" HcommResult HcommChannelConfigCreate(HcommChannelConfig* config)
{
    *config = reinterpret_cast<HcommChannelConfig>(0x1);
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelConfigDestroy(HcommChannelConfig) { return configDestroyResult; }

extern "C" HcommResult HcommChannelConfigSetInt(HcommChannelConfig, HcommChannelConfigType, uint32_t)
{
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelCreateWithConfig(
    EndpointHandle, CommEngine, HcommChannelDesc*, uint32_t channelNum, HcommChannelConfig, ChannelHandle* channels)
{
    for (uint32_t i = 0; i < channelNum; ++i) {
        channels[i] = reinterpret_cast<ChannelHandle>(static_cast<uintptr_t>(i + 1));
    }
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelGetStatus(const ChannelHandle*, uint32_t, int32_t*) { return HCCL_SUCCESS; }

extern "C" HcommResult HcommChannelDestroy(const ChannelHandle*, uint32_t channelNum)
{
    ++channelDestroyCalls;
    lastDestroyedChannelCount = channelNum;
    return channelDestroyResult;
}
