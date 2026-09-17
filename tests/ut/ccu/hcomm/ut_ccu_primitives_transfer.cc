/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.hiascend.com/software/licensing
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#include <gtest/gtest.h>

#include "hcomm/hcomm_ccu_channel.h"
#include "hcomm/hcomm_types.h"
#include "hcomm/resource/kernel/ccu_kernel.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_buflocread_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_buflocwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_loccpy_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_write_v1.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "hcomm/resource/microcode/ccu_assist_v1.h"
#include "ccu_channel_get_stub.h"

namespace {

constexpr ccu_buffer_handle kInvalidBufferHandle = 0xFFFF;
constexpr ccu_local_addr_handle kInvalidLocalAddrHandle = 0xFFFF;
constexpr ccu_remote_addr_handle kInvalidRemoteAddrHandle = 0xFFFF;
constexpr ccu_variable_handle kInvalidVariableHandle = 0xFFFF;
constexpr ccu_event_handle kInvalidEventHandle = 0xFFFF;
constexpr ChannelHandle kChannelHandle = 0;

class CcuTransferKernelTest : public testing::Test {
protected:
    void SetUp() override
    {
        kernel_.set_ins_generater(&insGenerator_);
        kernel_.set_die_id(0);
        ASSERT_EQ(kernel_.variable_alloc(&len_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.variable_alloc(&token_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.event_alloc(&event_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.buffer_alloc(&buffer_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.local_addr_alloc(&localSrc_, &localSrcAddr_, &localSrcToken_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.local_addr_alloc(&localDst_, &localDstAddr_, &localDstToken_), CcuResult::CCU_SUCCESS);
        ASSERT_EQ(kernel_.remote_addr_alloc(&remoteDst_, &remoteDstAddr_, &remoteDstToken_), CcuResult::CCU_SUCCESS);
    }

    void TearDown() override {}

    asc::ccu_rep::ccu_ins_generater_v1 insGenerator_{};
    asc::ccu_kernel kernel_{};
    ccu_variable_handle len_{0};
    ccu_variable_handle token_{0};
    ccu_event_handle event_{0};
    ccu_buffer_handle buffer_{0};
    ccu_local_addr_handle localSrc_{0};
    ccu_address_handle localSrcAddr_{0};
    ccu_variable_handle localSrcToken_{0};
    ccu_local_addr_handle localDst_{0};
    ccu_address_handle localDstAddr_{0};
    ccu_variable_handle localDstToken_{0};
    ccu_remote_addr_handle remoteDst_{0};
    ccu_address_handle remoteDstAddr_{0};
    ccu_variable_handle remoteDstToken_{0};
};

class CcuLocalCopyTransferTest : public CcuTransferKernelTest {};
class CcuWriteTransferTest : public CcuTransferKernelTest {};
class CcuWriteReduceTransferTest : public CcuTransferKernelTest {};

TEST_F(CcuLocalCopyTransferTest, MemToMem_AppendsRepresentationAndTranslatesV1)
{
    ASSERT_EQ(kernel_.local_copy_mem_to_mem(localDst_, localSrc_, len_, event_, 0xF), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::local_cpy);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    dependency.reserve_gsa_id = 2;
    dependency.reserve_cke_id = 3;
    EXPECT_TRUE(kernel_.get_rep_sequence()[0]->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
}

TEST_F(CcuLocalCopyTransferTest, MemToBuffer_AppendsRepresentationAndTranslatesV1)
{
    ASSERT_EQ(kernel_.local_copy_mem_to_buffer(buffer_, localSrc_, len_, event_, 0xF), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::buf_loc_read);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_channal_id[0] = 0;
    dependency.reserve_channal_id[1] = 1;
    EXPECT_TRUE(kernel_.get_rep_sequence()[0]->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
}

TEST_F(CcuLocalCopyTransferTest, BufferToMem_AppendsRepresentationAndTranslatesV1)
{
    ASSERT_EQ(kernel_.local_copy_buffer_to_mem(localDst_, buffer_, len_, event_, 0xF), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::buf_loc_write);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    dependency.reserve_gsa_id = 2;
    dependency.reserve_cke_id = 3;
    EXPECT_TRUE(kernel_.get_rep_sequence()[0]->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
}

TEST_F(CcuLocalCopyTransferTest, InvalidHandles_ReturnNotFound)
{
    EXPECT_EQ(
        kernel_.local_copy_mem_to_mem(localDst_, kInvalidLocalAddrHandle, len_, event_, 0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(
        kernel_.local_copy_mem_to_buffer(kInvalidBufferHandle, localSrc_, len_, event_, 0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(
        kernel_.local_copy_buffer_to_mem(localDst_, kInvalidBufferHandle, len_, event_, 0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(kernel_.get_rep_sequence().size(), 0U);
}

TEST_F(CcuWriteTransferTest, MemToMem_AppendsRepresentationAndTranslatesV1)
{
    HcommCcuChannelPod channelPod{};
    channelPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    channelPod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    channelPod.header.size = sizeof(HcommCcuChannelPod);
    channelPod.localXnIds[0] = 0;
    channelPod.remoteXnIds[0] = 0;
    channelPod.localCkeIds[0] = 0;
    channelPod.remoteCkeIds[0] = 0;
    channelPod.rmtCcuBufSize = 1;
    asc::SetHcommCcuChannelQueryStub(channelPod);

    ASSERT_EQ(kernel_.write_mem_to_mem(kChannelHandle, remoteDst_, localSrc_, len_, event_, 0xF), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::write);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    dependency.reserve_gsa_id = 2;
    dependency.reserve_cke_id = 3;
    EXPECT_TRUE(kernel_.get_rep_sequence()[0]->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
    asc::ResetHcommCcuChannelQueryStub();
}

TEST_F(CcuWriteTransferTest, BufferToMem_AppendsRepresentationAndTranslatesV1)
{
    HcommCcuChannelPod channelPod{};
    channelPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    channelPod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    channelPod.header.size = sizeof(HcommCcuChannelPod);
    channelPod.localXnIds[0] = 0;
    channelPod.remoteXnIds[0] = 0;
    channelPod.localCkeIds[0] = 0;
    channelPod.remoteCkeIds[0] = 0;
    channelPod.rmtCcuBufSize = 1;
    asc::SetHcommCcuChannelQueryStub(channelPod);

    ASSERT_EQ(
        kernel_.write_buffer_to_mem(kChannelHandle, remoteDst_, buffer_, len_, event_, 0xF), CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::buf_write);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    dependency.reserve_gsa_id = 2;
    dependency.reserve_cke_id = 3;
    EXPECT_TRUE(kernel_.get_rep_sequence()[0]->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
    asc::ResetHcommCcuChannelQueryStub();
}

TEST_F(CcuWriteTransferTest, InvalidHandles_ReturnNotFound)
{
    EXPECT_EQ(
        kernel_.write_mem_to_mem(kChannelHandle, remoteDst_, kInvalidLocalAddrHandle, len_, event_, 0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(
        kernel_.write_buffer_to_mem(kChannelHandle, remoteDst_, kInvalidBufferHandle, len_, event_, 0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(kernel_.get_rep_sequence().size(), 0U);
}

TEST_F(CcuWriteReduceTransferTest, MemToMem_AppendsReduceRepresentationAndTranslatesV1)
{
    HcommCcuChannelPod channelPod{};
    channelPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    channelPod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    channelPod.header.size = sizeof(HcommCcuChannelPod);
    channelPod.localXnIds[0] = 0;
    channelPod.remoteXnIds[0] = 0;
    channelPod.localCkeIds[0] = 0;
    channelPod.remoteCkeIds[0] = 0;
    channelPod.rmtCcuBufSize = 1;
    asc::SetHcommCcuChannelQueryStub(channelPod);

    ASSERT_EQ(
        kernel_.write_mem_to_mem_reduce(
            kChannelHandle, remoteDst_, localSrc_, len_, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, event_, 0xF),
        CcuResult::CCU_SUCCESS);
    ASSERT_EQ(kernel_.get_rep_sequence().size(), 1U);
    ASSERT_EQ(kernel_.get_rep_sequence()[0]->type(), asc::ccu_rep::ccu_rep_type::write);

    auto* representation = dynamic_cast<asc::ccu_rep::ccu_rep_write*>(kernel_.get_rep_sequence()[0].get());
    ASSERT_NE(representation, nullptr);
    EXPECT_EQ(representation->get_data_type(), asc::ccu_rep::get_ub_data_type(HCCL_DATA_TYPE_FP32));
    EXPECT_EQ(representation->get_op_type(), asc::ccu_rep::get_ub_reduce_type(HCCL_REDUCE_SUM));
    EXPECT_EQ(representation->get_reduce_flag(), 1U);

    asc::ccu_rep::ccu_instr instructions[8] = {};
    asc::ccu_rep::ccu_instr* instruction = instructions;
    uint16_t instructionId = 0;
    asc::ccu_rep::trans_dep dependency{};
    dependency.reserve_xn_id = 1;
    dependency.reserve_gsa_id = 2;
    dependency.reserve_cke_id = 3;
    EXPECT_TRUE(representation->translate(nullptr, instruction, instructionId, dependency));
    EXPECT_GT(instructionId, 0U);
    asc::ResetHcommCcuChannelQueryStub();
}

TEST_F(CcuWriteReduceTransferTest, InvalidHandles_ReturnNotFound)
{
    EXPECT_EQ(
        kernel_.write_mem_to_mem_reduce(
            kChannelHandle, remoteDst_, kInvalidLocalAddrHandle, len_, HCCL_DATA_TYPE_FP32, HCCL_REDUCE_SUM, event_,
            0xF),
        CcuResult::CCU_E_NOT_FOUND);
    EXPECT_EQ(kernel_.get_rep_sequence().size(), 0U);
}

} // namespace
