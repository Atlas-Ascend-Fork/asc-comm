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
#if __has_include("asccomm/asccomm_common.h")
// Preferred: new-package layouts stage asccomm/ next to hcomm/.
#include "asccomm/asccomm_common.h"
#else
// Compatibility: older package layouts only provide the hcomm/ public common header. Both
// headers define the same types and share the same include guard.
#endif
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
constexpr uint32_t DESCRIPTOR_SIZE = asc::ASCCOMM_URMA_TMP_BUF_SIZE + 32;
constexpr uint32_t AIN_SIGNAL_COUNT = 16;
constexpr uint32_t AIN_SIGNAL_POOL_SIZE = AIN_SIGNAL_COUNT * 2;
constexpr uint64_t LSA_STRIDE = 0x100;

class urma_channel_resource {
public:
    urma_channel_resource() : sq_buffer(URMA_SQ_DEPTH * URMA_WQE_SIZE, 0), cq_buffer(URMA_SQ_DEPTH * URMA_CQE_SIZE, 0)
    {
        channel.engine = asc::COMM_ENGINE_AIV;
        channel.protocol = asc::COMM_PROTOCOL_UB_MEM;
        channel.sqNum = 1;
        channel.cqNum = 1;
        channel.remoteBufferNum = URMA_BUFFER_NUM;
        channel.localBufferNum = URMA_BUFFER_NUM;
        channel.sqContextAddr = &sq_ctx;
        channel.cqContextAddr = &cq_ctx;
        channel.remoteBufferAddr = remote_buffers.data();
        channel.localBufferAddr = local_buffers.data();

        sq_ctx.type = asc::SQ_CONTEXT_TYPE_UB_JFS;
        sq_ctx.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sq_buffer.data());
        sq_ctx.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&sq_head);
        sq_ctx.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&sq_tail);
        sq_ctx.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sq_doorbell);
        sq_ctx.contextInfo.ubJfs.jfsID = 1;
        sq_ctx.contextInfo.ubJfs.wqeSize = URMA_WQE_SIZE;
        sq_ctx.contextInfo.ubJfs.sqDepth = URMA_SQ_DEPTH;
        sq_ctx.contextInfo.ubJfs.tpID = 1;

        cq_ctx.type = asc::CQ_CONTEXT_TYPE_UB_JFC;
        cq_ctx.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cq_buffer.data());
        cq_ctx.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&cq_head);
        cq_ctx.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cq_tail);
        cq_ctx.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cq_doorbell);
        cq_ctx.contextInfo.ubJfc.jfcID = 1;
        cq_ctx.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        cq_ctx.contextInfo.ubJfc.cqDepth = URMA_SQ_DEPTH;

        InitBuffer(remote_buffers[0], 0x1000, 0x1000, 0x123456, 0x654321);
        InitBuffer(remote_buffers[1], REMOTE_PEER_BASE, REMOTE_PEER_SIZE, 0x223456, 0x754321);
        InitBuffer(local_buffers[0], 0x2000, 0x1000, 0x111111, 0x222222);
        InitBuffer(local_buffers[1], 0x5000, 0x1000, 0x333333, 0x444444);
    }

    asc::ChannelHandle get_handle() { return reinterpret_cast<asc::ChannelHandle>(&channel); }

    const asc::ChannelEntity& get_channel_entity() const { return channel; }

    void set_active_entity(asc::ChannelEntity* active) { this->active = active; }

    uint32_t get_sq_head() const { return static_cast<uint32_t>(active->sqHead); }

    uint32_t get_sq_doorbell() const { return sq_doorbell; }

    uint32_t get_sq_tail() const { return active->cqTail; }

    const uint8_t* get_sq_wqe(uint32_t index) const { return sq_buffer.data() + index * URMA_WQE_SIZE; }

    void set_remote_buffer(uint32_t idx, uint64_t addr, uint64_t size)
    {
        if (idx < remote_buffers.size()) {
            remote_buffers[idx].bufferInfo.rma.addr = addr;
            remote_buffers[idx].bufferInfo.rma.size = size;
        }
    }

