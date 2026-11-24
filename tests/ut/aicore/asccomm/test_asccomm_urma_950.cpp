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
#include "asccomm/asccomm.h"

using namespace asc;

namespace {

constexpr uint32_t URMA_SQ_DEPTH = 10;
constexpr uint32_t URMA_WQE_SIZE = 64;
constexpr uint32_t URMA_CQE_SIZE = 64;
constexpr uint32_t URMA_BUFFER_NUM = 2;
constexpr uint32_t URMA_BATCH_QUEUE_DEPTH = 4;

static constexpr asc::wqe_entry URMA_NO_CQE_CFG = {
    .odr = 5,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

template <size_t N>
__ubuf__ uint8_t* wrap_ub_buffer(std::array<uint8_t, N>& buffer)
{
    return reinterpret_cast<__ubuf__ uint8_t*>(buffer.data());
}

static_assert(offsetof(asc::ChannelEntity, sqHead) == 144);
static_assert(sizeof(asc::ChannelEntity) == 256);

class urma_channel_resource {
public:
    explicit urma_channel_resource(uint32_t queue_depth = URMA_SQ_DEPTH)
        : sq_buffer(queue_depth * URMA_WQE_SIZE, 0), cq_buffer(queue_depth * URMA_CQE_SIZE, 0)
    {
        channel.engine = COMM_ENGINE_AIV;
        channel.protocol = COMM_PROTOCOL_UB_MEM;
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
        sq_ctx.contextInfo.ubJfs.sqDepth = queue_depth;
        sq_ctx.contextInfo.ubJfs.tpID = 1;
        const std::array<uint64_t, 2> remote_eid = {0x1122334455667788ULL, 0x99AABBCCDDEEFF00ULL};
        std::memcpy(sq_ctx.contextInfo.ubJfs.remoteEID, remote_eid.data(), sizeof(remote_eid));

        cq_ctx.type = asc::CQ_CONTEXT_TYPE_UB_JFC;
        cq_ctx.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cq_buffer.data());
        cq_ctx.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&cq_head);
        cq_ctx.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&cq_tail);
        cq_ctx.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cq_doorbell);
        cq_ctx.contextInfo.ubJfc.jfcID = 1;
        cq_ctx.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        cq_ctx.contextInfo.ubJfc.cqDepth = queue_depth;

        InitBuffer(remote_buffers[0], 0x1000, 0x1000, 0x123456, 0x654321);
        InitBuffer(remote_buffers[1], 0x3000, 0x1000, 0x223456, 0x754321);
        InitBuffer(local_buffers[0], 0x2000, 0x1000, 0x111111, 0x222222);
        InitBuffer(local_buffers[1], 0x5000, 0x1000, 0x333333, 0x444444);
    }

    asc::ChannelHandle get_handle() { return reinterpret_cast<asc::ChannelHandle>(&channel); }

    uint32_t get_sq_head() const { return channel.sqHead; }

    uint32_t get_lock() const { return *reinterpret_cast<const uint32_t*>(cq_ctx.contextInfo.ubJfc.headAddr); }

    uint32_t get_cq_head() const { return channel.cqHead; }

    uint32_t get_cq_tail() const { return channel.cqTail; }

    uint32_t get_sq_doorbell() const { return sq_doorbell; }

    uint32_t get_cq_doorbell() const { return cq_doorbell; }

    const uint8_t* get_sq_buffer() const { return sq_buffer.data(); }

    const uint8_t* get_cq_buffer() const { return cq_buffer.data(); }

    uint64_t get_sq_doorbell_addr() const { return reinterpret_cast<uint64_t>(&sq_doorbell); }

    uint64_t get_cq_doorbell_addr() const { return reinterpret_cast<uint64_t>(&cq_doorbell); }

    void set_sq_head(uint32_t sqHead) { channel.sqHead = sqHead; }
    void set_sq_tail(uint32_t sqTail) { channel.sqTail = sqTail; }

    void set_queue_state(uint32_t sqHead, uint32_t cqHead, uint32_t cqTail)
    {
        channel.sqHead = sqHead;
        channel.cqHead = cqHead;
        channel.cqTail = cqTail;
    }

    void set_remote_buffer_addr(uint32_t index, uint64_t addr) { remote_buffers[index].bufferInfo.rma.addr = addr; }

    void set_local_buffer_addr(uint32_t index, uint64_t addr) { local_buffers[index].bufferInfo.rma.addr = addr; }

    void complete_current_sq() { channel.cqTail = channel.cqHead; }

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

