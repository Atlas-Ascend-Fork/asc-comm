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
#include "jetty/hcomm_jetty.h"

using namespace AscendC;

namespace {

constexpr uint32_t JETTY_SQ_DEPTH = 10;
constexpr uint32_t JETTY_WQE_SIZE = 64;
constexpr uint32_t JETTY_CQE_SIZE = 64;
constexpr uint32_t JETTY_BUFFER_NUM = 2;

class JettyChannelResource {
public:
    JettyChannelResource()
        : sqBuffer_(JETTY_SQ_DEPTH * JETTY_WQE_SIZE, 0), cqBuffer_(JETTY_SQ_DEPTH * JETTY_CQE_SIZE, 0)
    {
        channel_.remoteBufferNum = JETTY_BUFFER_NUM;
        channel_.remoteBufferAddr = remoteBuffers_.data();
        channel_.sqContextAddr = &sqCtx_;
        channel_.cqContextAddr = &cqCtx_;
        jettyTable_[0] = reinterpret_cast<uint64_t>(&channel_);

        // The last registered buffer carries the shared remote token metadata.
        remoteBuffers_[1].type = REGED_BUFFER_RMA;
        remoteBuffers_[1].bufferInfo.rma.addr = 0x1000U;
        remoteBuffers_[1].bufferInfo.rma.size = 0x1000U;
        remoteBuffers_[1].bufferInfo.rma.protectionInfo.type = PROTECTION_TYPE_UB;
        remoteBuffers_[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 0x123456U;
        remoteBuffers_[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = 0x654321U;

        sqCtx_.type = SQ_CONTEXT_TYPE_UB_JFS;
        sqCtx_.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sqBuffer_.data());
        sqCtx_.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&sqPackedHead_);
        sqCtx_.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&completionTail_);
        sqCtx_.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sqDoorbell_);
        sqCtx_.contextInfo.ubJfs.wqeSize = JETTY_WQE_SIZE;
        sqCtx_.contextInfo.ubJfs.sqDepth = JETTY_SQ_DEPTH;
        sqCtx_.contextInfo.ubJfs.tpID = 77U;
        const std::array<uint32_t, 4> remoteEid = {0x11223344U, 0x55667788U, 0x99AABBCCU, 0xDDEEFF00U};
        std::memcpy(sqCtx_.contextInfo.ubJfs.remoteEID, remoteEid.data(), remoteEid.size() * sizeof(uint32_t));

        cqCtx_.type = CQ_CONTEXT_TYPE_UB_JFC;
        cqCtx_.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cqBuffer_.data());
        cqCtx_.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cqTail_);
        cqCtx_.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cqDoorbell_);
        cqCtx_.contextInfo.ubJfc.cqeSize = JETTY_CQE_SIZE;
        cqCtx_.contextInfo.ubJfc.cqDepth = JETTY_SQ_DEPTH;
    }

    GM_ADDR GetJettyTable() { return reinterpret_cast<GM_ADDR>(jettyTable_.data()); }

    void SetPackedHead(uint32_t sqHead, uint32_t expectedCqeCnt)
    {
        sqPackedHead_ = static_cast<uint64_t>(sqHead) | (static_cast<uint64_t>(expectedCqeCnt) << 32U);
    }

    uint64_t GetPackedHead() const { return sqPackedHead_; }

    uint32_t GetSqDoorbell() const { return sqDoorbell_; }

    void SetCompletionTail(uint32_t tail) { completionTail_ = tail; }

    void SetCqTail(uint32_t tail) { cqTail_ = tail; }

private:
    ChannelEntity channel_ = {};
    SqContext sqCtx_ = {};
    CqContext cqCtx_ = {};
    std::array<RegedBufferEntity, JETTY_BUFFER_NUM> remoteBuffers_ = {};
    std::vector<uint8_t> sqBuffer_;
    std::vector<uint8_t> cqBuffer_;
    std::array<uint64_t, 1> jettyTable_ = {};
    uint64_t sqPackedHead_ = 0;
    uint32_t completionTail_ = 0;
    uint32_t cqTail_ = 0;
    uint32_t sqDoorbell_ = 0;
    uint32_t cqDoorbell_ = 0;
};

