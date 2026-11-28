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
#include <algorithm>
#include <limits>
#include <memory>
#include <vector>

#include "acl/acl_rt.h"

#include "ccu_api/resource/common/asc_ccu_res_snapshot.h"

namespace asc {
namespace {

constexpr uint32_t UT_TRANSLATOR_SLOT_NUM = 16U;
constexpr uint32_t UT_TRANSLATOR_XN_PER_SLOT = 9U;

struct DirectApiCalls {
    static inline uint32_t instance_calls{0};
    static inline uint32_t destroy_calls{0};
    static inline bool fail_destroy{false};
    static inline std::vector<uint64_t> live_descs{};
    static inline bool low_resources{false};
    static inline bool fail_instance_create{false};
};

DirectApiCalls& ApiCalls()
{
    static DirectApiCalls calls;
    return calls;
}

struct DirectInstanceStorage {
    HcommCcuInstanceEntity entity{};
    HcommCcuDieMetadata metadata[HCOMM_CCU_MAX_DIE_NUM]{};
    std::vector<HcommCcuDieResRange> block_xn[HCOMM_CCU_MAX_DIE_NUM];
    std::vector<HcommCcuDieResRange> block_cke[HCOMM_CCU_MAX_DIE_NUM];
    std::vector<HcommCcuDieResRange> block_gsa[HCOMM_CCU_MAX_DIE_NUM];
};

int32_t& DeviceId()
{
    static int32_t device_id = 7;
    return device_id;
}

DirectInstanceStorage& DirectStorage()
{
    static DirectInstanceStorage storage;
    return storage;
}

void ResetDirectApi(DirectInstanceStorage& storage)
{
    auto& calls = ApiCalls();
    calls.instance_calls = 0;
    calls.destroy_calls = 0;
    calls.fail_destroy = false;
    calls.live_descs.clear();
    calls.low_resources = false;
    calls.fail_instance_create = false;

    storage = DirectInstanceStorage{};
    for (uint32_t die = 0; die < HCOMM_CCU_MAX_DIE_NUM; ++die) {
        storage.metadata[die].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        storage.entity.ccuDieReses[die].dieId = die;
        storage.entity.ccuDieReses[die].dieMetadata = &storage.metadata[die];
    }
    storage.entity.header = {
        HCOMM_CCU_INSTANCE_ENTITY_ABI_VERSION, HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD, sizeof(HcommCcuInstanceEntity), 0};
    storage.entity.ccuDieNum = HCOMM_CCU_MAX_DIE_NUM;
    storage.entity.ccuVersion = HCOMM_CCU_VERSION_V1;
    storage.entity.ascCustom[HCOMM_CCU_CONTROL_SLOT_CHANNEL_ENTITY] = 1;
    storage.entity.ascCustom[HCOMM_CCU_CONTROL_SLOT_ALLOC_INST_SPACE] = 1;
    storage.entity.ascCustom[HCOMM_CCU_CONTROL_SLOT_SUBMIT_INSTS] = 1;
}

extern "C" {
struct HcommCcuResDescStub {
    uint32_t die;
    uint32_t xn;
    uint32_t cke;
    uint32_t gsa;
};

ccu_result HcommCcuInsResDescCreate(uint32_t die, uint64_t* desc);
ccu_result HcommCcuInsResDescSetNum(uint64_t desc, int32_t type, uint32_t num);
ccu_result HcommCcuInsResDescDestroy(uint64_t desc);
ccu_result HcommCcuInsCreate(const uint64_t* descs, uint32_t count, uint64_t* handle);
ccu_result HcommCcuInsDestroy(uint64_t handle);
}

ccu_result HcommCcuInsResDescCreate(uint32_t die, uint64_t* desc)
{
    static std::vector<std::unique_ptr<HcommCcuResDescStub>> descriptors;
    descriptors.push_back(std::make_unique<HcommCcuResDescStub>(HcommCcuResDescStub{die, 0, 0, 0}));
    *desc = reinterpret_cast<uint64_t>(descriptors.back().get());
    ApiCalls().live_descs.push_back(*desc);
    return ccu_result::CCU_SUCCESS;
}

ccu_result HcommCcuInsResDescSetNum(uint64_t desc, int32_t type, uint32_t num)
{
    auto* stub = reinterpret_cast<HcommCcuResDescStub*>(desc);
    if (type == 2) {
        stub->xn = num;
    } else if (type == 4) {
        stub->cke = num;
    } else if (type == 3) {
        stub->gsa = num;
    } else {
        return ccu_result::CCU_E_PARA;
    }
    return ccu_result::CCU_SUCCESS;
}

ccu_result HcommCcuInsResDescDestroy(uint64_t desc)
{
    auto& live_descs = ApiCalls().live_descs;
    live_descs.erase(std::remove(live_descs.begin(), live_descs.end(), desc), live_descs.end());
    return ccu_result::CCU_SUCCESS;
}

ccu_result HcommCcuInsCreate(const uint64_t* descs, uint32_t count, uint64_t* handle)
{
    ++ApiCalls().instance_calls;
    if (ApiCalls().fail_instance_create) {
        return ccu_result::CCU_E_UNAVAIL;
    }
    if (count == 0 || count > HCOMM_CCU_MAX_DIE_NUM) {
        return ccu_result::CCU_E_UNAVAIL;
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (std::find(ApiCalls().live_descs.begin(), ApiCalls().live_descs.end(), descs[index]) ==
            ApiCalls().live_descs.end()) {
            return ccu_result::CCU_E_PTR;
        }
    }

    auto& storage = DirectStorage();
    uint32_t gsa_num = 0;
    for (uint32_t index = 0; index < count; ++index) {
        const auto& desc = *reinterpret_cast<const HcommCcuResDescStub*>(descs[index]);
        // block 分配会额外返回 8 个；短缺模式下刚好少 1 个。
        const uint32_t xn_num = desc.xn;
        storage.metadata[desc.die].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        storage.metadata[desc.die].enabled = 1;
        storage.block_xn[desc.die].push_back(
            {100 + desc.die * 1000, ApiCalls().low_resources ? xn_num - 1U : xn_num + 8U, 0});
        storage.block_cke[desc.die].push_back({200 + desc.die * 1000, desc.cke + 4, 0});
        if (desc.gsa != 0) {
            storage.block_gsa[desc.die].push_back({300 + desc.die * 1000, desc.gsa + 6, 0});
            gsa_num = desc.gsa;
        }
    }
    storage.entity.ccuVersion = gsa_num == 0 ? HCOMM_CCU_VERSION_V2 : HCOMM_CCU_VERSION_V1;
    for (uint32_t die = 0; die < HCOMM_CCU_MAX_DIE_NUM; ++die) {
        storage.metadata[die].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        auto& die_res = storage.entity.ccuDieReses[die];
        die_res = HcommCcuDieRes{};
        die_res.dieId = die;
        die_res.dieMetadata = &storage.metadata[die];
        if (!storage.block_cke[die].empty()) {
            auto& view = die_res.resTypes[die_res.resTypeNum++];
            view.resourceType = HCOMM_CCU_BATCH_RES_BLOCK_CKE;
            view.resRanges = storage.block_cke[die].data();
            view.resRangeNum = 1;
        }
        if (!storage.block_xn[die].empty()) {
            auto& view = die_res.resTypes[die_res.resTypeNum++];
            view.resourceType = HCOMM_CCU_BATCH_RES_BLOCK_XN;
            view.resRanges = storage.block_xn[die].data();
            view.resRangeNum = 1;
        }
        if (!storage.block_gsa[die].empty()) {
            auto& view = die_res.resTypes[die_res.resTypeNum++];
            view.resourceType = HCOMM_CCU_BATCH_RES_BLOCK_GSA;
            view.resRanges = storage.block_gsa[die].data();
            view.resRangeNum = 1;
        }
    }
    *handle = reinterpret_cast<uint64_t>(&storage.entity);
    storage.entity.ccuVersion = gsa_num == 0 ? HCOMM_CCU_VERSION_V2 : HCOMM_CCU_VERSION_V1;
    return ccu_result::CCU_SUCCESS;
}

ccu_result HcommCcuInsDestroy(uint64_t)
{
    ++ApiCalls().destroy_calls;
    return ApiCalls().fail_destroy ? ccu_result::CCU_E_INTERNAL : ccu_result::CCU_SUCCESS;
}

extern "C" aclError aclrtGetDevice(int32_t* deviceId)
{
    if (deviceId == nullptr) {
        return 1;
    }
    *deviceId = DeviceId();
    return ACL_SUCCESS;
}

ccu_result LoadSnapshot(asc_ccu_res_snapshot& snapshot, const HcommCcuInstanceEntity& instance)
{
    const ccu_result ret = snapshot.load(instance);
    if (ret != ccu_result::CCU_SUCCESS) {
        return ret;
    }
    return snapshot.load_data_plane_resources(DeviceId());
}

uint32_t CountRanges(const std::vector<asc_ccu_res_range>& ranges)
{
    uint32_t count = 0;
    for (const auto& range : ranges) {
        count += range.count;
    }
    return count;
}

// 把 range 存进 dieResources[die][type]，元数据存进 metadata 实体
struct ContextStorage {
    HcommCcuInstanceEntity instance_{};
    HcommCcuDieMetadata dieMetadata[HCOMM_CCU_MAX_DIE_NUM]{};
    std::vector<HcommCcuDieResRange> dieRanges[HCOMM_CCU_MAX_DIE_NUM][HCOMM_CCU_BATCH_RES_TYPE_COUNT]{};