class urma_multi_channel_resource {
public:
    urma_multi_channel_resource()
        : sq_buffer(URMA_SQ_DEPTH * URMA_WQE_SIZE, 0), cq_buffer(URMA_SQ_DEPTH * URMA_CQE_SIZE, 0)
    {
        InitBuffer(remote_buffers[0], 0x1000U, 0x1000U, 0x11111U, 0xAAAAU);
        InitBuffer(remote_buffers[1], 0x3000U, 0x1000U, 0x33333U, 0xCCCCU);
        InitBuffer(remote_buffers[2], 0x5000U, 0x1000U, 0x22222U, 0xBBBBU);

        remote_infos[0].remote_buffer_addr = reinterpret_cast<uint64_t>(&remote_buffers[0]);
        remote_infos[0].remote_buffer_num = 2U;
        remote_infos[0].tp_id = 11U;
        remote_infos[0].remote_eid_low = 0x1111222233334444ULL;
        remote_infos[0].remote_eid_high = 0x5555666677778888ULL;
        remote_infos[1].remote_buffer_addr = reinterpret_cast<uint64_t>(&remote_buffers[2]);
        remote_infos[1].remote_buffer_num = 1U;
        remote_infos[1].tp_id = 22U;
        remote_infos[1].remote_eid_low = 0x9999AAAABBBBCCCCULL;
        remote_infos[1].remote_eid_high = 0xDDDDEEEEFFFF0000ULL;

        sq_context.contextInfo.ubJfs.sqVa = reinterpret_cast<uint64_t>(sq_buffer.data());
        sq_context.contextInfo.ubJfs.headAddr = reinterpret_cast<uint64_t>(&channel.sqHead);
        sq_context.contextInfo.ubJfs.tailAddr = reinterpret_cast<uint64_t>(&channel.sqTail);
        sq_context.contextInfo.ubJfs.dbVa = reinterpret_cast<uint64_t>(&sq_doorbell);
        sq_context.contextInfo.ubJfs.sqDepth = URMA_SQ_DEPTH;
        cq_context.contextInfo.ubJfc.scqVa = reinterpret_cast<uint64_t>(cq_buffer.data());
        cq_context.contextInfo.ubJfc.headAddr = reinterpret_cast<uint64_t>(&channel.cqHead);
        cq_context.contextInfo.ubJfc.tailAddr = reinterpret_cast<uint64_t>(&channel.cqTail);
        cq_context.contextInfo.ubJfc.dbVa = reinterpret_cast<uint64_t>(&cq_doorbell);
        cq_context.contextInfo.ubJfc.cqDepth = URMA_SQ_DEPTH;
        cq_context.contextInfo.ubJfc.cqeSize = URMA_CQE_SIZE;
        channel.sqContextAddr = &sq_context;
        channel.cqContextAddr = &cq_context;
        entity.channel_handle = reinterpret_cast<asc::ChannelHandle>(&channel);
        entity.channel_num = remote_infos.size();
        entity.remote_info_addr = reinterpret_cast<uint64_t>(remote_infos.data());
    }

    asc::multi_channel_handle get_handle()
    {
        return static_cast<asc::multi_channel_handle>(reinterpret_cast<uint64_t>(&entity));
    }

    uint32_t get_sq_head() const { return channel.sqHead; }

    uint32_t get_sq_tail() const { return channel.sqTail; }

    uint32_t get_cq_head() const { return channel.cqHead; }

    uint32_t get_cq_tail() const { return channel.cqTail; }

    uint32_t get_sq_doorbell() const { return sq_doorbell; }

    uint32_t get_cq_doorbell() const { return cq_doorbell; }

    void set_remote_buffer_token(uint32_t index, uint32_t tokenId, uint32_t tokenValue)
    {
        remote_buffers[index].bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        remote_buffers[index].bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    static void InitBuffer(
        asc::RegedBufferEntity& buffer, uint64_t addr, uint64_t size, uint32_t tokenId, uint32_t tokenValue)
    {
        buffer.type = asc::REGED_BUFFER_RMA;
        buffer.bufferInfo.rma.addr = addr;
        buffer.bufferInfo.rma.size = size;
        buffer.bufferInfo.rma.protectionInfo.type = asc::PROTECTION_TYPE_UB;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenId = tokenId;
        buffer.bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue = tokenValue;
    }

private:
    asc::multi_channel_entity entity = {};
    asc::ChannelEntity channel = {};
    asc::SqContext sq_context = {};
    asc::CqContext cq_context = {};
    std::array<asc::multi_channel_remote_info, 2> remote_infos = {};
    std::array<asc::RegedBufferEntity, 3> remote_buffers = {};
    std::vector<uint8_t> sq_buffer;
    std::vector<uint8_t> cq_buffer;
    uint32_t sq_doorbell = 0;
    uint32_t cq_doorbell = 0;
};

} // namespace

class asccomm_urma_test_suite : public testing::Test {
protected:
    void SetUp() override { block_idx_bak = block_idx; }

    void TearDown() override { block_idx = block_idx_bak; }

    int32_t init_asccomm(asc::asccomm<asc::COMM_PROTOCOL_UB_CTP>& asccomm)
    {
        for (uint32_t i = 0; i < asc::ASCCOMM_URMA_TMP_BUF_SIZE; i++) {
            asccomm_tmp_buf[i] = 0;
        }
        return asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), asc::ASCCOMM_URMA_TMP_BUF_SIZE);
    }

    template <typename T, asc::asccomm_reduce_op reduce_op>
    void check_write_reduce(uint32_t expected_data_type, uint32_t expected_reduce_opcode)
    {
        constexpr uint64_t count = 3;
        constexpr uint64_t remote_addr = 0x1008;
        constexpr uint64_t local_addr = 0x2008;
        urma_channel_resource channel;

        asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
        ASSERT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
        int32_t ret = asccomm.write_reduce_nbi<T, reduce_op, false>(
            channel.get_handle(), reinterpret_cast<GM_ADDR>(remote_addr), reinterpret_cast<GM_ADDR>(local_addr), count);
        ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
        EXPECT_EQ(channel.get_sq_head(), 1U);

        const auto* sqe_ctx = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(asccomm.impl.wqe_item);
        EXPECT_EQ(sqe_ctx->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE));
        EXPECT_EQ(sqe_ctx->flag & asc::ASCCOMM_URMA_UDF_FLAG, asc::ASCCOMM_URMA_UDF_FLAG);
        EXPECT_EQ(sqe_ctx->reduce_data_type, expected_data_type);
        EXPECT_EQ(sqe_ctx->reduce_opcode, expected_reduce_opcode);
        EXPECT_EQ(sqe_ctx->sge_num, 1U);

        const auto* sge_ctx = reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(
            reinterpret_cast<const uint8_t*>(sqe_ctx) + sizeof(asc::asccomm_urma_sqe_ctx));
        EXPECT_EQ(sge_ctx->len, count * sizeof(T));
        EXPECT_EQ(sge_ctx->va, local_addr);
    }

