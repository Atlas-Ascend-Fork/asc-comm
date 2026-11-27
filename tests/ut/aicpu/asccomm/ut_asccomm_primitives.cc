/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "securec.h"
#include "gtest/gtest.h"

#include <cstdint>
#include <thread>
#include "asccomm_command_replay.h"
#include <cstring>
#include <limits>

#include "asccomm_primitives.h"
#include "asccomm_primitives_internal.h"
#include "hcomm_res_entity_defs.h"
#include "asccomm_launch.h"
#include "asccomm_runtime_stub.h"
#include "asccomm_ut_fixture.h"
#include "sqe_build_a5.h"

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

TEST_F(AsccommPrimitivesTest, AddrNotifyWaitPassesArguments)
{
    EXPECT_EQ(
        asccomm_addr_notify_wait_on_thread(ThreadHandleOf(thread_), 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 1U),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.addrNotifyWaitCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.addrNotifyWaitCalls[0].context, &thread_);
    EXPECT_EQ(g_runtimeStubState.addrNotifyWaitCalls[0].waitAddr, 0x1122334455667788ULL);
    EXPECT_EQ(g_runtimeStubState.addrNotifyWaitCalls[0].valueAddr, 0x99AABBCCDDEEFF00ULL);
    EXPECT_EQ(g_runtimeStubState.addrNotifyWaitCalls[0].last, 1U);
}

TEST_F(AsccommPrimitivesTest, AddrNotifyRecordPassesArguments)
{
    EXPECT_EQ(
        asccomm_addr_notify_record_on_thread(ThreadHandleOf(thread_), 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(g_runtimeStubState.addrNotifyRecordCalls.size(), 1U);
    EXPECT_EQ(g_runtimeStubState.addrNotifyRecordCalls[0].context, &thread_);
    EXPECT_EQ(g_runtimeStubState.addrNotifyRecordCalls[0].recordAddr, 0x1122334455667788ULL);
    EXPECT_EQ(g_runtimeStubState.addrNotifyRecordCalls[0].valueAddr, 0x99AABBCCDDEEFF00ULL);
}

TEST_F(AsccommPrimitivesTest, AddrNotifyRejectsNullThread)
{
    EXPECT_EQ(asccomm_addr_notify_wait_on_thread(0, 0ULL, 0ULL, 0U), ASCCOMM_E_PTR);
    EXPECT_EQ(asccomm_addr_notify_record_on_thread(0, 0ULL, 0ULL), ASCCOMM_E_PTR);
    EXPECT_TRUE(g_runtimeStubState.addrNotifyWaitCalls.empty());
    EXPECT_TRUE(g_runtimeStubState.addrNotifyRecordCalls.empty());
}

TEST_F(AsccommPrimitivesTest, AddrNotifyWaitLaunchesImmediately)
{
    HcommRegedNotifyEntity notify{};
    notify.type = HCOMM_REGED_NOTIFY_IPC_RT;
    notify.notifyInfo.ipcRt.notifyId = 66;
    thread_.localNotifyAddr = &notify;
    thread_.localNotifyNum = 1;

    EXPECT_EQ(asccomm_thread_notify_wait_on_thread(ThreadHandleOf(thread_), 0, 5), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_runtimeStubState.launchCalls, 0U);

    EXPECT_EQ(
        asccomm_addr_notify_wait_on_thread(ThreadHandleOf(thread_), 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0U),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(g_runtimeStubState.launchCalls, 1U);

    EXPECT_EQ(
        asccomm_addr_notify_record_on_thread(ThreadHandleOf(thread_), 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(g_runtimeStubState.launchCalls, 2U);
}

namespace {
uint32_t WordAt(const uint8_t* sqe, uint32_t index)
{
    uint32_t word = 0;
    (void)memcpy_sp(&word, sizeof(word), sqe + index * sizeof(uint32_t), sizeof(uint32_t));
    return word;
}
} // namespace

TEST_F(AsccommPrimitivesTest, CondNotifyWaitSqeGoldenEncoding)
{
    alignas(8) uint8_t sqe[64] = {};
    Asc::build_a5_sqe_ccore_notify_wait(0U, 0x00010002U, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0U, sqe);
    EXPECT_EQ(WordAt(sqe, 0), 0x00000014U); // type=COND(20), reserved 0
    EXPECT_EQ(WordAt(sqe, 1), 0x00010002U); // rtStreamId=low16(taskId), taskId=high16
    EXPECT_EQ(WordAt(sqe, 2), 0x80000000U); // csc=1
    EXPECT_EQ(WordAt(sqe, 3), 0x00fe0000U); // kernelCredit=254(RT_STARS_DEFAULT_KERNEL_CREDIT)
    EXPECT_EQ(WordAt(sqe, 4), 0x5de65187U); // ldrImm1: load actAddr to r3, LHU
    EXPECT_EQ(WordAt(sqe, 5), 0xddeeff00U); // actAddr low 32 bits
    EXPECT_EQ(WordAt(sqe, 6), 0x19a25107U); // ldrImm2: load waitAddr to r2, LHU
    EXPECT_EQ(WordAt(sqe, 7), 0x55667788U); // waitAddr low 32 bits
    EXPECT_EQ(WordAt(sqe, 8), 0x00316163U); // branch BLTU reload
    for (uint32_t i = 9U; i < 16U; ++i) {
        EXPECT_EQ(WordAt(sqe, i), 0x00000013U); // NOP (no clear when last=0)
    }
}

TEST_F(AsccommPrimitivesTest, CondNotifyWaitSqeLastAddsClearBranch)
{
    alignas(8) uint8_t normal[64] = {};
    alignas(8) uint8_t last[64] = {};
    Asc::build_a5_sqe_ccore_notify_wait(0U, 0x00010002U, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 0U, normal);
    Asc::build_a5_sqe_ccore_notify_wait(0U, 0x00010002U, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, 1U, last);
    EXPECT_EQ(std::memcmp(normal, last, 36U), 0);             // words 0-8 identical
    EXPECT_NE(std::memcmp(normal + 36U, last + 36U, 28U), 0); // words 9-15 differ (clear vs nop)
    EXPECT_EQ(WordAt(last, 9), 0x19a210dbU);                  // llwi1 r1=waitAddr
    EXPECT_EQ(WordAt(last, 10), 0x55667788U);                 // llwi1 immdLow = waitAddr low 32 bits
    EXPECT_EQ(WordAt(last, 11), 0x112200dbU);                 // lhwi1 r1=waitAddr high
    EXPECT_EQ(WordAt(last, 12), 0x00009027U);                 // sw: store r0(0) to r1, SH
    for (uint32_t i = 13U; i < 16U; ++i) {
        EXPECT_EQ(WordAt(last, i), 0x00000013U); // NOP
    }
}

TEST_F(AsccommPrimitivesTest, CondNotifyRecordSqeGoldenEncoding)
{
    alignas(8) uint8_t sqe[64] = {};
    Asc::build_a5_sqe_ccore_notify_record(0U, 0x00010002U, 0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL, sqe);
    EXPECT_EQ(WordAt(sqe, 0), 0x00000014U); // type=COND(20)
    EXPECT_EQ(WordAt(sqe, 1), 0x00010002U); // taskId 拼码
    EXPECT_EQ(WordAt(sqe, 2), 0x80000000U); // csc=1
    EXPECT_EQ(WordAt(sqe, 3), 0x00fe0000U); // kernelCredit=254
    EXPECT_EQ(WordAt(sqe, 4), 0x5de65087U); // ldrImm: load valueAddr to r1, LHU
    EXPECT_EQ(WordAt(sqe, 5), 0xddeeff00U); // valueAddr low 32 bits
    EXPECT_EQ(WordAt(sqe, 6), 0x19a2115bU); // llwi1 r2=writeAddr
    EXPECT_EQ(WordAt(sqe, 7), 0x55667788U); // writeAddr low 32 bits
    EXPECT_EQ(WordAt(sqe, 8), 0x1122015bU); // lhwi1 r2=writeAddr high
    EXPECT_EQ(WordAt(sqe, 9), 0x00111027U); // sw: store r1 to r2, SH
    for (uint32_t i = 10U; i < 16U; ++i) {
        EXPECT_EQ(WordAt(sqe, i), 0x00000013U); // NOP
    }
}

struct ReplayHandle {
    void* value = nullptr;
    ReplayHandle() { EXPECT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &value, nullptr, nullptr), 0); }
    ~ReplayHandle() { EXPECT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &value, nullptr, nullptr), 0); }
    int32_t Call(uint32_t action, AsccommReplayInfoV1* info = nullptr)
    {
        return asccomm_binary_replay_v1(action, &value, info, nullptr);
    }
};

TEST_F(AsccommPrimitivesTest, FailedOrEmptyCaptureIsNotPublished)
{
    ReplayHandle cache;
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_BEGIN), 0);
    EXPECT_EQ(asccomm_local_copy_on_thread(0, nullptr, nullptr, 4), ASCCOMM_E_PTR);
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_END, &info), 0);
    EXPECT_EQ(info.ready, 0U);
    EXPECT_EQ(cache.Call(ASCCOMM_REPLAY_EXECUTE), ASCCOMM_E_IN_STATUS);
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_BEGIN), 0);
    EXPECT_EQ(asccomm_batch_mode_end(nullptr), 0);
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_END, &info), 0);
    EXPECT_EQ(info.ready, 0U);
}