    explicit ContextStorage(int32_t direct_device_id = 1)
    {
        DeviceId() = direct_device_id;
        instance_.header = {
            HCOMM_CCU_INSTANCE_ENTITY_ABI_VERSION, HCOMM_CCU_INSTANCE_ENTITY_MAGIC_WORD, sizeof(HcommCcuInstanceEntity),
            0};
        instance_.ccuDieNum = HCOMM_CCU_MAX_DIE_NUM;
        instance_.ccuVersion = HCOMM_CCU_VERSION_V1;
        instance_.ascCustom[HCOMM_CCU_CONTROL_SLOT_CHANNEL_ENTITY] = 0x1;
        instance_.ascCustom[HCOMM_CCU_CONTROL_SLOT_ALLOC_INST_SPACE] = 0x1;
        instance_.ascCustom[HCOMM_CCU_CONTROL_SLOT_SUBMIT_INSTS] = 0x1;
        ResetDirectApi(DirectStorage());

        dieMetadata[0].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        dieMetadata[0].enabled = 1;
        dieMetadata[0].ccuResBuffer.type = REGED_BUFFER_RMA;
        dieMetadata[0].ccuResBuffer.bufferInfo.rma.addr = 0x1000;
        dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = 0x1000;
        dieMetadata[1].header = {
            HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
        dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP].push_back({10, 4, 0});
        AddTranslatorRanges();
        RefreshViews();
    }

