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

constexpr uint32_t ROCE_VALID_TMP_BUF_SIZE = AscendC::HCOMM_UB_BUF_SIZE;
constexpr uint32_t ROCE_INVALID_TMP_BUF_SIZE = ROCE_VALID_TMP_BUF_SIZE / 2;
constexpr uint32_t ROCE_SQ_WQE_SIZE = 48;
constexpr uint32_t ROCE_CQE_SIZE = 32;
constexpr uint32_t ROCE_QUEUE_DEPTH = 10;
constexpr uint32_t ROCE_SQ_BUFFER_SIZE = 100;
constexpr uint32_t ROCE_DB_BUFFER_SIZE = 8;
constexpr uint32_t ROCE_BUFFER_SIZE = 100;
constexpr uint32_t ROCE_TRANSFER_SIZE = 10;
constexpr uint32_t ROCE_NOTIFY_SIZE = 10;
constexpr uint32_t ROCE_LKEY = 123456;
constexpr uint32_t ROCE_RKEY = 123456;

class HcommRoCETestSuite : public testing::Test {
protected:
    virtual void SetUp()
    {
        blockIdxBak_ = block_idx;
        pipe_.InitBuffer(hcommBuf_, ROCE_VALID_TMP_BUF_SIZE);
        tempTensor_ = hcommBuf_.Get<uint8_t>();
        addr_ = (uint64_t)(tempTensor_.GetPhyAddr());
        channel_.sqNum = 1;
        channel_.cqNum = 1;
        sqCtx_.contextInfo.roceSq.sqVa = (uint64_t)sqVa_;
        sqCtx_.contextInfo.roceSq.dbVa = (uint64_t)dbVa_;
        sqCtx_.contextInfo.roceSq.wqeSize = ROCE_SQ_WQE_SIZE;
        sqCtx_.contextInfo.roceSq.depth = ROCE_QUEUE_DEPTH;
        sqCtx_.contextInfo.roceSq.qpn = 1;
        sqCtx_.contextInfo.roceSq.headAddr = (uint64_t)(&head_);
        sqCtx_.contextInfo.roceSq.tailAddr = (uint64_t)(&tail_);
        sqCtx_.contextInfo.roceSq.dbMode = 0;
        sqCtx_.contextInfo.roceSq.sl = 1;
        channel_.sqContextAddr = &sqCtx_;
        cqCtx_.contextInfo.roceCq.cqVa = (uint64_t)sqVa_;
        cqCtx_.contextInfo.roceCq.cqeSize = ROCE_CQE_SIZE;
        cqCtx_.contextInfo.roceCq.cqDepth = ROCE_QUEUE_DEPTH;
        cqCtx_.contextInfo.roceCq.cqn = 1;
        cqCtx_.contextInfo.roceCq.tailAddr = (uint64_t)(&tail_);
        channel_.cqContextAddr = &cqCtx_;
        channel_.localBufferNum = 1;
        localBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        localBuff_.bufferInfo.rma.addr = 0x100;
        localBuff_.bufferInfo.rma.size = ROCE_BUFFER_SIZE;
        localBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.lkey = ROCE_LKEY;
        channel_.localBufferAddr = &localBuff_;
        channel_.remoteBufferNum = 1;
        remoteBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        remoteBuff_.bufferInfo.rma.addr = 0x200;
        remoteBuff_.bufferInfo.rma.size = ROCE_BUFFER_SIZE;
        remoteBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.rkey = ROCE_RKEY;
        channel_.remoteBufferAddr = &remoteBuff_;
    }
    virtual void TearDown()
    {
        block_idx = blockIdxBak_;
        hcommBuf_.FreeTensor(tempTensor_);
    }

private:
    TPipe pipe_;
    int64_t blockIdxBak_;
    TBuf<TPosition::VECOUT> hcommBuf_;
    LocalTensor<uint8_t> tempTensor_;
    uint64_t addr_;
    ChannelEntity channel_;
    SqContext sqCtx_;
    CqContext cqCtx_;
    uint8_t sqVa_[ROCE_SQ_BUFFER_SIZE] = {0};
    uint8_t dbVa_[ROCE_DB_BUFFER_SIZE] = {0};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    RegedBufferEntity localBuff_;
    RegedBufferEntity remoteBuff_;
};

TEST_F(HcommRoCETestSuite, Init_ReadNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t *)(addr_), ROCE_INVALID_TMP_BUF_SIZE), -1);
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t *)(addr_), ROCE_VALID_TMP_BUF_SIZE), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret
        = hcomm.ReadNbi(channelHandle, reinterpret_cast<GM_ADDR>(0x110),
            reinterpret_cast<GM_ADDR>(0x220), ROCE_TRANSFER_SIZE);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Drain(channelHandle);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommRoCETestSuite, Init_WriteNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init(tempTensor_, ROCE_INVALID_TMP_BUF_SIZE), -1);
    EXPECT_EQ(hcomm.Init(tempTensor_, ROCE_VALID_TMP_BUF_SIZE), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.WriteNbi<false>(channelHandle,
        reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), ROCE_TRANSFER_SIZE);
    EXPECT_EQ(ret, 0);
    ret = hcomm.WriteNbi<false>(channelHandle,
        reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), ROCE_TRANSFER_SIZE);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channelHandle);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Drain(channelHandle);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommRoCETestSuite, Init_WriteWithNotifyNbi)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init(tempTensor_, ROCE_VALID_TMP_BUF_SIZE), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.WriteWithNotifyNbi<false>(channelHandle,
        reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), ROCE_TRANSFER_SIZE,
        reinterpret_cast<GM_ADDR>(0x310), ROCE_NOTIFY_SIZE);
    EXPECT_EQ(ret, -1);
}

} // namespace
