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

HcommCcuResRangePod Range(uint32_t type, uint32_t startId, uint32_t count)
{
    return {type, 0, startId, count};
}

TEST(CcuKernelResAllocatorTest, NormalRequestFallsBackToBlockPool)
{
    AscCcuResRepository available{};
    available.blockCke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 10, 48));
    available.blockGsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 100, 400));
    HcommCcuResRequestPod request{};
    request.count[HCOMM_CCU_BATCH_RES_CKE][0] = 1;
    request.count[HCOMM_CCU_BATCH_RES_GSA][0] = 8;

    AscCcuResRepository remaining{};
    AscCcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.cke[0].size(), 1);
    EXPECT_EQ(allocated.cke[0][0].resourceType, HCOMM_CCU_BATCH_RES_CKE);
    EXPECT_EQ(allocated.cke[0][0].startId, 10);
    EXPECT_EQ(allocated.cke[0][0].count, 1);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].resourceType, HCOMM_CCU_BATCH_RES_GSA);
    EXPECT_EQ(allocated.gsa[0][0].startId, 100);
    EXPECT_EQ(allocated.gsa[0][0].count, 8);
    ASSERT_EQ(remaining.blockCke[0].size(), 1);
    EXPECT_EQ(remaining.blockCke[0][0].count, 47);
    ASSERT_EQ(remaining.blockGsa[0].size(), 1);
    EXPECT_EQ(remaining.blockGsa[0][0].count, 392);
}

TEST(CcuKernelResAllocatorTest, BlockRequestIsPlannedBeforeNormalRequest)
{
    AscCcuResRepository available{};
    available.cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_CKE, 100, 2));
    available.blockCke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 0, 10));
    HcommCcuResRequestPod request{};
    request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][0] = 8;
    request.count[HCOMM_CCU_BATCH_RES_CKE][0] = 4;

    AscCcuResRepository remaining{};
    AscCcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.blockCke[0].size(), 1);
    EXPECT_EQ(allocated.blockCke[0][0].startId, 0);
    EXPECT_EQ(allocated.blockCke[0][0].count, 8);
    ASSERT_EQ(allocated.cke[0].size(), 2);
    EXPECT_EQ(allocated.cke[0][0].startId, 100);
    EXPECT_EQ(allocated.cke[0][1].startId, 8);
    EXPECT_EQ(allocated.cke[0][1].resourceType, HCOMM_CCU_BATCH_RES_CKE);
}

TEST(CcuKernelResAllocatorTest, FragmentedBlockPoolFailsWithoutChangingOutputs)
{
    AscCcuResRepository available{};
    available.blockCke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 0, 3));
    available.blockCke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 10, 3));
    HcommCcuResRequestPod request{};
    request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][0] = 5;
    AscCcuResRepository remaining{};
    remaining.ms[0].push_back(Range(HCOMM_CCU_BATCH_RES_MS, 77, 1));
    AscCcuResRepository allocated{};
    allocated.gsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_GSA, 88, 2));

    EXPECT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_E_UNAVAIL);
    ASSERT_EQ(remaining.ms[0].size(), 1);
    EXPECT_EQ(remaining.ms[0][0].startId, 77);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].startId, 88);
}

TEST(CcuKernelResAllocatorTest, NormalRequestPreservesReservedGap)
{
    AscCcuResRepository available{};
    available.blockGsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 0, 4));
    available.blockGsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 8, 4));
    HcommCcuResRequestPod request{};
    request.count[HCOMM_CCU_BATCH_RES_GSA][0] = 6;

    AscCcuResRepository remaining{};
    AscCcuResRepository allocated{};
    ASSERT_EQ(PlanKernelResources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.gsa[0].size(), 2);
    EXPECT_EQ(allocated.gsa[0][0].startId, 0);
    EXPECT_EQ(allocated.gsa[0][0].count, 4);
    EXPECT_EQ(allocated.gsa[0][1].startId, 8);
    EXPECT_EQ(allocated.gsa[0][1].count, 2);
    ASSERT_EQ(remaining.blockGsa[0].size(), 1);
    EXPECT_EQ(remaining.blockGsa[0][0].startId, 10);
}

} // namespace
} // namespace hcomm