    void AddTranslatorRanges(uint32_t die = 0)
    {
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_XN].push_back({1000, 144, 0});
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_CKE].push_back({2000, 32, 0});
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_GSA].push_back({3000, 48, 0});
    }

    void ClearTranslatorRanges(uint32_t die = 0)
    {
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_XN].clear();
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_CKE].clear();
        dieRanges[die][HCOMM_CCU_BATCH_RES_BLOCK_GSA].clear();
    }

    void RefreshViews()
    {
        for (uint32_t die = 0; die < HCOMM_CCU_MAX_DIE_NUM; ++die) {
            HcommCcuDieRes& dieRes = instance_.ccuDieReses[die];
            dieRes.dieId = die;
            dieRes.resTypeNum = 0;
            std::for_each(std::begin(dieRes.resTypes), std::end(dieRes.resTypes), [](HcommCcuDieResRangePack& view) {
                view = HcommCcuDieResRangePack{};
            });
            for (uint32_t type = 0; type < HCOMM_CCU_BATCH_RES_TYPE_COUNT; ++type) {
                if (!dieRanges[die][type].empty()) {
                    HcommCcuDieResRangePack& view = dieRes.resTypes[dieRes.resTypeNum++];
                    view.resourceType = type;
                    view.resRanges = dieRanges[die][type].data();
                    view.resRangeNum = static_cast<uint32_t>(dieRanges[die][type].size());
                }
            }
            dieRes.dieMetadata = &dieMetadata[die];
        }
    }
};