private:
    alignas(32) uint8_t asccomm_tmp_buf[asc::ASCCOMM_URMA_TMP_BUF_SIZE] = {};
    int64_t block_idx_bak;
};

TEST_F(asccomm_urma_test_suite, AIV_URMA_MAKE_BATCH_HANDLE_UB_POINTER)
{
    urma_channel_resource channel;
    channel.set_queue_state(3U, 2U, 1U);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) uint8_t batch_buf[128] = {};

    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), reinterpret_cast<__ubuf__ uint8_t*>(batch_buf), 128, reinterpret_cast<GM_ADDR>(0x3008));

    EXPECT_EQ(batch_handle.channel_handle, channel.get_handle());
    EXPECT_EQ(batch_handle.buffer.raw_buffer, reinterpret_cast<__ubuf__ uint32_t*>(batch_buf));
    EXPECT_EQ(batch_handle.buffer.buffer_capacity, 2U);
    EXPECT_EQ(batch_handle.sq_context.contextInfo.ubJfs.sqVa, reinterpret_cast<uint64_t>(channel.get_sq_buffer()));
    EXPECT_EQ(batch_handle.sq_context.contextInfo.ubJfs.dbVa, channel.get_sq_doorbell_addr());
    EXPECT_EQ(batch_handle.remote_info.remote_eid_low, 0x1122334455667788ULL);
    EXPECT_EQ(batch_handle.remote_info.remote_eid_high, 0x99AABBCCDDEEFF00ULL);
    EXPECT_EQ(batch_handle.cq_context.contextInfo.ubJfc.scqVa, reinterpret_cast<uint64_t>(channel.get_cq_buffer()));
    EXPECT_EQ(batch_handle.cq_context.contextInfo.ubJfc.dbVa, channel.get_cq_doorbell_addr());
    EXPECT_EQ(batch_handle.remote_info.token_id, 0x123456U);
    EXPECT_EQ(batch_handle.remote_info.token_value, 0x654321U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
    EXPECT_EQ(batch_handle.sq_context.contextInfo.ubJfs.sqDepth, URMA_SQ_DEPTH);
    EXPECT_EQ(batch_handle.remote_info.tp_id, 1U);
    EXPECT_EQ(batch_handle.cq_context.contextInfo.ubJfc.cqDepth, URMA_SQ_DEPTH);
    EXPECT_EQ(batch_handle.cq_context.contextInfo.ubJfc.cqeSize, URMA_CQE_SIZE);
    EXPECT_EQ(batch_handle.cursor.sq_head, 3U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 1U);

    auto& batch_handle_ref = asccomm.get_handle_ref(batch_handle, 1U, reinterpret_cast<GM_ADDR>(0x5008U));
    EXPECT_EQ(&batch_handle_ref, &batch_handle);
    EXPECT_EQ(batch_handle_ref.remote_info.token_id, 0x123456U);
    EXPECT_EQ(batch_handle_ref.remote_info.token_value, 0x654321U);

    auto default_remote_handle =
        asccomm.make_batch_handle(channel.get_handle(), reinterpret_cast<__ubuf__ uint8_t*>(batch_buf), 128);
    EXPECT_EQ(default_remote_handle.remote_info.token_id, 0x123456U);
    EXPECT_EQ(default_remote_handle.remote_info.token_value, 0x654321U);

    auto ignored_remote_addr_handle = asccomm.make_batch_handle(
        channel.get_handle(), reinterpret_cast<__ubuf__ uint8_t*>(batch_buf), 128, reinterpret_cast<GM_ADDR>(0x500U));
    EXPECT_EQ(ignored_remote_addr_handle.channel_handle, channel.get_handle());
    EXPECT_EQ(ignored_remote_addr_handle.buffer.buffer_capacity, 2U);
    EXPECT_EQ(ignored_remote_addr_handle.remote_info.token_id, 0x123456U);
    EXPECT_EQ(ignored_remote_addr_handle.remote_info.token_value, 0x654321U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_MULTI_CHANNEL_BATCH_USES_OUTER_HANDLE_FOR_COMMIT_AND_DRAIN)
{
    static_assert(
        IsSameType<asc::batch_handle<asc::ChannelHandle>, asc::ubc_batch_handle>::value,
        "ChannelHandle must create the execution batch_handle");
    static_assert(
        IsSameType<asc::batch_handle<asc::multi_channel_handle>, asc::ubc_multi_batch_handle>::value,
        "multi_channel_handle must create the peer-selection batch_handle");
    static_assert(
        IsSameType<asc::batch_handle<asc::ubc_batch_handle>, asc::ubc_batch_handle>::value,
        "ubc_batch_handle must resolve to the execution batch_handle");
    static_assert(
        IsSameType<asc::batch_handle<asc::ubc_multi_batch_handle>, asc::ubc_batch_handle>::value,
        "ubc_multi_batch_handle must resolve to its execution batch_handle");

    urma_multi_channel_resource multi_channel;
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batch_buffer{};
    auto multi_batch_handle =
        asccomm.make_batch_handle(multi_channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size());

    auto& batch_handle = asccomm.get_handle_ref(multi_batch_handle, 0U, reinterpret_cast<GM_ADDR>(0x3008U));
    EXPECT_EQ(&batch_handle, &multi_batch_handle.handle);
    EXPECT_EQ(batch_handle.channel_handle, multi_batch_handle.channel_handle);
    EXPECT_EQ(batch_handle.remote_info.token_id, 0x11111U);
    EXPECT_EQ(batch_handle.remote_info.token_value, 0xAAAAU);
    multi_channel.set_remote_buffer_token(0U, 0x44444U, 0xDDDDU);
    ASSERT_EQ(
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x3010U), reinterpret_cast<GM_ADDR>(0x7010U), 8U),
        asc::ASCCOMM_SUCCESS);

    auto& second_peer_handle = asccomm.get_handle_ref(multi_batch_handle, 1U, reinterpret_cast<GM_ADDR>(0x5008U));
    EXPECT_EQ(&second_peer_handle, &batch_handle);
    ASSERT_EQ(
        asccomm.read_nbi(
            second_peer_handle, reinterpret_cast<GM_ADDR>(0x8010U), reinterpret_cast<GM_ADDR>(0x5010U), 8U),
        asc::ASCCOMM_SUCCESS);

    const auto* first_sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(first_sqe->rmt_jetty_or_seg_id, 0x11111U);
    EXPECT_EQ(first_sqe->rmt_token_value, 0xAAAAU);
    EXPECT_EQ(first_sqe->tp_id, 11U);
    const auto* second_sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data() + URMA_WQE_SIZE);
    EXPECT_EQ(second_sqe->rmt_jetty_or_seg_id, 0x22222U);
    EXPECT_EQ(second_sqe->rmt_token_value, 0xBBBBU);
    EXPECT_EQ(second_sqe->tp_id, 22U);

    ASSERT_EQ(asccomm.batch_commit(multi_batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 2U);
    EXPECT_EQ(multi_channel.get_sq_head(), 2U);
    EXPECT_EQ(multi_channel.get_cq_head(), 2U);
    EXPECT_EQ(multi_channel.get_sq_doorbell(), 2U);

    ASSERT_EQ(asccomm.drain(multi_batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_tail, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 2U);
    EXPECT_EQ(multi_channel.get_sq_tail(), 2U);
    EXPECT_EQ(multi_channel.get_cq_tail(), 2U);
    EXPECT_EQ(multi_channel.get_cq_doorbell(), 2U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_NBI_MULTI_SGE)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 4U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    const buf_desc src_descs[3] = {
        {reinterpret_cast<GM_ADDR>(0x2010U), 8U},
        {reinterpret_cast<GM_ADDR>(0x2020U), 16U},
        {reinterpret_cast<GM_ADDR>(0x2030U), 24U},
    };
    // 48B SQE + 3 * 16B SGE = 96B -> 2 WQEBB
    int32_t ret = asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1010), src_descs, 3U);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);

    const auto* sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(sqe->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE));
    EXPECT_EQ(sqe->sge_num, 3U);
    EXPECT_EQ(sqe->rmt_addr_l_or_token_id, 0x1010U);

    const auto* sges =
        reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_EQ(sges[i].len, src_descs[i].len) << "SGE len=" << i;
        EXPECT_EQ(sges[i].va, reinterpret_cast<uint64_t>(src_descs[i].addr)) << "SGE va=" << i;
    }

    // Follow with a single-SGE op: next WQE starts at WQEBB 2 (compile-time path shares the cursor).
    ret = asccomm.write_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2040), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 3U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    const auto* second_sqe =
        reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data() + 2U * URMA_WQE_SIZE);
    EXPECT_EQ(second_sqe->sge_num, 1U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_WITH_NOTIFY_NBI_MULTI_SGE)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 4U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    constexpr uint64_t notify_value = 0x1122334455667788ULL;
    const buf_desc src_descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x2020U), 16U},
        {reinterpret_cast<GM_ADDR>(0x2030U), 24U},
    };
    // 48B SQE + 32B notify ctx + 2 * 16B SGE = 112B -> 2 WQEBB
    int32_t ret = asccomm.write_with_notify_nbi(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1020), src_descs, 2U, reinterpret_cast<GM_ADDR>(0x1030),
        notify_value);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);

    const auto* sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(sqe->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE_WITH_NOTIFY));
    EXPECT_EQ(sqe->sge_num, 2U);

    const auto* notify_ctx =
        reinterpret_cast<const asc::asccomm_urma_notify_ctx*>(batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    EXPECT_EQ(notify_ctx->notify_token_id, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(notify_ctx->notify_token_value, 0x654321U);
    EXPECT_EQ(notify_ctx->notify_addr_l, 0x1030U);
    EXPECT_EQ(notify_ctx->notify_data_l, 0x55667788U);
    EXPECT_EQ(notify_ctx->notify_data_h, 0x11223344U);

    const auto* sges = reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(
        batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_notify_ctx));
    EXPECT_EQ(sges[0].len, 16U);
    EXPECT_EQ(sges[0].va, 0x2020U);
    EXPECT_EQ(sges[1].len, 24U);
    EXPECT_EQ(sges[1].va, 0x2030U);

    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 2U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_NBI)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 128> batch_buffer;
    batch_buffer.fill(0xFFU);
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret =
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 1U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 0U);
    EXPECT_EQ(channel.get_cq_head(), 0U);

    const auto* first_sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(first_sqe->sqe_bb_idx, 0U);
    EXPECT_EQ(first_sqe->rsv0, 0U);
    EXPECT_EQ(first_sqe->nf, 0U);
    EXPECT_EQ(first_sqe->token_en, 1U);
    EXPECT_EQ(first_sqe->rmt_jetty_type, 1U);
    EXPECT_EQ(first_sqe->target_hint, 0U);
    EXPECT_EQ(first_sqe->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE));
    EXPECT_EQ(first_sqe->rsv1, 0U);
    EXPECT_EQ(first_sqe->inline_msg_len, 0U);
    EXPECT_EQ(first_sqe->sge_num, 1U);
    EXPECT_EQ(first_sqe->rmt_jetty_or_seg_id, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(first_sqe->rsv2, 0U);
    EXPECT_EQ(first_sqe->rmt_token_value, 0x654321U);
    EXPECT_EQ(first_sqe->udf_type, 0U);
    EXPECT_EQ(first_sqe->reduce_data_type, 0U);
    EXPECT_EQ(first_sqe->reduce_opcode, 0U);
    EXPECT_EQ(first_sqe->rsv3, 0U);
    EXPECT_EQ(first_sqe->rmt_addr_l_or_token_id, 0x1010U);
    const auto* first_sge =
        reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    EXPECT_EQ(first_sge->len, 8U);
    EXPECT_EQ(first_sge->va, 0x2010U);

    ret = asccomm.write_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 0U);
    EXPECT_EQ(channel.get_cq_head(), 0U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_READ_NBI)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 128> batch_buffer;
    batch_buffer.fill(0xFFU);
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret =
        asccomm.read_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 1U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 0U);
    EXPECT_EQ(channel.get_cq_head(), 0U);

    const auto* first_sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(first_sqe->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::READ));
    EXPECT_NE(first_sqe->flag & (1U << 5U), 0U);
    EXPECT_EQ(first_sqe->token_en, 1U);
    EXPECT_EQ(first_sqe->sge_num, 1U);
    EXPECT_EQ(first_sqe->rmt_jetty_or_seg_id, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(first_sqe->rmt_token_value, 0x654321U);
    EXPECT_EQ(first_sqe->rmt_addr_l_or_token_id, 0x1010U);
    EXPECT_EQ(first_sqe->rmt_addr_h_or_token_value, 0U);
    const auto* first_sge =
        reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    EXPECT_EQ(first_sge->len, 8U);
    EXPECT_EQ(first_sge->va, 0x2010U);

    ret = asccomm.read_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x2020), reinterpret_cast<GM_ADDR>(0x1020), 16);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    const auto* second_sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data() + URMA_WQE_SIZE);
    EXPECT_EQ(second_sqe->flag & (1U << 5U), 0U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_WITH_NOTIFY_NBI_MIXED)
{
    urma_channel_resource channel(8);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 384> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret = asccomm.write_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 1U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 0U);

    constexpr uint64_t notify_value = 0x1122334455667788ULL;
    ret = asccomm.write_with_notify_nbi(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 16,
        reinterpret_cast<GM_ADDR>(0x1030), notify_value);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 3U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 0U);
    EXPECT_EQ(channel.get_cq_head(), 0U);

    const uint8_t* notify_wqe_addr = batch_buffer.data() + URMA_WQE_SIZE;
    const auto* sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(notify_wqe_addr);
    EXPECT_EQ(sqe->opcode, static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE_WITH_NOTIFY));
    EXPECT_EQ(sqe->rmt_jetty_or_seg_id, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(sqe->rmt_token_value, 0x654321U);
    EXPECT_EQ(sqe->rmt_addr_l_or_token_id, 0x1020U);

    const auto* notify_ctx =
        reinterpret_cast<const asc::asccomm_urma_notify_ctx*>(notify_wqe_addr + sizeof(asc::asccomm_urma_sqe_ctx));
    EXPECT_EQ(notify_ctx->notify_token_id, 0x123456U & 0xFFFFFU);
    EXPECT_EQ(notify_ctx->notify_token_value, 0x654321U);
    EXPECT_EQ(notify_ctx->notify_addr_l, 0x1030U);
    EXPECT_EQ(notify_ctx->notify_addr_h, 0U);
    EXPECT_EQ(notify_ctx->notify_data_l, 0x55667788U);
    EXPECT_EQ(notify_ctx->notify_data_h, 0x11223344U);

    const auto* sge = reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(
        notify_wqe_addr + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_notify_ctx));
    EXPECT_EQ(sge->len, 16U);
    EXPECT_EQ(sge->va, 0x2020U);

    ret = asccomm.write_with_notify_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1040), reinterpret_cast<GM_ADDR>(0x2040), 8,
        reinterpret_cast<GM_ADDR>(0x1050), 1);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 5U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    const auto* no_cqe_notify_sqe =
        reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data() + 3U * URMA_WQE_SIZE);
    EXPECT_EQ(no_cqe_notify_sqe->flag & (1U << 5U), 0U);

    ret = asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1060), reinterpret_cast<GM_ADDR>(0x2060), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 6U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 2U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_WITH_NOTIFY_ENCODES64_BIT_ADDRESSES)
{
    constexpr uint64_t remote_base = 0x0000001210001000ULL;
    constexpr uint64_t local_base = 0x0000003420002000ULL;
    constexpr uint64_t remote_addr = remote_base + 0x10U;
    constexpr uint64_t local_addr = local_base + 0x10U;
    constexpr uint64_t notify_addr = remote_base + 0x20U;
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.set_remote_buffer_addr(0U, remote_base);
    channel.set_local_buffer_addr(0U, local_base);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(),
        reinterpret_cast<GM_ADDR>(remote_base + 8U));

    ASSERT_EQ(
        asccomm.write_with_notify_nbi(
            batch_handle, reinterpret_cast<GM_ADDR>(remote_addr), reinterpret_cast<GM_ADDR>(local_addr), 8,
            reinterpret_cast<GM_ADDR>(notify_addr), 1),
        asc::ASCCOMM_SUCCESS);

    const auto* sqe = reinterpret_cast<const asc::asccomm_urma_sqe_ctx*>(batch_buffer.data());
    EXPECT_EQ(sqe->rmt_addr_l_or_token_id, static_cast<uint32_t>(remote_addr));
    EXPECT_EQ(sqe->rmt_addr_h_or_token_value, static_cast<uint32_t>(remote_addr >> 32U));
    const auto* notify_ctx =
        reinterpret_cast<const asc::asccomm_urma_notify_ctx*>(batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    EXPECT_EQ(notify_ctx->notify_addr_l, static_cast<uint32_t>(notify_addr));
    EXPECT_EQ(notify_ctx->notify_addr_h, static_cast<uint32_t>(notify_addr >> 32U));
    const auto* sge = reinterpret_cast<const asc::asccomm_urma_sge_ctx*>(
        batch_buffer.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_notify_ctx));
    EXPECT_EQ(sge->va, local_addr);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_READ_WRITE_NOTIFY_MIXED_COMMIT)
{
    urma_channel_resource channel(8);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 256> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        asccomm.read_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.write_with_notify_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 4U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_cq_head(), 0U);

    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 4U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 4U);
    EXPECT_EQ(channel.get_cq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 4U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_HANDLE_COMMIT_WRAP_AND_REUSE)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.set_sq_head(URMA_BATCH_QUEUE_DEPTH - 1U);
    channel.set_sq_tail(URMA_BATCH_QUEUE_DEPTH - 1U);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 128> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret =
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    ret = asccomm.write_nbi<URMA_NO_CQE_CFG>(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);

    ret = asccomm.batch_commit(batch_handle);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 5U);
    EXPECT_EQ(channel.get_cq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 5U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
    EXPECT_EQ(batch_handle.cursor.sq_head, 5U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);

    ret = asccomm.write_with_notify_nbi(
        batch_handle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
        reinterpret_cast<GM_ADDR>(0x1040), 1);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 7U);
    EXPECT_EQ(channel.get_cq_head(), 2U);
    EXPECT_EQ(channel.get_sq_doorbell(), 7U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
    EXPECT_EQ(batch_handle.cursor.sq_head, 7U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 2U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_WRITE_WITH_NOTIFY_COMMIT_WRAPS_SINGLE_WQE)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    channel.set_sq_head(URMA_BATCH_QUEUE_DEPTH - 1U);
    channel.set_sq_tail(URMA_BATCH_QUEUE_DEPTH - 1U);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 2U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        asccomm.write_with_notify_nbi(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8,
            reinterpret_cast<GM_ADDR>(0x1020), 0x1122334455667788ULL),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);

    EXPECT_EQ(batch_handle.cursor.sq_head, URMA_BATCH_QUEUE_DEPTH + 1U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), URMA_BATCH_QUEUE_DEPTH + 1U);
    EXPECT_EQ(channel.get_cq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), URMA_BATCH_QUEUE_DEPTH + 1U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_ALL_NO_CQE_COMMIT_AND_DRAIN)
{
    urma_channel_resource channel(8);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 4U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        asccomm.read_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x2010), reinterpret_cast<GM_ADDR>(0x1010), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.write_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.write_with_notify_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 4U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 0U);

    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 4U);
    EXPECT_EQ(channel.get_sq_head(), 4U);
    EXPECT_EQ(channel.get_cq_head(), 0U);
    EXPECT_EQ(channel.get_sq_doorbell(), 4U);
    EXPECT_EQ(asccomm.drain(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 0U);
    EXPECT_EQ(channel.get_cq_tail(), 0U);
    EXPECT_EQ(channel.get_cq_doorbell(), 0U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_MULTIPLE_COMMITS_SINGLE_DRAIN)
{
    urma_channel_resource channel(8);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 3U * URMA_WQE_SIZE> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    ASSERT_EQ(
        asccomm.write_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.read_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x2020), reinterpret_cast<GM_ADDR>(0x1020), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 2U);
    EXPECT_EQ(channel.get_sq_head(), 2U);
    EXPECT_EQ(channel.get_cq_head(), 0U);

    ASSERT_EQ(
        asccomm.write_with_notify_nbi<URMA_NO_CQE_CFG>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x1030), reinterpret_cast<GM_ADDR>(0x2030), 8,
            reinterpret_cast<GM_ADDR>(0x1040), 1),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1050), reinterpret_cast<GM_ADDR>(0x2050), 8),
        asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 5U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(channel.get_sq_head(), 5U);
    EXPECT_EQ(channel.get_cq_head(), 1U);
    EXPECT_EQ(channel.get_sq_doorbell(), 5U);

    ASSERT_EQ(asccomm.drain(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 1U);
    EXPECT_EQ(channel.get_cq_tail(), 1U);
    EXPECT_EQ(channel.get_cq_doorbell(), 1U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_DRAIN_WITHOUT_INIT)
{
    urma_channel_resource channel(URMA_BATCH_QUEUE_DEPTH);
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    alignas(32) std::array<uint8_t, 64> batch_buffer{};
    auto batch_handle = asccomm.make_batch_handle(
        channel.get_handle(), wrap_ub_buffer(batch_buffer), batch_buffer.size(), reinterpret_cast<GM_ADDR>(0x1008));

    int32_t ret =
        asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1010), reinterpret_cast<GM_ADDR>(0x2010), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);

    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.drain(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 1U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 1U);
    EXPECT_EQ(channel.get_cq_tail(), 1U);
    EXPECT_EQ(channel.get_cq_doorbell(), 1U);

    ret = asccomm.write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x1020), reinterpret_cast<GM_ADDR>(0x2020), 8);
    ASSERT_EQ(ret, asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.batch_commit(batch_handle), asc::ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.drain(batch_handle), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(batch_handle.cursor.sq_head, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 2U);
    EXPECT_EQ(channel.get_cq_tail(), 2U);
    EXPECT_EQ(channel.get_cq_doorbell(), 2U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_LOCK_UNLOCK)
{
    urma_channel_resource channel;
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    EXPECT_EQ(channel.get_lock(), asc::ASCCOMM_LOCK_FREE);
    EXPECT_EQ(asccomm.lock(channel.get_handle()), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_lock(), asc::ASCCOMM_LOCK_HELD);
    EXPECT_EQ(asccomm.unlock(channel.get_handle()), asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_lock(), asc::ASCCOMM_LOCK_FREE);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_UNLOCK_WITHOUT_LOCK)
{
    urma_channel_resource channel;
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    EXPECT_EQ(channel.get_lock(), asc::ASCCOMM_LOCK_FREE);
    EXPECT_EQ(asccomm.unlock(channel.get_handle()), asc::ASCCOMM_FAILED);
    EXPECT_EQ(channel.get_lock(), asc::ASCCOMM_LOCK_FREE);
}

// read_nbi with default commit=true: auto-commit internally, then Drain succeeds
TEST_F(asccomm_urma_test_suite, AIV_URMA_READ)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
    int32_t ret =
        asccomm.read_nbi(channel.get_handle(), reinterpret_cast<GM_ADDR>(0x5008), reinterpret_cast<GM_ADDR>(0x3008), 8);
    EXPECT_EQ(ret, 0);
    channel.complete_current_sq();
    ret = asccomm.drain(channel.get_handle());
    EXPECT_EQ(ret, 0);
}

