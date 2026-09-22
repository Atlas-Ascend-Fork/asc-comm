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
        context_.header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_REGISTER_CONTEXT_MAGIC_WORD, sizeof(HcommCcuRegisterContextPod), 0};
        context_.generation = 1;
        context_.deviceLogicId = 0;
        context_.ccuVersion = 0;
        context_.dieNum = HCOMM_CCU_MAX_DIE_NUM;
        context_.validDieMask = 1;
        context_.die[0].enabled = 1;
        context_.controlOps.header = {
            HCOMM_CCU_CONTROL_ABI_VERSION, HCOMM_CCU_CONTROL_OPS_MAGIC_WORD, sizeof(HcommCcuControlOpsPod), 0};
        context_.controlOps.channelQuery = +[](ChannelHandle, HcommCcuChannelPod*) -> int32_t { return 0; };
        context_.controlOps.loadInstruction = +[](const HcommCcuInstructionLoadPod*) -> int32_t { return 0; };
        context_.controlOps.missionContextQuery =
            +[](int32_t, uint32_t, uint32_t, HcommCcuMissionContextPod*) -> int32_t { return 0; };
        context_.controlOps.loopContextQuery =
            +[](int32_t, uint32_t, uint32_t, HcommCcuLoopContextPod*) -> int32_t { return 0; };
        context_.controlOps.ckeQuery = +[](int32_t, uint32_t, uint32_t, uint64_t*) -> int32_t { return 0; };
        context_.controlOps.xnQuery = +[](int32_t, uint32_t, uint32_t, uint64_t*) -> int32_t { return 0; };
        context_.controlOps.gsaQuery = +[](int32_t, uint32_t, uint32_t, uint64_t*) -> int32_t { return 0; };
        RefreshViews();
    }

    void RefreshViews() { context_.instanceResources = {instance.data(), static_cast<uint32_t>(instance.size()), 0}; }
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

TEST(AscCcuResSnapshotTest, LoadsAllRepositoryResourceTypes)
{
    ContextStorage storage;
    // 覆盖 get_ranges 的全部资源类型分支
    storage.instance = {
        {HCOMM_CCU_BATCH_RES_LOOP, 0, 10, 4},    {HCOMM_CCU_BATCH_RES_BLOCK_LOOP, 0, 11, 4},
        {HCOMM_CCU_BATCH_RES_MS, 0, 12, 4},      {HCOMM_CCU_BATCH_RES_BLOCK_MS, 0, 13, 4},
        {HCOMM_CCU_BATCH_RES_CKE, 0, 14, 4},     {HCOMM_CCU_BATCH_RES_BLOCK_CKE, 0, 15, 4},
        {HCOMM_CCU_BATCH_RES_XN, 0, 16, 4},      {HCOMM_CCU_BATCH_RES_BLOCK_XN, 0, 17, 4},
        {HCOMM_CCU_BATCH_RES_GSA, 0, 18, 4},     {HCOMM_CCU_BATCH_RES_BLOCK_GSA, 0, 19, 4},
        {HCOMM_CCU_BATCH_RES_MISSION, 0, 20, 4}, {HCOMM_CCU_RES_INSTRUCTION, 0, 21, 4},
    };
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);
    const asc_ccu_res_repository& repo = pack.get_ccu_res_repo();
    EXPECT_EQ(repo.loop_engine[0].size(), 1U);
    EXPECT_EQ(repo.block_loop_engine[0].size(), 1U);
    EXPECT_EQ(repo.ms[0].size(), 1U);
    EXPECT_EQ(repo.block_ms[0].size(), 1U);
    EXPECT_EQ(repo.cke[0].size(), 1U);
    EXPECT_EQ(repo.block_cke[0].size(), 1U);
    EXPECT_EQ(repo.xn[0].size(), 1U);
    EXPECT_EQ(repo.block_xn[0].size(), 1U);
    EXPECT_EQ(repo.gsa[0].size(), 1U);
    EXPECT_EQ(repo.block_gsa[0].size(), 1U);
    EXPECT_EQ(repo.mission[0].size(), 1U);
    EXPECT_EQ(repo.instruction[0].size(), 1U);
}

TEST(AscCcuResSnapshotTest, RejectsUnknownResourceType)
{
    ContextStorage storage;
    storage.instance = {{0xFFFF, 0, 10, 4}}; // 未定义的资源类型
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    EXPECT_EQ(pack.load(storage.context_), CcuResult::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, ResetWithoutLoadReturnsUnavail)
{
    asc::asc_ccu_res_snapshot pack;
    EXPECT_EQ(pack.reset(), CcuResult::CCU_E_UNAVAIL);
}

TEST(AscCcuResSnapshotTest, GettersExposeSnapshotFields)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.context_), CcuResult::CCU_SUCCESS);

    EXPECT_EQ(pack.get_generation(), storage.context_.generation);
    EXPECT_EQ(pack.get_device_logic_id(), storage.context_.deviceLogicId);
    EXPECT_EQ(pack.get_ccu_version(), storage.context_.ccuVersion);
    EXPECT_EQ(pack.get_valid_die_mask(), storage.context_.validDieMask);
    EXPECT_NE(pack.get_die_metadata(0), nullptr);
    EXPECT_EQ(pack.get_die_metadata(1), nullptr);                     // validDieMask=1, die1 无效
    EXPECT_EQ(pack.get_die_metadata(HCOMM_CCU_MAX_DIE_NUM), nullptr); // 越界
    // control_ops 快照与原 POD 一致
    EXPECT_EQ(pack.get_control_ops().header.magicWord, storage.context_.controlOps.header.magicWord);
}

} // namespace
} // namespace asc
