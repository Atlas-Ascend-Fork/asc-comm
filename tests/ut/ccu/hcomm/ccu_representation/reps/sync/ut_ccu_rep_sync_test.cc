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
#include <memory>
#include <string>
#include <climits>

#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_event.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_wait_notify.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_loc_record_event.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_record_shared_notify.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_rempostsem_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_rempostvar_v1.h"
#include "hcomm/resource/representation/reps/sync/ccu_rep_remwaitsem_v1.h"
#include "hcomm/resource/representation/interface/ccu_datatype_v1.h"
#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepLocWaitEventTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepLocWaitNotifyTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepLocRecordEventTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepRecordSharedNotifyTest : public ::testing::Test {
protected:
    void SetUp() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepRemPostSemTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepRemPostVarTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

class CcuRepRemWaitSemTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepLocWaitEventTest, ConstructorInitializesCorrectly)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0xF, true);

    EXPECT_EQ(rep.type(), ccu_rep_type::loc_wait_event);
    EXPECT_EQ(rep.instr_count(), 1);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepLocWaitEventTest, ConstructorWithProfilingDisabled)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0xA, false);

    EXPECT_EQ(rep.type(), ccu_rep_type::loc_wait_event);
    EXPECT_EQ(rep.instr_count(), 1);
    EXPECT_FALSE(rep.translated());
}

TEST_F(CcuRepLocWaitEventTest, TranslateWithProfilingEnabled)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0xF, true);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(rep.start_instr_id(), 0);
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
}

TEST_F(CcuRepLocWaitEventTest, TranslateWithProfilingDisabled)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0x5, false);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 10;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instrId, 11);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x4);
}

TEST_F(CcuRepLocWaitEventTest, describe)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0xABCD, true);

    std::string desc = rep.describe();

    EXPECT_NE(desc.find("CcuRepLocWaitEvent"), std::string::npos);
    EXPECT_NE(desc.find("mask["), std::string::npos);
}

TEST_F(CcuRepLocWaitEventTest, SetAndGetDependencyInfo_SingleBit)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0x3, true);
    std::unordered_map<uint32_t, std::vector<std::shared_ptr<ccu_rep_base>>> dep_info_;
    auto depRep = std::make_shared<ccu_rep_nop>(&insGen);
    dep_info_[0x1].push_back(depRep);
    rep.set_dependency_info(dep_info_);
    auto result = rep.get_dependency_info(0x1);
    EXPECT_EQ(result.size(), 1U);
    EXPECT_EQ(result[0], depRep);
}

TEST_F(CcuRepLocWaitEventTest, GetDependencyInfo_NotFound_ReturnsEmpty)
{
    completed_event event;
    ccu_rep_loc_wait_event rep(&insGen, event, 0x3, true);
    auto result = rep.get_dependency_info(0x2);
    EXPECT_TRUE(result.empty());
}

class CcuRepContextDepTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepContextDepTest, SetDependencyInfo_SingleBitMask)
{
    ccu_rep_context context_;
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0x1, rep);
    auto dep = context_.get_dependency_info(1);
    EXPECT_EQ(dep.size(), 1U);
    EXPECT_EQ(dep.count(0x1), 1U);
    EXPECT_EQ(dep[0x1].size(), 1U);
    EXPECT_EQ(dep[0x1][0], rep);
}

TEST_F(CcuRepContextDepTest, SetDependencyInfo_MultiBitMask_SplitToSingleBitKeys)
{
    ccu_rep_context context_;
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    // mask_=0x3 (bit0+bit1) 应拆解到 0x1 和 0x2 两个单 bit key，与异常侧按 1<<i 查询对齐
    context_.set_dependency_info(1, 0x3, rep);
    auto dep = context_.get_dependency_info(1);
    EXPECT_EQ(dep.size(), 2U);
    EXPECT_EQ(dep.count(0x1), 1U);
    EXPECT_EQ(dep.count(0x2), 1U);
    EXPECT_EQ(dep[0x1].size(), 1U);
    EXPECT_EQ(dep[0x1][0], rep);
    EXPECT_EQ(dep[0x2].size(), 1U);
    EXPECT_EQ(dep[0x2][0], rep);
}