private:
    void InitBuffer(asc::RegedBufferEntity& buffer, uint64_t addr, uint64_t size, uint32_t tokenId, uint32_t tokenValue)
    {
        buffer.type = asc::REGED_BUFFER_RMA;
        buffer.bufferInfo.rma.addr = addr;
        buffer.bufferInfo.rma.size = size;
        buffer.bufferInfo.rma.protectionInfo.type = asc::PROTECTION_TYPE_UB;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    asc::ChannelEntity channel = {};
    asc::ChannelEntity* active = &channel;
    asc::SqContext sq_ctx = {};
    asc::CqContext cq_ctx = {};
    std::array<asc::RegedBufferEntity, URMA_BUFFER_NUM> remote_buffers = {};
    std::array<asc::RegedBufferEntity, URMA_BUFFER_NUM> local_buffers = {};
    std::vector<uint8_t> sq_buffer;
    std::vector<uint8_t> cq_buffer;
    uint64_t sq_head = 0;
    uint32_t sq_tail = 0;
    uint32_t cq_head = 0;
    uint32_t cq_tail = 0;
    uint32_t sq_doorbell = 0;
    uint32_t cq_doorbell = 0;
};

class ain_team_resource {
public:
    explicit ain_team_resource(urma_channel_resource& channel)
    {
        team.engine = COMM_ENGINE_AIV;
        team.memberNum = AIN_RANK_SIZE;
        team.selfMemberId = 0;
        team.netLayer = 0;
        channel_entities[PEER * MAX_CONTEXTS + 0] = channel.get_channel_entity();
        channel.set_active_entity(&channel_entities[PEER * MAX_CONTEXTS + 0]);
        team.channelsBaseAddr = reinterpret_cast<uint64_t>(channel_entities.data());
        team.channelCntAccumulatePerMember = channel_cnt_accumulate_per_member.data();
        for (uint32_t i = 0; i < MAX_TEAM_MEMBERS; ++i) {
            channel_cnt_accumulate_per_member[i] = i * MAX_CONTEXTS;
        }
        team.worldTeamIds = world_team_ids.data();
        world_team_ids[0] = 0;
        world_team_ids[1] = MAX_TEAM_MEMBERS - 1;
        team.syncMem.remoteMems = remote_mems.data();
        team.syncMem.remoteMemsNum = AIN_RANK_SIZE;
        team.syncMem.shadowMem.type = COMM_MEM_TYPE_DEVICE;
        team.syncMem.shadowMem.addr = shadow_pool.data();
        team.syncMem.shadowMem.size = shadow_pool.size() * sizeof(uint64_t);
        team.syncMem.syncMemReq.signalCount = AIN_SIGNAL_COUNT;
        team.syncMem.syncMemSize = shadow_pool.size() * sizeof(uint64_t);

        remote_mems[0].type = COMM_MEM_TYPE_DEVICE;
        remote_mems[0].addr = signal_pool.data();
        remote_mems[0].size = signal_pool.size() * sizeof(uint64_t);
        remote_mems[1].type = COMM_MEM_TYPE_DEVICE;
        remote_mems[1].addr = reinterpret_cast<void*>(REMOTE_PEER_BASE);
        remote_mems[1].size = REMOTE_PEER_SIZE;

        base_remote_mem_addrs[0] = reinterpret_cast<uint64_t>(signal_pool.data());
        for (uint32_t i = 1; i < MAX_TEAM_MEMBERS; ++i) {
            base_remote_mem_addrs[i] = REMOTE_PEER_BASE;
        }
        win.netWin.baseRemoteMemAddr = reinterpret_cast<uint64_t>(base_remote_mem_addrs.data());
        win.netWin.windowSize = REMOTE_PEER_SIZE;
        win.netWin.worldTeamAccumulateId = world_team_accumulate_id.data();
        win.netWin.netLayerNum = 1;
        world_team_accumulate_id[0] = 0;

        win.lsaWin.baseVa = reinterpret_cast<uint64_t>(lsa_base.data());
        win.lsaWin.stride = LSA_STRIDE;
        win.lsaWin.userSize = lsa_base.size();

        // Second window for Get: its remote table entry 0 resolves to the dedicated
        // receive buffer, so a dst/src swap in the Get path lands on different
        // addresses and fails the WQE address assertions below.
        recv_remote_mem_addrs[0] = reinterpret_cast<uint64_t>(recv_base.data());
        for (uint32_t i = 1; i < MAX_TEAM_MEMBERS; ++i) {
            recv_remote_mem_addrs[i] = REMOTE_PEER_BASE;
        }
        recv_win.netWin.baseRemoteMemAddr = reinterpret_cast<uint64_t>(recv_remote_mem_addrs.data());
        recv_win.netWin.windowSize = REMOTE_PEER_SIZE;
        recv_win.netWin.worldTeamAccumulateId = world_team_accumulate_id.data();
        recv_win.netWin.netLayerNum = 1;
        recv_win.lsaWin.baseVa = reinterpret_cast<uint64_t>(recv_base.data());
        recv_win.lsaWin.stride = LSA_STRIDE;
        recv_win.lsaWin.userSize = recv_base.size();

        channel.set_remote_buffer(
            0, reinterpret_cast<uint64_t>(signal_pool.data()), signal_pool.size() * sizeof(uint64_t));
    }

