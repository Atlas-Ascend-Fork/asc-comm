/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>
#include <dlfcn.h>

#include "asccomm/asccomm_host.h"

namespace {

class asccomm_host_cleanup_test : public testing::Test {
protected:
    void SetUp() override
    {
        mockHandle = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(mockHandle, nullptr);
        reset = reinterpret_cast<ResetFn>(dlsym(mockHandle, "asccomm_channel_mock_reset"));
        destroyCalls = reinterpret_cast<CounterFn>(dlsym(mockHandle, "asccomm_channel_mock_destroy_calls"));
        destroyedCount =
            reinterpret_cast<CounterFn>(dlsym(mockHandle, "asccomm_channel_mock_last_destroyed_channel_count"));
        ASSERT_NE(reset, nullptr);
        ASSERT_NE(destroyCalls, nullptr);
        ASSERT_NE(destroyedCount, nullptr);
        ASSERT_EQ(asc::dl_hcomm_api::load_hcomm_api(), HCCL_SUCCESS);
    }

    using ResetFn = void (*)(HcommResult, HcommResult);
    using CounterFn = uint32_t (*)();
    void* mockHandle = nullptr;
    ResetFn reset = nullptr;
    CounterFn destroyCalls = nullptr;
    CounterFn destroyedCount = nullptr;
    HcommChannelDesc channelDescs[3]{};
    ChannelHandle channels[3]{};
};

TEST_F(asccomm_host_cleanup_test, DESTROYS_CREATED_CHANNELS_WHEN_CONFIG_DESTROY_FAILS)
{
    reset(HCCL_E_INTERNAL, HCCL_SUCCESS);

    const HcommResult result =
        asc::asccomm_create_shared_channels(reinterpret_cast<EndpointHandle>(0x1), channelDescs, 3, channels);

    EXPECT_EQ(result, HCCL_E_INTERNAL);
    EXPECT_EQ(destroyCalls(), 1U);
    EXPECT_EQ(destroyedCount(), 3U);
}

TEST_F(asccomm_host_cleanup_test, PRESERVES_CONFIG_DESTROY_ERROR_WHEN_CHANNEL_CLEANUP_ALSO_FAILS)
{
    reset(HCCL_E_INTERNAL, HCCL_E_NOT_SUPPORT);

    const HcommResult result =
        asc::asccomm_create_shared_channels(reinterpret_cast<EndpointHandle>(0x1), channelDescs, 3, channels);

    EXPECT_EQ(result, HCCL_E_INTERNAL);
    EXPECT_EQ(destroyCalls(), 1U);
    EXPECT_EQ(destroyedCount(), 3U);
}

TEST_F(asccomm_host_cleanup_test, DOES_NOT_DESTROY_CHANNELS_WHEN_CONFIG_DESTROY_SUCCEEDS)
{
    reset(HCCL_SUCCESS, HCCL_E_NOT_SUPPORT);

    const HcommResult result =
        asc::asccomm_create_shared_channels(reinterpret_cast<EndpointHandle>(0x1), channelDescs, 3, channels);

    EXPECT_EQ(result, HCCL_SUCCESS);
    EXPECT_EQ(destroyCalls(), 0U);
}
} // namespace