TEST_F(CcuRepContextDepTest, SetDependencyInfo_HighBitMask_AllBitsRegistered)
{
    ccu_rep_context context_;
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(7, 0xABCD, rep);
    auto dep = context_.get_dependency_info(7);
    // 0xABCD = 1010 1011 1100 1101，置位 bit: 0,2,3,6,7,8,9,11,13,15 共 10 个
    EXPECT_EQ(dep.size(), 10U);
}

TEST_F(CcuRepContextDepTest, SetDependencyInfo_ZeroMask_ReturnsError)
{
    ccu_rep_context context_;
    auto rep = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0, rep);
}

TEST_F(CcuRepContextDepTest, GetDependencyInfo_NotFound_ReturnsEmpty)
{
    ccu_rep_context context_;
    auto dep = context_.get_dependency_info(999);
    EXPECT_TRUE(dep.empty());
}

TEST_F(CcuRepContextDepTest, SetDependencyInfo_MultipleRepsSameBit_AppendedInOrder)
{
    ccu_rep_context context_;
    auto rep1 = std::make_shared<ccu_rep_nop>(&insGen);
    auto rep2 = std::make_shared<ccu_rep_nop>(&insGen);
    context_.set_dependency_info(1, 0x1, rep1);
    context_.set_dependency_info(1, 0x1, rep2);
    auto dep = context_.get_dependency_info(1);
    EXPECT_EQ(dep[0x1].size(), 2U);
    EXPECT_EQ(dep[0x1][0], rep1);
    EXPECT_EQ(dep[0x1][1], rep2);
}

TEST_F(CcuRepContextDepTest, EraseDependencyInfo_OnlyErasesSpecifiedId)
{
    ccu_rep_context context_;
    auto repA = std::make_shared<ccu_rep_nop>(&insGen);
    auto repB = std::make_shared<ccu_rep_nop>(&insGen);
    // 模拟多 event 交错：A 注册后 B 注册，清 A 不应影响 B
    context_.set_dependency_info(1, 0x1, repA);
    context_.set_dependency_info(2, 0x2, repB);
    context_.erase_dependency_info(1);
    // B 的依赖应保留
    auto depB = context_.get_dependency_info(2);
    EXPECT_EQ(depB.size(), 1U);
    EXPECT_EQ(depB.count(0x2), 1U);
    EXPECT_EQ(depB[0x2].size(), 1U);
    EXPECT_EQ(depB[0x2][0], repB);
    // A 的依赖应已清除
    EXPECT_TRUE(context_.get_dependency_info(1).empty());
}

TEST_F(CcuRepContextDepTest, ClearDependencyInfo_RemovesAll)
{
    ccu_rep_context context_;
    context_.set_dependency_info(1, 0x1, std::make_shared<ccu_rep_nop>(&insGen));
    context_.set_dependency_info(2, 0x2, std::make_shared<ccu_rep_nop>(&insGen));
    context_.clear_dependency_info();
    EXPECT_TRUE(context_.get_dependency_info(1).empty());
    EXPECT_TRUE(context_.get_dependency_info(2).empty());
}

TEST_F(CcuRepLocWaitNotifyTest, ConstructorInitializesCorrectly)
{
    local_notify notify;
    uint32_t mask_ = 0xFF;
    ccu_rep_loc_wait_notify rep(&insGen, notify, mask_, true);

    EXPECT_EQ(rep.type(), ccu_rep_type::loc_wait_notify);
    EXPECT_EQ(rep.instr_count(), 1);
    EXPECT_EQ(rep.get_mask(), 0xFF);
}

TEST_F(CcuRepLocWaitNotifyTest, ConstructorWithProfilingDisabled)
{
    local_notify notify;
    uint32_t mask_ = 0x0F;
    ccu_rep_loc_wait_notify rep(&insGen, notify, mask_, false);

    EXPECT_EQ(rep.type(), ccu_rep_type::loc_wait_notify);
    EXPECT_FALSE(rep.translated());
}