    AscendC::HcommTeamHandle get_team() { return reinterpret_cast<AscendC::HcommTeamHandle>(&team); }

    AscendC::HcclCommSymWindow get_win() { return reinterpret_cast<AscendC::HcclCommSymWindow>(&win); }

    AscendC::HcclCommSymWindow get_recv_win() { return reinterpret_cast<AscendC::HcclCommSymWindow>(&recv_win); }

    uint64_t get_recv_local_base() const { return reinterpret_cast<uint64_t>(recv_base.data()); }

    uint64_t* get_signal_pool() { return signal_pool.data(); }

    uint64_t* get_shadow_pool() { return shadow_pool.data(); }

    uint64_t get_lsa_base_va() const { return reinterpret_cast<uint64_t>(lsa_base.data()); }

    uint64_t get_lsa_stride() const { return win.lsaWin.stride; }

    uint32_t get_world_team_id(uint32_t member_id) const { return world_team_ids[member_id]; }

private:
    HcommTeam team = {};
    HcommWindow win = {};
    HcommWindow recv_win = {};
    std::array<uint64_t, MAX_TEAM_MEMBERS> recv_remote_mem_addrs = {};
    std::vector<uint64_t> recv_base = std::vector<uint64_t>(0x10, 0);
    std::array<uint32_t, MAX_TEAM_MEMBERS> channel_cnt_accumulate_per_member = {};
    std::array<uint32_t, AIN_RANK_SIZE> world_team_ids = {};
    std::array<uint64_t, MAX_TEAM_MEMBERS> base_remote_mem_addrs = {};
    std::array<uint32_t, 1> world_team_accumulate_id = {};
    std::array<uint8_t, 0x1000> lsa_base = {};
    std::array<asc::ChannelEntity, MAX_TEAM_MEMBERS * MAX_CONTEXTS> channel_entities = {};
    std::array<CommMem, AIN_RANK_SIZE> remote_mems = {};
    std::array<uint64_t, AIN_SIGNAL_POOL_SIZE> signal_pool = {};
    std::array<uint64_t, AIN_SIGNAL_POOL_SIZE> shadow_pool = {};
};

} // namespace

class ain_urma_test_suite : public testing::Test {
protected:
    void SetUp() override
    {
        block_idx_bak = block_idx;
        descriptor_ubuf.addr = reinterpret_cast<__ubuf__ uint8_t*>(descriptor_buffer);
        descriptor_ubuf.bytes = DESCRIPTOR_SIZE;
        descriptor_ubuf.event_id = 0;
    }

    void TearDown() override { block_idx = block_idx_bak; }

    asc::ain_descriptor_ubuf get_descriptor() const { return descriptor_ubuf; }

private:
    alignas(32) uint8_t descriptor_buffer[DESCRIPTOR_SIZE] = {};
    asc::ain_descriptor_ubuf descriptor_ubuf = {};
    int64_t block_idx_bak = 0;
};

TEST_F(ain_urma_test_suite, PUT_IMMEDIATE_COMMITS_WRITE)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Put(
        ain_resource.get_team(), PEER, ain_resource.get_win(), 0x20, ain_resource.get_win(), 0x40, 8,
        AscendC::ain_remote_none{}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 1U);
}

TEST_F(ain_urma_test_suite, GET_IMMEDIATE_COMMITS_READ)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    // dst is the local receive window, src is the peer window: swapping either the
    // windows or the offsets resolves different addresses and fails the WQE checks.
    ain.Get(
        ain_resource.get_team(), PEER, ain_resource.get_recv_win(), 0x40, ain_resource.get_win(), 0x20, 8,
        get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 1U);

    const auto* wqe = reinterpret_cast<const uint64_t*>(channel.get_sq_wqe(0));
    const uint64_t expected_local = ain_resource.get_recv_local_base() + 0x40;
    const uint64_t expected_remote = REMOTE_PEER_BASE + 0x20;
    int32_t local_word = -1;
    int32_t remote_word = -1;
    for (uint32_t word = 0; word < URMA_WQE_SIZE / sizeof(uint64_t); ++word) {
        if (wqe[word] == expected_local) {
            local_word = static_cast<int32_t>(word);
        }
        if (wqe[word] == expected_remote) {
            remote_word = static_cast<int32_t>(word);
        }
    }
    EXPECT_NE(local_word, -1);
    EXPECT_NE(remote_word, -1);
    EXPECT_NE(local_word, remote_word);
}

