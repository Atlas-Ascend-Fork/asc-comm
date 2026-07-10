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
#include <vector>
#define private public
#include "hcomm/hcomm.h"
#include "kernel_operator.h"

using namespace std;
using namespace AscendC;

namespace {

constexpr uint32_t COMMON_SQ_WQE_SIZE = 48;
constexpr uint32_t COMMON_QUEUE_DEPTH = 10;
constexpr uint32_t COMMON_SQ_BUFFER_SIZE = 100;
constexpr uint32_t COMMON_DB_BUFFER_SIZE = 8;
constexpr uint32_t COMMON_ROCE_LKEY = 123456;
constexpr uint32_t COMMON_ROCE_RKEY = 123456;
constexpr uint64_t COMMON_NOTIFY_VALUE = 2;

class HcommCommonTestSuite : public testing::Test {
protected:
    virtual void SetUp()
    {
        blockIdxBak_ = block_idx;
        channel_.sqNum = 1;
        sqCtx_.contextInfo.roceSq.sqVa = (uint64_t)sqVa_;
        sqCtx_.contextInfo.roceSq.dbVa = (uint64_t)dbVa_;
        sqCtx_.contextInfo.roceSq.wqeSize = COMMON_SQ_WQE_SIZE;
        sqCtx_.contextInfo.roceSq.depth = COMMON_QUEUE_DEPTH;
        sqCtx_.contextInfo.roceSq.qpn = 1;
        sqCtx_.contextInfo.roceSq.headAddr = (uint64_t)(&head_);
        sqCtx_.contextInfo.roceSq.tailAddr = (uint64_t)(&tail_);
        sqCtx_.contextInfo.roceSq.dbMode = 0;
        sqCtx_.contextInfo.roceSq.sl = 1;
        channel_.sqContextAddr = &sqCtx_;
        channel_.localBufferNum = 1;
        localBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        localBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.lkey = COMMON_ROCE_LKEY;
        channel_.localBufferAddr = &localBuff_;
        channel_.remoteBufferNum = 1;
        remoteBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        remoteBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.rkey = COMMON_ROCE_RKEY;
        channel_.remoteBufferAddr = &remoteBuff_;
    }
    virtual void TearDown() { block_idx = blockIdxBak_; }

private:
    int64_t blockIdxBak_;
    ChannelEntity channel_;
    SqContext sqCtx_;
    uint8_t sqVa_[COMMON_SQ_BUFFER_SIZE] = {0};
    uint8_t dbVa_[COMMON_DB_BUFFER_SIZE] = {0};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    RegedBufferEntity localBuff_;
    RegedBufferEntity remoteBuff_;
};

TEST_F(HcommCommonTestSuite, Aiv_Read)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.ReadNbi(channelHandle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommCommonTestSuite, Aiv_Write)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.WriteNbi(channelHandle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommCommonTestSuite, Aiv_WriteWithNotifyNbi)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.WriteWithNotifyNbi(
        channelHandle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1,
        reinterpret_cast<GM_ADDR>(0x33), COMMON_NOTIFY_VALUE);
    EXPECT_EQ(ret, -1);
}

} // namespace