TEST(AscCcuXnMapping, DerivesVersionSpecificAddressWithoutChangingRegisteredBuffer)
{
    for (uint32_t version : {HCOMM_CCU_VERSION_V1, HCOMM_CCU_VERSION_V2}) {
        ContextStorage storage(static_cast<int32_t>(version + 1));
        storage.instance_.ccuVersion = version;
        storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = 72ULL * 1024 * 1024;
        asc_ccu_res_snapshot snapshot;
        ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);
        uint64_t address = 0;
        ASSERT_EQ(snapshot.get_xn_base_addr(0, address), ccu_result::CCU_SUCCESS);
        const uint64_t offset = version == HCOMM_CCU_VERSION_V1 ? 0x908000ULL : 0x100000ULL;
        EXPECT_EQ(address, 0x1000ULL + offset);
        EXPECT_EQ(snapshot.get_die_metadata(0)->ccuResBuffer.bufferInfo.rma.addr, 0x1000ULL);
        // Assign/arithmetic use XN ids; Load/Store must map those same ids into
        // the XN subregion, rather than round-trip through the resource prefix.
        EXPECT_EQ(address + 8 * 17, 0x1000ULL + offset + 8 * 17);
        EXPECT_NE(address + 8 * 17, 0x1000ULL + 8 * 17);
        EXPECT_EQ(snapshot.get_xn_base_addr(1, address), ccu_result::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, ParsesCompactResourceTypeList)
{
    ContextStorage storage(13);
    storage.ClearTranslatorRanges();
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP].clear();
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_MISSION].push_back({8, 2, 0});
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(storage.instance_.ccuDieReses[0].resTypeNum, 1U);
    EXPECT_EQ(storage.instance_.ccuDieReses[0].resTypes[0].resourceType, HCOMM_CCU_BATCH_RES_MISSION);
    ASSERT_EQ(pack.get_ccu_res_repo().mission[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().mission[0][0].start_id, 8U);
    EXPECT_EQ(pack.get_ccu_res_repo().mission[0][0].count, 2U);
}

TEST(AscCcuResSnapshotTest, RejectsNonAscendingAndNonZeroTailInCompactResourceTypeList)
{
    {
        ContextStorage storage;
        storage.ClearTranslatorRanges();
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_XN].push_back({20, 2, 0});
        storage.RefreshViews();
        storage.instance_.ccuDieReses[0].resTypes[0].resourceType = HCOMM_CCU_BATCH_RES_MISSION;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.ClearTranslatorRanges();
        storage.instance_.ccuDieReses[0].resTypes[1].resourceType = 1;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, SeparatesTranslatorResourcesFromInstancePool)
{
    ContextStorage storage(14);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_TRUE(pack.get_ccu_res_repo().xn[0].empty());
    ASSERT_EQ(pack.get_translator_res_repo().xn[0].size(), 1U);
    EXPECT_EQ(pack.get_translator_res_repo().xn[0][0].resource_type, HCOMM_CCU_BATCH_RES_XN);
    EXPECT_EQ(pack.get_translator_res_repo().xn[0][0].start_id, 100U);
    EXPECT_EQ(pack.get_translator_res_repo().xn[0][0].count, UT_TRANSLATOR_SLOT_NUM * UT_TRANSLATOR_XN_PER_SLOT);
    ASSERT_EQ(pack.get_translator_res_repo().cke[0].size(), 1U);
    EXPECT_EQ(pack.get_translator_res_repo().cke[0][0].start_id, 200U);
    ASSERT_EQ(pack.get_translator_res_repo().gsa[0].size(), 1U);
    EXPECT_EQ(pack.get_translator_res_repo().gsa[0][0].start_id, 300U);
}

