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
#include "kernel_operator.h"
#include "hcomm/hcomm_common.h"
#include "ain/ain.h"

using namespace AscendC;

namespace {

constexpr uint32_t URMA_SQ_DEPTH = 10;
constexpr uint32_t URMA_WQE_SIZE = 64;
constexpr uint32_t URMA_CQE_SIZE = 64;
constexpr uint32_t URMA_BUFFER_NUM = 2;
constexpr uint32_t AIN_RANK_SIZE = 2;
constexpr uint32_t PEER = 1;
constexpr uint32_t MAX_CONTEXTS = 2;
constexpr uint32_t MAX_TEAM_MEMBERS = 4;
constexpr uint64_t REMOTE_PEER_BASE = 0x3000;
constexpr uint64_t REMOTE_PEER_SIZE = 0x1000;
constexpr uint32_t DESCRIPTOR_SIZE = AscendC::HCOMM_URMA_TMP_BUF_SIZE + 32;
constexpr uint32_t AIN_SIGNAL_COUNT = 16;
constexpr uint32_t AIN_SIGNAL_POOL_SIZE = AIN_SIGNAL_COUNT * 2;

class UrmaChannelResource {
public:
    UrmaChannelResource() : sqBuffer_(URMA_SQ_DEPTH * URMA_WQE_SIZE, 0), cqBuffer_(URMA_SQ_DEPTH * URMA_CQE_SIZE, 0)
    {
        channel_.engine = AscendC::COMM_ENGINE_AIV;
        channel_.protocol = AscendC::COMM_PROTOCOL_UB_MEM;
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
        InitBuffer(remoteBuffers_[1], REMOTE_PEER_BASE, REMOTE_PEER_SIZE, 0x223456, 0x754321);
        InitBuffer(localBuffers_[0], 0x2000, 0x1000, 0x111111, 0x222222);
        InitBuffer(localBuffers_[1], 0x5000, 0x1000, 0x333333, 0x444444);
    }

    AscendC::ChannelHandle GetHandle() { return reinterpret_cast<AscendC::ChannelHandle>(&channel_); }

    const AscendC::ChannelEntity& GetChannelEntity() const { return channel_; }

    void SetActiveEntity(AscendC::ChannelEntity* active) { active_ = active; }

    uint32_t GetSqHead() const { return static_cast<uint32_t>(active_->sqHead); }

    uint32_t GetSqDoorbell() const { return sqDoorbell_; }

    uint32_t GetSqTail() const { return active_->cqTail; }

    void SetRemoteBuffer(uint32_t idx, uint64_t addr, uint64_t size)
    {
        if (idx < remoteBuffers_.size()) {
            remoteBuffers_[idx].bufferInfo.rma.addr = addr;
            remoteBuffers_[idx].bufferInfo.rma.size = size;
        }
    }

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
    AscendC::ChannelEntity* active_ = &channel_;
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

class AinTeamResource {
public:
    explicit AinTeamResource(UrmaChannelResource& channel)
    {
        team_.memberNum = AIN_RANK_SIZE;
        team_.selfMemberId = 0;
        channelEntities_[PEER * MAX_CONTEXTS + 0] = channel.GetChannelEntity();
        channel.SetActiveEntity(&channelEntities_[PEER * MAX_CONTEXTS + 0]);
        team_.channelsBaseAddr = reinterpret_cast<ChannelHandle>(channelEntities_.data());
        team_.channelNumPerMember = channelNumPerMember_.data();
        channelNumPerMember_[0] = MAX_CONTEXTS;
        channelNumPerMember_[1] = MAX_CONTEXTS;
        team_.syncMem.remoteMems = remoteMems_.data();
        team_.syncMem.remoteMemsNum = AIN_RANK_SIZE;
        team_.syncMem.shadowMem.type = COMM_MEM_TYPE_DEVICE;
        team_.syncMem.shadowMem.addr = shadowPool_.data();
        team_.syncMem.shadowMem.size = shadowPool_.size() * sizeof(uint64_t);

        remoteMems_[0].type = COMM_MEM_TYPE_DEVICE;
        remoteMems_[0].addr = signalPool_.data();
        remoteMems_[0].size = signalPool_.size() * sizeof(uint64_t);
        remoteMems_[1].type = COMM_MEM_TYPE_DEVICE;
        remoteMems_[1].addr = reinterpret_cast<void*>(REMOTE_PEER_BASE);
        remoteMems_[1].size = REMOTE_PEER_SIZE;

        win_.mems = remoteMems_.data();
        win_.memsNum = AIN_RANK_SIZE;

        channel.SetRemoteBuffer(
            0, reinterpret_cast<uint64_t>(signalPool_.data()), signalPool_.size() * sizeof(uint64_t));
    }

