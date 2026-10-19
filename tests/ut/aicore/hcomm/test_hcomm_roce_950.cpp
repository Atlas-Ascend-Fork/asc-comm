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
#include <cstring>
#include <vector>
#define private public
#include "kernel_operator.h"

// The CPU simulator stubs do not move CQ/SQ data. Model the copies synchronously
// so the tests exercise the production CQE parser and validate its UB workspace.
namespace AscendC {
inline void DataCopy(const LocalTensor<uint32_t>& dst, const GlobalTensor<uint32_t>& src, const uint32_t count)
{
    ASSERT_GE(dst.GetSize(), count);
    std::memcpy(dst.GetPhyAddr(), src.GetPhyAddr(), count * sizeof(uint32_t));
}

inline void DataCopy(const GlobalTensor<uint32_t>& dst, const LocalTensor<uint32_t>& src, const uint32_t count)
{
    ASSERT_GE(src.GetSize(), count);
    std::memcpy(const_cast<uint32_t*>(dst.GetPhyAddr()), src.GetPhyAddr(), count * sizeof(uint32_t));
}
} // namespace AscendC

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
        cqCtx_.contextInfo.roceCq.cqVa = (uint64_t)cqEntries_;
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

    void SetCqe(uint32_t cqIndex, uint32_t wqebbIndex)
    {
        uint32_t cqDepth = cqCtx_.contextInfo.roceCq.cqDepth;
        auto& cqe = cqEntries_[cqIndex & (cqDepth - 1U)];
        cqe = {};
        cqe.ownerIdQpn = sqCtx_.contextInfo.roceSq.qpn |
                         (static_cast<uint32_t>((cqIndex & cqDepth) != 0U) << ROCE_1825_CQE_OWNER_SHIFT);
        cqe.opSrWqebb = static_cast<uint32_t>(HCOMM_ROCE_OP_TYPE::WRITE) << ROCE_1825_CQE_OPCODE_SHIFT;
        cqe.opSrWqebb |= wqebbIndex & ROCE_SQ_INDEX_MASK;
    }

private:
    TPipe pipe_;
    int64_t blockIdxBak_;
    TBuf<TPosition::VECOUT> hcommBuf_;
    LocalTensor<uint8_t> tempTensor_;
    uint64_t addr_;
    ChannelEntity channel_{};
    SqContext sqCtx_{};
    CqContext cqCtx_{};
    uint8_t sqVa_[1024] = {0};
    RoceCqeEntry cqEntries_[16] = {};
    uint8_t dbVa_[8] = {0};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    RegedBufferEntity localBuff_{};
    RegedBufferEntity remoteBuff_{};
};

TEST_F(HcommRoCETestSuite, Init_ReadNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t*)(addr_), 256), HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.Init((__ubuf__ uint8_t*)(addr_), 512), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = hcomm.ReadNbi(channelHandle, reinterpret_cast<GM_ADDR>(0x110), reinterpret_cast<GM_ADDR>(0x220), 10);
    EXPECT_EQ(ret, 0);
    SetCqe(0, 1);
    ret = hcomm.Drain(channelHandle);
    EXPECT_EQ(ret, 0);
}

TEST_F(HcommRoCETestSuite, Init_WriteNbi_Drain)
{
    Hcomm<AscendC::COMM_PROTOCOL_ROCE> hcomm;
    EXPECT_EQ(hcomm.Init(tempTensor_, 256), HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.Init(tempTensor_, 512), 0);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret =
        hcomm.WriteNbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = hcomm.WriteNbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channelHandle);
    EXPECT_EQ(ret, 0);
    SetCqe(0, 1);
    SetCqe(1, 2);
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
    EXPECT_EQ(ret, HCOMM_SUCCESS);
}

TEST_F(HcommRoCETestSuite, DrainWithoutNewCqeKeepsSqTail)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.cqHead = 1;
    channel_.cqTail = 1;
    channel_.sqTail = 7U;

    EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 7U);
}

TEST_F(HcommRoCETestSuite, DrainConsumesCqeWhenTargetWrapsToZero)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.sqHead = 0;
    channel_.sqTail = UINT32_MAX;
    channel_.cqHead = 0;
    channel_.cqTail = UINT32_MAX;
    SetCqe(UINT32_MAX, 0U);

    EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.cqTail, 0U);
    EXPECT_EQ(channel_.sqTail, 0U);
}

TEST_F(HcommRoCETestSuite, DrainRejectsCounterAtOrBeyondProducer)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.sqHead = 10U;
    channel_.sqTail = 9U;
    channel_.cqHead = 1;
    channel_.cqTail = 0;

    for (uint32_t counter : {10U, 11U}) {
        SCOPED_TRACE(counter);
        SetCqe(0, counter);
        EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
        EXPECT_EQ(channel_.sqTail, counter);
        EXPECT_EQ(channel_.cqTail, 1U);
        channel_.cqTail = 0U;
    }
}

