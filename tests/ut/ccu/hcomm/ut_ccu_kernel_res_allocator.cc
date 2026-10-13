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
using asc::asc_ccu_res_range;
using asc::asc_ccu_res_repository;
using asc::asc_ccu_res_request;
using asc::plan_kernel_resources;

asc_ccu_res_range Range(uint32_t resource_type, uint32_t start_id, uint32_t count)
{
    return {resource_type, 0, start_id, count};
}

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
    EXPECT_EQ(allocated.cke[0][0].resource_type, HCOMM_CCU_BATCH_RES_CKE);
    EXPECT_EQ(allocated.cke[0][0].start_id, 10);
    EXPECT_EQ(allocated.cke[0][0].count, 1);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].resource_type, HCOMM_CCU_BATCH_RES_GSA);
    EXPECT_EQ(allocated.gsa[0][0].start_id, 100);
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
    EXPECT_EQ(allocated.block_cke[0][0].start_id, 0);
    EXPECT_EQ(allocated.block_cke[0][0].count, 8);
    ASSERT_EQ(allocated.cke[0].size(), 2);
    EXPECT_EQ(allocated.cke[0][0].start_id, 100);
    EXPECT_EQ(allocated.cke[0][1].start_id, 8);
    EXPECT_EQ(allocated.cke[0][1].resource_type, HCOMM_CCU_BATCH_RES_CKE);
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
    EXPECT_EQ(remaining.ms[0][0].start_id, 77);
    ASSERT_EQ(allocated.gsa[0].size(), 1);
    EXPECT_EQ(allocated.gsa[0][0].start_id, 88);
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
    EXPECT_EQ(allocated.gsa[0][0].start_id, 0);
    EXPECT_EQ(allocated.gsa[0][0].count, 4);
    EXPECT_EQ(allocated.gsa[0][1].start_id, 8);
    EXPECT_EQ(allocated.gsa[0][1].count, 2);
    ASSERT_EQ(remaining.block_gsa[0].size(), 1);
    EXPECT_EQ(remaining.block_gsa[0][0].start_id, 10);
}

// INS 不再走 range 池：指令空间由 ascCustom.allocInstSpace 向 hcomm 申请（见 ccu_kernel_mgr），
// allocator 不再切分 instruction 池，原连续区间用例已随该机制移除。

} // namespace
} // namespace asc