// write_nbi with commit=false: explicit Commit then Drain succeeds
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
    int32_t ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8);
    EXPECT_EQ(ret, 0);
    ret = asccomm.commit(channel.get_handle());
    EXPECT_EQ(ret, 0);
    channel.complete_current_sq();
    ret = asccomm.drain(channel.get_handle());
    EXPECT_EQ(ret, 0);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_REDUCE_MAX_INT8)
{
    check_write_reduce<int8_t, asc::asccomm_reduce_op::MAX>(0x0U, 0x8U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_REDUCE_MIN_HALF)
{
    check_write_reduce<half, asc::asccomm_reduce_op::MIN>(0x6U, 0x9U);
}

TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_REDUCE_SUM_FLOAT)
{
    check_write_reduce<float, asc::asccomm_reduce_op::SUM>(0x7U, 0xAU);
}

// write_with_notify_nbi with commit=false: explicit Commit then Drain succeeds
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_WITH_NOTIFY)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
    int32_t ret = asccomm.write_with_notify_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, 0);
    ret = asccomm.commit(channel.get_handle());
    EXPECT_EQ(ret, 0);
    channel.complete_current_sq();
    ret = asccomm.drain(channel.get_handle());
    EXPECT_EQ(ret, 0);
}

// write_value_nbi with default commit=true: inline value carried in WQE, auto-commit then Drain succeeds
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_WITH_VALUE)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
    int32_t ret = asccomm.write_value_nbi<uint64_t>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), 0xCAFEBABEDEADBEEFULL);
    EXPECT_EQ(ret, 0);
    channel.complete_current_sq();
    ret = asccomm.drain(channel.get_handle());
    EXPECT_EQ(ret, 0);
}

