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

// 把 range 存进 dieResources[die][type]，元数据存进 metadata 实体
struct ContextStorage {
    HcommCcuInstance instance_{};
    HcommCcuDieMetadata dieMetadata[HCOMM_CCU_MAX_DIE_NUM]{};
    std::vector<HcommCcuResRange> dieRanges[HCOMM_CCU_MAX_DIE_NUM][HCOMM_CCU_BATCH_RES_TYPE_COUNT]{};

    ContextStorage()
    {
        instance_.header = {HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_INSTANCE_MAGIC_WORD, sizeof(HcommCcuInstance), 0};
        instance_.ccuDieNum = HCOMM_CCU_MAX_DIE_NUM;
        instance_.ccuVersion = HCOMM_CCU_VERSION_V1;
        instance_.ascCustom[HCOMM_CCU_ASC_CUSTOM_CHANNEL_ENTITY] = 0x1;
        instance_.ascCustom[HCOMM_CCU_ASC_CUSTOM_ALLOC_INST_SPACE] = 0x1;
        instance_.ascCustom[HCOMM_CCU_ASC_CUSTOM_SUBMIT_INSTS] = 0x1;

        dieMetadata[0].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        dieMetadata[0].enabled = 1;
        dieMetadata[0].ccuResBuffer.type = REGED_BUFFER_RMA;
        dieMetadata[0].ccuResBuffer.bufferInfo.rma.addr = 0x1000;
        dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = 0x1000;
        dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP].push_back({10, 4, 0});
        RefreshViews();
    }

    void RefreshViews()
    {
        for (uint32_t die = 0; die < HCOMM_CCU_MAX_DIE_NUM; ++die) {
            HcommCcuDieRes& dieRes = instance_.ccuDieReses[die];
            dieRes.dieId = die;
            dieRes.resTypeNum = 0;
            for (uint32_t type = 0; type < HCOMM_CCU_BATCH_RES_TYPE_COUNT; ++type) {
                HcommCcuResRanges& view = dieRes.resTypes[type];
                view = HcommCcuResRanges{};
                view.resourceType = type;
                if (!dieRanges[die][type].empty()) {
                    view.resRanges = dieRanges[die][type].data();
                    view.resRangeNum = static_cast<uint32_t>(dieRanges[die][type].size());
                    dieRes.resTypeNum = type + 1;
                }
            }
            dieRes.dieMetadata = dieMetadata[die].enabled != 0 ? &dieMetadata[die] : nullptr;
        }
    }
};

TEST(AscCcuResSnapshotTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.instance_), CcuResult::CCU_SUCCESS);

    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = 999;
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
    pack.get_ccu_res_repo().loop_engine[0].clear();
    ASSERT_EQ(pack.reset(), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
}

TEST(AscCcuResSnapshotTest, ExposesDieMetadataSnapshot)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.instance_), CcuResult::CCU_SUCCESS);
    const HcommCcuDieMetadata* metadata = pack.get_die_metadata(0);
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(metadata->ccuResBuffer.bufferInfo.rma.addr, 0x1000U);
    EXPECT_EQ(pack.get_ccu_version(), HCOMM_CCU_VERSION_V1);
    EXPECT_EQ(pack.get_die_metadata(1), nullptr); // die 1 未启用
}

TEST(AscCcuResSnapshotTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.instance_.header.magicWord = 0;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = std::numeric_limits<uint32_t>::max();
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].count = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP].push_back({12, 4, 0}); // 与 [10,4) 重叠
        storage.RefreshViews();
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[0].dieId = 1; // die 槽位与 dieId 不一致
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].reserved = 1; // wire range 保留字段非零
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[2].reserved[0] = 1; // 超出 ccuDieNum 的预留 die 槽保留字段非零
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuVersion = HCOMM_CCU_VERSION_INVALID; // Instance 版本无效
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieMetadata[0].enabled = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        // die 1 未启用（validDieMask=1）却携带了元数据指针
        storage.dieMetadata[1].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        storage.dieMetadata[1].ccuResBuffer.type = REGED_BUFFER_RMA;
        // die 1 未启用（metadata.enabled=0）却携带元数据指针
        storage.instance_.ccuDieReses[1].dieMetadata = &storage.dieMetadata[1];
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage;
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.instance_), CcuResult::CCU_SUCCESS);

    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = std::numeric_limits<uint32_t>::max();
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].count = 2;
    EXPECT_EQ(pack.load(storage.instance_), CcuResult::CCU_E_PARA);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
}

TEST(AscCcuResSnapshotTest, SupportsTwoDies)
{
    ContextStorage storage;
    storage.dieMetadata[1].header = {
        HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
    storage.dieMetadata[1].enabled = 1;
    storage.dieMetadata[1].ccuResBuffer.type = REGED_BUFFER_RMA;
    storage.dieRanges[1][HCOMM_CCU_BATCH_RES_LOOP].push_back({20, 2, 0});
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.instance_), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[1].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[1][0].start_id, 20U);
}

} // namespace
} // namespace asc
