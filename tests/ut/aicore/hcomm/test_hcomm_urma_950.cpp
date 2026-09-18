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
#include <cstring>
#include <gtest/gtest.h>
#include <vector>
#define private public
#include "kernel_operator.h"
#include "hcomm/hcomm.h"

using namespace AscendC;

namespace {

constexpr uint32_t URMA_SQ_DEPTH = 10;
constexpr uint32_t URMA_WQE_SIZE = 64;
constexpr uint32_t URMA_CQE_SIZE = 64;
constexpr uint32_t URMA_BUFFER_NUM = 2;
constexpr uint32_t URMA_BATCH_QUEUE_DEPTH = 4;

static constexpr AscendC::UrmaWqeEntry URMA_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inlineEn = 0,
};

template <size_t N>
AscendC::LocalTensor<uint8_t> WrapUbBuffer(std::array<uint8_t, N>& buffer)
{
    AscendC::TBuffAddr addr{};
    addr.logicPos = static_cast<uint8_t>(AscendC::TPosition::VECOUT);
    addr.bufferAddr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer.data()));
    addr.dataLen = static_cast<uint32_t>(buffer.size());
    addr.absAddr = buffer.data();
    AscendC::LocalTensor<uint8_t> tensor;
    tensor.SetAddr(addr);
    return tensor;
}

static_assert(offsetof(AscendC::ChannelEntity, sqHead) == 96);
static_assert(sizeof(AscendC::ChannelEntity) == 256);

class UrmaChannelResource {
public:
    explicit UrmaChannelResource(uint32_t queueDepth = URMA_SQ_DEPTH)
        : sqBuffer_(queueDepth * URMA_WQE_SIZE, 0), cqBuffer_(queueDepth * URMA_CQE_SIZE, 0)
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
        sqCtx_.contextInfo.ubJfs.sqDepth = queueDepth;
        sqCtx_.contextInfo.ubJfs.tpID = 1;
        const std::array<uint64_t, 2> remoteEid = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
        std::memcpy(sqCtx_.contextInfo.ubJfs.remoteEID, remoteEid.data(), sizeof(remoteEid));

        cqCtx_.type = AscendC::CQ_CONTEXT_TYPE_UB_JFC;
        cqCtx_.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cqBuffer_.data());
        cqCtx_.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&cqHead_);
        cqCtx_.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cqTail_);
        cqCtx_.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cqDoorbell_);
        cqCtx_.contextInfo.ubJfc.jfcID = 1;
        cqCtx_.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        cqCtx_.contextInfo.ubJfc.cqDepth = queueDepth;

        InitBuffer(remoteBuffers_[0], 0x1000, 0x1000, 0x123456, 0x654321);
        InitBuffer(remoteBuffers_[1], 0x3000, 0x1000, 0x223456, 0x754321);
        InitBuffer(localBuffers_[0], 0x2000, 0x1000, 0x111111, 0x222222);
        InitBuffer(localBuffers_[1], 0x5000, 0x1000, 0x333333, 0x444444);
    }

    AscendC::ChannelHandle GetHandle() { return reinterpret_cast<AscendC::ChannelHandle>(&channel_); }

    uint32_t GetSqHead() const { return channel_.sqHead; }

    uint32_t GetLock() const { return *reinterpret_cast<const uint32_t*>(cqCtx_.contextInfo.ubJfc.headAddr); }

    uint32_t GetCqHead() const { return channel_.cqHead; }

    uint32_t GetCqTail() const { return channel_.cqTail; }

    uint32_t GetSqDoorbell() const { return sqDoorbell_; }

    uint32_t GetCqDoorbell() const { return cqDoorbell_; }

    const uint8_t* GetSqBuffer() const { return sqBuffer_.data(); }

    const uint8_t* GetCqBuffer() const { return cqBuffer_.data(); }

    uint64_t GetSqDoorbellAddr() const { return reinterpret_cast<uint64_t>(&sqDoorbell_); }

    uint64_t GetCqDoorbellAddr() const { return reinterpret_cast<uint64_t>(&cqDoorbell_); }

    void SetSqHead(uint32_t sqHead) { channel_.sqHead = sqHead; }

    void SetQueueState(uint32_t sqHead, uint32_t cqHead, uint32_t cqTail)
    {
        channel_.sqHead = sqHead;
        channel_.cqHead = cqHead;
        channel_.cqTail = cqTail;
    }

    void SetRemoteBufferAddr(uint32_t index, uint64_t addr) { remoteBuffers_[index].bufferInfo.rma.addr = addr; }

    void SetLocalBufferAddr(uint32_t index, uint64_t addr) { localBuffers_[index].bufferInfo.rma.addr = addr; }

    void CompleteCurrentSq() { channel_.cqTail = channel_.cqHead; }

private:
    void InitBuffer(
        AscendC::RegedBufferEntity& buffer, uint64_t addr, uint64_t size, uint32_t tokenId, uint32_t tokenValue)
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
    uint64_t sqHead_ = 0;
    uint32_t sqTail_ = 0;
    uint32_t cqHead_ = 0;
    uint32_t cqTail_ = 0;
    uint32_t sqDoorbell_ = 0;
    uint32_t cqDoorbell_ = 0;
};

