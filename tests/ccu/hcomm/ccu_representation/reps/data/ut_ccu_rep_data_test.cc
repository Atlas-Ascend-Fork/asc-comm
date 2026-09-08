/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/representation/reps/data/ccu_rep_read_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_write_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufread_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_buflocread_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_buflocwrite_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_bufreduce_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_loccpy_v1.h"
#include "hcomm/resource/representation/reps/data/ccu_rep_remMem_v1.h"
#include "ccu_api_exception.h"
#include "ccu_channel_get_stub.h"
#include "hcomm/resource/representation/reps/translator/ccu_ins_generater_v1.h"

#include "hcomm_c_adpt.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace hcomm {
namespace ccu_rep {
namespace {

class CcuRepChannelTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override
    {
        HcommCcuChannelPod channelPod{};
        channelPod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channelPod.header.magic_word = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channelPod.header.size = sizeof(HcommCcuChannelPod);
        channelPod.local_xn_ids[0] = 0;
        channelPod.remote_xn_ids[0] = 0;
        channelPod.local_cke_ids[0] = 0;
        channelPod.remote_cke_ids[0] = 0;
        channelPod.rmt_ccu_buf_size = 1;
        SetHcommCcuChannelGetPodStub(channelPod);
    }
    void TearDown() override
    {
        ResetHcommCcuChannelGetPodStub();
    }
};

class CcuRepReadTest : public CcuRepChannelTest {};
class CcuRepWriteTest : public CcuRepChannelTest {};
class CcuRepBufReadTest : public CcuRepChannelTest {};
class CcuRepBufWriteTest : public CcuRepChannelTest {};

class CcuRepBufLocReadTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
    void TearDown() override {}
};

class CcuRepBufLocWriteTest : public ::testing::Test {
protected:
    ccu_ins_generater_v1 insGen{};
    void SetUp() override {}
    void TearDown() override {}
};

class CcuRepBufReduceTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepLocCpyTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
    ccu_ins_generater_v1 insGen{};
};

class CcuRepRemMemTest : public CcuRepChannelTest {};

TEST_F(CcuRepReadTest, Constructor_Basic)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_read rep(&insGen, 0, loc_, rem_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::read);
    EXPECT_EQ(rep.get_mask(), 0xF);
    EXPECT_EQ(rep.get_sem_id(), sem_.id());
}

TEST_F(CcuRepReadTest, Constructor_WithDataType)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_read rep(&insGen, 0, loc_, rem_, len_, 1, 2, sem_, 0xA);
    EXPECT_EQ(rep.type(), ccu_rep_type::read);
    EXPECT_EQ(rep.get_mask(), 0xA);
}

TEST_F(CcuRepReadTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_read rep(&insGen, 0, loc_, rem_, len_, sem_, 0xF);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Read"), std::string::npos);
}

TEST_F(CcuRepReadTest, translate)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_read rep(&insGen, 0, loc_, rem_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::read);
}

TEST_F(CcuRepWriteTest, Constructor_Basic)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_write rep(&insGen, 0, rem_, loc_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::write);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepWriteTest, Constructor_WithDataType)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_write rep(&insGen, 0, rem_, loc_, len_, 1, 2, sem_, 0xA);
    EXPECT_EQ(rep.type(), ccu_rep_type::write);
}

TEST_F(CcuRepWriteTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_write rep(&insGen, 0, rem_, loc_, len_, sem_, 0xF);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Write"), std::string::npos);
}

TEST_F(CcuRepWriteTest, translate)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr loc_{addr_, token};
    remote_addr rem_{addr_, token};

    ccu_rep_write rep(&insGen, 0, rem_, loc_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::write);
}

TEST_F(CcuRepBufReadTest, Constructor)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    remote_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_read rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_read);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepBufReadTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    remote_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_read rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_read);
}

TEST_F(CcuRepBufReadTest, translate)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    remote_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_read rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_read);
}

TEST_F(CcuRepBufWriteTest, Constructor)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    remote_addr dst_{addr_, token};

    ccu_rep_buf_write rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_write);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepBufWriteTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    remote_addr dst_{addr_, token};

    ccu_rep_buf_write rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_write);
}

TEST_F(CcuRepBufWriteTest, translate)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    remote_addr dst_{addr_, token};

    ccu_rep_buf_write rep(&insGen, 0, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_write);
}

TEST_F(CcuRepBufLocReadTest, Constructor)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_loc_read rep(&insGen, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_loc_read);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepBufLocReadTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_loc_read rep(&insGen, src_, dst_, len_, sem_, 0xF);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Read"), std::string::npos);
}

TEST_F(CcuRepBufLocReadTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr_;
    variable token;
    local_addr src_{addr_, token};
    ccu_buf dst_{&context_};

    ccu_rep_buf_loc_read rep(&insGen, src_, dst_, len_, sem_, 0xF);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_channal_id[0] = 1;

    EXPECT_THROW(
        { rep.translate(nullptr, instrPtr, instrId, dep); }, ::AscendC::ccu::detail::ccu_exception);
    EXPECT_TRUE(rep.translated());
}