// write_value_nbi with commit=false: explicit Commit then Drain succeeds
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_WITH_VALUE_DELAYED_COMMIT)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);
    int32_t ret =
        asccomm.write_value_nbi<uint32_t, false>(channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1008), 0x12345678U);
    EXPECT_EQ(ret, 0);
    ret = asccomm.commit(channel.get_handle());
    EXPECT_EQ(ret, 0);
    channel.complete_current_sq();
    ret = asccomm.drain(channel.get_handle());
    EXPECT_EQ(ret, 0);
}

// write_value_nbi occupies 1 BB (WRITE opcode), advances sqHead by 1
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_WITH_VALUE_WQEBB_CNT)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);

    int32_t ret =
        asccomm.write_value_nbi<uint64_t, false>(channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1008), 0xAAULL);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 1U);

    ret = asccomm.write_value_nbi<uint32_t, false>(channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), 0xBBU);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 2U);
}

// write_with_notify_nbi occupies 2 BBs, write_nbi occupies 1 BB
// Verified by reading sqHead from ChannelEntity
TEST_F(asccomm_urma_test_suite, AIV_URMA_WRITE_WITH_NOTIFY_WQEBB_CNT)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);

    // write_nbi should advance sqHead by 1
    int32_t ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 1U);

    // write_with_notify_nbi should advance sq_pi by 2
    ret = asccomm.write_with_notify_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 3U);

    // Another write_nbi should advance by 1 more
    ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.get_sq_head(), 4U);
}