TEST_F(AsccommPrimitivesTest, CaptureRejectsNestedAndForeignThreadCompletion)
{
    ReplayHandle first, second;
    ASSERT_EQ(first.Call(ASCCOMM_REPLAY_BEGIN), 0);
    EXPECT_EQ(second.Call(ASCCOMM_REPLAY_BEGIN), ASCCOMM_E_IN_STATUS);
    int32_t foreignResult = 0;
    std::thread worker([&] {
        AsccommReplayInfoV1 info{};
        foreignResult = first.Call(ASCCOMM_REPLAY_END, &info);
    });
    worker.join();
    EXPECT_EQ(foreignResult, ASCCOMM_E_IN_STATUS);
    ASSERT_EQ(first.Call(ASCCOMM_REPLAY_ABORT), 0);
    EXPECT_EQ(second.Call(ASCCOMM_REPLAY_BEGIN), 0);
    EXPECT_EQ(second.Call(ASCCOMM_REPLAY_ABORT), 0);
}

TEST_F(AsccommPrimitivesTest, PrimitiveCallsWithoutNativeTemplatesAreNotPublished)
{
    ReplayHandle cache;
    int value = 0;
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_BEGIN), 0);
    for (unsigned i = 0; i < 8; ++i) {
        ASSERT_EQ(asccomm_local_copy_on_thread(ThreadHandleOf(thread_), &value, &value, 4), 0);
    }
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(cache.Call(ASCCOMM_REPLAY_END, &info), 0);
    EXPECT_EQ(info.ready, 0U);
    EXPECT_EQ(g_runtimeStubState.copyCalls.size(), 8U);
}

} // namespace
