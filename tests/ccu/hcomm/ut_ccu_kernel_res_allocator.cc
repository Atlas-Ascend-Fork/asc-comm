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

#include "hcomm/resource/kernel/ccu_kernel_res_allocator.h"

namespace hcomm {
namespace {

TEST(CcuKernelResAllocatorTest, NormalRequestFallsBackToBlockPool)
{
    CcuResRepository available{};
    available.blockCke[0].emplace_back(10, 48);
    available.blockGsa[0].emplace_back(100, 400);
    CcuResReq request{};
    request.ckeReq[0] = 1;
    request.gsaReq[0] = 8;

    CcuResRepository remaining{};
    CcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.cke[0].size(), 1);
    EXPECT_EQ(allocated.cke[0][0].startId, 10);
    EXPECT_EQ(allocated.cke[0][0].num, 1);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].startId, 100);
    EXPECT_EQ(allocated.gsa[0][0].num, 8);
    ASSERT_EQ(remaining.blockCke[0].size(), 1);
    EXPECT_EQ(remaining.blockCke[0][0].num, 47);
    ASSERT_EQ(remaining.blockGsa[0].size(), 1);
    EXPECT_EQ(remaining.blockGsa[0][0].num, 392);
}

TEST(CcuKernelResAllocatorTest, BlockRequestIsPlannedBeforeNormalRequest)
{
    CcuResRepository available{};
    available.cke[0].emplace_back(100, 2);
    available.blockCke[0].emplace_back(0, 10);
    CcuResReq request{};
    request.blockCkeReq[0] = 8;
    request.ckeReq[0] = 4;

    CcuResRepository remaining{};
    CcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.blockCke[0].size(), 1);
    EXPECT_EQ(allocated.blockCke[0][0].startId, 0);
    EXPECT_EQ(allocated.blockCke[0][0].num, 8);
    ASSERT_EQ(allocated.cke[0].size(), 2);
    EXPECT_EQ(allocated.cke[0][0].startId, 100);
    EXPECT_EQ(allocated.cke[0][1].startId, 8);
}

TEST(CcuKernelResAllocatorTest, FragmentedBlockPoolFailsWithoutChangingOutputs)
{
    CcuResRepository available{};
    available.blockCke[0].emplace_back(0, 3);
    available.blockCke[0].emplace_back(10, 3);
    CcuResReq request{};
    request.blockCkeReq[0] = 5;
    CcuResRepository remaining{};
    remaining.ms[0].emplace_back(77, 1);
    CcuResRepository allocated{};
    allocated.gsa[0].emplace_back(88, 2);

    EXPECT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_E_UNAVAIL);
    ASSERT_EQ(remaining.ms[0].size(), 1);
    EXPECT_EQ(remaining.ms[0][0].startId, 77);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].startId, 88);
}

TEST(CcuKernelResAllocatorTest, NormalRequestPreservesReservedGap)
{
    CcuResRepository available{};
    available.blockGsa[0].emplace_back(0, 4);
    available.blockGsa[0].emplace_back(8, 4);
    CcuResReq request{};
    request.gsaReq[0] = 6;

    CcuResRepository remaining{};
    CcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.gsa[0].size(), 2);
    EXPECT_EQ(allocated.gsa[0][0].startId, 0);
    EXPECT_EQ(allocated.gsa[0][0].num, 4);
    EXPECT_EQ(allocated.gsa[0][1].startId, 8);
    EXPECT_EQ(allocated.gsa[0][1].num, 2);
    ASSERT_EQ(remaining.blockGsa[0].size(), 1);
    EXPECT_EQ(remaining.blockGsa[0][0].startId, 10);
}

} // namespace
} // namespace hcomm
