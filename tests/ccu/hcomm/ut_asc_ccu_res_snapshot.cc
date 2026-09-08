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

namespace asc {
namespace {

struct ContextStorage {
    HcommCcuRegisterContextPod context_{};
    std::vector<HcommCcuResRangePod> instance{{HCOMM_CCU_BATCH_RES_LOOP, 0, 10, 4}};

    ContextStorage()
    {
        context_.header = {HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD,
            sizeof(HcommCcuRegisterContextPod), 0};
        context_.generation = 1;
        context_.deviceLogicId = 0;
        context_.ccuVersion = 0;
        context_.dieNum = HCOMM_CCU_MAX_DIE_NUM;
        context_.validDieMask = 1;
        context_.die[0].enabled = 1;
        context_.controlOps.header = {HCOMM_CCU_CONTROL_ABI_VERSION, HCOMM_CCU_CONTROL_OPS_MAGIC_WORD,
            sizeof(HcommCcuControlOpsPod), 0};
        context_.controlOps.channelQuery = +[](ChannelHandle, HcommCcuChannelPod *) -> int32_t { return 0; };
        context_.controlOps.loadInstruction =
            +[](const HcommCcuInstructionLoadPod *) -> int32_t { return 0; };
        context_.controlOps.missionContextQuery =
            +[](int32_t, uint32_t, uint32_t, HcommCcuMissionContextPod *) -> int32_t { return 0; };
        context_.controlOps.loopContextQuery =
            +[](int32_t, uint32_t, uint32_t, HcommCcuLoopContextPod *) -> int32_t { return 0; };
        context_.controlOps.ckeQuery = +[](int32_t, uint32_t, uint32_t, uint64_t *) -> int32_t { return 0; };
        context_.controlOps.xnQuery = +[](int32_t, uint32_t, uint32_t, uint64_t *) -> int32_t { return 0; };
        context_.controlOps.gsaQuery = +[](int32_t, uint32_t, uint32_t, uint64_t *) -> int32_t { return 0; };
        RefreshViews();
    }

    void RefreshViews()
    {
        context_.instanceResources = {instance.data(), static_cast<uint32_t>(instance.size()), 0};
    }
};

TEST(AscCcuResSnapshotTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = 999;
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].startId, 10U);
    pack.get_ccu_res_repo().loop_engine[0].clear();
    ASSERT_EQ(pack.reset(), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].startId, 10U);
}

TEST(AscCcuResSnapshotTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.context_.header.magicWord = 0;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].dieId = HCOMM_CCU_MAX_DIE_NUM;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].resourceType = HCOMM_CCU_RES_INSTRUCTION;
        asc::asc_ccu_res_snapshot pack;
        ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(pack.get_ccu_res_repo().instruction[0].size(), 1U);
        EXPECT_EQ(pack.get_ccu_res_repo().instruction[0][0].startId, 10U);
    }
    {
        ContextStorage storage;
        storage.instance[0].resourceType = HCOMM_CCU_RES_INSTRUCTION + 1;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
        storage.instance[0].count = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 0, 12, 4});
        storage.RefreshViews();
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, RejectsGenerationChange)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
    storage.context_.generation = 2;
    EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.context_.reserved[0] = 1;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context_.die[0].enabled = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.context_.die[1].missionKey = 1;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);

    storage.instance[0].startId = std::numeric_limits<uint32_t>::max();
    storage.instance[0].count = 2;
    EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].startId, 10U);
    EXPECT_EQ(pack.get_generation(), 1U);
}

TEST(AscCcuResSnapshotTest, SupportsTwoDies)
{
    ContextStorage storage;
    storage.context_.validDieMask = 3;
    storage.context_.die[1].enabled = 1;
    storage.instance.push_back({HCOMM_CCU_BATCH_RES_LOOP, 1, 20, 2});
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[1].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[1][0].startId, 20U);
}

} // namespace
} // namespace asc