    AscendC::HcommTeamHandle GetTeam() { return reinterpret_cast<AscendC::HcommTeamHandle>(&team_); }

    AscendC::HcommWindowHandle GetWin() { return reinterpret_cast<AscendC::HcommWindowHandle>(&win_); }

    uint64_t* GetSignalPool() { return signalPool_.data(); }

    uint64_t* GetShadowPool() { return shadowPool_.data(); }

private:
    HcommTeam team_ = {};
    HcommWindow win_ = {};
    std::array<uint32_t, MAX_TEAM_MEMBERS> channelNumPerMember_ = {};
    std::array<AscendC::ChannelEntity, MAX_TEAM_MEMBERS * MAX_CONTEXTS> channelEntities_ = {};
    std::array<CommMem, AIN_RANK_SIZE> remoteMems_ = {};
    std::array<uint64_t, AIN_SIGNAL_POOL_SIZE> signalPool_ = {};
    std::array<uint64_t, AIN_SIGNAL_POOL_SIZE> shadowPool_ = {};
};

} // namespace

class AinUrmaTestSuite : public testing::Test {
protected:
    void SetUp() override
    {
        blockIdxBak_ = block_idx;
        pipe_.InitBuffer(descriptorBuf_, DESCRIPTOR_SIZE);
        descriptor_ = descriptorBuf_.Get<uint8_t>();
        descriptorUbuf_.addr = reinterpret_cast<__ubuf__ uint8_t*>(descriptor_.GetPhyAddr());
        descriptorUbuf_.bytes = DESCRIPTOR_SIZE;
        descriptorUbuf_.eventId = 0;
    }

    void TearDown() override { block_idx = blockIdxBak_; }

    AscendC::AinDescriptorUbuf GetDescriptor() const { return descriptorUbuf_; }

private:
    AscendC::TPipe pipe_;
    AscendC::TBuf<AscendC::TPosition::VECOUT> descriptorBuf_;
    AscendC::LocalTensor<uint8_t> descriptor_;
    AscendC::AinDescriptorUbuf descriptorUbuf_ = {};
    int64_t blockIdxBak_ = 0;
};

TEST_F(AinUrmaTestSuite, PutImmediateCommitsWrite)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Put(
        ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x20, ainResource.GetWin(), 0x40, 8,
        AscendC::AinRemoteNone{}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 1U);
}

TEST_F(AinUrmaTestSuite, GetImmediateCommitsRead)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Get(ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x40, ainResource.GetWin(), 0x20, 8, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 1U);
}

TEST_F(AinUrmaTestSuite, DelayedPutFlushCommitsAndDrains)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Put<AscendC::AinRemoteNone, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x20, ainResource.GetWin(), 0x40, 8,
        AscendC::AinRemoteNone{}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);

    ain.Flush(ainResource.GetTeam());

    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
    EXPECT_EQ(channel.GetSqTail(), 1U);
}

TEST_F(AinUrmaTestSuite, DelayedGetFlushCommitsAndDrains)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Get<AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x40, ainResource.GetWin(), 0x20, 8, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 1U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);

    ain.Flush(ainResource.GetTeam());

    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
    EXPECT_EQ(channel.GetSqTail(), 1U);
}

TEST_F(AinUrmaTestSuite, DelayedOperationsFlushPeerChannel)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Put<AscendC::AinRemoteNone, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x20, ainResource.GetWin(), 0x40, 8,
        AscendC::AinRemoteNone{}, GetDescriptor());
    ain.Get<AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, ainResource.GetWin(), 0x60, ainResource.GetWin(), 0x80, 8, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);

    ain.Flush(ainResource.GetTeam());

    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
    EXPECT_EQ(channel.GetSqTail(), 2U);
}

TEST_F(AinUrmaTestSuite, ReadSignalReturnsValueFromPool)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ainResource.GetSignalPool()[0] = 42;
    ainResource.GetSignalPool()[1] = 100;

    EXPECT_EQ(ain.ReadSignal(ainResource.GetTeam(), ainResource.GetWin(), 0, 64), 42U);
    EXPECT_EQ(ain.ReadSignal(ainResource.GetTeam(), ainResource.GetWin(), sizeof(uint64_t), 64), 100U);
}