class UrmaMultiChannelResource {
public:
    UrmaMultiChannelResource()
        : sqBuffer_(URMA_SQ_DEPTH * URMA_WQE_SIZE, 0), cqBuffer_(URMA_SQ_DEPTH * URMA_CQE_SIZE, 0)
    {
        InitBuffer(remoteBuffers_[0], 0x1000U, 0x1000U, 0x11111U, 0xAAAAU);
        InitBuffer(remoteBuffers_[1], 0x3000U, 0x1000U, 0x33333U, 0xCCCCU);
        InitBuffer(remoteBuffers_[2], 0x5000U, 0x1000U, 0x22222U, 0xBBBBU);

        remoteInfos_[0].remoteBufferAddr = reinterpret_cast<uint64_t>(&remoteBuffers_[0]);
        remoteInfos_[0].remoteBufferNum = 2U;
        remoteInfos_[0].tpId = 11U;
        remoteInfos_[0].remoteEidLow = 0x1111222233334444ULL;
        remoteInfos_[0].remoteEidHigh = 0x5555666677778888ULL;
        remoteInfos_[1].remoteBufferAddr = reinterpret_cast<uint64_t>(&remoteBuffers_[2]);
        remoteInfos_[1].remoteBufferNum = 1U;
        remoteInfos_[1].tpId = 22U;
        remoteInfos_[1].remoteEidLow = 0x9999AAAABBBBCCCCULL;
        remoteInfos_[1].remoteEidHigh = 0xDDDDEEEEFFFF0000ULL;

        sqContext_.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sqBuffer_.data());
        sqContext_.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&channel_.sqHead);
        sqContext_.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&channel_.sqTail);
        sqContext_.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sqDoorbell_);
        sqContext_.contextInfo.ubJfs.sqDepth = URMA_SQ_DEPTH;
        cqContext_.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cqBuffer_.data());
        cqContext_.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&channel_.cqHead);
        cqContext_.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&channel_.cqTail);
        cqContext_.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cqDoorbell_);
        cqContext_.contextInfo.ubJfc.cqDepth = URMA_SQ_DEPTH;
        cqContext_.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        channel_.sqContextAddr = &sqContext_;
        channel_.cqContextAddr = &cqContext_;
        entity_.channelHandle = reinterpret_cast<AscendC::ChannelHandle>(&channel_);
        entity_.channelNum = remoteInfos_.size();
        entity_.remoteInfoAddr = reinterpret_cast<uint64_t>(remoteInfos_.data());
    }

    AscendC::MultiChannelHandle GetHandle()
    {
        return static_cast<AscendC::MultiChannelHandle>(reinterpret_cast<uint64_t>(&entity_));
    }

    uint32_t GetSqHead() const { return channel_.sqHead; }

    uint32_t GetSqTail() const { return channel_.sqTail; }

    uint32_t GetCqHead() const { return channel_.cqHead; }

    uint32_t GetCqTail() const { return channel_.cqTail; }

    uint32_t GetSqDoorbell() const { return sqDoorbell_; }

    uint32_t GetCqDoorbell() const { return cqDoorbell_; }

    void SetRemoteBufferToken(uint32_t index, uint32_t tokenId, uint32_t tokenValue)
    {
        remoteBuffers_[index].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        remoteBuffers_[index].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    static void InitBuffer(
        AscendC::RegedBufferEntity& buffer, uint64_t addr, uint64_t size, uint32_t tokenId, uint32_t tokenValue)
    {
        buffer.type = AscendC::REGED_BUFFER_RMA;
        buffer.bufferInfo.rma.addr = addr;
        buffer.bufferInfo.rma.size = size;
        buffer.bufferInfo.rma.protectionInfo.type = AscendC::PROTECTION_TYPE_UB;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    AscendC::MultiChannelEntity entity_ = {};
    AscendC::ChannelEntity channel_ = {};
    AscendC::SqContext sqContext_ = {};
    AscendC::CqContext cqContext_ = {};
    std::array<AscendC::MultiChannelRemoteInfo, 2> remoteInfos_ = {};
    std::array<AscendC::RegedBufferEntity, 3> remoteBuffers_ = {};
    std::vector<uint8_t> sqBuffer_;
    std::vector<uint8_t> cqBuffer_;
    uint32_t sqDoorbell_ = 0;
    uint32_t cqDoorbell_ = 0;
};

} // namespace

class HcommUrmaTestSuite : public testing::Test {
protected:
    void SetUp() override { blockIdxBak_ = block_idx; }

    void TearDown() override { block_idx = blockIdxBak_; }

