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
#include "jetty/asccomm_work_queue.h"

using namespace asc;

namespace {

constexpr uint32_t JETTY_SQ_DEPTH = 10;
constexpr uint32_t JETTY_WQE_SIZE = 64;
constexpr uint32_t JETTY_CQE_SIZE = 64;
constexpr uint32_t JETTY_BUFFER_NUM = 2;

class jetty_channel_resource {
public:
    jetty_channel_resource()
        : sq_buffer(JETTY_SQ_DEPTH * JETTY_WQE_SIZE, 0), cq_buffer(JETTY_SQ_DEPTH * JETTY_CQE_SIZE, 0)
    {
        channel.remoteBufferNum = JETTY_BUFFER_NUM;
        channel.remoteBufferAddr = remote_buffers.data();
        channel.sqContextAddr = &sq_ctx;
        channel.cqContextAddr = &cq_ctx;
        jetty_table[0] = reinterpret_cast<uint64_t>(&channel);

        // The last registered buffer carries the shared remote token metadata.
        remote_buffers[1].type = REGED_BUFFER_RMA;
        remote_buffers[1].bufferInfo.rma.addr = 0x1000U;
        remote_buffers[1].bufferInfo.rma.size = 0x1000U;
        remote_buffers[1].bufferInfo.rma.protectionInfo.type = PROTECTION_TYPE_UB;
        remote_buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = 0x123456U;
        remote_buffers[1].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = 0x654321U;

        sq_ctx.type = SQ_CONTEXT_TYPE_UB_JFS;
        sq_ctx.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sq_buffer.data());
        sq_ctx.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&sq_packed_head);
        sq_ctx.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&completion_tail);
        sq_ctx.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sq_doorbell);
        sq_ctx.contextInfo.ubJfs.wqeSize = JETTY_WQE_SIZE;
        sq_ctx.contextInfo.ubJfs.sqDepth = JETTY_SQ_DEPTH;
        sq_ctx.contextInfo.ubJfs.tpID = 77U;
        const std::array<uint32_t, 4> remote_eid = {0x11223344U, 0x55667788U, 0x99AABBCCU, 0xDDEEFF00U};
        std::memcpy(sq_ctx.contextInfo.ubJfs.remoteEID, remote_eid.data(), remote_eid.size() * sizeof(uint32_t));

        cq_ctx.type = CQ_CONTEXT_TYPE_UB_JFC;
        cq_ctx.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cq_buffer.data());
        cq_ctx.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cq_tail);
        cq_ctx.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cq_doorbell);
        cq_ctx.contextInfo.ubJfc.cqeSize = JETTY_CQE_SIZE;
        cq_ctx.contextInfo.ubJfc.cqDepth = JETTY_SQ_DEPTH;
    }

    GM_ADDR get_jetty_table() { return reinterpret_cast<GM_ADDR>(jetty_table.data()); }

    void set_packed_head(uint32_t sqHead, uint32_t expected_cqe_cnt)
    {
        sq_packed_head = static_cast<uint64_t>(sqHead) | (static_cast<uint64_t>(expected_cqe_cnt) << 32U);
    }

    uint64_t get_packed_head() const { return sq_packed_head; }

    uint32_t get_sq_doorbell() const { return sq_doorbell; }

    void set_completion_tail(uint32_t tail) { completion_tail = tail; }

    void set_cq_tail(uint32_t tail) { cq_tail = tail; }

private:
    ChannelEntity channel = {};
    SqContext sq_ctx = {};
    CqContext cq_ctx = {};
    std::array<RegedBufferEntity, JETTY_BUFFER_NUM> remote_buffers = {};
    std::vector<uint8_t> sq_buffer;
    std::vector<uint8_t> cq_buffer;
    std::array<uint64_t, 1> jetty_table = {};
    uint64_t sq_packed_head = 0;
    uint32_t completion_tail = 0;
    uint32_t cq_tail = 0;
    uint32_t sq_doorbell = 0;
    uint32_t cq_doorbell = 0;
};

