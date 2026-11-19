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
// Opaque placeholder for the mocked hcomm SDK channel descriptor. The packaged SDK
// headers define the real ABI type (typedef int HcommChannelDesc on some SDK versions)
// and are intentionally not included here, so a local forward declaration keeps the
// mock independent of the SDK header set.
struct HcommChannelDescPlaceholder;
typedef enum {
    HCOMM_CHANNEL_CONFIG_TYPE_INVALID = -1,
    HCOMM_CHANNEL_CONFIG_TYPE_IS_SHARED_QUEUE = 0,
} HcommChannelConfigType;

namespace {
HcommResult config_destroy_result = HCCL_SUCCESS;
HcommResult channel_destroy_result = HCCL_SUCCESS;
uint32_t channel_destroy_calls = 0;
uint32_t last_destroyed_channel_count = 0;
} // namespace

extern "C" void asccomm_channel_mock_reset(HcommResult configResult, HcommResult channelResult)
{
    config_destroy_result = configResult;
    channel_destroy_result = channelResult;
    channel_destroy_calls = 0;
    last_destroyed_channel_count = 0;
}

extern "C" uint32_t asccomm_channel_mock_destroy_calls() { return channel_destroy_calls; }

extern "C" uint32_t asccomm_channel_mock_last_destroyed_channel_count() { return last_destroyed_channel_count; }

extern "C" HcommResult HcommChannelConfigCreate(HcommChannelConfig* config)
{
    *config = reinterpret_cast<HcommChannelConfig>(0x1);
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelConfigDestroy(HcommChannelConfig) { return config_destroy_result; }

extern "C" HcommResult HcommChannelConfigSetInt(HcommChannelConfig, HcommChannelConfigType, uint32_t)
{
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelCreateWithConfig(
    EndpointHandle, CommEngine, HcommChannelDescPlaceholder*, uint32_t channelNum, HcommChannelConfig,
    ChannelHandle* channels)
{
    for (uint32_t i = 0; i < channelNum; ++i) {
        channels[i] = reinterpret_cast<ChannelHandle>(static_cast<uintptr_t>(i + 1));
    }
    return HCCL_SUCCESS;
}

extern "C" HcommResult HcommChannelGetStatus(const ChannelHandle*, uint32_t, int32_t*) { return HCCL_SUCCESS; }

extern "C" HcommResult HcommChannelDestroy(const ChannelHandle*, uint32_t channelNum)
{
    ++channel_destroy_calls;
    last_destroyed_channel_count = channelNum;
    return channel_destroy_result;
}