    int32_t InitHcomm(AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP>& hcomm)
    {
        pipe_.InitBuffer(hcommBuf_, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
        AscendC::LocalTensor<uint8_t> hcommLocal = hcommBuf_.Get<uint8_t>();
        __ubuf__ uint8_t* bufPtr = reinterpret_cast<__ubuf__ uint8_t*>(hcommLocal.GetPhyAddr());
        for (uint32_t i = 0; i < AscendC::HCOMM_URMA_TMP_BUF_SIZE; i++) {
            bufPtr[i] = 0;
        }
        return hcomm.Init(bufPtr, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    }

    template <typename T, AscendC::HcommUrmaReduceOp reduceOp>
    void CheckWriteReduce(uint32_t expectedDataType, uint32_t expectedReduceOpcode)
    {
        constexpr uint64_t count = 3;
        constexpr uint64_t remoteAddr = 0x1008;
        constexpr uint64_t localAddr = 0x2008;
        UrmaChannelResource channel;

        AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
        ASSERT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
        int32_t ret = hcomm.WriteReduceNbi<T, reduceOp, false>(
            channel.GetHandle(), reinterpret_cast<GM_ADDR>(remoteAddr), reinterpret_cast<GM_ADDR>(localAddr), count);
        ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
        EXPECT_EQ(channel.GetSqHead(), 1U);

        const auto* sqeCtx = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(hcomm.impl_.wqeItem_.GetPhyAddr());
        EXPECT_EQ(sqeCtx->opcode, static_cast<uint32_t>(AscendC::HcommUrmaOpCode::WRITE));
        EXPECT_EQ(sqeCtx->flag & AscendC::HCOMM_URMA_UDF_FLAG, AscendC::HCOMM_URMA_UDF_FLAG);
        EXPECT_EQ(sqeCtx->reduceDataType, expectedDataType);
        EXPECT_EQ(sqeCtx->reduceOpcode, expectedReduceOpcode);
        EXPECT_EQ(sqeCtx->sgeNum, 1U);

        const auto* sgeCtx = reinterpret_cast<const AscendC::HcommUrmaSgeCtx*>(
            reinterpret_cast<const uint8_t*>(sqeCtx) + sizeof(AscendC::HcommUrmaSqeCtx));
        EXPECT_EQ(sgeCtx->len, count * sizeof(T));
        EXPECT_EQ(sgeCtx->va, localAddr);
    }

private:
    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECOUT> hcommBuf_;
    int64_t blockIdxBak_;
};

TEST_F(HcommUrmaTestSuite, Aiv_Urma_MakeBatchHandleLocalTensor)
{
    UrmaChannelResource channel;
    channel.SetQueueState(3U, 2U, 1U);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> batchBuffer;
    pipe.InitBuffer(batchBuffer, 128);
    AscendC::LocalTensor<uint32_t> batchTensor = batchBuffer.Get<uint32_t>();

    auto batchHandle = hcomm.MakeBatchHandle(channel.GetHandle(), batchTensor, 128, reinterpret_cast<GM_ADDR>(0x3008));

    EXPECT_EQ(batchHandle.channelHandle, channel.GetHandle());
    EXPECT_EQ(batchHandle.buffer.buffer.GetPhyAddr(), batchTensor.GetPhyAddr());
    EXPECT_EQ(batchHandle.buffer.bufferCapacity, 2U);
    EXPECT_EQ(batchHandle.sqContext.contextInfo.ubJfs.sqVa, reinterpret_cast<uint64_t>(channel.GetSqBuffer()));
    EXPECT_EQ(batchHandle.sqContext.contextInfo.ubJfs.dbVa, channel.GetSqDoorbellAddr());
    EXPECT_EQ(batchHandle.remoteInfo.remoteEidLow, 0x1122334455667788ULL);
    EXPECT_EQ(batchHandle.remoteInfo.remoteEidHigh, 0x99AABBCCDDEEFF00ULL);
    EXPECT_EQ(batchHandle.cqContext.contextInfo.ubJfc.scqVa, reinterpret_cast<uint64_t>(channel.GetCqBuffer()));
    EXPECT_EQ(batchHandle.cqContext.contextInfo.ubJfc.dbVa, channel.GetCqDoorbellAddr());
    EXPECT_EQ(batchHandle.remoteInfo.tokenId, 0x223456U);
    EXPECT_EQ(batchHandle.remoteInfo.tokenValue, 0x754321U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
    EXPECT_EQ(batchHandle.sqContext.contextInfo.ubJfs.sqDepth, URMA_SQ_DEPTH);
    EXPECT_EQ(batchHandle.remoteInfo.tpId, 1U);
    EXPECT_EQ(batchHandle.cqContext.contextInfo.ubJfc.cqDepth, URMA_SQ_DEPTH);
    EXPECT_EQ(batchHandle.cqContext.contextInfo.ubJfc.cqeSize, URMA_CQE_SIZE);
    EXPECT_EQ(batchHandle.cursor.sqHead, 3U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);
    EXPECT_EQ(batchHandle.cursor.cqTail, 1U);

    auto& batchHandleRef = hcomm.GetHandleRef(batchHandle, 1U, reinterpret_cast<GM_ADDR>(0x5008U));
    EXPECT_EQ(&batchHandleRef, &batchHandle);
    EXPECT_EQ(batchHandleRef.remoteInfo.tokenId, 0x223456U);
    EXPECT_EQ(batchHandleRef.remoteInfo.tokenValue, 0x754321U);

    auto defaultRemoteHandle = hcomm.MakeBatchHandle(channel.GetHandle(), batchTensor, 128);
    EXPECT_EQ(defaultRemoteHandle.remoteInfo.tokenId, 0x123456U);
    EXPECT_EQ(defaultRemoteHandle.remoteInfo.tokenValue, 0x654321U);

    auto invalidRemoteHandle =
        hcomm.MakeBatchHandle(channel.GetHandle(), batchTensor, 128, reinterpret_cast<GM_ADDR>(0x500U));
    EXPECT_EQ(invalidRemoteHandle.channelHandle, 0U);
    EXPECT_EQ(invalidRemoteHandle.buffer.bufferCapacity, 0U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_MultiChannelBatchUsesOuterHandleForCommitAndDrain)
{
    static_assert(
        IsSameType<AscendC::BatchHandle<AscendC::ChannelHandle>, AscendC::UbcBatchHandle>::value,
        "ChannelHandle must create the execution BatchHandle");
    static_assert(
        IsSameType<AscendC::BatchHandle<AscendC::MultiChannelHandle>, AscendC::UbcMultiBatchHandle>::value,
        "MultiChannelHandle must create the peer-selection BatchHandle");
    static_assert(
        IsSameType<AscendC::BatchHandle<AscendC::UbcBatchHandle>, AscendC::UbcBatchHandle>::value,
        "UbcBatchHandle must resolve to the execution BatchHandle");
    static_assert(
        IsSameType<AscendC::BatchHandle<AscendC::UbcMultiBatchHandle>, AscendC::UbcBatchHandle>::value,
        "UbcMultiBatchHandle must resolve to its execution BatchHandle");

    UrmaMultiChannelResource multiChannel;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batchBuffer{};
    auto multiBatchHandle =
        hcomm.MakeBatchHandle(multiChannel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size());

    auto& batchHandle = hcomm.GetHandleRef(multiBatchHandle, 0U, reinterpret_cast<GM_ADDR>(0x3008U));
    EXPECT_EQ(&batchHandle, &multiBatchHandle.handle);
    EXPECT_EQ(batchHandle.channelHandle, multiBatchHandle.channelHandle);
    EXPECT_EQ(batchHandle.remoteInfo.tokenId, 0x33333U);
    EXPECT_EQ(batchHandle.remoteInfo.tokenValue, 0xCCCCU);
    multiChannel.SetRemoteBufferToken(1U, 0x44444U, 0xDDDDU);
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x3010U), reinterpret_cast<GM_ADDR>(0x7010U), 8U),
        AscendC::HCOMM_SUCCESS);

    auto& secondPeerHandle = hcomm.GetHandleRef(multiBatchHandle, 1U, reinterpret_cast<GM_ADDR>(0x5008U));
    EXPECT_EQ(&secondPeerHandle, &batchHandle);
    ASSERT_EQ(
        hcomm.ReadNbi(secondPeerHandle, reinterpret_cast<GM_ADDR>(0x8010U), reinterpret_cast<GM_ADDR>(0x5010U), 8U),
        AscendC::HCOMM_SUCCESS);

    const auto* firstSqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data());
    EXPECT_EQ(firstSqe->rmtJettyOrSegId, 0x33333U);
    EXPECT_EQ(firstSqe->rmtTokenValue, 0xCCCCU);
    EXPECT_EQ(firstSqe->tpId, 11U);
    const auto* secondSqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data() + URMA_WQE_SIZE);
    EXPECT_EQ(secondSqe->rmtJettyOrSegId, 0x22222U);
    EXPECT_EQ(secondSqe->rmtTokenValue, 0xBBBBU);
    EXPECT_EQ(secondSqe->tpId, 22U);

    ASSERT_EQ(hcomm.BatchCommit(multiBatchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);
    EXPECT_EQ(multiChannel.GetSqHead(), 2U);
    EXPECT_EQ(multiChannel.GetCqHead(), 2U);
    EXPECT_EQ(multiChannel.GetSqDoorbell(), 2U);

    ASSERT_EQ(hcomm.Drain(multiBatchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqTail, 2U);
    EXPECT_EQ(batchHandle.cursor.cqTail, 2U);
    EXPECT_EQ(multiChannel.GetSqTail(), 2U);
    EXPECT_EQ(multiChannel.GetCqTail(), 2U);
    EXPECT_EQ(multiChannel.GetCqDoorbell(), 2U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchWriteNbi)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 128> batchBuffer;
    batchBuffer.fill(0xFFU);
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 1U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 0U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    const auto* firstSqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data());
    EXPECT_EQ(firstSqe->sqeBbIdx, 0U);
    EXPECT_EQ(firstSqe->rsv0, 0U);
    EXPECT_EQ(firstSqe->nf, 0U);
    EXPECT_EQ(firstSqe->tokenEn, 1U);
    EXPECT_EQ(firstSqe->rmtJettyType, 1U);
    EXPECT_EQ(firstSqe->targetHint, 0U);
    EXPECT_EQ(firstSqe->opcode, static_cast<uint32_t>(AscendC::HcommUrmaOpCode::WRITE));
    EXPECT_EQ(firstSqe->rsv1, 0U);
    EXPECT_EQ(firstSqe->inlineMsgLen, 0U);
    EXPECT_EQ(firstSqe->sgeNum, 1U);
    EXPECT_EQ(firstSqe->rmtJettyOrSegId, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(firstSqe->rsv2, 0U);
    EXPECT_EQ(firstSqe->rmtTokenValue, 0x654321U);
    EXPECT_EQ(firstSqe->udfType, 0U);
    EXPECT_EQ(firstSqe->reduceDataType, 0U);
    EXPECT_EQ(firstSqe->reduceOpcode, 0U);
    EXPECT_EQ(firstSqe->rsv3, 0U);
    EXPECT_EQ(firstSqe->rmtAddrLOrTokenId, 0x1010U);
    const auto* firstSge =
        reinterpret_cast<const AscendC::HcommUrmaSgeCtx*>(batchBuffer.data() + sizeof(AscendC::HcommUrmaSqeCtx));
    EXPECT_EQ(firstSge->len, 8U);
    EXPECT_EQ(firstSge->va, 0x2010U);

    ret = hcomm.WriteNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 0U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_FAILED);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchReadNbi)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 128> batchBuffer;
    batchBuffer.fill(0xFFU);
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = hcomm.ReadNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 1U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 0U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    const auto* firstSqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data());
    EXPECT_EQ(firstSqe->opcode, static_cast<uint32_t>(AscendC::HcommUrmaOpCode::READ));
    EXPECT_NE(firstSqe->flag & (1U << 5U), 0U);
    EXPECT_EQ(firstSqe->tokenEn, 1U);
    EXPECT_EQ(firstSqe->sgeNum, 1U);
    EXPECT_EQ(firstSqe->rmtJettyOrSegId, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(firstSqe->rmtTokenValue, 0x654321U);
    EXPECT_EQ(firstSqe->rmtAddrLOrTokenId, 0x1010U);
    EXPECT_EQ(firstSqe->rmtAddrHOrTokenValue, 0U);
    const auto* firstSge =
        reinterpret_cast<const AscendC::HcommUrmaSgeCtx*>(batchBuffer.data() + sizeof(AscendC::HcommUrmaSqeCtx));
    EXPECT_EQ(firstSge->len, 8U);
    EXPECT_EQ(firstSge->va, 0x2010U);

    ret = hcomm.ReadNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x2020), reinterpret_cast<GM_ADDR>(0x1020), 16);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    const auto* secondSqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data() + URMA_WQE_SIZE);
    EXPECT_EQ(secondSqe->flag & (1U << 5U), 0U);

    ret = hcomm.ReadNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x2030), reinterpret_cast<GM_ADDR>(0x1030), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_FAILED);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchWriteWithNotifyNbiMixed)
{
    UrmaChannelResource channel(8);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 384> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = hcomm.WriteNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 1U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 0U);

    constexpr uint64_t notifyValue = 0x1122334455667788ULL;
    ret = hcomm.WriteWithNotifyNbi(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 16,
        reinterpret_cast<GM_ADDR>(0x1030), notifyValue);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 3U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 0U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    const uint8_t* notifyWqeAddr = batchBuffer.data() + URMA_WQE_SIZE;
    const auto* sqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(notifyWqeAddr);
    EXPECT_EQ(sqe->opcode, static_cast<uint32_t>(AscendC::HcommUrmaOpCode::WRITE_WITH_NOTIFY));
    EXPECT_EQ(sqe->rmtJettyOrSegId, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(sqe->rmtTokenValue, 0x654321U);
    EXPECT_EQ(sqe->rmtAddrLOrTokenId, 0x1020U);

    const auto* notifyCtx =
        reinterpret_cast<const AscendC::HcommUrmaNotifyCtx*>(notifyWqeAddr + sizeof(AscendC::HcommUrmaSqeCtx));
    EXPECT_EQ(notifyCtx->notifyTokenId, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(notifyCtx->notifyTokenValue, 0x654321U);
    EXPECT_EQ(notifyCtx->notifyAddrL, 0x1030U);
    EXPECT_EQ(notifyCtx->notifyAddrH, 0U);
    EXPECT_EQ(notifyCtx->notifyDataL, 0x55667788U);
    EXPECT_EQ(notifyCtx->notifyDataH, 0x11223344U);

    const auto* sge = reinterpret_cast<const AscendC::HcommUrmaSgeCtx*>(
        notifyWqeAddr + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaNotifyCtx));
    EXPECT_EQ(sge->len, 16U);
    EXPECT_EQ(sge->va, 0x2020U);

    ret = hcomm.WriteWithNotifyNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1040), reinterpret_cast<GM_ADDR>(0x2040), 8,
        reinterpret_cast<GM_ADDR>(0x1050), 1);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 5U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    const auto* noCqeNotifySqe =
        reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data() + 3U * URMA_WQE_SIZE);
    EXPECT_EQ(noCqeNotifySqe->flag & (1U << 5U), 0U);

    ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1060), reinterpret_cast<GM_ADDR>(0x2060), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 6U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);

    ret = hcomm.WriteWithNotifyNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1070), reinterpret_cast<GM_ADDR>(0x2070), 8,
        reinterpret_cast<GM_ADDR>(0x1080), 1);
    EXPECT_EQ(ret, AscendC::HCOMM_FAILED);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 6U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchWriteWithNotifyEncodes64BitAddresses)
{
    constexpr uint64_t remoteBase = 0x0000001210001000ULL;
    constexpr uint64_t localBase = 0x0000003420002000ULL;
    constexpr uint64_t remoteAddr = remoteBase + 0x10U;
    constexpr uint64_t localAddr = localBase + 0x10U;
    constexpr uint64_t notifyAddr = remoteBase + 0x20U;
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.SetRemoteBufferAddr(0U, remoteBase);
    channel.SetLocalBufferAddr(0U, localBase);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(remoteBase + 8U));

    ASSERT_EQ(
        hcomm.WriteWithNotifyNbi(
            batchHandle, reinterpret_cast<GM_ADDR>(remoteAddr), reinterpret_cast<GM_ADDR>(localAddr), 8,
            reinterpret_cast<GM_ADDR>(notifyAddr), 1),
        AscendC::HCOMM_SUCCESS);

    const auto* sqe = reinterpret_cast<const AscendC::HcommUrmaSqeCtx*>(batchBuffer.data());
    EXPECT_EQ(sqe->rmtAddrLOrTokenId, static_cast<uint32_t>(remoteAddr));
    EXPECT_EQ(sqe->rmtAddrHOrTokenValue, static_cast<uint32_t>(remoteAddr >> 32U));
    const auto* notifyCtx =
        reinterpret_cast<const AscendC::HcommUrmaNotifyCtx*>(batchBuffer.data() + sizeof(AscendC::HcommUrmaSqeCtx));
    EXPECT_EQ(notifyCtx->notifyAddrL, static_cast<uint32_t>(notifyAddr));
    EXPECT_EQ(notifyCtx->notifyAddrH, static_cast<uint32_t>(notifyAddr >> 32U));
    const auto* sge = reinterpret_cast<const AscendC::HcommUrmaSgeCtx*>(
        batchBuffer.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaNotifyCtx));
    EXPECT_EQ(sge->va, localAddr);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchReadWriteNotifyMixedCommit)
{
    UrmaChannelResource channel(8);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 256> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        hcomm.ReadNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteWithNotifyNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 4U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 4U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 4U);
    EXPECT_EQ(channel.GetCqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 4U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchCommitRejectsInvalidCounts)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    EXPECT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_FAILED);

    auto invalidHandle = batchHandle;
    invalidHandle.cursor.preSqCnt = 2U;
    EXPECT_EQ(hcomm.BatchCommit(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.cursor.preSqCnt = 1U;
    invalidHandle.sqContext.contextInfo.ubJfs.sqDepth = 0U;
    EXPECT_EQ(hcomm.BatchCommit(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.buffer.bufferCapacity = URMA_BATCH_QUEUE_DEPTH;
    invalidHandle.cursor.preSqCnt = URMA_BATCH_QUEUE_DEPTH;
    invalidHandle.sqContext.contextInfo.ubJfs.sqDepth = URMA_BATCH_QUEUE_DEPTH;
    EXPECT_EQ(hcomm.BatchCommit(invalidHandle), AscendC::HCOMM_FAILED);

    EXPECT_EQ(channel.GetSqHead(), 0U);
    EXPECT_EQ(channel.GetCqHead(), 0U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchHandleCommitWrapAndReuse)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.SetSqHead(URMA_BATCH_QUEUE_DEPTH - 1U);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 128> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ret = hcomm.WriteNbi<URMA_NO_CQE_CFG>(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);

    ret = hcomm.BatchCommit(batchHandle);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 5U);
    EXPECT_EQ(channel.GetCqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 5U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
    EXPECT_EQ(batchHandle.cursor.sqHead, 5U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);

    EXPECT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_FAILED);
    EXPECT_EQ(channel.GetSqHead(), 5U);
    EXPECT_EQ(channel.GetCqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 5U);

    ret = hcomm.WriteWithNotifyNbi(
        batchHandle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
        reinterpret_cast<GM_ADDR>(0x1040), 1);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 7U);
    EXPECT_EQ(channel.GetCqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 7U);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 0U);
    EXPECT_EQ(batchHandle.cursor.sqHead, 7U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchWriteWithNotifyCommitWrapsSingleWqe)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.SetSqHead(URMA_BATCH_QUEUE_DEPTH - 1U);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        hcomm.WriteWithNotifyNbi(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8,
            reinterpret_cast<GM_ADDR>(0x1020), 0x1122334455667788ULL),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(batchHandle.cursor.preSqCnt, 2U);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);

    EXPECT_EQ(batchHandle.cursor.sqHead, URMA_BATCH_QUEUE_DEPTH + 1U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), URMA_BATCH_QUEUE_DEPTH + 1U);
    EXPECT_EQ(channel.GetCqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), URMA_BATCH_QUEUE_DEPTH + 1U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchAllNoCqeCommitAndDrain)
{
    UrmaChannelResource channel(8);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 4U * URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        hcomm.ReadNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteWithNotifyNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.preSqCnt, 4U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 0U);

    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 4U);
    EXPECT_EQ(channel.GetSqHead(), 4U);
    EXPECT_EQ(channel.GetCqHead(), 0U);
    EXPECT_EQ(channel.GetSqDoorbell(), 4U);
    EXPECT_EQ(hcomm.Drain(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.cqTail, 0U);
    EXPECT_EQ(channel.GetCqTail(), 0U);
    EXPECT_EQ(channel.GetCqDoorbell(), 0U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchMultipleCommitsSingleDrain)
{
    UrmaChannelResource channel(8);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 3U * URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        hcomm.WriteNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.ReadNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x2020), reinterpret_cast<GM_ADDR>(0x1020), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 2U);
    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetCqHead(), 0U);

    ASSERT_EQ(
        hcomm.WriteWithNotifyNbi<URMA_NO_CQE_CFG>(
            batchHandle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(
        hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1050), reinterpret_cast<GM_ADDR>(0x2050), 8),
        AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 5U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(channel.GetSqHead(), 5U);
    EXPECT_EQ(channel.GetCqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 5U);

    ASSERT_EQ(hcomm.Drain(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.cqTail, 1U);
    EXPECT_EQ(channel.GetCqTail(), 1U);
    EXPECT_EQ(channel.GetCqDoorbell(), 1U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchDrainRejectsInvalidHandleState)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, URMA_WQE_SIZE> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    auto invalidHandle = batchHandle;
    invalidHandle.channelHandle = 0U;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.buffer.bufferCapacity = 0;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.cursor.preSqCnt = 1;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.cqContext.contextInfo.ubJfc.cqeSize = 0;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.cqContext.contextInfo.ubJfc.cqeSize = URMA_WQE_SIZE + 1U;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);

    invalidHandle = batchHandle;
    invalidHandle.cqContext.contextInfo.ubJfc.cqDepth = 0;
    EXPECT_EQ(hcomm.Drain(invalidHandle), AscendC::HCOMM_FAILED);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_BatchDrainWithoutInit)
{
    UrmaChannelResource channel(URMA_BATCH_QUEUE_DEPTH);
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    alignas(32) std::array<uint8_t, 64> batchBuffer{};
    auto batchHandle = hcomm.MakeBatchHandle(
        channel.GetHandle(), WrapUbBuffer(batchBuffer), batchBuffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.Drain(batchHandle), AscendC::HCOMM_FAILED);

    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(hcomm.Drain(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 1U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 1U);
    EXPECT_EQ(batchHandle.cursor.cqTail, 1U);
    EXPECT_EQ(channel.GetCqTail(), 1U);
    EXPECT_EQ(channel.GetCqDoorbell(), 1U);

    ret = hcomm.WriteNbi(batchHandle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.BatchCommit(batchHandle), AscendC::HCOMM_SUCCESS);
    ASSERT_EQ(hcomm.Drain(batchHandle), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.sqHead, 2U);
    EXPECT_EQ(batchHandle.cursor.cqHead, 2U);
    EXPECT_EQ(batchHandle.cursor.cqTail, 2U);
    EXPECT_EQ(channel.GetCqTail(), 2U);
    EXPECT_EQ(channel.GetCqDoorbell(), 2U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_LockUnlock)
{
    UrmaChannelResource channel;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    EXPECT_EQ(channel.GetLock(), AscendC::HCOMM_LOCK_FREE);
    EXPECT_EQ(hcomm.Lock(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetLock(), AscendC::HCOMM_LOCK_HELD);
    EXPECT_EQ(hcomm.Unlock(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetLock(), AscendC::HCOMM_LOCK_FREE);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_UnlockWithoutLock)
{
    UrmaChannelResource channel;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    EXPECT_EQ(channel.GetLock(), AscendC::HCOMM_LOCK_FREE);
    EXPECT_EQ(hcomm.Unlock(channel.GetHandle()), AscendC::HCOMM_FAILED);
    EXPECT_EQ(channel.GetLock(), AscendC::HCOMM_LOCK_FREE);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_LockUnlockInvalidChannel)
{
    UrmaChannelResource channel;
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    ChannelHandle misalignedChannel = channel.GetHandle() + 1U;

    EXPECT_EQ(hcomm.Lock(0U), AscendC::HCOMM_FAILED);
    EXPECT_EQ(hcomm.Unlock(0U), AscendC::HCOMM_FAILED);
    EXPECT_EQ(hcomm.Lock(misalignedChannel), AscendC::HCOMM_FAILED);
    EXPECT_EQ(hcomm.Unlock(misalignedChannel), AscendC::HCOMM_FAILED);
}

// ReadNbi with default commit=true: auto-commit internally, then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_Read)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret =
        hcomm.ReadNbi(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x5008), reinterpret_cast<GM_ADDR>(0x3008), 8);
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteNbi with commit=false: explicit Commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_Write)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
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

TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteReduce_MaxInt8)
{
    CheckWriteReduce<int8_t, AscendC::HcommUrmaReduceOp::MAX>(0x0U, 0x8U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteReduce_MinHalf)
{
    CheckWriteReduce<half, AscendC::HcommUrmaReduceOp::MIN>(0x6U, 0x9U);
}

TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteReduce_SumFloat)
{
    CheckWriteReduce<float, AscendC::HcommUrmaReduceOp::SUM>(0x7U, 0xAU);
}

// WriteWithNotifyNbi with commit=false: explicit Commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithNotify)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret = hcomm.WriteWithNotifyNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channel.GetHandle());
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteValueNbi with default commit=true: inline value carried in WQE, auto-commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithValue)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret =
        hcomm.WriteValueNbi<uint64_t>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), 0xCAFEBABEDEADBEEFULL);
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteValueNbi with commit=false: explicit Commit then Drain succeeds
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithValue_DelayedCommit)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);
    int32_t ret =
        hcomm.WriteValueNbi<uint32_t, false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), 0x12345678U);
    EXPECT_EQ(ret, 0);
    ret = hcomm.Commit(channel.GetHandle());
    EXPECT_EQ(ret, 0);
    channel.CompleteCurrentSq();
    ret = hcomm.Drain(channel.GetHandle());
    EXPECT_EQ(ret, 0);
}

