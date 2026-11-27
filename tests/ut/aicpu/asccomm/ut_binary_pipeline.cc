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
#include "asccomm_command_replay.h"
#include "asccomm_primitives_internal.h"
#include "asccomm_runtime.h"
#include "asccomm_transport.h"
#include "asccomm_thread_notify.h"
#include "ub_jetty_lite.h"
#include "asccomm_launch.h"
#include "ascend_hal.h"
#include <array>
#include <cstring>
#include <vector>

extern "C" int32_t asccomm_binary_replay_v1(uint32_t, void**, AsccommReplayInfoV1*, uint64_t*);
extern "C" int32_t asccomm_binary_replay_v2(uint32_t, void**, AsccommReplayInfoV1*, uint64_t*, const uint64_t*);
extern "C" uint64_t asccomm_ub_domain_exchange_v1(uint64_t);
bool HcclCheckLogLevel(int, int) { return false; }
bool IsErrorToWarn() { return false; }
namespace {
uint32_t tail = 0;
uint32_t hardwareHead = 0;
bool failResolve = false;
uint32_t resolveCalls = 0, headQueries = 0, tailQueries = 0;
bool failTail = false;
bool autoComplete = false;
uint64_t testDomain = 0;
uint32_t tailCalls = 0;
std::vector<uint32_t> publicationSqIds;
} // namespace
namespace aicpu {
void GetSqeId(uint32_t batch, uint32_t& begin, uint32_t& end)
{
    begin = 1000;
    end = 1000 + batch;
}
} // namespace aicpu
extern "C" drvError_t drvGetLocalDevIDByHostDevID(uint32_t host, uint32_t* local)
{
    ++resolveCalls;
    if (failResolve) {
        return static_cast<drvError_t>(1);
    }
    *local = host;
    return DRV_ERROR_NONE;
}
extern "C" drvError_t halSqCqQuery(uint32_t, struct halSqCqQueryInfo* info)
{
    if (info->prop == DRV_SQCQ_PROP_SQ_HEAD) {
        ++headQueries;
        if (autoComplete) {
            hardwareHead = tail;
        }
    }
    if (info->prop == DRV_SQCQ_PROP_SQ_TAIL) {
        ++tailQueries;
    }
    info->value[0] = info->prop == DRV_SQCQ_PROP_SQ_TAIL ? tail : hardwareHead;
    return DRV_ERROR_NONE;
}
extern "C" drvError_t halSqCqConfig(uint32_t, struct halSqCqConfigInfo* info)
{
    ++tailCalls;
    publicationSqIds.push_back(info->sqId);
    if (failTail) {
        return static_cast<drvError_t>(1);
    }
    tail = info->value[0];
    return DRV_ERROR_NONE;
}
namespace {
class BinaryPipeline : public testing::Test {
protected:
    ThreadEntity thread{};
    ChannelEntity channel{};
    HcommThreadSqContext rtsq{};
    std::array<uint8_t, 512 * 64> queue{};
    std::array<uint8_t, HCOMM_RESOURCE_RTSQ_BATCH_SIZE * 64> local{};
    std::array<uint8_t, 8 * 64> wq{};
    SqContext cq{};
    HcommRegedBufferEntity buffers[2]{};
    void* handle = nullptr;
    void SetUp() override
    {
        thread.abiHeader = {HCOMM_THREAD_ENTITY_VERSION, HCOMM_THREAD_ENTITY_MAGIC_WORD, sizeof(thread), 0};
        channel.abiHeader = {HCOMM_CHANNEL_ENTITY_VERSION, HCOMM_CHANNEL_ENTITY_MAGIC_WORD, sizeof(channel), 0};
        thread.sqContextAddr = &rtsq;
        rtsq.type = HCOMM_THREAD_SQ_CONTEXT_TYPE_RTSQ;
        auto& q = rtsq.contextInfo.rtsqContext;
        q.localSqAddr = reinterpret_cast<uint64_t>(local.data());
        q.sqBaseAddr = reinterpret_cast<uint64_t>(queue.data());
        q.sqDepth = 512;
        q.taskIdEnd = 100;
        channel.protocol = COMM_PROTOCOL_UB_MEM;
        channel.sqContextAddr = &cq;
        channel.sqNum = 1;
        cq.type = HCOMM_SQ_CONTEXT_TYPE_UB_JFS;
        auto& j = cq.contextInfo.ubJfs;
        j.sqVa = reinterpret_cast<uint64_t>(wq.data());
        j.sqDepth = 8;
        j.maxReadSize = 128;
        j.maxWriteSize = 128;
        for (int i = 0; i < 2; ++i) {
            buffers[i].type = HCOMM_REGED_BUFFER_RMA;
            auto& b = buffers[i].bufferInfo.rma;
            b.addr = 0x10000;
            b.size = 0x10000;
            b.protectionInfo.type = HCOMM_PROTECTION_TYPE_URMA;
            b.protectionInfo.memInfo.ub.tokenId = 17;
            b.protectionInfo.memInfo.ub.tokenValue = 23;
        }
        channel.localBufferAddr = buffers;
        channel.localBufferNum = 2;
        channel.remoteBufferAddr = buffers;
        channel.remoteBufferNum = 2;
        Reset();
    }
    void Reset()
    {
        tail = 0;
        hardwareHead = 0;
        resolveCalls = headQueries = tailQueries = 0;
        tailCalls = 0;
        failTail = false;
        failResolve = false;
        autoComplete = false;
        queue.fill(0);
        local.fill(0);
        wq.fill(0);
        auto& q = rtsq.contextInfo.rtsqContext;
        q.pendingSqeCnt = 0;
        q.sqHead = 0;
        q.sqTail = 0;
        q.taskId = 5;
        q.taskIdEnd = 100;
        cq.contextInfo.ubJfs.sqHead = 7;
        cq.contextInfo.ubJfs.sqTail = 0;
        cq.fence = 0;
        asccomm_clear_registered_threads();
        asccomm_set_batch_launch_mode(false);
    }
    void TearDown() override
    {
        asccomm_ub_domain_exchange_v1(0);
        asccomm_set_sq_full_timeout(0);
        if (handle) {
            asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &handle, nullptr, nullptr);
        }
        asccomm_clear_registered_threads();
        asccomm_set_batch_launch_mode(false);
    }
    int Native()
    {
        const auto t = reinterpret_cast<ThreadHandle>(&thread), c = reinterpret_cast<ChannelHandle>(&channel);
        if (asccomm_local_copy_on_thread(t, reinterpret_cast<void*>(0x12000), reinterpret_cast<void*>(0x11000), 64)) {
            return -1;
        }
        if (asccomm_channel_fence_on_thread(t, c)) {
            return -1;
        }
        return asccomm_write_on_thread(t, c, reinterpret_cast<void*>(0x14000), reinterpret_cast<void*>(0x13000), 256);
    }
    void Capture()
    {
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(Native(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
        ASSERT_EQ(info.ready, 1U);
        ASSERT_EQ(info.reserved, 1U);
    }
};
TEST_F(BinaryPipeline, CompleteEntryMatchesNativeAndRefreshesTokens)
{
    Capture();
    Reset();
    buffers[0].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 61;
    buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 71;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedWq = wq;
    const auto expectedPi = cq.contextInfo.ubJfs.sqHead;
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    uint64_t counts[4]{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedWq);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, expectedPi);
    EXPECT_EQ(tailCalls, 1U);
    EXPECT_EQ(counts[1], 2U);
    EXPECT_EQ(counts[2], 2U);
    EXPECT_EQ(counts[3], 1U);
    EXPECT_FALSE(asccomm_is_batch_launch_mode());
}
// Slices captured in one registration may resolve differently after the live
// registration table changes. Preserve native first-match/fallback semantics.
TEST_F(BinaryPipeline, RegistrationGroupPreservesOverlappingSliceSelection)
{
    Capture();
    for (const bool overlap : {true, false}) {
        Reset();
        auto& first = buffers[0].bufferInfo.rma;
        first.addr = overlap ? 0x13000 : 0x90000;
        first.size = 128;
        first.protectionInfo.memInfo.ub.tokenId = 81;
        buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 91;
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(Native(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        const auto expected = wq;
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
        EXPECT_EQ(info.reserved, overlap ? 5U : 1U);
        if (overlap) {
            EXPECT_EQ(tailCalls, 0U);
            EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 7U);
        } else {
            EXPECT_EQ(wq, expected);
        }
    }
}
TEST_F(BinaryPipeline, FreshQueueDoesNotWaitForHardwareBeforeFirstWqe)
{
    cq.contextInfo.ubJfs.sqHead = 0;
    asccomm_ub_domain_exchange_v1(++testDomain);
    autoComplete = false; // No hardware progress until the whole graph exists.
    asccomm_set_sq_full_timeout(1);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    EXPECT_EQ(tailCalls, 0U); // No initialization checkpoint/forced launch.
    EXPECT_EQ(hardwareHead, 0U);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 2U);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
}
TEST_F(BinaryPipeline, UsedQueueUsesSharedCompletedCiWithoutPrivateHistory)
{
    asccomm_ub_domain_exchange_v1(++testDomain);
    cq.contextInfo.ubJfs.sqTail = cq.contextInfo.ubJfs.sqHead;
    autoComplete = false;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 9U);
    EXPECT_EQ(tailCalls, 0U);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
}
TEST_F(BinaryPipeline, NativeFlowControlRejectsOversizedBinaryCapture)
{
    cq.contextInfo.ubJfs.sqHead = 0;
    asccomm_ub_domain_exchange_v1(++testDomain);
    autoComplete = true;
    asccomm_set_sq_full_timeout(1);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
    const auto t = reinterpret_cast<ThreadHandle>(&thread), c = reinterpret_cast<ChannelHandle>(&channel);
    ASSERT_EQ(
        asccomm_write_on_thread(t, c, reinterpret_cast<void*>(0x14000), reinterpret_cast<void*>(0x13000), 2560), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.ready, 0U);
    EXPECT_EQ(info.reserved, 2U); // Live queue-space waits are not a replayable template.
    EXPECT_GT(headQueries, 0U);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 20U);
}
TEST_F(BinaryPipeline, DbCompletionDoesNotWaitForLaterSqes)
{
    asccomm_ub_domain_exchange_v1(++testDomain);
    asccomm_set_sq_full_timeout(1);
    AsccommUbCompletion completion;
    completion.generation = 1;
    const Asc::UbJettyLiteId jetty(0, 0, 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_rtsq_ub_checkpoint(thread, jetty, 7, {&completion, 1}), 0);
    const auto afterDb = tail;
    ASSERT_EQ(asccomm_rtsq_notify_wait(thread, 1, 1), 0);
    ASSERT_EQ(asccomm_rtsq_launch(thread), 0);
    ASSERT_GT(tail, afterDb);
    hardwareHead = afterDb; // DB is retired, following wait is still blocked.
    ASSERT_EQ(asccomm_rtsq_wait_ub(thread, completion), 0);
    EXPECT_TRUE(completion.done);
    EXPECT_LT(hardwareHead, tail);
}
TEST_F(BinaryPipeline, DbSubmissionFailureCannotReleaseCi)
{
    asccomm_ub_domain_exchange_v1(++testDomain);
    AsccommUbCompletion completion;
    completion.generation = 1;
    const Asc::UbJettyLiteId jetty(0, 0, 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    failTail = true;
    EXPECT_NE(asccomm_rtsq_ub_checkpoint(thread, jetty, 7, {&completion, 1}), 0);
    EXPECT_TRUE(completion.failed);
    EXPECT_FALSE(completion.done);
    EXPECT_NE(asccomm_rtsq_wait_ub(thread, completion), 0);
}
TEST_F(BinaryPipeline, FlowControlTimeoutDoesNotPretendCiAdvanced)
{
    cq.contextInfo.ubJfs.sqHead = 0;
    asccomm_ub_domain_exchange_v1(++testDomain);
    autoComplete = true;
    asccomm_set_sq_full_timeout(1);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    autoComplete = false;
    const auto t = reinterpret_cast<ThreadHandle>(&thread), c = reinterpret_cast<ChannelHandle>(&channel);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    EXPECT_NE(
        asccomm_write_on_thread(t, c, reinterpret_cast<void*>(0x14000), reinterpret_cast<void*>(0x13000), 2560), 0);
    const auto failedWq = wq;
    EXPECT_NE(
        asccomm_write_on_thread(t, c, reinterpret_cast<void*>(0x14000), reinterpret_cast<void*>(0x13000), 128), 0);
    EXPECT_EQ(wq, failedWq);
}
TEST_F(BinaryPipeline, NativeSubmissionReusesDeviceIdAndInvalidatesChangedBinding)
{
    asccomm_ub_domain_exchange_v1(++testDomain);
    auto submit = [&]() { return asccomm_rtsq_addr_notify_record(thread, 0x12000, 0x13000); };
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 1U);
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 1U);
    ++thread.devInfo.devPhyId;
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 2U);
    ++rtsq.contextInfo.rtsqContext.sqId;
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 3U);
    asccomm_ub_domain_exchange_v1(++testDomain);
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 4U);
    ++thread.devInfo.devPhyId;
    failResolve = true;
    EXPECT_NE(submit(), 0);
    EXPECT_EQ(resolveCalls, 5U);
    failResolve = false;
    ASSERT_EQ(asccomm_rtsq_launch(thread), 0);
    EXPECT_EQ(resolveCalls, 6U);
    ASSERT_EQ(submit(), 0);
    EXPECT_EQ(resolveCalls, 6U);
}

