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
#include "kernel_operator.h"
#include "hcomm/hcomm.h"

using namespace std;
using namespace AscendC;

static constexpr UrmaWqeEntry kRoceNoCqeCfg = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};

class HcommRoCETestSuite : public testing::Test {
protected:
    virtual void SetUp()
    {
        blockIdxBak_ = block_idx;
        pipe_.InitBuffer(hcommBuf_, 512);
        tempTensor_ = hcommBuf_.Get<uint8_t>();
        addr_ = (uint64_t)(tempTensor_.GetPhyAddr());
        channel_.sqNum = 1;
        channel_.cqNum = 1;
        channel_.protocol = COMM_PROTOCOL_ROCE;
        sqCtx_.contextInfo.roceSq.sqVa = (uint64_t)sqVa_;
        sqCtx_.contextInfo.roceSq.dbHwVa = (uint64_t)dbVa_;
        sqCtx_.contextInfo.roceSq.dbSwVa = (uint64_t)dbVa_;
        sqCtx_.contextInfo.roceSq.wqeSize = 64;
        sqCtx_.contextInfo.roceSq.depth = 16;
        sqCtx_.contextInfo.roceSq.qpn = 1;
        sqCtx_.contextInfo.roceSq.headAddr = (uint64_t)(&head_);
        sqCtx_.contextInfo.roceSq.tailAddr = (uint64_t)(&tail_);
        sqCtx_.contextInfo.roceSq.sl = 1;
        sqCtx_.contextInfo.roceSq.dbVendorSpecified = 1;
        channel_.sqContextAddr = &sqCtx_;
        cqCtx_.contextInfo.roceCq.cqVa = (uint64_t)sqVa_;
        cqCtx_.contextInfo.roceCq.dbHwVa = (uint64_t)dbVa_;
        cqCtx_.contextInfo.roceCq.dbSwVa = (uint64_t)dbVa_;
        cqCtx_.contextInfo.roceCq.cqeSize = sizeof(RoceCqeEntry);
        cqCtx_.contextInfo.roceCq.cqDepth = 16;
        cqCtx_.contextInfo.roceCq.cqn = 1;
        cqCtx_.contextInfo.roceCq.tailAddr = (uint64_t)(&tail_);
        channel_.cqContextAddr = &cqCtx_;
        channel_.localBufferNum = 1;
        localBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        localBuff_.bufferInfo.rma.addr = 0x100;
        localBuff_.bufferInfo.rma.size = 100;
        localBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.lkey = 123456;
        channel_.localBufferAddr = &localBuff_;
        channel_.remoteBufferNum = 1;
        remoteBuff_.type = RegedBufferType::REGED_BUFFER_RMA;
        remoteBuff_.bufferInfo.rma.addr = 0x200;
        remoteBuff_.bufferInfo.rma.size = 100;
        remoteBuff_.bufferInfo.rma.protectionInfo.memInfo.roce.rkey = 123456;
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
    uint8_t sqVa_[1024] = {0};
    uint8_t dbVa_[8] = {0};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    RegedBufferEntity localBuff_;
    RegedBufferEntity remoteBuff_;
};

TEST_F(HcommRoCETestSuite, Init_ReadNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t*)(addr_), 256), -1);
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t*)(addr_), 512), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.ReadNbi(channelHandle, reinterpret_cast<GM_ADDR>(0x110), reinterpret_cast<GM_ADDR>(0x220), 10);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Drain(channelHandle);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommRoCETestSuite, Init_WriteNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init(tempTensor_, 256), -1);
    EXPECT_EQ(hcomm.Init(tempTensor_, 512), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret =
        hcomm.WriteNbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = hcomm.WriteNbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channelHandle);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Drain(channelHandle);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommRoCETestSuite, Init_WriteWithNotifyNbi)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init(tempTensor_, 512), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.WriteWithNotifyNbi<false>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10,
        reinterpret_cast<GM_ADDR>(0x310), 10);
    EXPECT_EQ(ret, -1);
}