// WriteValueNbi occupies 1 BB (WRITE opcode), advances sqHead by 1
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithValue_WqeBbCnt)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    int32_t ret = hcomm.WriteValueNbi<uint64_t, false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), 0xAAULL);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 1U);

    ret = hcomm.WriteValueNbi<uint32_t, false>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), 0xBBU);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 2U);
}

// WriteValueNbi remote buffer lookup failure when address is out of range
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithValue_RemoteBufferNotFound)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    int32_t ret = hcomm.WriteValueNbi<uint64_t>(channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x9000), 0x1ULL);
    EXPECT_EQ(ret, AscendC::HCOMM_FAILED);
}

// WriteWithNotifyNbi occupies 2 BBs, WriteNbi occupies 1 BB
// Verified by reading sqHead from ChannelEntity
TEST_F(HcommUrmaTestSuite, Aiv_Urma_WriteWithNotify_WqeBbCnt)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    // WriteNbi should advance sqHead by 1
    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    EXPECT_EQ(channel.GetSqHead(), 1U);

    // WriteWithNotifyNbi should advance sqPI by 2
    ret = hcomm.WriteWithNotifyNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
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

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    // Address completely outside any remote buffer range (remote buffers are at 0x1000-0x2000 and 0x3000-0x4000)
    int32_t h = hcomm.WriteWithNotifyNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x9000), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
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

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;
    EXPECT_EQ(InitHcomm(hcomm), AscendC::HCOMM_SUCCESS);

    int32_t ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ret = hcomm.WriteWithNotifyNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);
    ret = hcomm.WriteNbi<false>(
        channel.GetHandle(), reinterpret_cast<GM_ADDR>(0x1018), reinterpret_cast<GM_ADDR>(0x2018), 8);
    EXPECT_EQ(ret, AscendC::HCOMM_SUCCESS);

    EXPECT_EQ(hcomm.Commit(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
    channel.CompleteCurrentSq();
    EXPECT_EQ(hcomm.Drain(channel.GetHandle()), AscendC::HCOMM_SUCCESS);
}

// Init with LocalTensor overload: uses LocalTensor directly without TBuffAddr/SetAddr
TEST_F(HcommUrmaTestSuite, Aiv_Urma_InitLocalTensor)
{
    UrmaChannelResource channel;

    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm;

    // Allocate buffer via TPipe and get LocalTensor
    AscendC::TPipe pipe;
    AscendC::TBuf<AscendC::TPosition::VECOUT> buf;
    pipe.InitBuffer(buf, AscendC::HCOMM_URMA_TMP_BUF_SIZE);
    AscendC::LocalTensor<uint8_t> localBuf = buf.Get<uint8_t>();

    // Init with LocalTensor should succeed
    EXPECT_EQ(hcomm.Init(localBuf, AscendC::HCOMM_URMA_TMP_BUF_SIZE), AscendC::HCOMM_SUCCESS);

    // Init with insufficient length should fail
    AscendC::Hcomm<AscendC::COMM_PROTOCOL_UB_CTP> hcomm2;
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
// These guards are never reached through the normal send path, so exercise them directly.
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpHelpers_Nullptr)
{
    AscendC::HcommUrmaDumpWqeCtx(nullptr, sizeof(uint8_t));
    AscendC::HcommUrmaDumpCqeCtx(nullptr);
    AscendC::HcommUrmaDumpSgeCtx(nullptr, nullptr, sizeof(uint8_t));
    AscendC::HcommUrmaDumpAmoCtx(nullptr, sizeof(uint32_t));

    // sqeCtx non-null but sgeAddr null still hits the guard
    std::vector<uint8_t> sqeBuf(sizeof(AscendC::HcommUrmaSqeCtx), 0);
    auto* sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx*>(sqeBuf.data());
    AscendC::HcommUrmaDumpSgeCtx(sqe, nullptr, sizeof(uint8_t));
}

// Dump helpers: full function bodies.
// HcommUrmaDumpCqeCtx is otherwise unreachable in UT_TEST mode because PollCq's polling
// loop (the only caller) is compiled out under UT_TEST.
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpHelpers_Valid)
{
    // CQE dump: full body over a zero-initialized valid CQE
    AscendC::HcommUrmaJfcCqeCtx cqe = {};
    AscendC::HcommUrmaDumpCqeCtx(reinterpret_cast<__ubuf__ AscendC::HcommUrmaJfcCqeCtx*>(&cqe));

    // Notify dump: full body over a zero-initialized valid notify ctx
    AscendC::HcommUrmaNotifyCtx notify = {};
    AscendC::HcommUrmaDumpNotifyCtx(reinterpret_cast<__ubuf__ AscendC::HcommUrmaNotifyCtx*>(&notify));

    // SGE dump: drive the loop body with sgeNum > 0 and a matching SGE array
    constexpr uint32_t sgeNum = 2;
    std::vector<uint8_t> buf(sizeof(AscendC::HcommUrmaSqeCtx) + sgeNum * sizeof(AscendC::HcommUrmaSgeCtx), 0);
    auto* sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx*>(buf.data());
    sqe->sgeNum = sgeNum;
    auto* sgeAddr = reinterpret_cast<__ubuf__ uint8_t*>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx));
    AscendC::HcommUrmaDumpSgeCtx(sqe, sgeAddr, sizeof(uint8_t));

    // WQE dump with WRITE_WITH_NOTIFY opcode: covers the notify branch inside HcommUrmaDumpWqeCtx
    constexpr uint32_t wqeBufSize =
        sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaNotifyCtx) + sizeof(AscendC::HcommUrmaSgeCtx);
    std::vector<uint8_t> wqeBuf(wqeBufSize, 0);
    auto* wqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx*>(wqeBuf.data());
    wqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::WRITE_WITH_NOTIFY);
    wqe->sgeNum = 1;
    AscendC::HcommUrmaDumpWqeCtx(wqe, sizeof(uint8_t));
}