class asccomm_jetty_test_suite : public testing::Test {
protected:
    jetty_channel_resource channel;
    alignas(8) std::array<uint8_t, 128U> ubuf_sqe = {};
    alignas(8) asccomm_peer_entity<asc::COMM_PROTOCOL_UB_CTP> peer_info = {};
    alignas(8) asccomm_work_queue_info<asc::COMM_PROTOCOL_UB_CTP> jetty_info = {};

    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> make_work_queue()
    {
        return asccomm_work_queue<COMM_PROTOCOL_UB_CTP>(&jetty_info, ubuf_sqe.data(), channel.get_jetty_table(), 0U);
    }
};

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_PEER_RESOLVES_REMOTE_METADATA)
{
    channel.set_packed_head(0U, 0U);
    asccomm_peer<COMM_PROTOCOL_UB_CTP> peer(&peer_info, channel.get_jetty_table(), 0U);

    EXPECT_TRUE(peer.valid);
    // tp_id occupies bits [23:0] and remote_token_id bits [51:32] of the packed word.
    EXPECT_EQ(peer_info.tp_id_num_sges_remote_token_id & 0xFFFFFFU, 77U);
    EXPECT_EQ((peer_info.tp_id_num_sges_remote_token_id >> 32U) & 0xFFFFFU, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(peer_info.remote_token_value_udf, 0x654321U);
    EXPECT_EQ(peer_info.remote_eid[0], 0x5566778811223344ULL);
    EXPECT_EQ(peer_info.remote_eid[1], 0xDDEEFF0099AABBCCULL);
}

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_INFO_SPLITS_PACKED_HEAD)
{
    channel.set_packed_head(5U, 9U);
    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> workQueue = make_work_queue();

    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head), 5U);
    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head >> 32U), 9U);
    EXPECT_EQ(jetty_info.sq_depth, JETTY_SQ_DEPTH);
    EXPECT_EQ(jetty_info.num_wqebb_bytes, JETTY_WQE_SIZE);
    EXPECT_EQ(jetty_info.cq_depth, JETTY_SQ_DEPTH);
    EXPECT_EQ(jetty_info.num_cqe_bytes, JETTY_CQE_SIZE);
    EXPECT_NE(jetty_info.sq_base_addr, 0U);
    EXPECT_NE(jetty_info.sq_head_addr, 0U);
    EXPECT_NE(jetty_info.sq_doorbell_addr, 0U);
    EXPECT_NE(jetty_info.cq_base_addr, 0U);
}

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_ADVANCE_SQ_AND_RING_DOORBELL)
{
    channel.set_packed_head(3U, 7U);
    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> workQueue = make_work_queue();

    workQueue.advance_sq(2U);
    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head), 5U);
    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head >> 32U), 8U);
    EXPECT_EQ(channel.get_packed_head(), (5U | (8ULL << 32U)));

    workQueue.ring_doorbell();
    EXPECT_EQ(channel.get_sq_doorbell(), 5U);
}

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_ADVANCE_SQ_WRAPS_HEAD)
{
    channel.set_packed_head(0xFFFFFFFFU, 1U);
    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> workQueue = make_work_queue();

    workQueue.advance_sq(1U);
    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head), 0U);
    EXPECT_EQ(static_cast<uint32_t>(jetty_info.packed_head >> 32U), 2U);
    EXPECT_EQ(channel.get_packed_head(), (0U | (2ULL << 32U)));
}

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_WRITE_WITH_NOTIFY_DELAYED_COMMIT)
{
    channel.set_packed_head(3U, 7U);
    channel.set_completion_tail(7U);
    channel.set_cq_tail(7U);
    asccomm_peer<COMM_PROTOCOL_UB_CTP> peer(&peer_info, channel.get_jetty_table(), 0U);
    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> workQueue = make_work_queue();

    int32_t ret = workQueue.template write_with_notify_nbi<1000, false>(
        peer, reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8U,
        reinterpret_cast<GM_ADDR>(0x1038), 1U);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(jetty_info.packed_head, (5U | (8ULL << 32U)));
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);

    // The test queue has only 10 CQEs, while the implementation polls once the near-full
    // threshold (10) is reached. Model hardware progress before the second post so the test
    // exercises submission and commit accounting without CQ polling.
    channel.set_completion_tail(8U);
    channel.set_cq_tail(8U);

    // The omitted do_commit argument keeps the original auto-commit behavior and publishes
    // both WQEs accumulated in the SQ.
    ret = workQueue.template write_with_notify_nbi<1000>(
        peer, reinterpret_cast<GM_ADDR>(0x1018), reinterpret_cast<GM_ADDR>(0x2018), 8U,
        reinterpret_cast<GM_ADDR>(0x1048), 2U);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(jetty_info.packed_head, (7U | (9ULL << 32U)));
    EXPECT_EQ(channel.get_sq_doorbell(), 7U);
}

TEST_F(asccomm_jetty_test_suite, AIV_JETTY_DRAIN_NO_PENDING_COMPLETIONS)
{
    channel.set_packed_head(2U, 4U);
    asccomm_work_queue<COMM_PROTOCOL_UB_CTP> workQueue = make_work_queue();
    // Set the hardware completion tail so no CQE needs to be polled.
    channel.set_cq_tail(4U);

    int32_t ret = workQueue.template drain<1000>();
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
}

} // namespace