TEST_F(HcommRoCETestSuite, DrainAcceptsLastOutstandingWqe)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.sqHead = 10U;
    channel_.sqTail = 7U;
    channel_.cqHead = 1;
    channel_.cqTail = 0;
    SetCqe(0, 10);

    EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 10U);
    EXPECT_EQ(channel_.cqTail, 1U);
}

TEST_F(HcommRoCETestSuite, DrainHandles16BitWqebbCounterWrap)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    channel_.sqHead = 1U;
    channel_.sqTail = UINT16_MAX - 1U;
    channel_.cqHead = 1;
    channel_.cqTail = 0;
    SetCqe(0, 1U);

    EXPECT_EQ(hcomm.Drain(reinterpret_cast<ChannelHandle>(&channel_)), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, channel_.sqHead);
    EXPECT_EQ(channel_.cqTail, 1U);
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
    channel_.sqHead = 14U;
    channel_.sqTail = 0;
    channel_.cqHead = 14;
    channel_.cqTail = 14;

    int32_t ret = hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, kRoceNoCqeCfg>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 15U);
    ret = hcomm.WriteNbi<false, PIPE_S, PIPE_MTE3, kRoceNoCqeCfg>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, HCOMM_SUCCESS);
}

TEST_F(HcommRoCETestSuite, BatchCommitChecksAccumulatedSqCapacity)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 14U;
    channel_.sqTail = 0;
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    ASSERT_EQ(
        hcomm.WriteNbi<kRoceNoCqeCfg>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 15U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);

    ASSERT_EQ(
        hcomm.WriteNbi<kRoceNoCqeCfg>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 16U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
}

TEST_F(HcommRoCETestSuite, BatchHandleAccepts32ByteAlignedBuffer)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_[32], 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    EXPECT_NE(batchHandle.channelHandle, 0U);
    EXPECT_EQ(batchHandle.buffer.bufferCapacity, 2U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
}

TEST_F(HcommRoCETestSuite, BatchHandleRejectsMisalignedBuffer)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_[1], 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    EXPECT_EQ(batchHandle.channelHandle, 0U);
    EXPECT_EQ(batchHandle.buffer.bufferCapacity, 0U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
}

TEST_F(HcommRoCETestSuite, BatchCommitPollsAndReusesWorkspaceWithoutInit)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    auto batchHandle = hcomm.MakeBatchHandle(
        reinterpret_cast<ChannelHandle>(&channel_), tempTensor_[64], 64, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);
    const uint32_t pollCount = cqCtx_.contextInfo.roceCq.cqDepth - HCOMM_POLL_CQ_THRESHOLD;

    for (uint32_t i = 0; i <= pollCount; ++i) {
        SCOPED_TRACE(i);
        ASSERT_EQ(
            hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
            HCOMM_SUCCESS);
        RoceWqeEntry expectedWqe;
        std::memcpy(&expectedWqe, batchHandle.buffer.buffer.GetPhyAddr(), sizeof(expectedWqe));
        SetCqe(i, i + 1U);
        ASSERT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_SUCCESS);
        EXPECT_EQ(std::memcmp(sqVa_ + i * HCOMM_ROCE_WQEBB_SIZE, &expectedWqe, sizeof(expectedWqe)), 0);
        EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
        if (i + 1U == pollCount) {
            // This completion was consumed by BatchCommit, before any explicit Drain.
            EXPECT_EQ(channel_.cqTail, pollCount);
            EXPECT_EQ(channel_.sqTail, pollCount);
            EXPECT_EQ(std::memcmp(batchHandle.buffer.buffer.GetPhyAddr(), &cqEntries_[i], sizeof(RoceCqeEntry)), 0);
        }
    }

    EXPECT_EQ(hcomm.Drain(batchHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.cqTail, pollCount + 1U);
    EXPECT_EQ(channel_.sqTail, pollCount + 1U);
    EXPECT_EQ(batchHandle.cursor.sqTail, channel_.sqTail);
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
    hcomm.impl_.FillCtrlSeg<ROCE_DEFAULT_CFG>(&expected[0], 0, channel_.sqContextAddr[0].contextInfo.roceSq.depth);
    hcomm.impl_.FillTaskSeg<ROCE_DEFAULT_CFG, HCOMM_ROCE_OP_TYPE::WRITE>(
        &expected[0], reinterpret_cast<GM_ADDR>(0x200), 10, batchHandle.mrKey.rKey, batchHandle.mrKey.lKey);
    hcomm.impl_.FillDataSeg(&expected[0], reinterpret_cast<GM_ADDR>(0x110), 10, batchHandle.mrKey.lKey);

    hcomm.impl_.FillCtrlSeg<kRoceNoCqeCfg>(&expected[1], 1, channel_.sqContextAddr[0].contextInfo.roceSq.depth);
    hcomm.impl_.FillTaskSeg<kRoceNoCqeCfg, HCOMM_ROCE_OP_TYPE::READ>(
        &expected[1], reinterpret_cast<GM_ADDR>(0x220), 11, batchHandle.mrKey.rKey, batchHandle.mrKey.lKey);
    hcomm.impl_.FillDataSeg(&expected[1], reinterpret_cast<GM_ADDR>(0x120), 11, batchHandle.mrKey.lKey);

    auto* actualWords = reinterpret_cast<uint64_t*>(batchHandle.buffer.buffer.GetPhyAddr());
    auto* expectedWords = reinterpret_cast<uint64_t*>(expected);
    for (uint32_t i = 0; i < 2 * HCOMM_ROCE_WQEBB_U64_NUM; ++i) {
        EXPECT_EQ(actualWords[i], expectedWords[i]) << "word=" << i;
    }
}

