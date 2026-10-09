/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"

#include <cstdint>
#include <limits>

#include "asccomm_primitives.h"
#include "asccomm_runtime_stub.h"

namespace {

ThreadEntity MakeThread()
{
    ThreadEntity thread{};
    thread.abiHeader = {HCOMM_RESOURCE_THREAD_VERSION, HCOMM_RESOURCE_THREAD_MAGIC_WORD, sizeof(ThreadEntity), 0};
    static HcommRtsqContext rtsq{};
    thread.sqContextAddr = &rtsq;
    return thread;
}

ChannelEntity MakeChannel()
{
    ChannelEntity channel{};
    channel.abiHeader = {HCOMM_RESOURCE_CHANNEL_VERSION, HCOMM_RESOURCE_CHANNEL_MAGIC_WORD, sizeof(ChannelEntity), 0};
    return channel;
}

ThreadHandle ThreadHandleOf(ThreadEntity& thread) { return reinterpret_cast<ThreadHandle>(&thread); }

ChannelHandle ChannelHandleOf(ChannelEntity& channel) { return reinterpret_cast<ChannelHandle>(&channel); }

class AsccommPrimitivesTest : public testing::Test {
protected:
    void SetUp() override
    {
        g_runtimeStubState = {};
        asccomm_batch_mode_end(nullptr);
        g_runtimeStubState = {};
    }

    ThreadEntity thread_{MakeThread()};
    ChannelEntity channel_{MakeChannel()};
};

TEST_F(AsccommPrimitivesTest, RejectsNullThreadAndInvalidResourceHeader)
{
    EXPECT_EQ(asccomm_write_on_thread(0, 0, nullptr, nullptr, 0), ASCCOMM_E_PTR);

    ThreadEntity invalid = MakeThread();
    invalid.abiHeader.magicWord = 0;
    EXPECT_EQ(asccomm_write_on_thread(ThreadHandleOf(invalid), 0, nullptr, nullptr, 0), ASCCOMM_E_PTR);
}

TEST_F(AsccommPrimitivesTest, ValidatesChannelResourceBeforeTransport)
{
    ChannelEntity invalid = MakeChannel();
    invalid.abiHeader.size = 0;
    int value = 0;
    EXPECT_EQ(
        asccomm_write_on_thread(ThreadHandleOf(thread_), ChannelHandleOf(invalid), &value, &value, sizeof(value)),
        ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.writeCalls.empty());
}

TEST_F(AsccommPrimitivesTest, WritePassesResourceEntitiesAndAddresses)
{
    int value = 123;
    const auto handle = ThreadHandleOf(thread_);
    EXPECT_EQ(
        asccomm_write_on_thread(handle, ChannelHandleOf(channel_), &value, &value, sizeof(value)), ASCCOMM_SUCCESS);

    ASSERT_EQ(g_runtimeStubState.writeCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.writeCalls[0].thread, &thread_);
    EXPECT_EQ(g_runtimeStubState.writeCalls[0].channel, &channel_);
    EXPECT_EQ(g_runtimeStubState.writeCalls[0].local, reinterpret_cast<uintptr_t>(&value));
    EXPECT_EQ(g_runtimeStubState.writeCalls[0].remote, reinterpret_cast<uintptr_t>(&value));
    EXPECT_EQ(g_runtimeStubState.writeCalls[0].len, sizeof(value));
    EXPECT_EQ(g_runtimeStubState.resolveCalls.size(), 1U);
}

TEST_F(AsccommPrimitivesTest, BatchModeStartAndEndLaunchRegisteredThread)
{
    const auto handle = ThreadHandleOf(thread_);
    EXPECT_EQ(asccomm_batch_mode_start("ut"), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm_write_on_thread(handle, ChannelHandleOf(channel_), nullptr, nullptr, 0), ASCCOMM_E_PTR);

    int value = 1;
    EXPECT_EQ(
        asccomm_write_on_thread(handle, ChannelHandleOf(channel_), &value, &value, sizeof(value)), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm_batch_mode_end("ut"), ASCCOMM_SUCCESS);

    ASSERT_EQ(g_runtimeStubState.launchThreads.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.launchThreads[0], handle);
    EXPECT_EQ(asccomm_batch_mode_start("ut"), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm_batch_mode_end("ut"), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_runtimeStubState.launchThreads.size(), 1U);
}

TEST_F(AsccommPrimitivesTest, TimeoutApisValidateAndPersistSeconds)
{
    EXPECT_EQ(asccomm_set_notify_wait_timeout(-1.0f), ASCCOMM_E_PARA);
    EXPECT_EQ(asccomm_set_notify_wait_timeout(std::numeric_limits<float>::quiet_NaN()), ASCCOMM_E_PARA);
    EXPECT_EQ(asccomm_set_notify_wait_timeout(2.9f), ASCCOMM_SUCCESS);

    EXPECT_EQ(asccomm_thread_res_acquire_timeout(3.9f), ASCCOMM_SUCCESS);
    HcommRegedNotifyEntity notify{};
    notify.type = HCOMM_REGED_NOTIFY_IPC_RT;
    notify.notifyInfo.ipcRt.notifyId = 77;
    thread_.localNotifyAddr = &notify;
    thread_.localNotifyNum = 1;

    EXPECT_EQ(asccomm_thread_notify_wait_on_thread_with_default_timeout(ThreadHandleOf(thread_), 0), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.notifyWaitCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.notifyWaitCalls[0].notifyId, 77U);
    EXPECT_EQ(g_runtimeStubState.notifyWaitCalls[0].timeout, 2U);
}

TEST_F(AsccommPrimitivesTest, ThreadNotifyApisResolveNotifyAndPassIds)
{
    HcommRegedNotifyEntity notify{};
    notify.type = HCOMM_REGED_NOTIFY_IPC_RT;
    notify.notifyInfo.ipcRt.notifyId = 88;
    thread_.localNotifyAddr = &notify;
    thread_.localNotifyNum = 1;

    EXPECT_EQ(
        asccomm_thread_notify_record_on_thread(ThreadHandleOf(thread_), ThreadHandleOf(thread_), 0), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.notifyRecordCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.notifyRecordCalls[0].notifyId, 88U);

    EXPECT_EQ(asccomm_thread_notify_wait_on_thread(ThreadHandleOf(thread_), 0, 9), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.notifyWaitCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.notifyWaitCalls[0].notifyId, 88U);
    EXPECT_EQ(g_runtimeStubState.notifyWaitCalls[0].timeout, 9U);

    EXPECT_EQ(asccomm_thread_notify_wait_on_thread(ThreadHandleOf(thread_), 1, 9), ASCCOMM_E_PARA);
}

TEST_F(AsccommPrimitivesTest, LocalCopyAndReducePassArguments)
{
    uint64_t buffer[2] = {1, 2};
    const auto handle = ThreadHandleOf(thread_);
    EXPECT_EQ(asccomm_local_copy_on_thread(handle, &buffer[1], &buffer[0], sizeof(uint64_t)), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.copyCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.copyCalls[0].dst, reinterpret_cast<uintptr_t>(&buffer[1]));
    EXPECT_EQ(g_runtimeStubState.copyCalls[0].src, reinterpret_cast<uintptr_t>(&buffer[0]));
    EXPECT_EQ(g_runtimeStubState.copyCalls[0].size, sizeof(uint64_t));

    EXPECT_EQ(
        asccomm_local_reduce_on_thread(handle, &buffer[1], &buffer[0], 2, ASCCOMM_DATA_TYPE_INT32, ASCCOMM_REDUCE_SUM),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.reduceCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[0].size, 8U);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[0].dataType, Asc::DataType::INT32);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[0].reduceOp, Asc::ReduceOp::SUM);
}