TEST(AscCcuResSnapshotTest, ResetKeepsTranslatorResourcesReadonly)
{
    ContextStorage storage(15);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(pack.reset(), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(pack.get_translator_res_repo().xn[0].size(), 1U);
}

TEST(AscCcuResSnapshotTest, FallsBackToUserPoolWhenDirectInstanceCreateFails)
{
    ContextStorage storage(25);
    ApiCalls().fail_instance_create = true;

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(CountRanges(pack.get_translator_res_repo().xn[0]), 144U);
    EXPECT_EQ(CountRanges(pack.get_translator_res_repo().cke[0]), 32U);
    EXPECT_EQ(CountRanges(pack.get_translator_res_repo().gsa[0]), 48U);
    EXPECT_TRUE(pack.get_ccu_res_repo().block_xn[0].empty());
    EXPECT_TRUE(pack.get_ccu_res_repo().block_cke[0].empty());
    EXPECT_TRUE(pack.get_ccu_res_repo().block_gsa[0].empty());
}

TEST(AscCcuResSnapshotTest, ResetKeepsUserPoolFallbackResourcesReserved)
{
    ContextStorage storage(26);
    ApiCalls().fail_instance_create = true;

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    pack.get_ccu_res_repo().block_xn[0].push_back({1200, 8, 0});
    ASSERT_EQ(pack.reset(), ccu_result::CCU_SUCCESS);
    EXPECT_TRUE(pack.get_ccu_res_repo().block_xn[0].empty());
    EXPECT_EQ(CountRanges(pack.get_translator_res_repo().xn[0]), 144U);
}

TEST(AscCcuResSnapshotTest, RejectsWhenBothDirectInstanceAndUserPoolAreUnavailable)
{
    ContextStorage storage(27);
    ApiCalls().fail_instance_create = true;
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_BLOCK_XN][0].count = 143U;
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_UNAVAIL);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);
}

