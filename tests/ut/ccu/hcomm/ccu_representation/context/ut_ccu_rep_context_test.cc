/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/context/ccu_rep_context_v1.h"
#include "hcomm/resource/representation/reps/arithmetic/ccu_rep_assign_v1.h"
#include "hcomm/resource/representation/reps/loop/ccu_rep_loopgroup_bundle_v1.h"
#include "hcomm/resource/representation/reps/common/ccu_rep_nop_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"
#include "hcomm/common/ccu_device_context.h"
#include "ccu/hcomm/ccu_api_types.h"
#include "ccu_channel_get_stub.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace asc {
namespace ccu_rep {
namespace {

class CcuRepContextTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
};

TEST_F(CcuRepContextTest, Constructor)
{
    ccu_rep_context context_;
    EXPECT_NE(context_.current_block(), nullptr);
    EXPECT_EQ(context_.current_block(), context_.current_block());
}

TEST_F(CcuRepContextTest, CurrentBlock_InitiallyMainBlock)
{
    ccu_rep_context context_;
    auto block = context_.current_block();
    EXPECT_NE(block, nullptr);
}

TEST_F(CcuRepContextTest, set_current_block)
{
    ccu_rep_context context_;
    auto newBlock = std::make_shared<ccu_rep_block>(&insGen, "newBlock");
    context_.set_current_block(newBlock);
    EXPECT_EQ(context_.current_block(), newBlock);
}

TEST_F(CcuRepContextTest, Append_SingleRep)
{
    ccu_rep_context context_;
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop);
    EXPECT_EQ(context_.get_rep_sequence().size(), 1U);
}

TEST_F(CcuRepContextTest, Append_MultipleReps)
{
    ccu_rep_context context_;
    auto nop1 = std::make_shared<ccu_rep_nop>(&insGen);
    auto nop2 = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop1);
    context_.append(nop2);
    EXPECT_EQ(context_.get_rep_sequence().size(), 2U);
}

TEST_F(CcuRepContextTest, GetRepSequence_InitiallyEmpty)
{
    ccu_rep_context context_;
    EXPECT_EQ(context_.get_rep_sequence().size(), 0U);
}

TEST_F(CcuRepContextTest, GetRepByInstrId_NotFound)
{
    ccu_rep_context context_;
    auto result = context_.get_rep_by_instr_id(100);
    EXPECT_EQ(result, nullptr);
}

TEST_F(CcuRepContextTest, DumpReprestation_NoCrash)
{
    ccu_rep_context context_;
    EXPECT_NO_FATAL_FAILURE(context_.dump_represtation());
}

TEST_F(CcuRepContextTest, DumpReprestation_WithReps)
{
    ccu_rep_context context_;
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop);
    EXPECT_NO_FATAL_FAILURE(context_.dump_represtation());
}

TEST_F(CcuRepContextTest, SetAndGetDieId)
{
    ccu_rep_context context_;
    uint32_t dieId = 5;
    context_.set_die_id(dieId);
    EXPECT_EQ(context_.get_die_id(), dieId);
}

TEST_F(CcuRepContextTest, SetAndGetMissionId)
{
    ccu_rep_context context_;
    uint32_t missionId = 10;
    context_.set_mission_id(missionId);
    EXPECT_EQ(context_.get_mission_id(), missionId);
}

TEST_F(CcuRepContextTest, SetMissionId_OnlySetsOnce)
{
    ccu_rep_context context_;
    context_.set_mission_id(10);
    context_.set_mission_id(20);
    EXPECT_EQ(context_.get_mission_id(), 10U);
}

TEST_F(CcuRepContextTest, SetAndGetMissionKey)
{
    ccu_rep_context context_;
    uint32_t missionKey = 15;
    context_.set_mission_key(missionKey);
    EXPECT_EQ(context_.get_mission_key(), missionKey);
}

TEST_F(CcuRepContextTest, GetProfilingInfo_InitiallyEmpty)
{
    ccu_rep_context context_;
    EXPECT_EQ(context_.get_profiling_info().size(), 0U);
}

TEST_F(CcuRepContextTest, GetLGProfilingInfo_InitiallyEmpty)
{
    ccu_rep_context context_;
    EXPECT_EQ(context_.get_lg_profiling_info().ccu_profiling_infos.size(), 0U);
}

TEST_F(CcuRepContextTest, GetWaiteCkeProfilingReps_InitiallyEmpty)
{
    ccu_rep_context context_;
    EXPECT_EQ(context_.get_waite_cke_profiling_reps().size(), 0U);
}