// Batch: mixed Write + write_with_notify, single channel Commit/Drain covers all
TEST_F(asccomm_urma_test_suite, AIV_URMA_BATCH_COMMIT_DRAIN)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;
    EXPECT_EQ(init_asccomm(asccomm), asc::ASCCOMM_SUCCESS);

    int32_t ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1008), reinterpret_cast<GM_ADDR>(0x2008), 8);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    ret = asccomm.write_with_notify_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8,
        reinterpret_cast<GM_ADDR>(0x33), 1);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x1018), reinterpret_cast<GM_ADDR>(0x2018), 8);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);

    EXPECT_EQ(asccomm.commit(channel.get_handle()), asc::ASCCOMM_SUCCESS);
    channel.complete_current_sq();
    EXPECT_EQ(asccomm.drain(channel.get_handle()), asc::ASCCOMM_SUCCESS);
}

// Init via a 32-byte-aligned local buffer: pass the raw __ubuf__ pointer directly
TEST_F(asccomm_urma_test_suite, AIV_URMA_INIT_UB_POINTER)
{
    urma_channel_resource channel;

    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm;

    // 32-byte-aligned local buffer backing the raw __ubuf__ pointer passed to init
    alignas(32) uint8_t local_buf[asc::ASCCOMM_URMA_TMP_BUF_SIZE] = {};

    // Init with the UB buffer pointer should succeed
    EXPECT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(local_buf), asc::ASCCOMM_URMA_TMP_BUF_SIZE),
        asc::ASCCOMM_SUCCESS);

    // Short buffers and misaligned addresses are debug-trap contract violations.
    // This UT target compiles out ASCENDC_DEBUG, so ASCCOMM_DEBUG_TRAP_IF does not stop
    // execution and Init keeps returning success. Debug builds trap.
    asc::asccomm<asc::COMM_PROTOCOL_UB_CTP> asccomm2;
    EXPECT_EQ(
        asccomm2.init(reinterpret_cast<__ubuf__ uint8_t*>(local_buf), asc::ASCCOMM_URMA_TMP_BUF_SIZE - 1),
        asc::ASCCOMM_SUCCESS);

    // Verify the initialized asccomm can perform operations
    int32_t ret = asccomm.write_nbi<false>(
        channel.get_handle(), reinterpret_cast<GM_ADDR>(0x3008), reinterpret_cast<GM_ADDR>(0x5008), 8);
    EXPECT_EQ(ret, asc::ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.commit(channel.get_handle()), asc::ASCCOMM_SUCCESS);
    channel.complete_current_sq();
    EXPECT_EQ(asccomm.drain(channel.get_handle()), asc::ASCCOMM_SUCCESS);
}