TEST_F(ain_urma_test_suite, DELAYED_PUT_FLUSH_COMMITS_AND_DRAINS)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Put<AscendC::ain_remote_none, AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, ain_resource.get_win(), 0x20, ain_resource.get_win(), 0x40, 8,
        AscendC::ain_remote_none{}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);

    ain.Flush(ain_resource.get_team());

    EXPECT_EQ(channel.get_sq_doorbell(), 0U);
    EXPECT_EQ(channel.get_sq_tail(), 1U);
}

TEST_F(ain_urma_test_suite, DELAYED_GET_FLUSH_COMMITS_AND_DRAINS)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Get<AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, ain_resource.get_recv_win(), 0x40, ain_resource.get_win(), 0x20, 8,
        get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);

    ain.Flush(ain_resource.get_team());

    EXPECT_EQ(channel.get_sq_doorbell(), 0U);
    EXPECT_EQ(channel.get_sq_tail(), 1U);

    const auto* wqe = reinterpret_cast<const uint64_t*>(channel.get_sq_wqe(0));
    const uint64_t expected_local = ain_resource.get_recv_local_base() + 0x40;
    const uint64_t expected_remote = REMOTE_PEER_BASE + 0x20;
    int32_t local_word = -1;
    int32_t remote_word = -1;
    for (uint32_t word = 0; word < URMA_WQE_SIZE / sizeof(uint64_t); ++word) {
        if (wqe[word] == expected_local) {
            local_word = static_cast<int32_t>(word);
        }
        if (wqe[word] == expected_remote) {
            remote_word = static_cast<int32_t>(word);
        }
    }
    EXPECT_NE(local_word, -1);
    EXPECT_NE(remote_word, -1);
    EXPECT_NE(local_word, remote_word);
}

TEST_F(ain_urma_test_suite, DELAYED_OPERATIONS_FLUSH_PEER_CHANNEL)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Put<AscendC::ain_remote_none, AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, ain_resource.get_win(), 0x20, ain_resource.get_win(), 0x40, 8,
        AscendC::ain_remote_none{}, get_descriptor());
    ain.Get<AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, ain_resource.get_win(), 0x60, ain_resource.get_win(), 0x80, 8, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);

    ain.Flush(ain_resource.get_team());

    EXPECT_EQ(channel.get_sq_doorbell(), 0U);
    EXPECT_EQ(channel.get_sq_tail(), 2U);
}

TEST_F(ain_urma_test_suite, READ_SIGNAL_RETURNS_VALUE_FROM_POOL)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain_resource.get_signal_pool()[0] = 42;
    ain_resource.get_signal_pool()[1] = 100;

    EXPECT_EQ(ain.read_signal(ain_resource.get_team(), ain_resource.get_win(), 0, 64), 42U);
    EXPECT_EQ(ain.read_signal(ain_resource.get_team(), ain_resource.get_win(), sizeof(uint64_t), 64), 100U);
}

TEST_F(ain_urma_test_suite, READ_SIGNAL_WITH_BITS_MASKS_VALUE)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain_resource.get_signal_pool()[0] = 0xFF;

    EXPECT_EQ(ain.read_signal(ain_resource.get_team(), ain_resource.get_win(), 0, 8), 0xFFU);
    EXPECT_EQ(ain.read_signal(ain_resource.get_team(), ain_resource.get_win(), 0, 4), 0xFU);
    EXPECT_EQ(ain.read_signal(ain_resource.get_team(), ain_resource.get_win(), 0, 32), 0xFFU);
}

TEST_F(ain_urma_test_suite, SIGNAL_IMMEDIATE_COMMITS_ATOMIC_FAA)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal(ain_resource.get_team(), PEER, AscendC::ain_signal_inc{ain_resource.get_win(), 0}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 2U);
}

TEST_F(ain_urma_test_suite, SIGNAL_WITH_SET_VALUE_COMMITS_ATOMIC_FAA)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal(
        ain_resource.get_team(), PEER, AscendC::ain_signal_add{ain_resource.get_win(), 0, 100}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 2U);
}

