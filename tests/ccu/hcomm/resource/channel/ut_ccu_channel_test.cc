/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/channel_/ccu_channel.h"

#include "ccu_channel_get_stub.h"
#include "gtest/gtest.h"

namespace hcomm {
namespace {

class CcuChannelTest : public testing::Test {
protected:
    void SetUp() override
    {
        channel_pod_.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
        channel_pod_.header.magic_word = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
        channel_pod_.header.size = sizeof(HcommCcuChannelPod);
        channel_pod_.die_id = 3;
        channel_pod_.channel_id = 17;
        channel_pod_.local_xn_ids[0] = 21;
        channel_pod_.local_xn_ids[1] = 22;
        channel_pod_.remote_xn_ids[0] = 121;
        channel_pod_.remote_xn_ids[1] = 122;
        channel_pod_.local_cke_ids[0] = 11;
        channel_pod_.local_cke_ids[1] = 12;
        channel_pod_.remote_cke_ids[0] = 111;
        channel_pod_.remote_cke_ids[1] = 112;
        channel_pod_.rmt_ccu_buf_addr = 0x123456789ABCDEF0ULL;
        channel_pod_.rmt_ccu_buf_size = 0x8000;
        channel_pod_.rmt_ccu_buf_token_id = 0x51;
        channel_pod_.rmt_ccu_buf_token_value = 0x62;
        SetHcommCcuChannelGetPodStub(channel_pod_);
    }

    void TearDown() override
    {
        ResetHcommCcuChannelGetPodStub();
    }

    HcommCcuChannelPod channel_pod_{};
};

TEST_F(CcuChannelTest, InitCopiesAllFieldsAndResources)
{
    ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);
    EXPECT_EQ(channel_.get_die_id(), 3U);
    EXPECT_EQ(channel_.get_channel_id(), 17U);

    uint32_t value = 0;
    EXPECT_EQ(channel_.get_loc_cke_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 11U);
    EXPECT_EQ(channel_.get_loc_xn_by_index(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 22U);
    EXPECT_EQ(channel_.get_rmt_cke_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 111U);
    EXPECT_EQ(channel_.get_rmt_xn_by_index(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 122U);

    uint64_t addr_ = 0;
    uint32_t size = 0;
    uint32_t token_id = 0;
    uint32_t token_value = 0;
    EXPECT_EQ(channel_.get_rmt_buffer(addr_, size, token_id, token_value), HCCL_SUCCESS);
    EXPECT_EQ(addr_, 0x123456789ABCDEF0ULL);
    EXPECT_EQ(size, 0x8000U);
    EXPECT_EQ(token_id, 0x51U);
    EXPECT_EQ(token_value, 0x62U);
}

TEST_F(CcuChannelTest, SnapshotOwnsDeepCopy)
{
    ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);

    channel_pod_.die_id = 99;
    channel_pod_.local_cke_ids[0] = 999;
    SetHcommCcuChannelGetPodStub(channel_pod_);

    uint32_t local_cke_id = 0;
    EXPECT_EQ(channel_.get_die_id(), 3U);
    EXPECT_EQ(channel_.get_loc_cke_by_index(0, local_cke_id), HCCL_SUCCESS);
    EXPECT_EQ(local_cke_id, 11U);
}

TEST_F(CcuChannelTest, ResourceIndexOutOfRangeReturnsParameterError)
{
    ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);
    uint32_t value = 0;
    EXPECT_EQ(channel_.get_loc_cke_by_index(HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY, value), HCCL_E_PARA);
    EXPECT_EQ(channel_.get_loc_xn_by_index(HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY, value), HCCL_E_PARA);
    EXPECT_EQ(channel_.get_rmt_cke_by_index(HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY, value), HCCL_E_PARA);
    EXPECT_EQ(channel_.get_rmt_xn_by_index(HCOMM_CCU_CHANNEL_RESOURCE_CAPACITY, value), HCCL_E_PARA);
}

TEST_F(CcuChannelTest, FirstQueryFailureIsPropagated)
{
    SetHcommCcuChannelGetPodStubResult(HCCL_E_UNAVAIL);
    ccu_channel channel_(1);
    EXPECT_EQ(channel_.get_result(), HCCL_E_UNAVAIL);

    uint32_t value = 0;
    EXPECT_EQ(channel_.get_loc_cke_by_index(0, value), HCCL_E_UNAVAIL);
}

TEST_F(CcuChannelTest, RemoteBufferSizeZeroReturnsInternalError)
{
    channel_pod_.rmt_ccu_buf_size = 0;
    SetHcommCcuChannelGetPodStub(channel_pod_);
    ccu_channel channel_(1);
    EXPECT_EQ(channel_.get_result(), HCCL_E_INTERNAL);
}

} // namespace
} // namespace hcomm
