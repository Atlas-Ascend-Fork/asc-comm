/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"

#include "asccomm/experimental/asccomm_experimental_primitives.h"
#include "asccomm_runtime_stub.h"
#include "../asccomm_ut_fixture.h"

using namespace asccomm_ut;

namespace {

TEST_F(AsccommPrimitivesTest, SendPassesLocalBuffer)
{
    int value = 7;
    const auto threadHandle = ThreadHandleOf(thread_);
    const auto channelHandle = ChannelHandleOf(channel_);
    EXPECT_EQ(asccomm_send_on_thread(threadHandle, channelHandle, nullptr, sizeof(value)), ASCCOMM_E_PTR);
    EXPECT_TRUE(g_runtimeStubState.sendCalls.empty());

    EXPECT_EQ(asccomm_send_on_thread(threadHandle, channelHandle, &value, sizeof(value)), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.sendCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.sendCalls[0].thread, &thread_);
    EXPECT_EQ(g_runtimeStubState.sendCalls[0].channel, &channel_);
    EXPECT_EQ(g_runtimeStubState.sendCalls[0].local, reinterpret_cast<uintptr_t>(&value));
    EXPECT_EQ(g_runtimeStubState.sendCalls[0].len, sizeof(value));

    g_runtimeStubState.sendResult = ASCCOMM_E_NOT_SUPPORT;
    EXPECT_EQ(asccomm_send_on_thread(threadHandle, channelHandle, &value, sizeof(value)), ASCCOMM_E_NOT_SUPPORT);
}

TEST_F(AsccommPrimitivesTest, SendRejectsLengthAboveLimit)
{
    constexpr uint64_t len = 64ULL * 1024ULL + 1ULL;
    const auto threadHandle = ThreadHandleOf(thread_);
    const auto channelHandle = ChannelHandleOf(channel_);
    EXPECT_EQ(asccomm_send_on_thread(threadHandle, channelHandle, &thread_, len), ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.sendCalls.empty());
}

TEST_F(AsccommPrimitivesTest, SendWithImmPassesLocalBufferAndImmediate)
{
    int value = 7;
    const auto threadHandle = ThreadHandleOf(thread_);
    const auto channelHandle = ChannelHandleOf(channel_);
    EXPECT_EQ(
        asccomm_send_with_imm_on_thread(threadHandle, channelHandle, nullptr, sizeof(value), 0xa5), ASCCOMM_E_PTR);
    EXPECT_TRUE(g_runtimeStubState.sendWithImmCalls.empty());

    EXPECT_EQ(
        asccomm_send_with_imm_on_thread(threadHandle, channelHandle, &value, sizeof(value), 0xa5), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.sendWithImmCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.sendWithImmCalls[0].thread, &thread_);
    EXPECT_EQ(g_runtimeStubState.sendWithImmCalls[0].channel, &channel_);
    EXPECT_EQ(g_runtimeStubState.sendWithImmCalls[0].local, reinterpret_cast<uintptr_t>(&value));
    EXPECT_EQ(g_runtimeStubState.sendWithImmCalls[0].len, sizeof(value));
    EXPECT_EQ(g_runtimeStubState.sendWithImmCalls[0].immData, 0xa5U);

    g_runtimeStubState.sendWithImmResult = ASCCOMM_E_NOT_SUPPORT;
    EXPECT_EQ(
        asccomm_send_with_imm_on_thread(threadHandle, channelHandle, &value, sizeof(value), 0xa5),
        ASCCOMM_E_NOT_SUPPORT);
}

TEST_F(AsccommPrimitivesTest, SendWithImmRejectsLengthAboveLimit)
{
    constexpr uint64_t len = 64ULL * 1024ULL + 1ULL;
    const auto threadHandle = ThreadHandleOf(thread_);
    const auto channelHandle = ChannelHandleOf(channel_);
    EXPECT_EQ(asccomm_send_with_imm_on_thread(threadHandle, channelHandle, &thread_, len, 0xa5), ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.sendWithImmCalls.empty());
}

} // namespace