TEST_F(AsccommPrimitivesTest, RejectsUnsupportedReduceParameters)
{
    uint64_t buffer = 0;
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            ThreadHandleOf(thread_), &buffer, &buffer, 1, static_cast<AsccommDataType>(999), ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            ThreadHandleOf(thread_), &buffer, &buffer, 1, ASCCOMM_DATA_TYPE_INT32, static_cast<AsccommReduceOp>(999)),
        ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, ChannelNotifyWaitAndFenceDrainPassArguments)
{
    const auto threadHandle = ThreadHandleOf(thread_);
    const auto channelHandle = ChannelHandleOf(channel_);
    EXPECT_EQ(asccomm_channel_notify_wait_on_thread(threadHandle, channelHandle, 3, 9), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.channelNotifyWaitCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.channelNotifyWaitCalls[0].notifyIdx, 3U);
    EXPECT_EQ(g_runtimeStubState.channelNotifyWaitCalls[0].timeout, 9U);

    EXPECT_EQ(asccomm_channel_fence_on_thread(threadHandle, channelHandle), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.fenceCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.fenceCalls[0], &channel_);

    EXPECT_EQ(asccomm_channel_drain_on_thread(threadHandle, channelHandle), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.drainCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.drainCalls[0].thread, &thread_);
    EXPECT_EQ(g_runtimeStubState.drainCalls[0].channel, &channel_);
}

} // namespace