// Dump AMO ctx: FAA opcode path
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpAmoCtx_FAA)
{
    // FAA uses atomicLen to determine data size (uint32_t or uint64_t)
    // amoDataAddr = sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx)
    constexpr uint32_t amoBufSize =
        sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx) + sizeof(uint64_t);
    std::vector<uint8_t> buf(amoBufSize, 0);
    auto* sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx*>(buf.data());
    sqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::FAA);

    // Test uint32_t atomicLen
    uint32_t* amoData32 =
        reinterpret_cast<uint32_t*>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    *amoData32 = 0x12345678;
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint32_t));

    // Test uint64_t atomicLen
    uint64_t* amoData64 =
        reinterpret_cast<uint64_t*>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    *amoData64 = 0x12345678ABCDEF00ULL;
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint64_t));
}

// Dump AMO ctx: CAS opcode path
TEST_F(HcommUrmaTestSuite, Aiv_Urma_DumpAmoCtx_CAS)
{
    // CAS reads two values: swapValue and condValue
    // amoDataAddr = sqeCtx + sizeof(HcommUrmaSqeCtx) + sizeof(HcommUrmaSgeCtx)
    constexpr uint32_t amoBufSize =
        sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx) + 2 * sizeof(uint64_t);
    std::vector<uint8_t> buf(amoBufSize, 0);
    auto* sqe = reinterpret_cast<__ubuf__ AscendC::HcommUrmaSqeCtx*>(buf.data());
    sqe->opcode = static_cast<uint32_t>(AscendC::HcommUrmaOpCode::CAS);

    // Test uint32_t atomicLen: swapValue at offset 0, condValue at offset atomicLen
    uint32_t* amoData32 =
        reinterpret_cast<uint32_t*>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    amoData32[0] = 0xDEADBEEF; // swapValue
    amoData32[1] = 0xCAFEBABE; // condValue
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint32_t));

    // Test uint64_t atomicLen: swapValue at offset 0, condValue at offset atomicLen
    uint64_t* amoData64 =
        reinterpret_cast<uint64_t*>(buf.data() + sizeof(AscendC::HcommUrmaSqeCtx) + sizeof(AscendC::HcommUrmaSgeCtx));
    amoData64[0] = 0xAAAABBBBCCCCDDDDEULL; // swapValue
    amoData64[1] = 0x1111222233334444FULL; // condValue
    AscendC::HcommUrmaDumpAmoCtx(sqe, sizeof(uint64_t));
}
