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

#include <vector>

#include "asccomm_launch.h"
#include "asccomm_launch_stub.h"

namespace {
class AsccommLaunchTest : public testing::Test {
protected:
    void SetUp() override
    {
        asccomm_set_batch_launch_mode(false);
        asccomm_set_batch_launch_mode_with_tag(nullptr, false);
        asccomm_clear_registered_threads(nullptr);
        asccomm_clear_registered_threads("tag-a");
        asccomm_clear_registered_threads("tag-b");
        g_launchStubState = {};
    }
};

TEST_F(AsccommLaunchTest, ManagesBatchModeAndTimeoutDefaults)
{
    EXPECT_FALSE(asccomm_is_batch_launch_mode());
    asccomm_set_batch_launch_mode(true);
    EXPECT_TRUE(asccomm_is_batch_launch_mode());

    EXPECT_EQ(asccomm_get_notify_wait_timeout(), 1836U);
    asccomm_set_notify_wait_timeout(3U);
    EXPECT_EQ(asccomm_get_notify_wait_timeout(), 3U);

    EXPECT_EQ(asccomm_get_sq_full_timeout(), 1856U);
    asccomm_set_sq_full_timeout(5U);
    EXPECT_EQ(asccomm_get_sq_full_timeout(), 5U);
}

TEST_F(AsccommLaunchTest, RegistersThreadsOnlyInBatchModeAndDeduplicates)
{
    asccomm_register_thread(101U);
    EXPECT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_TRUE(g_launchStubState.lastThreads.empty());

    asccomm_set_batch_launch_mode(true);
    asccomm_register_thread(101U);
    asccomm_register_thread(102U);
    asccomm_register_thread(101U);
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastThreads, (std::vector<ThreadHandle>{101U, 102U}));

    ASSERT_EQ(asccomm_try_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastTryThreads, (std::vector<ThreadHandle>{101U, 102U}));

    asccomm_clear_registered_threads(nullptr);
    g_launchStubState.lastThreads = {999U};
    EXPECT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastThreads, (std::vector<ThreadHandle>{999U}));
}

TEST_F(AsccommLaunchTest, ReturnsLaunchResult)
{
    asccomm_set_batch_launch_mode(true);
    asccomm_register_thread(201U);
    g_launchStubState.taskResult = ASCCOMM_E_INTERNAL;
    g_launchStubState.tryResult = ASCCOMM_E_AGAIN;
    EXPECT_EQ(asccomm_launch_registered_threads(), ASCCOMM_E_INTERNAL);
    EXPECT_EQ(asccomm_try_launch_registered_threads(), ASCCOMM_E_AGAIN);
}

TEST_F(AsccommLaunchTest, TagScopesRegisteredThreadsAndFailureRetriesSameTag)
{
    asccomm_set_batch_launch_mode_with_tag("tag-a", true);
    asccomm_register_thread(101U);
    asccomm_set_batch_launch_mode_with_tag("tag-b", true);
    asccomm_register_thread(202U);

    ASSERT_EQ(asccomm_set_batch_launch_mode_with_tag("tag-a", false), ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastThreads, (std::vector<ThreadHandle>{101U}));
    ASSERT_EQ(asccomm_clear_registered_threads("tag-a"), ASCCOMM_SUCCESS);

    ASSERT_EQ(asccomm_set_batch_launch_mode_with_tag("tag-b", false), ASCCOMM_SUCCESS);
    g_launchStubState.taskResult = ASCCOMM_E_INTERNAL;
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_E_INTERNAL);
    g_launchStubState.taskResult = ASCCOMM_SUCCESS;
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastThreads, (std::vector<ThreadHandle>{202U}));
    ASSERT_EQ(asccomm_clear_registered_threads("tag-b"), ASCCOMM_SUCCESS);
}

TEST_F(AsccommLaunchTest, TryLaunchUsesSnapshotOfRegisteredThreads)
{
    asccomm_set_batch_launch_mode_with_tag(nullptr, true);
    asccomm_register_thread(301U);
    g_launchStubState.threadToRegisterDuringTry = 302U;

    ASSERT_EQ(asccomm_try_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastTryThreads, (std::vector<ThreadHandle>{301U}));

    g_launchStubState.threadToRegisterDuringTry = 0;
    g_launchStubState.lastTryThreads.clear();
    ASSERT_EQ(asccomm_try_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastTryThreads, (std::vector<ThreadHandle>{301U, 302U}));
}

TEST_F(AsccommLaunchTest, ActiveClearPreservesOtherTaggedBatch)
{
    asccomm_set_batch_launch_mode_with_tag("keep", true);
    asccomm_register_thread(101U);
    asccomm_set_batch_launch_mode_with_tag("retire", true);
    asccomm_register_thread(202U);
    ASSERT_EQ(asccomm_clear_active_threads(), ASCCOMM_SUCCESS);
    asccomm_set_batch_launch_mode_with_tag("retire", false);
    g_launchStubState.lastThreads.clear();
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_TRUE(g_launchStubState.lastThreads.empty());
    asccomm_set_batch_launch_mode_with_tag("keep", false);
    ASSERT_EQ(asccomm_launch_registered_threads(), ASCCOMM_SUCCESS);
    EXPECT_EQ(g_launchStubState.lastThreads, (std::vector<ThreadHandle>{101U}));
    asccomm_clear_registered_threads("keep");
}

} // namespace