class HcommJettyTestSuite : public testing::Test {
protected:
    JettyChannelResource channel_;
    alignas(8) std::array<uint8_t, HcommJetty::kNumMaxSqeBytes> ubufSqe_ = {};
    alignas(8) HcommJettyPeerInfo peerInfo_ = {};
    alignas(8) HcommJettyInfo jettyInfo_ = {};

    HcommJetty MakeJetty() { return HcommJetty(&jettyInfo_, ubufSqe_.data(), channel_.GetJettyTable(), 0U); }
};

TEST_F(HcommJettyTestSuite, Aiv_Jetty_PeerResolvesRemoteMetadata)
{
    channel_.SetPackedHead(0U, 0U);
    HcommPeer peer(&peerInfo_, channel_.GetJettyTable(), 0U);

    EXPECT_TRUE(peer.valid);
    EXPECT_EQ(peerInfo_.tpId, 77U);
    EXPECT_EQ(peerInfo_.remoteTokenId, 0x123456U);
    EXPECT_EQ(peerInfo_.remoteTokenValue, 0x654321U);
    EXPECT_EQ(peerInfo_.remoteEid[0], 0x5566778811223344ULL);
    EXPECT_EQ(peerInfo_.remoteEid[1], 0xDDEEFF0099AABBCCULL);
}

TEST_F(HcommJettyTestSuite, Aiv_Jetty_InfoSplitsPackedHead)
{
    channel_.SetPackedHead(5U, 9U);
    HcommJetty jetty = MakeJetty();

    EXPECT_EQ(jettyInfo_.sqHead, 5U);
    EXPECT_EQ(jettyInfo_.expectedCqeCnt, 9U);
    EXPECT_EQ(jettyInfo_.sqDepth, JETTY_SQ_DEPTH);
    EXPECT_EQ(jettyInfo_.numWqebbBytes, JETTY_WQE_SIZE);
    EXPECT_EQ(jettyInfo_.cqDepth, JETTY_SQ_DEPTH);
    EXPECT_EQ(jettyInfo_.numCqeBytes, JETTY_CQE_SIZE);
    EXPECT_NE(jettyInfo_.sqBaseAddr, 0U);
    EXPECT_NE(jettyInfo_.sqHeadAddr, 0U);
    EXPECT_NE(jettyInfo_.sqDoorbellAddr, 0U);
    EXPECT_NE(jettyInfo_.cqBaseAddr, 0U);
}

TEST_F(HcommJettyTestSuite, Aiv_Jetty_AdvanceSqAndRingDoorbell)
{
    channel_.SetPackedHead(3U, 7U);
    HcommJetty jetty = MakeJetty();

    jetty.AdvanceSq(2U);
    EXPECT_EQ(jettyInfo_.sqHead, 5U);
    EXPECT_EQ(jettyInfo_.expectedCqeCnt, 8U);
    EXPECT_EQ(channel_.GetPackedHead(), (5U | (8ULL << 32U)));

    jetty.RingDoorbell();
    EXPECT_EQ(channel_.GetSqDoorbell(), 5U);
}

TEST_F(HcommJettyTestSuite, Aiv_Jetty_AdvanceSqWrapsHead)
{
    channel_.SetPackedHead(0xFFFFFFFFU, 1U);
    HcommJetty jetty = MakeJetty();

    jetty.AdvanceSq(1U);
    EXPECT_EQ(jettyInfo_.sqHead, 0U);
    EXPECT_EQ(jettyInfo_.expectedCqeCnt, 2U);
    EXPECT_EQ(channel_.GetPackedHead(), (0U | (2ULL << 32U)));
}

TEST_F(HcommJettyTestSuite, Aiv_Jetty_DrainNoPendingCompletions)
{
    channel_.SetPackedHead(2U, 4U);
    HcommJetty jetty = MakeJetty();
    // Set the hardware completion tail so no CQE needs to be polled.
    channel_.SetCqTail(4U);

    int32_t ret = jetty.Drain<1000>();
    EXPECT_EQ(ret, HCOMM_SUCCESS);
}

} // namespace