// Dump helpers: full function bodies.
TEST_F(asccomm_urma_test_suite, AIV_URMA_DUMP_HELPERS_VALID)
{
    // Notify dump: full body over a zero-initialized valid notify ctx
    asc::asccomm_urma_notify_ctx notify = {};
    asc::asccomm_urma_dump_notify_ctx(reinterpret_cast<__ubuf__ asc::asccomm_urma_notify_ctx*>(&notify));

    // SGE dump: drive the loop body with sge_num > 0 and a matching SGE array
    constexpr uint32_t sge_num = 2;
    std::vector<uint8_t> buf(sizeof(asc::asccomm_urma_sqe_ctx) + sge_num * sizeof(asc::asccomm_urma_sge_ctx), 0);
    auto* sqe = reinterpret_cast<__ubuf__ asc::asccomm_urma_sqe_ctx*>(buf.data());
    sqe->sge_num = sge_num;
    auto* sge_addr = reinterpret_cast<__ubuf__ uint8_t*>(buf.data() + sizeof(asc::asccomm_urma_sqe_ctx));
    asc::asccomm_urma_dump_sge_ctx(sqe, sge_addr, sizeof(uint8_t));

    // WQE dump with WRITE_WITH_NOTIFY opcode: covers the notify branch inside asccomm_urma_dump_wqe_ctx
    constexpr uint32_t wqe_buf_size =
        sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_notify_ctx) + sizeof(asc::asccomm_urma_sge_ctx);
    std::vector<uint8_t> wqe_buf(wqe_buf_size, 0);
    auto* wqe = reinterpret_cast<__ubuf__ asc::asccomm_urma_sqe_ctx*>(wqe_buf.data());
    wqe->opcode = static_cast<uint32_t>(asc::asccomm_urma_op_code::WRITE_WITH_NOTIFY);
    wqe->sge_num = 1;
    asc::asccomm_urma_dump_wqe_ctx(wqe, sizeof(uint8_t));
}

