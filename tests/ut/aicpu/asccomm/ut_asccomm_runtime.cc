/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "securec.h"
#include "gtest/gtest.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "asccomm_runtime.cc"

namespace {
std::string g_capturedLog;
uint32_t g_queryHead = 12U;
uint32_t g_queryTail = 12U;
} // namespace

bool HcclCheckLogLevel(int, int) { return true; }

bool IsErrorToWarn() { return false; }

void DlogRecord(int32_t, int32_t, const char* fmt, ...)
{
    char buffer[1024] = {};
    va_list args;
    va_start(args, fmt);
    (void)vsnprintf_s(buffer, sizeof(buffer), sizeof(buffer) - 1, fmt, args);
    va_end(args);
    g_capturedLog += buffer;
}

drvError_t halSqCqQuery(uint32_t, struct halSqCqQueryInfo* info)
{
    info->value[0] = (info->prop == DRV_SQCQ_PROP_SQ_DEPTH) ?
                         128U :
                         (info->prop == DRV_SQCQ_PROP_SQ_TAIL ? g_queryTail : g_queryHead);
    return DRV_ERROR_NONE;
}

drvError_t halSqCqConfig(uint32_t, struct halSqCqConfigInfo*) { return DRV_ERROR_NONE; }

drvError_t drvGetLocalDevIDByHostDevID(uint32_t, uint32_t* localDevId)
{
    *localDevId = 0U;
    return DRV_ERROR_NONE;
}

namespace {
class AsccommRuntimeTest : public testing::Test {
protected:
    void SetUp() override
    {
        g_capturedLog.clear();
        g_queryHead = g_queryTail = 12U;
        asccomm_clear_registered_threads();
        asccomm_set_sq_full_timeout(6U);
    }
};
} // namespace

TEST_F(AsccommRuntimeTest, SharedTailIsAuthoritativeBeforeSubmission)
{
    ThreadEntity thread{};
    HcommRtsqContext live{};
    live.sqDepth = 128;
    live.sqTail = 10;
    live.sqHead = 0;
    live.pendingSqeCnt = 1;
    auto work = live;
    work.sqTail = 3; // binary stack snapshot predates another writer
    g_queryTail = 2; // stale hardware must not replace a valid entity tail
    g_queryHead = 0;
    bool ready = false;
    EXPECT_EQ(prepare_shared_rtsq(0, thread, work, live, false, ready), ASCCOMM_SUCCESS);
    EXPECT_TRUE(ready);
    EXPECT_EQ(work.sqTail, 10U);
    EXPECT_EQ(live.sqTail, 10U);
}
