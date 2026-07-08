/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <array>
#include <gtest/gtest.h>
#include <vector>
#define private public
#include "hcomm/hcomm.h"
#include "kernel_operator.h"

using namespace AscendC;

namespace {

constexpr uint32_t URMA_SQ_DEPTH = 10;
constexpr uint32_t URMA_WQE_SIZE = 64;
constexpr uint32_t URMA_CQE_SIZE = 64;
constexpr uint32_t URMA_BUFFER_NUM = 2;

class UrmaChannelResource {
public:
    UrmaChannelResource() : sqBuffer_(URMA_SQ_DEPTH * URMA_WQE_SIZE, 0), cqBuffer_(URMA_SQ_DEPTH * URMA_CQE_SIZE, 0)
    {
        channel_.engine = COMM_ENGINE_AIV;
        channel_.protocol = COMM_PROTOCOL_UB_MEM;
        channel_.sqNum = 1;
        channel_.cqNum = 1;
        channel_.remoteBufferNum = URMA_BUFFER_NUM;
        channel_.localBufferNum = URMA_BUFFER_NUM;
        channel_.sqContextAddr = &sqCtx_;
        channel_.cqContextAddr = &cqCtx_;
        channel_.remoteBufferAddr = remoteBuffers_.data();
        channel_.localBufferAddr = localBuffers_.data();

        sqCtx_.type = AscendC::SQ_CONTEXT_TYPE_UB_JFS;
        sqCtx_.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sqBuffer_.data());
        sqCtx_.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&sqHead_);
        sqCtx_.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&sqTail_);
        sqCtx_.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sqDoorbell_);
        sqCtx_.contextInfo.ubJfs.jfsID = 1;
        sqCtx_.contextInfo.ubJfs.wqeSize = URMA_WQE_SIZE;
        sqCtx_.contextInfo.ubJfs.sqDepth = URMA_SQ_DEPTH;
        sqCtx_.contextInfo.ubJfs.tpID = 1;

        cqCtx_.type = AscendC::CQ_CONTEXT_TYPE_UB_JFC;
        cqCtx_.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cqBuffer_.data());
        cqCtx_.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&cqHead_);
        cqCtx_.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cqTail_);
        cqCtx_.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cqDoorbell_);
        cqCtx_.contextInfo.ubJfc.jfcID = 1;
        cqCtx_.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        cqCtx_.contextInfo.ubJfc.cqDepth = URMA_SQ_DEPTH;

        InitBuffer(remoteBuffers_[0], 0x1000, 0x1000, 0x123456, 0x654321);
        InitBuffer(remoteBuffers_[1], 0x3000, 0x1000, 0x223456, 0x754321);
        InitBuffer(localBuffers_[0], 0x2000, 0x1000, 0x111111, 0x222222);
        InitBuffer(localBuffers_[1], 0x5000, 0x1000, 0x333333, 0x444444);
    }

    AscendC::ChannelHandle GetHandle()
    {
        return reinterpret_cast<AscendC::ChannelHandle>(&channel_);
    }

    uint32_t GetSqHead() const
    {
        return sqHead_;
    }

    void CompleteCurrentSq()
    {
        cqTail_ = sqHead_;
        sqTail_ = sqHead_;
    }

private:
    void InitBuffer(
        AscendC::RegedBufferEntity &buffer, uint64_t addr, uint64_t size, uint32_t tokenId, uint32_t tokenValue)
    {
        buffer.type = AscendC::REGED_BUFFER_RMA;
        buffer.bufferInfo.rma.addr = addr;
        buffer.bufferInfo.rma.size = size;
        buffer.bufferInfo.rma.protectionInfo.type = AscendC::PROTECTION_TYPE_UB;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    AscendC::ChannelEntity channel_ = {};
    AscendC::SqContext sqCtx_ = {};
    AscendC::CqContext cqCtx_ = {};
    std::array<AscendC::RegedBufferEntity, URMA_BUFFER_NUM> remoteBuffers_ = {};
    std::array<AscendC::RegedBufferEntity, URMA_BUFFER_NUM> localBuffers_ = {};
    std::vector<uint8_t> sqBuffer_;
    std::vector<uint8_t> cqBuffer_;
    uint32_t sqHead_ = 0;
    uint32_t sqTail_ = 0;
    uint32_t cqHead_ = 0;
    uint32_t cqTail_ = 0;
    uint32_t sqDoorbell_ = 0;
    uint32_t cqDoorbell_ = 0;
};

} // namespace