TEST_F(ain_urma_test_suite, SIGNAL_DELAYED_DOES_NOT_RING_DOORBELL)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal<AscendC::ain_signal_inc, AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, AscendC::ain_signal_inc{ain_resource.get_win(), 0}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);
}

TEST_F(ain_urma_test_suite, SIGNAL_FLUSH_COMMITS_AND_DRAINS)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain.Signal<AscendC::ain_signal_inc, AscendC::ain_descriptor_ubuf, AscendC::AIN_COMMIT_DELAYED>(
        ain_resource.get_team(), PEER, AscendC::ain_signal_inc{ain_resource.get_win(), 0}, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 0U);

    ain.Flush(ain_resource.get_team());

    EXPECT_EQ(channel.get_sq_doorbell(), 0U);
    EXPECT_EQ(channel.get_sq_tail(), 1U);
}

TEST_F(ain_urma_test_suite, WAIT_SIGNAL_SUCCEEDS_WHEN_VALUE_UPDATED)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    constexpr uint64_t target_value = 10ULL;
    ain_resource.get_signal_pool()[5] = 9ULL;

    ain_resource.get_signal_pool()[5]++;

    ain.wait_signal(ain_resource.get_team(), ain_resource.get_win(), 5 * sizeof(uint64_t), target_value, 64);

    EXPECT_EQ(ain_resource.get_signal_pool()[5], target_value);
}

TEST_F(ain_urma_test_suite, WAIT_SIGNAL_WITH_MASK_BITS_WHEN_VALUE_UPDATED)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    constexpr uint64_t test_value = 0x12345678ULL;
    ain_resource.get_signal_pool()[6] = 0x12345677ULL;

    ain_resource.get_signal_pool()[6]++;

    ain.wait_signal(ain_resource.get_team(), ain_resource.get_win(), 6 * sizeof(uint64_t), test_value, 32);

    EXPECT_EQ(ain_resource.get_signal_pool()[6] & 0xFFFFFFFFULL, test_value);
}

TEST_F(ain_urma_test_suite, BARRIER_SESSION_SYNC_SIGNALS_PEERS_AND_WAITS)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain_resource.get_shadow_pool()[0] = 0;
    ain_resource.get_signal_pool()[PEER] = 1;

    AscendC::ain_barrier_session<> barrier_session(&ain, ain_resource.get_team(), 0);
    barrier_session.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, get_descriptor());

    EXPECT_EQ(channel.get_sq_head(), 2U);
}

TEST_F(ain_urma_test_suite, BARRIER_SESSION_SYNC_WITH_TIMEOUT)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain_resource.get_shadow_pool()[0] = 0;
    ain_resource.get_signal_pool()[PEER] = 1;

    AscendC::ain_barrier_session<> barrier_session(&ain, ain_resource.get_team(), 0);
    int32_t result = barrier_session.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 1000000ULL, get_descriptor());

    EXPECT_EQ(result, 0);
    EXPECT_EQ(channel.get_sq_head(), 2U);
}

TEST_F(ain_urma_test_suite, BARRIER_SESSION_SYNC_TIMEOUT_EXPIRED)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);
    AscendC::Ain<> ain(0);

    ain_resource.get_shadow_pool()[0] = 0;
    ain_resource.get_signal_pool()[PEER] = 0;

    AscendC::ain_barrier_session<> barrier_session(&ain, ain_resource.get_team(), 0);
    int32_t result = barrier_session.Sync(AscendC::AIN_MEMORY_ORDER_RELAX, 1ULL, get_descriptor());

    EXPECT_EQ(result, -1);
}

TEST_F(ain_urma_test_suite, GET_PEER_POINTER_RETURNS_PEER_ADDRESS_FROM_WORLD_TEAM_ID)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);

    AscendC::HcommMemHandle ptr = get_peer_pointer(PEER, ain_resource.get_win(), 0x40);

    uint64_t expected = ain_resource.get_lsa_base_va() + PEER * ain_resource.get_lsa_stride() + 0x40;
    EXPECT_EQ(reinterpret_cast<uint64_t>(ptr), expected);
}

TEST_F(ain_urma_test_suite, GET_PEER_POINTER_RETURNS_SELF_ADDRESS_WITHOUT_STRIDE)
{
    urma_channel_resource channel;
    ain_team_resource ain_resource(channel);

    AscendC::HcommMemHandle ptr = get_peer_pointer(0, ain_resource.get_win(), 0x80);

    EXPECT_EQ(reinterpret_cast<uint64_t>(ptr), ain_resource.get_lsa_base_va() + 0x80);
}
