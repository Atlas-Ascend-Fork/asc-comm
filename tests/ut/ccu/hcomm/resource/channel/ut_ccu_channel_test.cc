/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the License).
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "hcomm/resource/channel/ccu_channel.h"

#include "ccu_channel_get_stub.h"
#include "gtest/gtest.h"

namespace asc {
namespace {

HcommCcuChannelPod MakeChannelPod()
{
    HcommCcuChannelPod pod{};
    pod.header.version = HCOMM_CCU_CHANNEL_ABI_VERSION;
    pod.header.magicWord = HCOMM_CCU_CHANNEL_POD_MAGIC_WORD;
    pod.header.size = sizeof(HcommCcuChannelPod);
    pod.dieId = 3;
    pod.channelId = 17;
    pod.localVarNum = 2;
    pod.remoteVarNum = 2;
    pod.localEventNum = 2;
    pod.remoteEventNum = 2;
    pod.localVarIds[0] = 21;
    pod.localVarIds[1] = 22;
    pod.remoteVarIds[0] = 121;
    pod.remoteVarIds[1] = 122;
    pod.localEventIds[0] = 11;
    pod.localEventIds[1] = 12;
    pod.remoteEventIds[0] = 111;
    pod.remoteEventIds[1] = 112;
    pod.rmtCcuResBuffer.type = REGED_BUFFER_RMA;
    pod.rmtCcuResBuffer.bufferInfo.rma.addr = 0x123456789ABCDEF0ULL;
    pod.rmtCcuResBuffer.bufferInfo.rma.size = 0x8000;
    pod.rmtCcuResBuffer.bufferInfo.rma.protectionInfo.type = PROTECTION_TYPE_UB;
    pod.rmtCcuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 0x51;
    pod.rmtCcuResBuffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = 0x62;
    return pod;
}

class CcuChannelTest : public testing::Test {
protected:
    void SetUp() override
    {
        channel_pod_ = MakeChannelPod();
        SetHcommCcuChannelGetPodStub(channel_pod_);
    }

    void TearDown() override { ResetHcommCcuChannelGetPodStub(); }

    HcommCcuChannelPod channel_pod_{};
};

TEST_F(CcuChannelTest, InitCopiesAllFieldsAndResources)
{
    asc::ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);
    EXPECT_EQ(channel_->get_die_id(), 3U);
    EXPECT_EQ(channel_->get_channel_id(), 17U);

    uint32_t value = 0;
    EXPECT_EQ(channel_->get_loc_cke_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 11U);
    EXPECT_EQ(channel_->get_loc_cke_by_index(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 12U);
    EXPECT_EQ(channel_->get_rmt_cke_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 111U);
    EXPECT_EQ(channel_->get_loc_xn_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 21U);
    EXPECT_EQ(channel_->get_loc_xn_by_index(1, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 22U);
    EXPECT_EQ(channel_->get_rmt_xn_by_index(0, value), HCCL_SUCCESS);
    EXPECT_EQ(value, 121U);
}

TEST_F(CcuChannelTest, GetRmtBufferDecodesRegedBufferEntity)
{
    asc::ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);

    uint64_t addr = 0;
    uint32_t size = 0;
    uint32_t token_id = 0;
    uint32_t token_value = 0;
    EXPECT_EQ(channel_->get_rmt_buffer(addr, size, token_id, token_value), HCCL_SUCCESS);
    EXPECT_EQ(addr, 0x123456789ABCDEF0ULL);
    EXPECT_EQ(size, 0x8000U);
    EXPECT_EQ(token_id, 0x51U);
    EXPECT_EQ(token_value, 0x62U);
}

TEST_F(CcuChannelTest, IndexBeyondValidNumRejected)
{
    asc::ccu_channel channel_(1);
    ASSERT_EQ(channel_.get_result(), HCCL_SUCCESS);

    // 有效数量 localVarNum=2：index 2 越界（容量 16 也允许更大的有效数量）
    uint32_t value = 0;
    EXPECT_NE(channel_->get_loc_xn_by_index(2, value), HCCL_SUCCESS);
    EXPECT_NE(channel_->get_rmt_cke_by_index(2, value), HCCL_SUCCESS);
}

TEST_F(CcuChannelTest, UnreadyRemoteBufferRejected)
{
    HcommCcuChannelPod pod = MakeChannelPod();
    pod.rmtCcuResBuffer.bufferInfo.rma.size = 0; // 远端资源空间未就绪
    SetHcommCcuChannelGetPodStub(pod);
    asc::ccu_channel channel_(1);
    EXPECT_NE(channel_.get_result(), HCCL_SUCCESS);
}

} // namespace
} // namespace asc
