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

#include "hcomm/hcomm_host.h"

namespace {
class HcommHostCleanupTest : public testing::Test {
protected:
    void SetUp() override
    {
        mockHandle_ = dlopen("libhcomm.so", RTLD_NOW | RTLD_LOCAL);
        ASSERT_NE(mockHandle_, nullptr);
        reset_ = reinterpret_cast<ResetFn>(dlsym(mockHandle_, "HcommChannelMockReset"));
        destroyCalls_ = reinterpret_cast<CounterFn>(dlsym(mockHandle_, "HcommChannelMockDestroyCalls"));
        destroyedCount_ = reinterpret_cast<CounterFn>(dlsym(mockHandle_, "HcommChannelMockLastDestroyedChannelCount"));
        ASSERT_NE(reset_, nullptr);
        ASSERT_NE(destroyCalls_, nullptr);
        ASSERT_NE(destroyedCount_, nullptr);
        ASSERT_EQ(DlHcommApi::LoadHcommApi(), HCCL_SUCCESS);
    }

    using ResetFn = void (*)(HcommResult, HcommResult);
    using CounterFn = uint32_t (*)();
    void* mockHandle_ = nullptr;
    ResetFn reset_ = nullptr;
    CounterFn destroyCalls_ = nullptr;
    CounterFn destroyedCount_ = nullptr;
    HcommChannelDesc channelDescs_[3]{};
    ChannelHandle channels_[3]{};
};

TEST_F(HcommHostCleanupTest, DestroysCreatedChannelsWhenConfigDestroyFails)
{
    reset_(HCCL_E_INTERNAL, HCCL_SUCCESS);

    const HcommResult result =
        HcommCreateSharedChannels(reinterpret_cast<EndpointHandle>(0x1), channelDescs_, 3, channels_);

    EXPECT_EQ(result, HCCL_E_INTERNAL);
    EXPECT_EQ(destroyCalls_(), 1U);
    EXPECT_EQ(destroyedCount_(), 3U);
}

TEST_F(HcommHostCleanupTest, PreservesConfigDestroyErrorWhenChannelCleanupAlsoFails)
{
    reset_(HCCL_E_INTERNAL, HCCL_E_NOT_SUPPORT);

    const HcommResult result =
        HcommCreateSharedChannels(reinterpret_cast<EndpointHandle>(0x1), channelDescs_, 3, channels_);

    EXPECT_EQ(result, HCCL_E_INTERNAL);
    EXPECT_EQ(destroyCalls_(), 1U);
    EXPECT_EQ(destroyedCount_(), 3U);
}

TEST_F(HcommHostCleanupTest, DoesNotDestroyChannelsWhenConfigDestroySucceeds)
{
    reset_(HCCL_SUCCESS, HCCL_E_NOT_SUPPORT);

    const HcommResult result =
        HcommCreateSharedChannels(reinterpret_cast<EndpointHandle>(0x1), channelDescs_, 3, channels_);

    EXPECT_EQ(result, HCCL_SUCCESS);
    EXPECT_EQ(destroyCalls_(), 0U);
}
} // namespace