TEST(AscCcuResSnapshotTest, ParsesPodBeforeLoadingTranslatorResources)
{
    ContextStorage storage(16);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(pack.load(storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(pack.load_data_plane_resources(DeviceId()), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);

    asc::asc_ccu_res_snapshot reused;
    ASSERT_EQ(LoadSnapshot(reused, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);
    ASSERT_EQ(reused.get_translator_res_repo().xn[0].size(), 1U);
    EXPECT_EQ(reused.get_translator_res_repo().xn[0][0].count, 144U);
}

TEST(AscCcuResSnapshotTest, KeepsDirectInstanceAfterSnapshotRelease)
{
    ContextStorage storage(17);
    {
        asc::asc_ccu_res_snapshot pack;
        ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
        EXPECT_EQ(ApiCalls().destroy_calls, 0U);
    }
    EXPECT_EQ(ApiCalls().destroy_calls, 0U);

    asc::asc_ccu_res_snapshot reused;
    EXPECT_EQ(reused.load(storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(reused.load_data_plane_resources(DeviceId()), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);
}

TEST(AscCcuResSnapshotTest, KeepsDirectInstanceForRetryWhenDestroyFails)
{
    ContextStorage storage(18);
    ApiCalls().low_resources = true;
    ApiCalls().fail_destroy = true;
    storage.ClearTranslatorRanges();
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot failed;
    ASSERT_EQ(failed.load(storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(failed.load_data_plane_resources(DeviceId()), ccu_result::CCU_E_UNAVAIL);
    EXPECT_EQ(ApiCalls().destroy_calls, 1U);

    ApiCalls().fail_destroy = false;
    ApiCalls().low_resources = false;
    // 第一次装载返回 143 个 XN，正常返回 152 个。保留 DirectStorage，使
    // 销毁失败的 handle 继续指向可解析实体，模拟 hcomm 侧未完成销毁。
    DirectStorage().block_xn[0][0].count = 152U;

    asc::asc_ccu_res_snapshot retry;
    ASSERT_EQ(LoadSnapshot(retry, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);
}

TEST(AscCcuResSnapshotTest, RejectsPhysicalOverlapBetweenTranslatorAndOtherXn)
{
    ContextStorage storage(19);
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_XN].push_back({120, 8, 0});
    storage.RefreshViews();
    asc::asc_ccu_res_snapshot pack;
    EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, AcceptsV2OnlyWhenTranslatorGsaIsEmpty)
{
    ContextStorage storage(6);
    storage.instance_.ccuVersion = HCOMM_CCU_VERSION_V2;
    storage.RefreshViews();
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_TRUE(pack.get_translator_res_repo().gsa[0].empty());
}

TEST(AscCcuResSnapshotTest, RejectsDataPlaneInstanceVersionChangeOnSameDevice)
{
    ContextStorage storage(5);
    storage.instance_.ccuVersion = HCOMM_CCU_VERSION_V1;
    asc::asc_ccu_res_snapshot v1;
    ASSERT_EQ(LoadSnapshot(v1, storage.instance_), ccu_result::CCU_SUCCESS);

    storage.instance_.ccuVersion = HCOMM_CCU_VERSION_V2;
    asc::asc_ccu_res_snapshot v2;
    EXPECT_EQ(LoadSnapshot(v2, storage.instance_), ccu_result::CCU_E_PARA);
}

TEST(AscCcuXnMapping, RejectsShortAndOverflowingResourceMappings)
{
    ContextStorage storage(10);
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = sizeof(uint64_t) - 1;
    asc_ccu_res_snapshot snapshot;
    ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);
    uint64_t address = 123;
    EXPECT_EQ(snapshot.get_xn_base_addr(0, address), ccu_result::CCU_E_PARA);
    EXPECT_EQ(address, 123ULL);
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = 72ULL * 1024 * 1024;
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.addr = std::numeric_limits<uint64_t>::max() - 7;
    ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(snapshot.get_xn_base_addr(0, address), ccu_result::CCU_E_PARA);
    EXPECT_EQ(address, 123ULL);
}

TEST(AscCcuXnMapping, AcceptsLegacyV1CapacitySmallerThanXnOffset)
{
    ContextStorage storage(11);
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.addr = 0xf98000000000ULL;
    // 蓝区 V1 9.3 真机 metadata；原版 HComm 的 XN[4] Load 地址为 0xf98000908020。
    // 不应把 1.5 MiB 容量错当成覆盖 XN 的地址窗口长度。
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.size = 0x180000;
    asc_ccu_res_snapshot snapshot;
    ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);
    uint64_t address = 0;
    ASSERT_EQ(snapshot.get_xn_base_addr(0, address), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(address + 4 * sizeof(uint64_t), 0xf98000908020ULL);
    EXPECT_EQ(snapshot.get_die_metadata(0)->ccuResBuffer.bufferInfo.rma.size, 0x180000U);

    // 未启用 die、零地址和溢出仍然必须拒绝，修复不是无条件放行。
    EXPECT_EQ(snapshot.get_xn_base_addr(1, address), ccu_result::CCU_E_PARA);
    storage.dieMetadata[0].ccuResBuffer.bufferInfo.rma.addr = 0;
    ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(snapshot.get_xn_base_addr(0, address), ccu_result::CCU_E_PARA);
}

TEST(AscCcuResSnapshotTest, DeepCopiesAndResetsOrdinaryResources)
{
    ContextStorage storage(20);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);

    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = 999;
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
    pack.get_ccu_res_repo().loop_engine[0].clear();
    ASSERT_EQ(pack.reset(), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
    ASSERT_EQ(pack.get_translator_res_repo().xn[0].size(), 1U);
    EXPECT_EQ(pack.get_translator_res_repo().xn[0][0].start_id, 100U);
}

TEST(AscCcuResSnapshotTest, ExposesDieMetadataSnapshot)
{
    ContextStorage storage(21);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    const HcommCcuDieMetadata* metadata = pack.get_die_metadata(0);
    ASSERT_NE(metadata, nullptr);
    EXPECT_EQ(metadata->ccuResBuffer.bufferInfo.rma.addr, 0x1000U);
    EXPECT_EQ(pack.get_ccu_version(), HCOMM_CCU_VERSION_V1);
    const HcommCcuDieMetadata* disabledMetadata = pack.get_die_metadata(1);
    ASSERT_NE(disabledMetadata, nullptr); // die 1 未启用，但仍携带占位元数据
    EXPECT_EQ(disabledMetadata->enabled, 0U);
}

TEST(AscCcuXnMapping, CollectsEnabledDieAddressesOnly)
{
    ContextStorage storage(12);
    asc_ccu_res_snapshot snapshot;
    ASSERT_EQ(LoadSnapshot(snapshot, storage.instance_), ccu_result::CCU_SUCCESS);

    uint64_t addresses[HCOMM_CCU_MAX_DIE_NUM]{};
    ASSERT_EQ(snapshot.get_xn_base_addrs(addresses), ccu_result::CCU_SUCCESS);
    EXPECT_EQ(addresses[0], 0x1000ULL + 0x908000ULL);
    EXPECT_EQ(addresses[1], 0ULL); // 单 die 环境：禁用槽位保持占位 0，不触发校验失败。
}

TEST(AscCcuResSnapshotTest, RejectsInvalidAbiDieTypeOverflowAndOverlap)
{
    {
        ContextStorage storage;
        storage.instance_.header.magicWord = 0;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = std::numeric_limits<uint32_t>::max();
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].count = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP].push_back({12, 4, 0}); // 与 [10,4) 重叠
        storage.RefreshViews();
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[0].dieId = 1; // die 槽位与 dieId 不一致
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].reserved = 1; // wire range 保留字段非零
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, RejectsReservedAndInvalidDisabledDieMetadata)
{
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[2].reserved[0] = 1; // 超出 ccuDieNum 的预留 die 槽保留字段非零
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuVersion = HCOMM_CCU_VERSION_INVALID; // Instance 版本无效
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieMetadata[0].enabled = 2;
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[1].dieMetadata = nullptr; // die 槽位缺少元数据
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.dieMetadata[1].missionKey = 1; // 禁用 die 的业务字段必须保持占位值
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
    {
        ContextStorage storage;
        storage.instance_.ccuDieReses[1].resTypes[0].resourceType = 1; // 禁用 die 不得携带 range 视图
        asc::asc_ccu_res_snapshot pack;
        EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    }
}

TEST(AscCcuResSnapshotTest, FailedLoadDoesNotReplaceActiveSnapshot)
{
    ContextStorage storage(22);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);

    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].startId = std::numeric_limits<uint32_t>::max();
    storage.dieRanges[0][HCOMM_CCU_BATCH_RES_LOOP][0].count = 2;
    EXPECT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_PARA);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[0].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[0][0].start_id, 10U);
}