TEST_F(BinaryPipeline, DirectSubmissionBoundariesAndWrapPreserveLocalBuffer)
{
    for (const uint32_t count : {1U, 120U, 128U, 129U}) {
        if (handle) {
            ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &handle, nullptr, nullptr), 0);
        }
        Reset();
        auto copies = [&]() {
            for (uint32_t i = 0; i < count; ++i) {
                if (asccomm_local_copy_on_thread(
                        reinterpret_cast<ThreadHandle>(&thread), reinterpret_cast<void*>(0x12000),
                        reinterpret_cast<void*>(0x11000), 64)) {
                    return -1;
                }
            }
            return 0;
        };
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(copies(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
        ASSERT_EQ(info.reserved, 1U);
        auto wrap = [&]() {
            Reset();
            hardwareHead = rtsq.contextInfo.rtsqContext.sqHead = 400;
            tail = rtsq.contextInfo.rtsqContext.sqTail = 500;
        };
        wrap();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(copies(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        const auto expected = queue;
        const auto expectedTail = tail;
        wrap();
        local.fill(0xa5);
        const auto untouched = local;
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
        EXPECT_EQ(info.reserved, 1U);
        EXPECT_EQ(queue, expected);
        EXPECT_EQ(tail, expectedTail);
        EXPECT_EQ(local, untouched);
        EXPECT_EQ(rtsq.contextInfo.rtsqContext.pendingSqeCnt, 0U);
        // Preserve captured publication boundaries, including a native staging flush.
        EXPECT_EQ(tailCalls, (count + HCOMM_RESOURCE_RTSQ_BATCH_SIZE - 1) / HCOMM_RESOURCE_RTSQ_BATCH_SIZE);
        EXPECT_EQ(resolveCalls, 0U); // device resolved once when the plan is sealed
        EXPECT_EQ(tailQueries, 0U);
        EXPECT_EQ(headQueries, 0U);
    }
}
TEST_F(BinaryPipeline, SingleWriterUsesLocalTailWithoutHardwareQueries)
{
    Capture();
    Reset();
    tail = rtsq.contextInfo.rtsqContext.sqTail = 12; // single authoritative writer
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(tail, 14U);
    EXPECT_EQ(headQueries, 0U);
    EXPECT_EQ(tailQueries, 0U);
    EXPECT_EQ(rtsq.contextInfo.rtsqContext.sqTail, tail);
    for (size_t i = 0; i < 12 * 64; ++i) {
        EXPECT_EQ(queue[i], 0U);
    }
}
TEST_F(BinaryPipeline, RepeatedPrepareAndSuccessiveHitsMatchNative)
{
    for (uint32_t repeats : {1U, 2U, 8U}) {
        if (handle) {
            ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &handle, nullptr, nullptr), 0);
        }
        Reset();
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
        for (uint32_t i = 0; i < repeats; ++i) {
            ASSERT_EQ(Native(), 0);
        }
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
        ASSERT_EQ(info.reserved, 1U);
        for (uint32_t hit = 0; hit < 3; ++hit) {
            Reset();
            buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 100 + hit;
            ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
            for (uint32_t i = 0; i < repeats; ++i) {
                ASSERT_EQ(Native(), 0);
            }
            ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
            const auto expectedQueue = queue;
            const auto expectedWq = wq;
            const auto expectedPi = cq.contextInfo.ubJfs.sqHead;
            const auto expectedDetour = cq.contextInfo.ubJfs.sqTail;
            Reset();
            ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
            ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
            EXPECT_EQ(info.reserved, 1U);
            EXPECT_EQ(queue, expectedQueue);
            EXPECT_EQ(wq, expectedWq);
            EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, expectedPi);
            EXPECT_EQ(cq.contextInfo.ubJfs.sqTail, expectedDetour);
        }
    }
}
TEST_F(BinaryPipeline, AliasedChannelsShareLiveConnectionProgress)
{
    ChannelEntity alias = channel;
    auto run = [&]() {
        if (Native()) {
            return -1;
        }
        return asccomm_write_on_thread(
            reinterpret_cast<ThreadHandle>(&thread), reinterpret_cast<ChannelHandle>(&alias),
            reinterpret_cast<void*>(0x14000), reinterpret_cast<void*>(0x13000), 128);
    };
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(run(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    ASSERT_EQ(info.reserved, 1U);
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(run(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedW = wq;
    const auto pi = cq.contextInfo.ubJfs.sqHead;
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedW);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, pi);
}
TEST_F(BinaryPipeline, RelocatedRepeatMatchesNativeAndNeverFallsBackToOldAddresses)
{
    auto run = [&](uint64_t input, uint64_t output) {
        auto t = reinterpret_cast<ThreadHandle>(&thread);
        if (asccomm_local_copy_on_thread(t, reinterpret_cast<void*>(output), reinterpret_cast<void*>(input), 64)) {
            return -1;
        }
        return asccomm_write_on_thread(
            t, reinterpret_cast<ChannelHandle>(&channel), reinterpret_cast<void*>(0x18000),
            reinterpret_cast<void*>(input), 128);
    };
    uint64_t captured[4] = {0x11000, 256, 0x12000, 256};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v2(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr, captured), 0);
    ASSERT_EQ(run(captured[0], captured[2]), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    ASSERT_EQ(info.reserved, 1U);
    for (uint64_t offset : {0x4000U, 0x2000U, 0U, 0x4000U}) {
        uint64_t current[4] = {captured[0] + offset, 256, captured[2] + offset, 256};
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(run(current[0], current[2]), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        auto expected = queue;
        auto expectedWq = wq;
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v2(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr, current), 0);
        EXPECT_EQ(info.reserved, 1U);
        EXPECT_EQ(queue, expected);
        EXPECT_EQ(wq, expectedWq);
    }
    Reset();
    cq.contextInfo.ubJfs.tpID++;
    uint64_t current[4] = {0x15000, 256, 0x16000, 256};
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v2(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr, current), 0);
    EXPECT_EQ(info.reserved, 5U);
    EXPECT_EQ(tailCalls, 0U);
    EXPECT_EQ(rtsq.contextInfo.rtsqContext.taskId, 5U);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 7U);
}
// The native path permits a local address outside the advertised buffer range.
// Binary replay must select the same live protection, not retain the captured token.
TEST_F(BinaryPipeline, LocalProtectionFallbackMatchesNativeAndRefreshesToken)
{
    HcommRegedBufferEntity localBuffer = buffers[0];
    localBuffer.bufferInfo.rma.addr = 0x50000;
    channel.localBufferAddr = &localBuffer;
    channel.localBufferNum = 1;
    Capture();
    Reset();
    localBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 91;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedWq = wq;
    const auto expectedPi = cq.contextInfo.ubJfs.sqHead;
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    uint64_t counts[4]{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedWq);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, expectedPi);
    EXPECT_EQ(tailCalls, 1U);
    EXPECT_EQ(counts[2], 2U);
}
TEST_F(BinaryPipeline, RemoteRangeReturnsToAlgorithmWithoutPublication)
{
    Capture();
    Reset();
    buffers[1].bufferInfo.rma.addr = 0x50000;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    uint64_t counts[4]{};
    EXPECT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 5U);
    EXPECT_EQ(counts[0], 0x50460000ULL | 28U);  // remote protection rejection
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 7U); // no WQE was published
}
TEST_F(BinaryPipeline, LocalFallbackStillRequiresUrmaProtection)
{
    HcommRegedBufferEntity localBuffer = buffers[0];
    localBuffer.bufferInfo.rma.addr = 0x50000;
    channel.localBufferAddr = &localBuffer;
    channel.localBufferNum = 1;
    Capture();
    Reset();
    localBuffer.bufferInfo.rma.protectionInfo.type = HCOMM_PROTECTION_TYPE_RDMA;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    uint64_t counts[4]{};
    EXPECT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 5U);
    EXPECT_EQ(counts[0], 0x50460000ULL | 32U); // local protection rejection
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 7U);
}
TEST_F(BinaryPipeline, ChangedResourceFallsBackBeforePublication)
{
    Capture();
    Reset();
    cq.contextInfo.ubJfs.tpID++;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 5U);
    EXPECT_EQ(tailCalls, 0U);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 7U);
}
TEST_F(BinaryPipeline, ChangedRemoteEidFallsBackBeforePublication)
{
    Capture();
    Reset();
    cq.contextInfo.ubJfs.remoteEID[15] ^= 1;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    uint64_t counts[4]{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 5U);
    EXPECT_EQ(counts[0], 0x50460000ULL | 52U);
}
TEST_F(BinaryPipeline, BoundTokenSourcesRefreshAcrossSuccessiveHits)
{
    Capture();
    for (uint32_t token : {61U, 91U, 121U}) {
        Reset();
        buffers[0].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = token;
        buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = token + 10;
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(Native(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        const auto expected = wq;
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
        EXPECT_EQ(info.reserved, 1U);
        EXPECT_EQ(wq, expected);
    }
}
TEST_F(BinaryPipeline, TailFailureNeverReexecutesAndInvalidates)
{
    Capture();
    Reset();
    failTail = true;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    EXPECT_NE(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 4U);
    EXPECT_EQ(tailCalls, 1U);
    EXPECT_EQ(cq.contextInfo.ubJfs.sqHead, 9U);
    failTail = false;
    EXPECT_NE(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
}
TEST_F(BinaryPipeline, RdmaCompleteEntryPreservesCqAndRefreshesKeys)
{
    CqContext resourceCq{};
    std::array<uint8_t, 2048 * 64> completion{};
    uint32_t softPi = 0, softCi = 0;
    auto configure = [&]() {
        Reset();
        cq = {};
        resourceCq = {};
        completion.fill(0);
        softPi = 0;
        softCi = 0;
        channel.protocol = COMM_PROTOCOL_ROCE;
        channel.cqContextAddr = &resourceCq;
        channel.cqNum = 1;
        cq.type = HCOMM_SQ_CONTEXT_TYPE_ROCE;
        auto& q = cq.contextInfo.roceSq;
        q.sqVa = reinterpret_cast<uint64_t>(wq.data());
        q.depth = 8;
        q.wqeSize = 64;
        q.dbHwVa = 0x40000;
        q.dbSwVa = reinterpret_cast<uint64_t>(&softPi);
        q.sqHead = 7;
        q.sqTail = 7;
        resourceCq.type = HCOMM_CQ_CONTEXT_TYPE_ROCE;
        auto& c = resourceCq.contextInfo.roceCq;
        c.cqVa = reinterpret_cast<uint64_t>(completion.data());
        c.cqDepth = 1024;
        c.dbSwVa = reinterpret_cast<uint64_t>(&softCi);
        for (auto& b : buffers) {
            b.bufferInfo.rma.protectionInfo.type = HCOMM_PROTECTION_TYPE_RDMA;
        }
    };
    configure();
    Capture();
    configure();
    buffers[0].bufferInfo.rma.protectionInfo.memInfo.roce.lkey = 61;
    buffers[1].bufferInfo.rma.protectionInfo.memInfo.roce.rkey = 71;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedWq = wq;
    const auto expectedHead = cq.contextInfo.roceSq.sqHead, expectedTail = cq.contextInfo.roceSq.sqTail;
    const auto expectedCqHead = resourceCq.contextInfo.roceCq.cqHead,
               expectedCqTail = resourceCq.contextInfo.roceCq.cqTail;
    const auto expectedPi = softPi, expectedCi = softCi;
    configure();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedWq);
    EXPECT_EQ(cq.contextInfo.roceSq.sqHead, expectedHead);
    EXPECT_EQ(cq.contextInfo.roceSq.sqTail, expectedTail);
    EXPECT_EQ(resourceCq.contextInfo.roceCq.cqHead, expectedCqHead);
    EXPECT_EQ(resourceCq.contextInfo.roceCq.cqTail, expectedCqTail);
    EXPECT_EQ(softPi, expectedPi);
    EXPECT_EQ(softCi, expectedCi);
}
TEST_F(BinaryPipeline, UbBatchNotifyUsesCurrentIndexedProtection)
{
    HcommRegedNotifyEntity notify{};
    notify.type = HCOMM_REGED_NOTIFY_RMA_RT;
    auto& n = notify.notifyInfo.rmaRt;
    n.addr = 0x30000;
    n.size = 4;
    n.protectionInfo.type = HCOMM_PROTECTION_TYPE_URMA;
    n.protectionInfo.memInfo.ub.tokenId = 31;
    n.protectionInfo.memInfo.ub.tokenValue = 37;
    channel.remoteNotifyAddr = &notify;
    channel.remoteNotifyNum = 1;
    asccomm_batch_transfer_desc descriptors[2]{};
    descriptors[0].transType = ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY;
    descriptors[0].transferInfo.writeWithNotify.src = reinterpret_cast<void*>(0x13000);
    descriptors[0].transferInfo.writeWithNotify.dst = reinterpret_cast<void*>(0x14000);
    descriptors[0].transferInfo.writeWithNotify.len = 128;
    descriptors[0].transferInfo.writeWithNotify.notifyIdx = 0;
    descriptors[1].transType = ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD;
    descriptors[1].transferInfo.notifyRecord.notifyIdx = 0;
    auto operation = [&]() {
        return asccomm_batch_transfer_on_thread(
            reinterpret_cast<ThreadHandle>(&thread), reinterpret_cast<ChannelHandle>(&channel), descriptors, 2);
    };
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(operation(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    Reset();
    n.protectionInfo.memInfo.ub.tokenId = 41;
    n.protectionInfo.memInfo.ub.tokenValue = 43;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(operation(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedWq = wq;
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedWq);
}
} // namespace
TEST_F(BinaryPipeline, CompiledRtsqBindingRejectsChangedDepthBeforeBinaryWrite)
{
    Capture();
    Reset();
    thread.sqContextAddr->contextInfo.rtsqContext.sqDepth /= 2;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.reserved, 5U);
}

#include "../../../../src/aicpu/asccomm/primitives/task_cache/binary_template.h"
TEST_F(BinaryPipeline, CompiledScheduleKeepsNativeOrderAndReducesChecks)
{
    using namespace AsccommBinaryCache;
    Entry entry;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommBinaryCache::Capture() = &entry;
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommBinaryCache::Capture() = nullptr;
    const auto expected = queue;
    const auto expectedWq = wq;
    ASSERT_TRUE(entry.Seal(true));
    ASSERT_TRUE(CompileTransport(entry));
    ASSERT_TRUE(CompileRtsq(entry));
    ASSERT_TRUE(entry.direct);
    // Recompilation reuses storage but must not bind owners to stale slots.
    const auto resolutionsBeforeRecompile = resolveCalls;
    ASSERT_TRUE(CompileTransport(entry));
    ASSERT_TRUE(CompileRtsq(entry));
    EXPECT_EQ(resolveCalls, resolutionsBeforeRecompile);
    EXPECT_LT(entry.checks.size(), entry.events.size());
    EXPECT_LT(entry.segments.size(), entry.events.size());
    // Direct hit must execute only its sealed plan, never captured events/payload.
    entry.events.clear();
    std::fill(entry.bytes.begin(), entry.bytes.end(), 0xa5);
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    EXPECT_EQ(ReplayRtsq(entry), Result::Complete);
    EXPECT_EQ(queue, expected);
    EXPECT_EQ(wq, expectedWq);
}

TEST_F(BinaryPipeline, ExplicitPublicationBoundariesArePreserved)
{
    for (const bool notifyBoundary : {false, true}) {
        if (handle) {
            ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &handle, nullptr, nullptr), 0);
        }
        Reset();
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
        auto copy = [&]() {
            return asccomm_local_copy_on_thread(
                reinterpret_cast<ThreadHandle>(&thread), reinterpret_cast<void*>(0x12000),
                reinterpret_cast<void*>(0x11000), 64);
        };
        for (int i = 0; i < 4; ++i) {
            ASSERT_EQ(copy(), 0);
        }
        if (notifyBoundary) {
            ASSERT_EQ(asccomm_rtsq_addr_notify_record(thread, 0x13000, 0x14000), 0);
        } else {
            // Model two independently captured arrays with an explicit flush.
            AsccommBinaryCache::CaptureTask(
                AsccommBinaryCache::Kind::Launch, reinterpret_cast<uint64_t>(&thread), nullptr, 0);
            ASSERT_EQ(asccomm_rtsq_launch(thread), 0);
        }
        for (int i = 0; i < 4; ++i) {
            ASSERT_EQ(copy(), 0);
        }
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        AsccommReplayInfoV1 info{};
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
        ASSERT_EQ(info.reserved, 1U);
        const auto expected = queue;
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
        ASSERT_EQ(info.reserved, 1U);
        EXPECT_EQ(queue, expected);
        EXPECT_EQ(tailCalls, 2U);
    }
}

TEST_F(BinaryPipeline, BoundLaunchOnlyThreadRejectsPendingWorkBeforePublication)
{
    using namespace AsccommBinaryCache;
    Entry entry;
    auto otherThread = thread;
    auto otherContext = *thread.sqContextAddr;
    otherThread.sqContextAddr = &otherContext;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommBinaryCache::Capture() = &entry;
    CaptureTask(Kind::Launch, reinterpret_cast<uint64_t>(&otherThread), nullptr, 0);
    ASSERT_EQ(Native(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommBinaryCache::Capture() = nullptr;
    ASSERT_TRUE(entry.Seal(true));
    ASSERT_TRUE(CompileTransport(entry));
    ASSERT_TRUE(CompileRtsq(entry));
    ASSERT_TRUE(entry.direct);
    ASSERT_EQ(entry.queues.size(), 2U);
    unsigned submissions = 0;
    for (const auto& op : entry.replay.instructions) {
        if (op.kind == Kind::Submit) {
            ++submissions;
            EXPECT_EQ(entry.queues[op.plan.sqIndex].thread, reinterpret_cast<uint64_t>(&thread));
        }
    }
    EXPECT_EQ(submissions, 1U);
    for (auto index : entry.checks) {
        EXPECT_NE(entry.events[index].kind, Kind::Launch);
        EXPECT_NE(entry.events[index].kind, Kind::UbWqe);
    }
    bool fused = false;
    for (const auto& segment : entry.segments) {
        fused |= segment.kind == Kind::UbBegin && segment.count > 2;
    }
    EXPECT_TRUE(fused);
    Reset();
    otherContext.contextInfo.rtsqContext.pendingSqeCnt = 1;
    const auto untouched = wq;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    EXPECT_EQ(ReplayRtsq(entry), Result::Fallback);
    EXPECT_EQ(wq, untouched);
    EXPECT_EQ(tailCalls, 0U);
}

// Recycled compile storage must not retain old event kinds, queue fields or tokens.
TEST_F(BinaryPipeline, RecycledEntryRecompilesDifferentShape)
{
    Capture();
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_DESTROY, &handle, nullptr, nullptr), 0);
    Reset();
    buffers[0].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 61;
    buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 71;
    const auto t = reinterpret_cast<ThreadHandle>(&thread);
    const auto c = reinterpret_cast<ChannelHandle>(&channel);
    auto small = [&]() {
        return asccomm_write_on_thread(t, c, reinterpret_cast<void*>(0x15000), reinterpret_cast<void*>(0x16000), 64);
    };
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(small(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto expectedQ = queue;
    const auto expectedWq = wq;
    Reset();
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(small(), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    ASSERT_EQ(info.reserved, 1U);
    Reset();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    uint64_t counts[4]{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, counts), 0);
    EXPECT_EQ(info.reserved, 1U);
    EXPECT_EQ(queue, expectedQ);
    EXPECT_EQ(wq, expectedWq);
}

TEST_F(BinaryPipeline, UnsupportedControlDiscardsOnlyCapture)
{
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_CREATE, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_BEGIN, &handle, nullptr, nullptr), 0);
    ASSERT_EQ(Native(), 0);
    // Runtime-global timeout has no binary event. The original native work
    // still completes, but no command fallback template may be published.
    ASSERT_EQ(asccomm_set_notify_wait_timeout(10.0F), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    const auto publications = tailCalls;
    AsccommReplayInfoV1 info{};
    ASSERT_EQ(asccomm_binary_replay_v1(ASCCOMM_REPLAY_END, &handle, &info, nullptr), 0);
    EXPECT_EQ(info.ready, 0U);
    EXPECT_EQ(info.reserved, 2U);
    EXPECT_NE(asccomm_binary_replay_v1(ASCCOMM_REPLAY_EXECUTE, &handle, &info, nullptr), 0);
    EXPECT_EQ(tailCalls, publications);
}

TEST_F(BinaryPipeline, CapturedPublicationOrderOverridesFirstWorkOrder)
{
    using namespace AsccommBinaryCache;
    auto other = thread;
    auto otherContext = rtsq;
    std::array<uint8_t, 512 * 64> otherQueue{};
    std::array<uint8_t, HCOMM_RESOURCE_RTSQ_BATCH_SIZE * 64> otherLocal{};
    other.sqContextAddr = &otherContext;
    auto& oq = otherContext.contextInfo.rtsqContext;
    oq.sqBaseAddr = reinterpret_cast<uint64_t>(otherQueue.data());
    oq.localSqAddr = reinterpret_cast<uint64_t>(otherLocal.data());
    oq.sqId = 7;
    oq.taskIdEnd = 1024;
    rtsq.contextInfo.rtsqContext.taskIdEnd = 1024;
    Entry entry;
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    AsccommBinaryCache::Capture() = &entry;
    auto copy = [&](ThreadEntity& t) {
        return asccomm_local_copy_on_thread(
            reinterpret_cast<ThreadHandle>(&t), reinterpret_cast<void*>(0x12000), reinterpret_cast<void*>(0x11000), 64);
    };
    ASSERT_EQ(copy(thread), 0); // first work is on queue 0
    ASSERT_EQ(copy(other), 0);
    publicationSqIds.clear();
    ASSERT_EQ(asccomm_rtsq_launch(other), 0); // first publication is on queue 7
    for (unsigned i = 0; i < 129; ++i) {
        ASSERT_EQ(copy(thread), 0); // native full-buffer flush
    }
    ASSERT_EQ(copy(other), 0);
    ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
    AsccommBinaryCache::Capture() = nullptr;
    const auto order = publicationSqIds;
    ASSERT_EQ(order, (std::vector<uint32_t>{7, 0, 0, 7}));
    ASSERT_TRUE(entry.Seal(true));
    ASSERT_TRUE(CompileTransport(entry));
    ASSERT_TRUE(CompileRtsq(entry));
    const auto expected = queue;
    const auto expectedOther = otherQueue;
    Reset();
    rtsq.contextInfo.rtsqContext.taskIdEnd = 1024;
    oq.sqHead = oq.sqTail = oq.pendingSqeCnt = 0;
    oq.taskId = 5;
    otherQueue.fill(0);
    otherLocal.fill(0);
    publicationSqIds.clear();
    ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
    EXPECT_EQ(ReplayRtsq(entry), Result::Complete);
    EXPECT_EQ(publicationSqIds, order);
    EXPECT_EQ(queue, expected);
    EXPECT_EQ(otherQueue, expectedOther);
}

TEST(ThreadNotifyResolver, SegmentedAndMissingLinks)
{
    std::array<HcommRegedNotifyEntity, 128> first{}, second{}, third{};
    uint64_t next = reinterpret_cast<uint64_t>(second.data());
    std::memcpy(&first[127], &next, sizeof(next));
    next = reinterpret_cast<uint64_t>(third.data());
    std::memcpy(&second[127], &next, sizeof(next));
    ThreadEntity thread{};
    thread.localNotifyAddr = first.data();
    thread.localNotifyNum = 255;
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 126), &first[126]);
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 127), &second[0]);
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 128), &second[1]);
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 254), &third[0]);
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 255), nullptr);
    next = 0;
    std::memcpy(&first[127], &next, sizeof(next));
    EXPECT_EQ(Asc::ResolveThreadNotify(thread, 127), nullptr);
}

