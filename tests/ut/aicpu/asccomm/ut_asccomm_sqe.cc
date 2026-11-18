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

#include <cstdint>

#include "asccomm_primitives.h"
#include "asccomm_device.h"
#include "asccomm_result.h"
#include "aprof_pub.h"
#include "sqe_build_a5.h"

namespace {

class AsccommSqeTest : public testing::Test {
protected:
    void SetUp() override { asccomm_set_sqe_profiling(false); }
};

int32_t AsccommTestProfilingCommand(uint32_t type, void* data, uint32_t len)
{
    MsprofCommandHandle command{};
    command.type = type;
    command.profSwitch = ADPROF_TASK_TIME_L1;
    return asccomm_handle_profiling_command(type, &command, sizeof(command));
}

TEST_F(AsccommSqeTest, ProfilingCallbackEnablesSqeProfilingOnA6)
{
#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_START, nullptr, 0), 0);
    EXPECT_TRUE(asccomm_is_sqe_profiling_enabled());
#else
    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_START, nullptr, 0), 0);
    EXPECT_FALSE(asccomm_is_sqe_profiling_enabled());
#endif

    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_STOP, nullptr, 0), 0);
    EXPECT_FALSE(asccomm_is_sqe_profiling_enabled());
}

TEST_F(AsccommSqeTest, AllSqeBuildersFollowProfilingState)
{
    std::uint8_t buffer[64] = {0};
    auto* header = reinterpret_cast<Asc::Rt91095StarsSqeHeader*>(buffer);

    Asc::build_a5_sqe_notify_wait(0, 0x12345678U, 8, 3, buffer);
    EXPECT_EQ(header->sqeProf, 0U);

#if defined(__NPU_ARCH__) && (__NPU_ARCH__ == 9201 || __NPU_ARCH__ == 9202)
    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_START, nullptr, 0), 0);
    Asc::build_a5_sqe_notify_wait(0, 0x12345678U, 8, 3, buffer);
    EXPECT_EQ(header->sqeProf, 1U);

    Asc::build_a5_sqe_notify_record(0, 0x12345678U, 8, buffer);
    EXPECT_EQ(header->sqeProf, 1U);

    Asc::build_a5_sqe_sdma_copy(0, 0x12345678U, 0x1000, 0x2000, 64, 0, 0, buffer);
    EXPECT_EQ(header->sqeProf, 1U);

    Asc::UbJettyLiteId jettyId(1, 2, 3);
    Asc::build_a5_sqe_ub_db_send(0, 0x12345678U, jettyId, 4, buffer);
    EXPECT_EQ(header->sqeProf, 1U);

    Asc::build_a5_sqe_rdma_db_send(0, 0x12345678U, 0x3000, 0x1234, buffer);
    EXPECT_EQ(header->sqeProf, 1U);

    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_STOP, nullptr, 0), 0);
#else
    EXPECT_EQ(AsccommTestProfilingCommand(PROF_COMMANDHANDLE_TYPE_START, nullptr, 0), 0);
    Asc::build_a5_sqe_notify_wait(0, 0x12345678U, 8, 3, buffer);
    EXPECT_EQ(header->sqeProf, 0U);

    Asc::build_a5_sqe_notify_record(0, 0x12345678U, 8, buffer);
    EXPECT_EQ(header->sqeProf, 0U);

    Asc::build_a5_sqe_sdma_copy(0, 0x12345678U, 0x1000, 0x2000, 64, 0, 0, buffer);
    EXPECT_EQ(header->sqeProf, 0U);

    Asc::UbJettyLiteId jettyId(1, 2, 3);
    Asc::build_a5_sqe_ub_db_send(0, 0x12345678U, jettyId, 4, buffer);
    EXPECT_EQ(header->sqeProf, 0U);

    Asc::build_a5_sqe_rdma_db_send(0, 0x12345678U, 0x3000, 0x1234, buffer);
    EXPECT_EQ(header->sqeProf, 0U);
#endif
}

} // namespace