TEST_F(AinUrmaTestSuite, ReadSignalWithBitsMasksValue)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ainResource.GetSignalPool()[0] = 0xFF;

    EXPECT_EQ(ain.ReadSignal(ainResource.GetTeam(), ainResource.GetWin(), 0, 8), 0xFFU);
    EXPECT_EQ(ain.ReadSignal(ainResource.GetTeam(), ainResource.GetWin(), 0, 4), 0xFU);
    EXPECT_EQ(ain.ReadSignal(ainResource.GetTeam(), ainResource.GetWin(), 0, 32), 0xFFU);
}

TEST_F(AinUrmaTestSuite, SignalImmediateCommitsAtomicFAA)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal(ainResource.GetTeam(), PEER, AscendC::AinSignalInc{ainResource.GetWin(), 0}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 2U);
}

TEST_F(AinUrmaTestSuite, SignalWithSetValueCommitsAtomicFAA)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal(ainResource.GetTeam(), PEER, AscendC::AinSignalAdd{ainResource.GetWin(), 0, 100}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 2U);
}

TEST_F(AinUrmaTestSuite, SignalDelayedDoesNotRingDoorbell)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal<AscendC::AinSignalInc, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, AscendC::AinSignalInc{ainResource.GetWin(), 0}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
}

TEST_F(AinUrmaTestSuite, SignalFlushCommitsAndDrains)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal<AscendC::AinSignalInc, AscendC::AinDescriptorUbuf, AscendC::AIN_COMMIT_DELAYED>(
        ainResource.GetTeam(), PEER, AscendC::AinSignalInc{ainResource.GetWin(), 0}, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
    EXPECT_EQ(channel.GetSqDoorbell(), 0U);

    ain.Flush(ainResource.GetTeam());

    EXPECT_EQ(channel.GetSqDoorbell(), 0U);
    EXPECT_EQ(channel.GetSqTail(), 1U);
}

TEST_F(AinUrmaTestSuite, WaitSignalSucceedsWhenValueUpdated)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    constexpr uint64_t targetValue = 10ULL;
    ainResource.GetSignalPool()[5] = 9ULL;

    ainResource.GetSignalPool()[5]++;

    ain.WaitSignal(ainResource.GetTeam(), ainResource.GetWin(), 5 * sizeof(uint64_t), targetValue, 64);

    EXPECT_EQ(ainResource.GetSignalPool()[5], targetValue);
}

TEST_F(AinUrmaTestSuite, WaitSignalWithMaskBitsWhenValueUpdated)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    constexpr uint64_t testValue = 0x12345678ULL;
    ainResource.GetSignalPool()[6] = 0x12345677ULL;

    ainResource.GetSignalPool()[6]++;

    ain.WaitSignal(ainResource.GetTeam(), ainResource.GetWin(), 6 * sizeof(uint64_t), testValue, 32);

    EXPECT_EQ(ainResource.GetSignalPool()[6] & 0xFFFFFFFFULL, testValue);
}

TEST_F(AinUrmaTestSuite, BarrierSessionSyncSignalsPeersAndWaits)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ainResource.GetShadowPool()[0] = 0;
    ainResource.GetSignalPool()[PEER] = 1;

    AscendC::AinBarrierSession<> barrierSession(&ain, ainResource.GetTeam(), 0);
    barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, GetDescriptor());

    EXPECT_EQ(channel.GetSqHead(), 2U);
}

TEST_F(AinUrmaTestSuite, BarrierSessionSyncWithTimeout)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ainResource.GetShadowPool()[0] = 0;
    ainResource.GetSignalPool()[PEER] = 1;

    AscendC::AinBarrierSession<> barrierSession(&ain, ainResource.GetTeam(), 0);
    int32_t result = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 1000000ULL, GetDescriptor());

    EXPECT_EQ(result, 0);
    EXPECT_EQ(channel.GetSqHead(), 2U);
}

TEST_F(AinUrmaTestSuite, BarrierSessionSyncTimeoutExpired)
{
    UrmaChannelResource channel;
    AinTeamResource ainResource(channel);
    AscendC::Ain<> ain(0);

    ainResource.GetShadowPool()[0] = 0;
    ainResource.GetSignalPool()[PEER] = 0;

    AscendC::AinBarrierSession<> barrierSession(&ain, ainResource.GetTeam(), 0);
    int32_t result = barrierSession.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 1ULL, GetDescriptor());

    EXPECT_EQ(result, -1);
}