TEST_F(CcuRepBufLocWriteTest, Constructor)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    local_addr dst_{addr_, token};

    ccu_rep_buf_loc_write rep(&insGen, src_, dst_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_loc_write);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepBufLocWriteTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    local_addr dst_{addr_, token};

    ccu_rep_buf_loc_write rep(&insGen, src_, dst_, len_, sem_, 0xF);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Write"), std::string::npos);
}

TEST_F(CcuRepBufLocWriteTest, translate)
{
    ccu_ins_generater_v1 insGen;
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    ccu_buf src_{&context_};
    address addr_;
    variable token;
    local_addr dst_{addr_, token};

    ccu_rep_buf_loc_write rep(&insGen, src_, dst_, len_, sem_, 0xF);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_channal_id[0] = 1;

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
}

TEST_F(CcuRepBufReduceTest, Constructor)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 4, 1, 1, 0, sem_, len_, 1);
    EXPECT_EQ(rep.type(), ccu_rep_type::buf_reduce);
    EXPECT_EQ(rep.get_count(), 4);
    EXPECT_EQ(rep.get_data_type(), 1);
    EXPECT_EQ(rep.get_mask(), 1);
}

TEST_F(CcuRepBufReduceTest, describe)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}, ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 2, 1, 1, 0, sem_, len_, 1);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("Reduce"), std::string::npos);
}

TEST_F(CcuRepBufReduceTest, Translate_Sum)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 4, 1, 1, 0, sem_, len_, 1);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
}

TEST_F(CcuRepBufReduceTest, Translate_Max)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 4, 1, 1, 1, sem_, len_, 1);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepBufReduceTest, Translate_Min)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}, ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 4, 1, 1, 2, sem_, len_, 1);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepBufReduceTest, Translate_CountLessThanMin)
{
    ccu_rep_context context_;
    std::vector<ccu_buf> mem_ = {ccu_buf{&context_}};
    variable len_{&context_};
    completed_event sem_{&context_};

    ccu_rep_buf_reduce rep(&insGen, mem_, 1, 1, 1, 0, sem_, len_, 1);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepLocCpyTest, Constructor_Basic)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr1;
    variable token1;
    address addr2;
    variable token2;
    local_addr dst_{addr1, token1};
    local_addr src_{addr2, token2};

    ccu_rep_loc_cpy rep(&insGen, dst_, src_, len_, sem_, 0xF);
    EXPECT_EQ(rep.type(), ccu_rep_type::local_cpy);
    EXPECT_EQ(rep.get_mask(), 0xF);
}

TEST_F(CcuRepLocCpyTest, Constructor_WithDataType)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr1;
    variable token1;
    address addr2;
    variable token2;
    local_addr dst_{addr1, token1};
    local_addr src_{addr2, token2};

    ccu_rep_loc_cpy rep(&insGen, dst_, src_, len_, 1, 2, sem_, 0xA);
    EXPECT_EQ(rep.type(), ccu_rep_type::local_reduce);
}

TEST_F(CcuRepLocCpyTest, describe)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr1;
    variable token1;
    address addr2;
    variable token2;
    local_addr dst_{addr1, token1};
    local_addr src_{addr2, token2};

    ccu_rep_loc_cpy rep(&insGen, dst_, src_, len_, sem_, 0xF);
    std::string desc = rep.describe();
    EXPECT_NE(desc.find("local_addr"), std::string::npos);
}

TEST_F(CcuRepLocCpyTest, Translate_LocalCpy)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr1;
    variable token1;
    address addr2;
    variable token2;
    local_addr dst_{addr1, token1};
    local_addr src_{addr2, token2};

    ccu_rep_loc_cpy rep(&insGen, dst_, src_, len_, sem_, 0xF);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_channal_id[0] = 1;

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
    EXPECT_TRUE(rep.translated());
}

TEST_F(CcuRepLocCpyTest, Translate_LocalReduce)
{
    ccu_rep_context context_;
    variable len_{&context_};
    completed_event sem_{&context_};
    address addr1;
    variable token1;
    address addr2;
    variable token2;
    local_addr dst_{addr1, token1};
    local_addr src_{addr2, token2};

    ccu_rep_loc_cpy rep(&insGen, dst_, src_, len_, 1, 2, sem_, 0xF);
    ccu_instr instr_;
    ccu_instr* instrPtr = &instr_;
    uint16_t instrId = 0;
    trans_dep dep = {};
    dep.reserve_channal_id[0] = 1;

    bool result = rep.translate(nullptr, instrPtr, instrId, dep);
    EXPECT_TRUE(result);
}

TEST_F(CcuRepRemMemTest, Constructor)
{
    address addr_;
    variable token;
    remote_addr rem_{addr_, token};
    ccu_rep_rem_mem rep(&insGen, 0, rem_);
    EXPECT_EQ(rep.type(), ccu_rep_type::rem_mem);
}

TEST_F(CcuRepRemMemTest, translate)
{
    address addr_;
    variable token;
    remote_addr rem_{addr_, token};
    ccu_rep_rem_mem rep(&insGen, 0, rem_);
    EXPECT_EQ(rep.type(), ccu_rep_type::rem_mem);
}

} // namespace
} // namespace ccu_rep
} // namespace hcomm