TEST_F(HcommRoCETestSuite, BatchPostSendSharesPublicHeaderAcrossSges)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ASSERT_EQ(hcomm.Init(tempTensor_, 512), HCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 256, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    BufDesc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
    };
    ASSERT_EQ(hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), HCOMM_SUCCESS);

    auto* words = reinterpret_cast<uint64_t*>(batchHandle.buffer.buffer.GetPhyAddr());
    RoceWqeEntry expected{};
    hcomm.impl_.FillCtrlSeg<ROCE_DEFAULT_CFG>(&expected, 0, channel_.sqContextAddr[0].contextInfo.roceSq.depth);
    expected.ctrl.wfBdsl &=
        ~HtoNS(static_cast<uint16_t>(0xffU << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    expected.ctrl.wfBdsl |=
        HtoNS(static_cast<uint16_t>(2U << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    hcomm.impl_.FillTaskSeg<ROCE_DEFAULT_CFG, HCOMM_ROCE_OP_TYPE::WRITE>(
        &expected, reinterpret_cast<GM_ADDR>(0x200), 21, batchHandle.mrKey.rKey, batchHandle.mrKey.lKey);
    auto* expectedWords = reinterpret_cast<uint64_t*>(&expected);
    for (uint32_t i = 0; i < 6; ++i) {
        EXPECT_EQ(words[i], expectedWords[i]) << "public word=" << i;
    }

    constexpr uint32_t sgeWordNum = sizeof(RoceWqeDataSeg) / sizeof(uint64_t);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint64_t* sge = words + 6 + i * sgeWordNum;
        EXPECT_EQ(sge[0], HtoNLL(reinterpret_cast<uint64_t>(descs[i].addr))) << "SGE address=" << i;
        EXPECT_EQ(sge[1] & 0xffffffffU, HtoNL(descs[i].len)) << "SGE length=" << i;
        const uint64_t lastSgeBit = i == 1U ? 0x80U : 0U;
        EXPECT_EQ((sge[1] >> 32U) & 0x80U, lastSgeBit) << "SGE last marker=" << i;
    }
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
}

TEST_F(HcommRoCETestSuite, BatchPostSendMaintainsWqebbCursor)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 14U;
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_, 256, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    BufDesc descs[4] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
        {reinterpret_cast<GM_ADDR>(0x130), 12},
        {reinterpret_cast<GM_ADDR>(0x140), 13},
    };
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 4), HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x150), 10),
        HCOMM_SUCCESS);

    const auto* words = reinterpret_cast<const uint64_t*>(batchHandle.buffer.buffer.GetPhyAddr());
    constexpr uint32_t firstWqeWqebb = 1;
    constexpr uint32_t multiWqeWqebb = 2;
    constexpr uint64_t ownerMask = 1ULL << ROCE_1825_WQE_OWNER_SHIFT;
    EXPECT_EQ(words[0] & ownerMask, 0U) << "owner before SQ wrap";
    EXPECT_EQ(words[(firstWqeWqebb + multiWqeWqebb) * HCOMM_ROCE_WQEBB_U64_NUM] & ownerMask, ownerMask)
        << "owner after SQ wrap";
    EXPECT_EQ(batchHandle.cursor.sqHead, 14U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 4U);
}

TEST_F(HcommRoCETestSuite, BatchCommitPreservesWqebbCounter)
{
    Hcomm<COMM_PROTOCOL_ROCE> hcomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = hcomm.MakeBatchHandle(
        channelHandle, tempTensor_[128], 128, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batchHandle.channelHandle, 0U);

    BufDesc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
    };
    ASSERT_EQ(hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 2U);
    SetCqe(0, 2);
    ASSERT_EQ(hcomm.Drain(channelHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 2U);

    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        HCOMM_SUCCESS);
    const auto* words = reinterpret_cast<const uint64_t*>(batchHandle.buffer.buffer.GetPhyAddr());
    EXPECT_EQ(batchHandle.cursor.sqHead, 2U);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 3U);
    SetCqe(1, 3);
    ASSERT_EQ(hcomm.Drain(channelHandle), HCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 3U);
}
