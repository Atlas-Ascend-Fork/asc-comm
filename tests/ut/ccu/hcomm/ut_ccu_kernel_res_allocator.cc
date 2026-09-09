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

namespace asc {
namespace {

HcommCcuResRangePod Range(uint32_t type_, uint32_t startId, uint32_t count_) { return {type_, 0, startId, count_}; }

TEST(CcuKernelResAllocatorTest, NormalRequestFallsBackToBlockPool)
{
    asc_ccu_res_repository available{};
    available.block_cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 10, 48));
    available.block_gsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 100, 400));
    asc_ccu_res_request request{};
    request.count[HCOMM_CCU_BATCH_RES_CKE][0] = 1;
    request.count[HCOMM_CCU_BATCH_RES_GSA][0] = 8;

    asc_ccu_res_repository remaining{};
    asc_ccu_res_repository allocated{};
    ASSERT_EQ(plan_kernel_resources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.cke[0].size(), 1);
    EXPECT_EQ(allocated.cke[0][0].resourceType, HCOMM_CCU_BATCH_RES_CKE);
    EXPECT_EQ(allocated.cke[0][0].startId, 10);
    EXPECT_EQ(allocated.cke[0][0].count, 1);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].resourceType, HCOMM_CCU_BATCH_RES_GSA);
    EXPECT_EQ(allocated.gsa[0][0].startId, 100);
    EXPECT_EQ(allocated.gsa[0][0].count, 8);
    ASSERT_EQ(remaining.block_cke[0].size(), 1);
    EXPECT_EQ(remaining.block_cke[0][0].count, 47);
    ASSERT_EQ(remaining.block_gsa[0].size(), 1);
    EXPECT_EQ(remaining.block_gsa[0][0].count, 392);
}

TEST(CcuKernelResAllocatorTest, BlockRequestIsPlannedBeforeNormalRequest)
{
    asc_ccu_res_repository available{};
    available.cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_CKE, 100, 2));
    available.block_cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 0, 10));
    asc_ccu_res_request request{};
    request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][0] = 8;
    request.count[HCOMM_CCU_BATCH_RES_CKE][0] = 4;

    asc_ccu_res_repository remaining{};
    asc_ccu_res_repository allocated{};
    ASSERT_EQ(plan_kernel_resources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.block_cke[0].size(), 1);
    EXPECT_EQ(allocated.block_cke[0][0].startId, 0);
    EXPECT_EQ(allocated.block_cke[0][0].count, 8);
    ASSERT_EQ(allocated.cke[0].size(), 2);
    EXPECT_EQ(allocated.cke[0][0].startId, 100);
    EXPECT_EQ(allocated.cke[0][1].startId, 8);
    EXPECT_EQ(allocated.cke[0][1].resourceType, HCOMM_CCU_BATCH_RES_CKE);
}

TEST(CcuKernelResAllocatorTest, FragmentedBlockPoolFailsWithoutChangingOutputs)
{
    asc_ccu_res_repository available{};
    available.block_cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 0, 3));
    available.block_cke[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_CKE, 10, 3));
    asc_ccu_res_request request{};
    request.count[HCOMM_CCU_BATCH_RES_BLOCK_CKE][0] = 5;
    asc_ccu_res_repository remaining{};
    remaining.ms[0].push_back(Range(HCOMM_CCU_BATCH_RES_MS, 77, 1));
    asc_ccu_res_repository allocated{};
    allocated.gsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_GSA, 88, 2));

    EXPECT_EQ(plan_kernel_resources(available, request, remaining, allocated), CcuResult::CCU_E_UNAVAIL);
    ASSERT_EQ(remaining.ms[0].size(), 1);
    EXPECT_EQ(remaining.ms[0][0].startId, 77);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].startId, 88);
}

TEST(CcuKernelResAllocatorTest, NormalRequestPreservesReservedGap)
{
    asc_ccu_res_repository available{};
    available.block_gsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 0, 4));
    available.block_gsa[0].push_back(Range(HCOMM_CCU_BATCH_RES_BLOCK_GSA, 8, 4));
    asc_ccu_res_request request{};
    request.count[HCOMM_CCU_BATCH_RES_GSA][0] = 6;

    asc_ccu_res_repository remaining{};
    asc_ccu_res_repository allocated{};
    ASSERT_EQ(plan_kernel_resources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.gsa[0].size(), 2);
    EXPECT_EQ(allocated.gsa[0][0].startId, 0);
    EXPECT_EQ(allocated.gsa[0][0].count, 4);
    EXPECT_EQ(allocated.gsa[0][1].startId, 8);
    EXPECT_EQ(allocated.gsa[0][1].count, 2);
    ASSERT_EQ(remaining.block_gsa[0].size(), 1);
    EXPECT_EQ(remaining.block_gsa[0][0].startId, 10);
}

TEST(CcuKernelResAllocatorTest, InstructionRequestNeedsContiguousRange)
{
    asc_ccu_res_repository available{};
    available.instruction[0].push_back(Range(HCOMM_CCU_RES_INSTRUCTION, 0, 4));
    available.instruction[0].push_back(Range(HCOMM_CCU_RES_INSTRUCTION, 8, 8));
    asc_ccu_res_request request{};
    request.instruction[0] = 6;

    asc_ccu_res_repository remaining{};
    asc_ccu_res_repository allocated{};
    ASSERT_EQ(plan_kernel_resources(available, request, remaining, allocated), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(allocated.instruction[0].size(), 1);
    EXPECT_EQ(allocated.instruction[0][0].startId, 8);
    EXPECT_EQ(allocated.instruction[0][0].count, 6);
    ASSERT_EQ(remaining.instruction[0].size(), 2);
    EXPECT_EQ(remaining.instruction[0][1].startId, 14);
    EXPECT_EQ(remaining.instruction[0][1].count, 2);
}

} // namespace
} // namespace asc