class HcommUrmaTestSuite : public testing::Test {
protected:
    void SetUp() override
    {
        blockIdxBak_ = block_idx;
    }

    void TearDown() override
    {
        block_idx = blockIdxBak_;
    }

    int32_t InitHcomm(AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> &hcomm)
    {
        pipe_.InitBuffer(hcommBuf_, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
        AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf_.Get<uint8_t>();
        __ubuf__ uint8_t *bufPtr = reinterpret_cast<__ubuf__ uint8_t *>(hcommLocal.GetPhyAddr());
        for (uint32_t i = 0; i < AscendC::HCOMM_URMA_TMP_BUF_SIZE; i++) {
            bufPtr[i] = 0;
        }
        return hcomm.Init(bufPtr, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    }

private:
    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    int64_t blockIdxBak_;
};

// ReadNbi with default commit=true: auto-commit internally, then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_Read)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret
        = hcomm.ReadNbi(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x5008), reinterpret_cast<GM_ADDR>(0x3008), 8);
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteNbi with commit=false: explicit Commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_Write)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channel.GetHandle());
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteWithNotifyNbi with commit=false: explicit Commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithNotify)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret = hcomm.WriteWithNotifyNbi<false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008),
        reinterpret_cast<GM_ADDR>(0x5008), 8, reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channel.GetHandle());
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteWithNotifyNbi occupies 2 BBs, WriteNbi occupies 1 BB
// Verified by reading sqHead from channel's headAddr (where st_dev writes
// curHead)
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithNotify_WqeBbCnt)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    // WriteNbi should advance sqHead by 1
    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 1U);

    // WriteWithNotifyNbi should advance sqPI by 2
    ret = hcomm.WriteWithNotifyNbi<false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008),
        reinterpret_cast<GM_ADDR>(0x5008), 8, reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 3U);

    // Another WriteNbi should advance by 1 more
    ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 4U);
}

// Remote buffer lookup failure when address is out of range
TEST_F(HcommUrmaTestSuite, Aiv_Urma_RemoteBufferNotFound)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    // Address completely outside any remote buffer range (remote buffers are at
    // 0x1000-0x2000 and 0x3000-0x4000)
    int32_t h = hcomm.WriteWithNotifyNbi<false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x9000),
        reinterpret_cast<GM_ADDR>(0x5008), 8, reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(h, AscendC::HCOMM_FAILED);

    // Address in range but len exceeds buffer boundary
    h = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1FF0), reinterpret_cast<GM_ADDR>(0x2008), 0x100);
    EXPECT_EQ(h, AscendC::HCOMM_FAILED);
}

