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

#include "hcomm/resource/instance/ccu_res_pack.h"

namespace hcomm {
namespace {

struct ContextStorage {
    HcommCcuRegisterContextPod context{};
    std::vector<HcommCcuResRangePod> instance{{HCOMM_CCU_BATCH_RES_LOOP, 0, 10, 4}};
    std::vector<HcommCcuResRangePod> translator{{HCOMM_CCU_BATCH_RES_XN, 0, 20, 3}};
    std::vector<HcommCcuResRangePod> instruction{{HCOMM_CCU_RES_INSTRUCTION, 0, 100, 5}};

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
        context.die[0].instructionCapacity = 5;
        RefreshViews();
    }

    void RefreshViews()
    {
        context.instanceResources = {instance.data(), static_cast<uint32_t>(instance.size()), 0};
        context.translatorResources = {translator.data(), static_cast<uint32_t>(translator.size()), 0};
        context.instructionResources = {instruction.data(), static_cast<uint32_t>(instruction.size()), 0};
    }
};

TEST(CcuResPackTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage;
    CcuResPack pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = 999;
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
    pack.GetCcuResRepo().loopEngine[0].clear();
    ASSERT_EQ(pack.Reset(), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0].size(), 1U);
    EXPECT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
}

TEST(CcuResPackTest, KeepsInstructionAccountingAcrossRegisterRounds)
{
    ContextStorage storage;
    CcuResPack pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);

    HcommCcuResRangePod first{};
    ASSERT_EQ(pack.AllocateInstruction(0, 2, first), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(first.startId, 100U);
    EXPECT_EQ(first.count, 2U);

    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
    HcommCcuResRangePod second{};
    ASSERT_EQ(pack.AllocateInstruction(0, 3, second), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(second.startId, 102U);
    EXPECT_EQ(second.count, 3U);
    EXPECT_EQ(pack.AllocateInstruction(0, 1, second), CcuResult::CCU_E_UNAVAIL);
}

TEST(CcuResPackTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.context.header.magicWord = 0;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].dieId = HCOMM_CCU_MAX_DIE_NUM;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.translator[0].resourceType = HCOMM_CCU_RES_INSTRUCTION;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
        storage.instance[0].count = 2;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 0, 12, 4});
        storage.RefreshViews();
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
}

TEST(CcuResPackTest, RejectsGenerationChange)
{
    ContextStorage storage;
    CcuResPack pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
    storage.context.generation = 2;
    EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
}

TEST(CcuResPackTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.context.reserved[0] = 1;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context.die[0].enabled = 2;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context.die[1].missionKey = 1;
        CcuResPack pack;
        EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    }
}

TEST(CcuResPackTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage;
    CcuResPack pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
    storage.instance[0].count = 2;
    EXPECT_EQ(pack.Load(storage.context), CcuResult::CCU_E_PARA);
    ASSERT_EQ(pack.GetCcuResRepo().loopEngine[0].size(), 1U);
    EXPECT_EQ(pack.GetCcuResRepo().loopEngine[0][0].startId, 10U);
    EXPECT_EQ(pack.GetGeneration(), 1U);
}

TEST(CcuResPackTest, SupportsIndependentInstructionPoolsOnTwoDies)
{
    ContextStorage storage;
    storage.context.validDieMask = 3;
    storage.context.die[1].enabled = 1;
    storage.context.die[1].instructionCapacity = 4;
    storage.instruction.push_back({HCOMM_CCU_RES_INSTRUCTION, 1, 200, 4});
    storage.RefreshViews();

    CcuResPack pack;
    ASSERT_EQ(pack.Load(storage.context), CcuResult::CCU_SUCCESS);
    HcommCcuResRangePod range{};
    ASSERT_EQ(pack.AllocateInstruction(1, 4, range), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(range.startId, 200U);
    EXPECT_EQ(pack.AllocateInstruction(1, 1, range), CcuResult::CCU_E_UNAVAIL);
    EXPECT_EQ(pack.AllocateInstruction(0, 1, range), CcuResult::CCU_SUCCESS);
    EXPECT_EQ(range.startId, 100U);
}

} // namespace
} // namespace hcomm