extern "C" uint64_t asccomm_ub_domain_exchange_v2(uint64_t, const void*);
TEST(BinaryDomainLifecycle, RetiresExpiredWorkerDomainsOnAdmission)
{
    for (uint64_t id = 10000; id < 10600; ++id) {
        auto owner = std::make_shared<int>(1);
        std::weak_ptr<void> lifetime = owner;
        const auto previous = asccomm_ub_domain_exchange_v2(id, &lifetime);
        EXPECT_EQ(asccomm_ub_current_domain(), id);
        asccomm_ub_domain_exchange_v2(previous, nullptr);
    }
}

TEST_F(BinaryPipeline, CompiledNotifyGuardsCoverSegmentBoundaries)
{
    using namespace AsccommBinaryCache;
    std::array<HcommRegedNotifyEntity, 128> first{}, second{}, third{};
    uint64_t next = reinterpret_cast<uint64_t>(second.data());
    std::memcpy(&first[127], &next, sizeof(next));
    next = reinterpret_cast<uint64_t>(third.data());
    std::memcpy(&second[127], &next, sizeof(next));
    thread.localNotifyAddr = first.data();
    thread.localNotifyNum = 255;
    for (const uint64_t index : {126U, 127U, 128U, 254U}) {
        Reset();
        auto* notify = const_cast<HcommRegedNotifyEntity*>(Asc::ResolveThreadNotify(thread, index));
        ASSERT_NE(notify, nullptr);
        notify->type = HCOMM_REGED_NOTIFY_IPC_RT;
        notify->notifyInfo.ipcRt.notifyId = 17;
        Entry entry;
        const uint64_t binding[]{index, 17};
        ASSERT_TRUE(entry.Append(
            Kind::ThreadNotifyGuard, reinterpret_cast<uint64_t>(&thread), VERSION, binding, sizeof(binding), {},
            ASCCOMM_BINARY_ENTRY_BUDGET));
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        AsccommBinaryCache::Capture() = &entry;
        ASSERT_EQ(Native(), 0);
        ASSERT_EQ(asccomm_batch_mode_end(nullptr), 0);
        AsccommBinaryCache::Capture() = nullptr;
        ASSERT_TRUE(entry.Seal(true));
        ASSERT_TRUE(CompileTransport(entry));
        ASSERT_TRUE(CompileRtsq(entry));
        ASSERT_TRUE(entry.direct);
        EXPECT_EQ(
            entry.replay.instructions.front().kind,
            index < 127 ? Kind::ThreadNotifyGuard : Kind::ThreadNotifyGuardSegmented);
        Reset();
        ASSERT_EQ(asccomm_batch_mode_start(nullptr), 0);
        EXPECT_EQ(ReplayRtsq(entry), Result::Complete);
        Reset();
        notify->notifyInfo.ipcRt.notifyId = 18;
        const auto submitted = tailCalls;
        EXPECT_EQ(ReplayRtsq(entry), Result::Fallback);
        EXPECT_EQ(tailCalls, submitted);
        if (index >= 127) {
            notify->notifyInfo.ipcRt.notifyId = 17;
            const uint64_t missing = 0;
            std::memcpy(&first[127], &missing, sizeof(missing));
            EXPECT_EQ(ReplayRtsq(entry), Result::Fallback);
            next = reinterpret_cast<uint64_t>(second.data());
            std::memcpy(&first[127], &next, sizeof(next));
        }
    }
}