// Batch: mixed Write + WriteWithNotify, single channel Commit/Drain covers all
TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchCommitDrain)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ret = hcomm.WriteWithNotifyNbi<false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008),
        reinterpret_cast<GM_ADDR>(0x5008), 8, reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1018), reinterpret_cast<GM_ADDR>(0x2018), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);

    EXPECT_EQ(hcomm.Commit(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
    channel.CompleteCurrentSq();
    EXPECT_EQ(hcomm.Drain(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
}

// Init with LocalTensor overload: uses LocalTensor directly without
// TBuffAddr/SetAddr
TEST_F(HcommUrmaTestSuite, Aiv_Urma_InitLocalTensor)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm;

    // Allocate buffer via TPipe and get LocalTensor
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> buf;
    pipe.InitBuffer(buf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> localBuf = buf.Get<uint8_t>();

    // Init with LocalTensor should succeed
    EXPECT_EQ(hcomm.Init(localBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE), AscendC::HCOMM_SUCCESS);

    // Init with insufficient length should fail
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UBC_CTP> hcomm2;
    EXPECT_EQ(hcomm2.Init(localBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE - 1), AscendC::HCOMM_FAILED);

    // Verify the initialized hcomm can perform operations
    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.Commit(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
    channel.CompleteCurrentSq();
    EXPECT_EQ(hcomm.Drain(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
}

// Dump helpers: nullptr early-return guards.
// These guards are never reached through the normal send path, so exercise them
// directly.
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpHelpers_Nullptr)
{
    AscendC::HcommUrmaDumpWqeCtx(nullptr, sizeof(uint8_t));
    AscendC::HcommUrmaDumpCqeCtx(nullptr);
    AscendC::HcommUrmaDumpSgeCtx(nullptr, nullptr, sizeof(uint8_t));
    AscendC::HcommUrmaDumpAmoCtx(nullptr, sizeof(uint32_t));

    // sqeCtx non-null but sgeAddr null still hits the guard
    std::vector<uint8_t> sqeBuf(sizeof(AscendC::HcommUrmaSqeCtx), 0);
    auto *sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx *>(sqeBuf.data());
    AscendC::HcommUrmaDumpSgeCtx(sqe, nullptr, sizeof(uint8_t));
}

// Dump helpers: full function bodies.
// HcommUrmaDumpCqeCtx is otherwise unreachable in UT_TEST mode because PollCq's
// polling loop (the only caller) is compiled out under UT_TEST.
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpHelpers_Valid)
{
    // CQE dump: full body over a zero-initialized valid CQE
    AscendC::HcommUrmaJfcCqeCtx cqe = {};
    AscendC::HcommUrmaDumpCqeCtx(reinterpret_cast<__ubuf__ AscendC::HcommUrmaJfcCqeCtx *>(&cqe));

    // Notify dump: full body over a zero-initialized valid notify ctx
    AscendC::HcommUrmaNotifyCtx notify = {};
    AscendC::HcommUrmaDumpNotifyCtx(reinterpret_cast<__ubuf__ AscendC::HcommUrmaNotifyCtx *>(&notify));

    // SGE dump: drive the loop body with sgeNum > 0 and a matching SGE array
    constexpr uint32_t sgeNum = 2;
    std::vector<uint8_t> buf(sizeof(AscendC::HcommUrmaSqeCtx) + sgeNum * sizeof(AscendC::HcommUrmaSgeCtx), 0);
    auto *sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx *>(buf.data());
    sqe->sgeNum = sgeNum;
    auto *sgeAddr = reinterpret_cast<__ubuf__ uint8_t *>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx));
    AscendC::HcommUrmaDumpSgeCtx(sqe, sgeAddr, sizeof(uint8_t));

    // WQE dump with WRITE_WITH_NOTIFY opcode: covers the notify branch inside
    // HcommUrmaDumpWqeCtx
    constexpr uint32_t wqeBufSize
        = sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaNotifyCtx) + sizeof(AscendC::HcommUrmaSgeCtx);
    std::vector<uint8_t> wqeBuf(wqeBufSize, 0);
    auto *wqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx *>(wqeBuf.data());
    wqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::WRITE_WITH_NOTIFY);
    wqe->sgeNum = 1;
    AscendC::HcommUrmaDumpWqeCtx(wqe, sizeof(uint8_t));
}

// Dump AMO ctx: FAA opcode path
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpAmoCtx_FAA)
{
    // FAA uses atomicLen to determine data size (uint32_t or uint64_t)
    // amoDataAddr = sqeCtx + sizeof(HcommUrmaSqeCtx) +
    // sizeof(HcommUrmaSgeCtx)
    constexpr uint32_t amoBufSize
        = sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx) + sizeof(uint64_t);
    std::vector<uint8_t> buf(amoBufSize, 0);
    auto *sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx *>(buf.data());
    sqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::FAA);

    // Test uint32_t atomicLen
    uint32_t *amoData32 = reinterpret_cast<uint32_t *>(
        buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    *amoData32 = 0x12345678;
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint32_t));

    // Test uint64_t atomicLen
    uint64_t *amoData64 = reinterpret_cast<uint64_t *>(
        buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    *amoData64 = 0x12345678ABCDEF00ULL;
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint64_t));
}

// Dump AMO ctx: CAS opcode path
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpAmoCtx_CAS)
{
    // CAS reads two values: swapValue and condValue
    // amoDataAddr = sqeCtx + sizeof(HcommUrmaSqeCtx) +
    // sizeof(HcommUrmaSgeCtx)
    constexpr uint32_t amoBufSize
        = sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx) + 2 * sizeof(uint64_t);
    std::vector<uint8_t> buf(amoBufSize, 0);
    auto *sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx *>(buf.data());
    sqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::CAS);

    // Test uint32_t atomicLen: swapValue at offset 0, condValue at offset
    // atomicLen
    uint32_t *amoData32 = reinterpret_cast<uint32_t *>(
        buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    amoData32[0] = 0xDEADBEEF; // swapValue
    amoData32[1] = 0xCAFEBABE; // condValue
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint32_t));

    // Test uint64_t atomicLen: swapValue at offset 0, condValue at offset
    // atomicLen
    uint64_t *amoData64 = reinterpret_cast<uint64_t *>(
        buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    amoData64[0] = 0xAAAABBBBCCCCDDDDEULL; // swapValue
    amoData64[1] = 0x1111222233334444FULL; // condValue
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint64_t));
}