TEST_F(HcommRoCETestSuite, DrainWithoutNewCqeKeepsSqTail)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.cqHead = 1;
    channel_.cqTail = 1;
    channel_.sqTail = 7;

    EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 7U);
}

TEST_F(HcommRoCETestSuite, WriteNbiWithoutCqeDoesNotAdvanceCqHead)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 0;
    channel_.sqTail = 0;
    channel_.cqHead = 0;
    channel_.cqTail = 0;

    int32_t ret = hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, kRoceNoCqeCfg>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 1U);
    EXPECT_EQ(channel_.cqHead, 0U);
}

TEST_F(HcommRoCETestSuite, PostSendKeepsOneSqSlotForInvalidMarker)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 14;
    channel_.sqTail = 0;
    channel_.cqHead = 14;
    channel_.cqTail = 14;

    int32_t ret = hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, kRoceNoCqeCfg>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 15U);
    ret = hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, kRoceNoCqeCfg>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, HCOMM_FAILED);
}

TEST_F(HcommRoCETestSuite, BatchCommitChecksAccumulatedSqCapacity)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 14;
    channel_.sqTail = 0;
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    EXPECT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_FAILED);
}

TEST_F(HcommRoCETestSuite, BatchPostSendUsesWqebbStride)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x120), 10),
        HCOMM_SUCCESS);

    auto* firstWqe = reinterpret_cast<RoceWqeEntry*>(batchHandle.buffer.buffer.GetPhyAddr());
    auto* secondWqe = firstWqe + 1;
    EXPECT_EQ(secondWqe->task.dataLen, firstWqe->task.dataLen);
    EXPECT_NE(secondWqe->data.vaLocal, firstWqe->data.vaLocal);
}

TEST_F(HcommRoCETestSuite, BatchPostSendUses64BitWqeEncoding)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 0;
    channel_.sqTail = 0;
    channel_.cqHead = 0;
    channel_.cqTail = 0;
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 256, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.ReadNbi<kRoceNoCqeCfg>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x120), reinterpret_cast<GM_ADDR>(0x220), 11),
        HCOMM_SUCCESS);

    RoceWqeEntry expected[2]{};
    hcomm.impl_.FillCtrlSeg(&expected[0], 0, channel_.sqContextAddr[0].contextInfo.roceSq.depth, 1);
    hcomm.impl_.FillTaskSeg(
        &expected[0], reinterpret_cast<GM_ADDR>(0x200), 10, static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::WRITE),
        batchHandle.mrKey.rKey, batchHandle.mrKey.lKey, ROCE_DEFAULT_CFG.fence);
    hcomm.impl_.FillDataSeg(&expected[0], reinterpret_cast<GM_ADDR>(0x110), 10, batchHandle.mrKey.lKey);

    hcomm.impl_.FillCtrlSeg(&expected[1], 1, channel_.sqContextAddr[0].contextInfo.roceSq.depth, 0);
    hcomm.impl_.FillTaskSeg(
        &expected[1], reinterpret_cast<GM_ADDR>(0x220), 11, static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::READ),
        batchHandle.mrKey.rKey, batchHandle.mrKey.lKey, kRoceNoCqeCfg.fence);
    hcomm.impl_.FillDataSeg(&expected[1], reinterpret_cast<GM_ADDR>(0x120), 11, batchHandle.mrKey.lKey);

    auto* actualWords = reinterpret_cast<uint64_t*>(batchHandle.buffer.buffer.GetPhyAddr());
    auto* expectedWords = reinterpret_cast<uint64_t*>(expected);
    for (uint32_t i = 0; i < 2 * HCOMM_ROCE_WQEBB_U64_NUM; ++i) {
        EXPECT_EQ(actualWords[i], expectedWords[i]) << "word=" << i;
    }
}