TEST_F(CcuRepContextTest, CollectProfilingReps_Assign_NotVarToVar)
{
    ccu_rep_context context_;
    variable var_a_(nullptr);
    ccu_rep_assign assign(&insGen, var_a_, 100);
    assign.sub_type_ = assign_sub_type::imd_to_variable;
    auto assignPtr = std::make_shared<ccu_rep_assign>(assign);
    context_.collect_profiling_reps(assignPtr);
    EXPECT_EQ(context_.get_lg_profiling_info().assign_profiling_reps.size(), 0U);
}

TEST_F(CcuRepContextTest, CollectProfilingReps_Assign_VarToVar)
{
    ccu_rep_context context_;
    variable var_a_(nullptr);
    variable var_b_(nullptr);
    ccu_rep_assign assign(&insGen, var_b_, var_a_);
    assign.sub_type_ = assign_sub_type::var_to_var;
    auto assignPtr = std::make_shared<ccu_rep_assign>(assign);
    context_.collect_profiling_reps(assignPtr);
    EXPECT_EQ(context_.get_lg_profiling_info().assign_profiling_reps.size(), 1U);
}

TEST_F(CcuRepContextTest, CollectProfilingReps_LoopGroup)
{
    ccu_rep_context context_;
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    auto loopGroupPtr = std::make_shared<ccu_rep_loop_group_bundle>(&insGen, grpCfg, parallel_param, offsetParam);
    context_.collect_profiling_reps(loopGroupPtr);
    EXPECT_EQ(context_.all_lg_profiling_reps.size(), 1U);
}

TEST_F(CcuRepContextTest, add_sqe_profiling)
{
    ccu_rep_context context_;
    context_.set_die_id(3);
    context_.add_sqe_profiling("context_kernel");
    EXPECT_EQ(context_.get_profiling_info().size(), 1U);
    EXPECT_EQ(context_.get_profiling_info()[0].type, static_cast<uint8_t>(ccu_profilin_type::ccu_task_profiling));
    EXPECT_EQ(context_.get_profiling_info()[0].die_id, 0U);
    EXPECT_EQ(context_.get_profiling_info()[0].name, "context_kernel");
}

TEST_F(CcuRepContextTest, AddProfiling_Simple)
{
    ccu_rep_context context_;
    std::string name = "test_profiling";
    uint32_t mask_ = 0xF;
    int32_t result = context_.add_profiling(name, mask_);
    EXPECT_EQ(result, HCCL_SUCCESS);
    EXPECT_EQ(context_.get_profiling_info().size(), 1U);
    EXPECT_EQ(context_.get_profiling_info()[0].type, static_cast<uint8_t>(ccu_profilin_type::ccu_waitcke_profiling));
    EXPECT_EQ(context_.get_profiling_info()[0].name, name);
}

TEST_F(CcuRepContextTest, Append_CollectsProfilingReps)
{
    ccu_rep_context context_;
    variable var_a_(nullptr);
    variable var_b_(nullptr);
    ccu_rep_assign assign(&insGen, var_b_, var_a_);
    assign.sub_type_ = assign_sub_type::var_to_var;
    auto assignPtr = std::make_shared<ccu_rep_assign>(assign);
    context_.append(assignPtr);
    EXPECT_EQ(context_.get_lg_profiling_info().assign_profiling_reps.size(), 1U);
}

TEST_F(CcuRepContextTest, SetCurrentBlock_ToNullBlock)
{
    ccu_rep_context context_;
    auto nullBlock = std::make_shared<ccu_rep_block>(&insGen);
    context_.set_current_block(nullBlock);
    EXPECT_EQ(context_.current_block(), nullBlock);
}

TEST_F(CcuRepContextTest, GetRepSequence_ReturnsMainBlockReps)
{
    ccu_rep_context context_;
    auto nop = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop);
    const auto& reps = context_.get_rep_sequence();
    EXPECT_EQ(reps.size(), 1U);
}

TEST_F(CcuRepContextTest, MultipleBlocks_SetCurrentBlock)
{
    ccu_rep_context context_;
    auto block1 = std::make_shared<ccu_rep_block>(&insGen, "block1");
    auto block2 = std::make_shared<ccu_rep_block>(&insGen, "block2");

    context_.set_current_block(block1);
    auto nop1 = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop1);

    context_.set_current_block(block2);
    auto nop2 = std::make_shared<ccu_rep_nop>(&insGen);
    context_.append(nop2);

    EXPECT_EQ(block1->get_reps().size(), 1U);
    EXPECT_EQ(block2->get_reps().size(), 1U);
}

class CcuRepContextProfilingTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};

    void SetUp() override
    {
        HcommCcuChannelPod channelPod{};
        channelPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channelPod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channelPod.header.size = sizeof(HcommCcuChannelPod);
        channelPod.localXnIds[0] = 0;
        channelPod.remoteXnIds[0] = 0;
        channelPod.localCkeIds[0] = 5;
        channelPod.remoteCkeIds[0] = 6;
        channelPod.rmtCcuBufSize = 1;
        SetHcommCcuChannelQueryStub(channelPod);
    }

    void TearDown() override { ResetHcommCcuChannelQueryStub(); }
};

TEST_F(CcuRepContextProfilingTest, AddProfiling_WithChannel)
{
    ccu_rep_context context_;
    ChannelHandle ch = 0;
    int32_t ret = context_.add_profiling(ch, std::string("wait_cke"), 0, 0xF);
    EXPECT_EQ(ret, HCCL_SUCCESS);
    EXPECT_EQ(context_.get_profiling_info().size(), 1U);
    EXPECT_EQ(context_.get_profiling_info()[0].type, static_cast<uint8_t>(ccu_profilin_type::ccu_waitcke_profiling));
}

TEST_F(CcuRepContextProfilingTest, AddProfiling_ChannelArrayBroadcast)
{
    ccu_rep_context context_;
    ChannelHandle channels[2] = {0, 0};
    int32_t ret = context_.add_profiling(channels, 2);
    EXPECT_EQ(ret, HCCL_SUCCESS);
    EXPECT_FALSE(context_.get_lg_profiling_info().ccu_profiling_infos.empty());
    EXPECT_EQ(context_.get_lg_profiling_info().ccu_profiling_infos[0].name, "GroupBroadcast");
}

TEST_F(CcuRepContextProfilingTest, AddProfiling_ChannelArrayReduce)
{
    ccu_rep_context context_;
    ccu_loop_group_config grpCfg{};
    variable parallel_param(nullptr);
    variable offsetParam(nullptr);
    auto loopGroupPtr = std::make_shared<ccu_rep_loop_group_bundle>(&insGen, grpCfg, parallel_param, offsetParam);
    context_.collect_profiling_reps(loopGroupPtr);

    ChannelHandle channels[2] = {0, 0};
    int32_t ret = context_.add_profiling(channels, 2, 0, 0, 0);
    EXPECT_EQ(ret, HCCL_SUCCESS);
    EXPECT_FALSE(context_.get_lg_profiling_info().ccu_profiling_infos.empty());
    EXPECT_EQ(context_.get_lg_profiling_info().ccu_profiling_infos[0].name, "GroupReduce");
    EXPECT_FALSE(context_.get_lg_profiling_info().lg_profiling_reps.empty());
}

TEST_F(CcuRepContextProfilingTest, AddProfiling_NullChannelArray)
{
    ccu_rep_context context_;
    int32_t ret = context_.add_profiling(nullptr, 2);
    EXPECT_NE(ret, HCCL_SUCCESS);
}

TEST_F(CcuRepContextTest, GetRepByInstrId_Found)
{
    ccu_rep_context context_;
    variable var_a_(nullptr);
    auto assignPtr = std::make_shared<ccu_rep_assign>(&insGen, var_a_, uint64_t(5));
    context_.append(assignPtr);

    auto found = context_.get_rep_by_instr_id(0);
    EXPECT_EQ(found, assignPtr);
    EXPECT_EQ(context_.get_rep_by_instr_id(1000), nullptr);
}

TEST_F(CcuRepContextTest, DeviceLogicId_SetAndGet)
{
    set_current_ccu_device_logic_id(7);
    EXPECT_EQ(get_current_ccu_device_logic_id(), 7);
    set_current_ccu_device_logic_id(-1);
}

TEST_F(CcuRepContextProfilingTest, AddProfiling_WithChannel_StubFailure)
{
    SetHcommCcuChannelQueryStubResult(-1);
    ccu_rep_context context_;
    ChannelHandle ch = 0;
    int32_t ret = context_.add_profiling(ch, std::string("wait_cke"), 0, 0xF);
    EXPECT_NE(ret, HCCL_SUCCESS);
}

} // namespace
} // namespace ccu_rep
} // namespace asc
