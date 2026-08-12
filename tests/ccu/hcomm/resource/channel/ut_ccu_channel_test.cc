/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/channel/ccu_channel.h"

#include "ccu_channel_get_stub.h"
#include "gtest/gtest.h"

namespace hcomm {
namespace {

class CcuChannelTest : public testing::Test {
protected:
    void SetUp() override
    {
        channelPod_.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channelPod_.header.magicWord = HCOMM_CCU_CHANNEL_MAGIC_WORD;
        channelPod_.header.size = sizeof(HcommCcuChannelPod);
        channelPod_.dieId = 3;
        channelPod_.channelId = 17;
        channelPod_.xnRange = {21, 2};
        channelPod_.ckeRange = {11, 2};
        channelPod_.rmtCcuBufAddr = 0x123456789ABCDEF0ULL;
        channelPod_.rmtCcuBufSize = 0x8000;
        channelPod_.rmtCcuBufTokenId = 0x51;
        channelPod_.rmtCcuBufTokenValue = 0x62;
        SetHcommCcuChannelGetPodStub(channelPod_);
    }

    void TearDown() override
    {
        ResetHcommCcuChannelGetPodStub();
    }

    HcommCcuChannelPod channelPod_{};
};

TEST_F(CcuChannelTest, InitCopiesAllFieldsAndResources)
{
    CcuChannel channel(1);
    ASSERT_EQ(channel.GetResult(), HCCL_SUCCESS);
    EXPECT_EQ(channel.GetDieId(), 3U);
    EXPECT_EQ(channel.GetChannelId(), 17U);

    uint32_t value = 0;
    EXPECT_EQ(channel.GetLocCkeByIndex(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 11U);
    EXPECT_EQ(channel.GetLocXnByIndex(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 22U);
    EXPECT_EQ(channel.GetRmtCkeByIndex(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 11U);
    EXPECT_EQ(channel.GetRmtXnByIndex(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 22U);

    uint64_t addr = 0;
    uint32_t size = 0;
    uint32_t tokenId = 0;
    uint32_t tokenValue = 0;
    EXPECT_EQ(channel.GetRmtBuffer(addr, size, tokenId, tokenValue), HCCL_SUCCESS);
    EXPECT_EQ(addr, 0x123456789ABCDEF0ULL);
    EXPECT_EQ(size, 0x8000U);
    EXPECT_EQ(tokenId, 0x51U);
    EXPECT_EQ(tokenValue, 0x62U);
}

TEST_F(CcuChannelTest, SnapshotOwnsDeepCopy)
{
    CcuChannel channel(1);
    ASSERT_EQ(channel.GetResult(), HCCL_SUCCESS);

    channelPod_.dieId = 99;
    channelPod_.ckeRange.startId = 999;
    SetHcommCcuChannelGetPodStub(channelPod_);

    uint32_t localCkeId = 0;
    EXPECT_EQ(channel.GetDieId(), 3U);
    EXPECT_EQ(channel.GetLocCkeByIndex(0, localCkeId), HCCL_SUCCESS);
    EXPECT_EQ(localCkeId, 11U);
}

TEST_F(CcuChannelTest, ResourceIndexOutOfRangeReturnsParameterError)
{
    CcuChannel channel(1);
    ASSERT_EQ(channel.GetResult(), HCCL_SUCCESS);
    uint32_t value = 0;
    EXPECT_EQ(channel.GetLocCkeByIndex(2, value), HCCL_E_PARA);
    EXPECT_EQ(channel.GetLocXnByIndex(2, value), HCCL_E_PARA);
    EXPECT_EQ(channel.GetRmtCkeByIndex(2, value), HCCL_E_PARA);
    EXPECT_EQ(channel.GetRmtXnByIndex(2, value), HCCL_E_PARA);
}

TEST_F(CcuChannelTest, FirstQueryFailureIsPropagated)
{
    SetHcommCcuChannelGetPodStubResult(HCCL_E_UNAVAIL);
    CcuChannel channel(1);
    EXPECT_EQ(channel.GetResult(), HCCL_E_UNAVAIL);

    uint32_t value = 0;
    EXPECT_EQ(channel.GetLocCkeByIndex(0, value), HCCL_E_UNAVAIL);
}

TEST_F(CcuChannelTest, InconsistentResourceCountReturnsInternalError)
{
    channelPod_.xnRange.count = 0;
    SetHcommCcuChannelGetPodStub(channelPod_);
    CcuChannel channel(1);
    EXPECT_EQ(channel.GetResult(), HCCL_E_INTERNAL);
}

} // namespace
} // namespace hcomm
