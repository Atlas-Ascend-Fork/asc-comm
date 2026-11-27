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
#include "hcomm_res_entity_defs.h"
#include "asccomm_launch.h"
#include "asccomm_runtime_stub.h"
#include "asccomm_ut_fixture.h"

using namespace asccomm_ut;

namespace {

TEST_F(AsccommPrimitivesTest, RejectsNullThreadAndInvalidResourceHeader)
{
    EXPECT_EQ(asccomm_write_on_thread(0, 0, nullptr, nullptr, 0), ASCCOMM_E_PTR);

    HcommThreadEntity invalid = MakeThread();
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

TEST_F(AsccommPrimitivesTest, LocalCopySplitsFourGiBIntoNonZeroSdmaTasks)
{
    int value = 0;
    const auto handle = ThreadHandleOf(thread_);
    constexpr uint64_t fourGiB = 1ULL << 32U;
    EXPECT_EQ(asccomm_local_copy_on_thread(handle, &value, &value, fourGiB), ASCCOMM_SUCCESS);

    ASSERT_EQ(g_runtimeStubState.copyCalls.size(), 2U);
    EXPECT_EQ(g_runtimeStubState.copyCalls[0].size, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(g_runtimeStubState.copyCalls[1].size, 1U);
    EXPECT_EQ(g_runtimeStubState.copyCalls[1].dst, reinterpret_cast<uint64_t>(&value) + fourGiB - 1U);
    EXPECT_EQ(g_runtimeStubState.copyCalls[1].src, reinterpret_cast<uint64_t>(&value) + fourGiB - 1U);
}

TEST_F(AsccommPrimitivesTest, LocalReduceSplitsFourGiBAlignedToDataType)
{
    int value = 0;
    const auto handle = ThreadHandleOf(thread_);
    constexpr uint64_t count = 1ULL << 30U;
    constexpr uint32_t firstReduceSize = 0xfffffffcU;
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(handle, &value, &value, count, ASCCOMM_DATA_TYPE_FP32, ASCCOMM_REDUCE_SUM),
        ASCCOMM_SUCCESS);

    ASSERT_EQ(g_runtimeStubState.reduceCalls.size(), 2U);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[0].size, firstReduceSize);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[1].size, sizeof(float));
    EXPECT_EQ(g_runtimeStubState.reduceCalls[1].dst, reinterpret_cast<uint64_t>(&value) + firstReduceSize);
    EXPECT_EQ(g_runtimeStubState.reduceCalls[1].src, reinterpret_cast<uint64_t>(&value) + firstReduceSize);
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

TEST_F(AsccommPrimitivesTest, BatchModeUsesTagToScopeAndRetryRegisteredThread)
{
    const auto handle = ThreadHandleOf(thread_);
    int value = 1;
    EXPECT_EQ(asccomm_batch_mode_start("tag-a"), ASCCOMM_SUCCESS);
    EXPECT_EQ(
        asccomm_write_on_thread(handle, ChannelHandleOf(channel_), &value, &value, sizeof(value)), ASCCOMM_SUCCESS);
    g_runtimeStubState.launchResult = ASCCOMM_E_INTERNAL;
    EXPECT_EQ(asccomm_batch_mode_end("tag-a"), ASCCOMM_E_INTERNAL);

    g_runtimeStubState.launchResult = ASCCOMM_SUCCESS;
    EXPECT_EQ(asccomm_batch_mode_start("tag-a"), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm_batch_mode_end("tag-a"), ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.launchThreads.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.launchThreads[0], handle);
}

TEST_F(AsccommPrimitivesTest, BatchModeControlsAutomaticSubmission)
{
    EXPECT_TRUE(asccomm_is_automatic_launch_mode());
    EXPECT_EQ(asccomm_batch_mode_start("auto-submit"), ASCCOMM_SUCCESS);
    EXPECT_FALSE(asccomm_is_automatic_launch_mode());
    EXPECT_EQ(asccomm_batch_mode_end("auto-submit"), ASCCOMM_SUCCESS);
    EXPECT_TRUE(asccomm_is_automatic_launch_mode());
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
            ThreadHandleOf(thread_), &buffer, &buffer, 1, static_cast<asccomm_data_type>(999), ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            ThreadHandleOf(thread_), &buffer, &buffer, 1, ASCCOMM_DATA_TYPE_INT32, static_cast<asccomm_reduce_op>(999)),
        ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, LocalReduceValidatesThreadBeforeReduceParams)
{
    uint64_t buffer = 0;
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            1, &buffer, &buffer, std::numeric_limits<uint64_t>::max(), ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PTR);
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            std::numeric_limits<uint64_t>::max(), &buffer, &buffer, 1, ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PTR);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, LocalReduceRejectsOverflowCountWithoutSubmit)
{
    uint64_t buffer = 0;
    const auto handle = ThreadHandleOf(thread_);
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            handle, &buffer, &buffer, std::numeric_limits<uint64_t>::max(), ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            handle, &buffer, &buffer, std::numeric_limits<uint64_t>::max() - 1, ASCCOMM_DATA_TYPE_INT8,
            ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, LocalReduceRejectsInvalidDstDeviceAddress)
{
    uint64_t buffer = 0;
    const auto handle = ThreadHandleOf(thread_);
    g_runtimeStubState.deviceRangeResult = 1;
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            handle, reinterpret_cast<void*>(0xDEADBEEFU), &buffer, 1, ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    ASSERT_EQ(g_runtimeStubState.deviceRangeQueries.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.deviceRangeQueries[0].addr, 0xDEADBEEFU);
    EXPECT_EQ(g_runtimeStubState.deviceRangeQueries[0].len, 1U);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, LocalReduceRejectsInvalidSrcDeviceAddress)
{
    uint64_t buffer = 0;
    const auto handle = ThreadHandleOf(thread_);
    g_runtimeStubState.deviceRange = {reinterpret_cast<uint64_t>(&buffer), sizeof(buffer)};
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            handle, &buffer, reinterpret_cast<const void*>(0xDEADBEEFU), 1, ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
        ASCCOMM_E_PARA);
    ASSERT_EQ(g_runtimeStubState.deviceRangeQueries.size(), 2U);
    EXPECT_EQ(g_runtimeStubState.deviceRangeQueries[0].addr, reinterpret_cast<uint64_t>(&buffer));
    EXPECT_EQ(g_runtimeStubState.deviceRangeQueries[1].addr, 0xDEADBEEFU);
    EXPECT_EQ(g_runtimeStubState.deviceRangeQueries[1].len, 1U);
    EXPECT_TRUE(g_runtimeStubState.reduceCalls.empty());
}

TEST_F(AsccommPrimitivesTest, LocalReduceRejectsDeviceAddressOutOfRange)
{
    uint64_t buffer = 0;
    const auto handle = ThreadHandleOf(thread_);
    g_runtimeStubState.deviceRange = {reinterpret_cast<uint64_t>(&buffer), sizeof(buffer)};
    EXPECT_EQ(
        asccomm_local_reduce_on_thread(
            handle, &buffer, &buffer, sizeof(buffer) + 1U, ASCCOMM_DATA_TYPE_INT8, ASCCOMM_REDUCE_SUM),
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