// Dump AMO ctx: FAA opcode path
TEST_F(asccomm_urma_test_suite, AIV_URMA_DUMP_AMO_CTX_FAA)
{
    // FAA uses atomic_len to determine data size (uint32_t or uint64_t)
    // amo_data_addr = sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx)
    constexpr uint32_t amo_buf_size =
        sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx) + sizeof(uint64_t);
    std::vector<uint8_t> buf(amo_buf_size, 0);
    auto* sqe = reinterpret_cast<__ubuf__ asc::asccomm_urma_sqe_ctx*>(buf.data());
    sqe->opcode = static_cast<uint32_t>(asc::asccomm_urma_op_code::FAA);

    // Test uint32_t atomic_len
    uint32_t* amo_data32 =
        reinterpret_cast<uint32_t*>(buf.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx));
    *amo_data32 = 0x12345678;
    asc::asccomm_urma_dump_amo_ctx(sqe, sizeof(uint32_t));

    // Test uint64_t atomic_len
    uint64_t* amo_data64 =
        reinterpret_cast<uint64_t*>(buf.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx));
    *amo_data64 = 0x12345678ABCDEF00ULL;
    asc::asccomm_urma_dump_amo_ctx(sqe, sizeof(uint64_t));
}

// Dump AMO ctx: CAS opcode path
TEST_F(asccomm_urma_test_suite, AIV_URMA_DUMP_AMO_CTX_CAS)
{
    // CAS reads two values: swap_value and cond_value
    // amo_data_addr = sqe_ctx + sizeof(asccomm_urma_sqe_ctx) + sizeof(asccomm_urma_sge_ctx)
    constexpr uint32_t amo_buf_size =
        sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx) + 2 * sizeof(uint64_t);
    std::vector<uint8_t> buf(amo_buf_size, 0);
    auto* sqe = reinterpret_cast<__ubuf__ asc::asccomm_urma_sqe_ctx*>(buf.data());
    sqe->opcode = static_cast<uint32_t>(asc::asccomm_urma_op_code::CAS);

    // Test uint32_t atomic_len: swap_value at offset 0, cond_value at offset atomic_len
    uint32_t* amo_data32 =
        reinterpret_cast<uint32_t*>(buf.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx));
    amo_data32[0] = 0xDEADBEEF; // swap_value
    amo_data32[1] = 0xCAFEBABE; // cond_value
    asc::asccomm_urma_dump_amo_ctx(sqe, sizeof(uint32_t));

    // Test uint64_t atomic_len: swap_value at offset 0, cond_value at offset atomic_len
    uint64_t* amo_data64 =
        reinterpret_cast<uint64_t*>(buf.data() + sizeof(asc::asccomm_urma_sqe_ctx) + sizeof(asc::asccomm_urma_sge_ctx));
    amo_data64[0] = 0xAAAABBBBCCCCDDDDEULL; // swap_value
    amo_data64[1] = 0x1111222233334444FULL; // cond_value
    asc::asccomm_urma_dump_amo_ctx(sqe, sizeof(uint64_t));
}
