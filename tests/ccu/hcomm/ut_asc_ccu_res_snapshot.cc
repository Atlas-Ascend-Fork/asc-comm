/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "hcomm/resource/common/asc_ccu_res_snapshot.h"

namespace hcomm {
namespace {

struct ContextStorage {
    HcommCcuRegisterContextPod context_{};
    std::vector<HcommCcuResRangePod> instance{{HCOMM_CCU_BATCH_RES_LOOP, 0, 10, 4}};

    ContextStorage()
    {
        context_.header = {HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD,
            sizeof(HcommCcuRegisterContextPod), 0};
        context_.generation = 1;
        context_.device_logic_id = 0;
        context_.ccu_version = 0;
        context_.die_num = HCOMM_CCU_MAX_DIE_NUM;
        context_.valid_die_mask = 1;
        context_.die[0].enabled = 1;
        RefreshViews();
    }

    void RefreshViews()
    {
        context_.instance_resources = {instance.data(), static_cast<uint32_t>(instance.size()), 0};
    }
};

TEST(AscCcuResSnapshotTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage;
    asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);

    storage.instance[0].start_id = 999;
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
    pack.get_ccu_res_repo().loop_engine[0].clear();
    ASSERT_EQ(pack.reset(), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
}

TEST(AscCcuResSnapshotTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.context_.header.magic_word = 0;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].die_id = HCOMM_CCU_MAX_DIE_NUM;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].resource_type = HCOMM_CCU_RES_INSTRUCTION;
        asc_ccu_res_snapshot pack;
        ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(pack.get_ccu_res_repo().instruction[0].size(), 1U);
        EXPECT_EQ(pack.get_ccu_res_repo().instruction[0][0].start_id, 10U);
    }
    {
        ContextStorage storage;
        storage.instance[0].resource_type = HCOMM_CCU_RES_INSTRUCTION + 1;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].start_id = std::numeric_limits<uint32_t>::max();
        storage.instance[0].count_ = 2;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 0, 12, 4});
        storage.RefreshViews();
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, RejectsGenerationChange)
{
    ContextStorage storage;
    asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
    storage.context_.generation = 2;
    EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.context_.reserved[0] = 1;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context_.die[0].enabled = 2;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context_.die[1].mission_key = 1;
        asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage;
    asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);

    storage.instance[0].start_id = std::numeric_limits<uint32_t>::max();
    storage.instance[0].count_ = 2;
    EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
    EXPECT_EQ(pack.get_generation(), 1U);
}

TEST(AscCcuResSnapshotTest, SupportsTwoDies)
{
    ContextStorage storage;
    storage.context_.valid_die_mask = 3;
    storage.context_.die[1].enabled = 1;
    storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 1, 20, 2});
    storage.RefreshViews();

    asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[1].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[1][0].start_id, 20U);
}

} // namespace
} // namespace hcomm
