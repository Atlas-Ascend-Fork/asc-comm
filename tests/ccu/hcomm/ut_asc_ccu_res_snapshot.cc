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
    HcommCcuRegisterContextPod context{};
    std::vector<HcommCcuResRangePod> instance{{HCOMM_CCU_BATCH_RES_LOOP, 0, 10, 4}};

    ContextStorage()
    {
        context.header = {HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD,
            sizeof(HcommCcuRegisterContextPod), 0};
        context.generation = 1;
        context.deviceLogicId = 0;
        context.ccuVersion = 0;
        context.dieNum = HCOMM_CCU_MAX_DIE_NUM;
        context.validDieMask = 1;
        context.die[0].enabled = 1;
        RefreshViews();
    }

    void RefreshViews()
    {
        context.instanceResources = {instance.data(), static_cast<uint32_t>(instance.size()), 0};
    }
};

TEST(AscCcuResSnapshotTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage;
    AscCcuResSnapshot pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = 999;
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
    pack.GetCcuResRepo().loopEngine[0].clear();
    ASSERT_EQ(pack.Reset(), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0].size(), 1U);
    EXPECT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
}

TEST(AscCcuResSnapshotTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.context.header.magicWord = 0;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].dieId = HCOMM_CCU_MAX_DIE_NUM;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].resourceType = HCOMM_CCU_RES_INSTRUCTION;
        AscCcuResSnapshot pack;
        ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(pack.GetCcuResRepo().instruction[0].size(), 1U);
        EXPECT_EQ(pack.GetCcuResRepo().instruction[0][0].startId, 10U);
    }
    {
        ContextStorage storage;
        storage.instance[0].resourceType = HCOMM_CCU_RES_INSTRUCTION + 1;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
        storage.instance[0].count = 2;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 0, 12, 4});
        storage.RefreshViews();
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, RejectsGenerationChange)
{
    ContextStorage storage;
    AscCcuResSnapshot pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
    storage.context.generation = 2;
    EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.context.reserved[0] = 1;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context.die[0].enabled = 2;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context.die[1].missionKey = 1;
        AscCcuResSnapshot pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage;
    AscCcuResSnapshot pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
    storage.instance[0].count = 2;
    EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0].size(), 1U);
    EXPECT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
    EXPECT_EQ(pack.GetGeneration(), 1U);
}

TEST(AscCcuResSnapshotTest, SupportsTwoDies)
{
    ContextStorage storage;
    storage.context.validDieMask = 3;
    storage.context.die[1].enabled = 1;
    storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 1, 20, 2});
    storage.RefreshViews();

    AscCcuResSnapshot pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[1].size(), 1U);
    EXPECT_EQ(pack.GetCcuResRepo().loopEngine[1][0].startId, 20U);
}

} // namespace
} // namespace hcomm