TEST_F(CcuRepLocWaitNotifyTest, TranslateWithProfilingEnabled)
{
    local_notify notify;
    uint32_t mask_ = 0xF;
    ccu_rep_loc_wait_notify rep(&insGen, notify, mask_, true);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
}

TEST_F(CcuRepLocWaitNotifyTest, TranslateWithProfilingDisabled)
{
    local_notify notify;
    uint32_t mask_ = 0xA;
    ccu_rep_loc_wait_notify rep(&insGen, notify, mask_, false);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 5;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instrId, 6);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x4);
}

TEST_F(CcuRepLocWaitNotifyTest, describe)
{
    local_notify notify;
    uint32_t mask_ = 0x1234;
    ccu_rep_loc_wait_notify rep(&insGen, notify, mask_, true);

    std::string desc = rep.describe();

    EXPECT_NE(desc.find("CcuRepLocWaitNotify"), std::string::npos);
}

TEST_F(CcuRepLocRecordEventTest, ConstructorInitializesCorrectly)
{
    completed_event event;
    ccu_rep_loc_record_event rep(&insGen, event, 0xF);

    EXPECT_EQ(rep.type(), ccu_rep_type::loc_record_event);
    EXPECT_EQ(rep.instr_count(), 1);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepLocRecordEventTest, translate)
{
    ccu_ins_generater_v1 insGen;
    completed_event event;
    ccu_rep_loc_record_event rep(&insGen, event, 0xABCD);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 100;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(rep.start_instr_id(), 100);
    EXPECT_EQ(instrId, 101);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
}

TEST_F(CcuRepLocRecordEventTest, describe)
{
    completed_event event;
    ccu_rep_loc_record_event rep(&insGen, event, 0xABCD);

    std::string desc = rep.describe();

    EXPECT_NE(desc.find("CcuRepLocRecordEvent"), std::string::npos);
    EXPECT_NE(desc.find("mask["), std::string::npos);
}

TEST_F(CcuRepRecordSharedNotifyTest, ConstructorInitializesCorrectly)
{
    local_notify notify;
    uint16_t mask_ = 0xFF;
    ccu_rep_record_shared_notify rep(&insGen, notify, mask_);

    EXPECT_EQ(rep.type(), ccu_rep_type::record_shared_notify);
    EXPECT_EQ(rep.instr_count(), 1);
    EXPECT_EQ(rep.get_mask(), 0xFF);
}

TEST_F(CcuRepRecordSharedNotifyTest, TranslateSameDie)
{
    local_notify notify;
    uint16_t mask_ = 0xF;
    ccu_rep_record_shared_notify rep(&insGen, notify, mask_);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.die_id = 0;

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instrId, 1);
    EXPECT_EQ(instr_.header.type, 0x1);
    EXPECT_EQ(instr_.header.code, 0x2);
}

TEST_F(CcuRepRecordSharedNotifyTest, TranslateDifferentDie)
{
    local_notify notify;
    uint16_t mask_ = 0xA;
    ccu_rep_record_shared_notify rep(&insGen, notify, mask_);

    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 5;
    trans_dep dep = {};
    dep.die_id = 1;
    dep.reserve_cke_id = 2;
    dep.reserve_channal_id[1] = 3;

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);

    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
    EXPECT_EQ(instrId, 6);
    EXPECT_EQ(instr_.header.type, 0x2);
    EXPECT_EQ(instr_.header.code, 0xb);
}

TEST_F(CcuRepRecordSharedNotifyTest, describe)
{
    local_notify notify;
    uint16_t mask_ = 0x1234;
    ccu_rep_record_shared_notify rep(&insGen, notify, mask_);

    std::string desc = rep.describe();

    EXPECT_NE(desc.find("Post"), std::string::npos);
    EXPECT_NE(desc.find("mask["), std::string::npos);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