TEST(AscCcuResSnapshotTest, SupportsTwoDies)
{
    ContextStorage storage(23);
    storage.dieMetadata[1].header = {
        HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
    storage.dieMetadata[1].enabled = 1;
    storage.dieMetadata[1].ccuResBuffer.type = REGED_BUFFER_RMA;
    storage.dieRanges[1][HCOMM_CCU_BATCH_RES_LOOP].push_back({20, 2, 0});
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(pack.get_ccu_res_repo().loop_engine[1].size(), 1U);
    EXPECT_EQ(pack.get_ccu_res_repo().loop_engine[1][0].start_id, 20U);
}

TEST(AscCcuResSnapshotTest, DirectInstanceSupportsTwoEnabledDies)
{
    ContextStorage storage(24);
    storage.dieMetadata[1].header = {
        HCOMM_CCU_RES_ABI_VERSION, HCOMM_CCU_DIE_METADATA_MAGIC_WORD, sizeof(HcommCcuDieMetadata), 0};
    storage.dieMetadata[1].enabled = 1;
    storage.dieRanges[1][HCOMM_CCU_BATCH_RES_LOOP].push_back({20, 2, 0});
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().xn[0]), 144U);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().xn[1]), 144U);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().cke[0]), 32U);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().cke[1]), 32U);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().gsa[0]), 48U);
    ASSERT_EQ(CountRanges(pack.get_translator_res_repo().gsa[1]), 48U);
}

TEST(AscCcuResSnapshotTest, RejectsMissingTranslatorXn)
{
    ContextStorage storage(3);
    ApiCalls().low_resources = true;
    storage.ClearTranslatorRanges();
    storage.RefreshViews();

    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_E_UNAVAIL);
    EXPECT_EQ(ApiCalls().instance_calls, 1U);
}

TEST(AscCcuResSnapshotTest, RequiresExactTranslatorResourceCount)
{
    ContextStorage storage(4);
    asc::asc_ccu_res_snapshot pack;
    ASSERT_EQ(LoadSnapshot(pack, storage.instance_), ccu_result::CCU_SUCCESS);
    ASSERT_EQ(pack.get_translator_res_repo().xn[0].size(), 1U);
    EXPECT_EQ(pack.get_translator_res_repo().xn[0][0].count, 144U);
    EXPECT_EQ(pack.get_translator_res_repo().cke[0][0].count, 32U);
    EXPECT_EQ(pack.get_translator_res_repo().gsa[0][0].count, 48U);
}

} // namespace
} // namespace asc
