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

// The CPU simulator stubs do not move SQ data. Model the WQE copies synchronously
// so the tests exercise the production submission path.
namespace AscendC {
// RoCE poll_cq reads CQE fields directly from GM with ld_dev. The CPU-debug
// environment does not provide a host-memory overload for this access, so model
// the device load as a normal host read for the RoCE UT.
template <typename T>
inline T ld_dev(__gm__ T* src, int16_t)
{
    return *reinterpret_cast<const T*>(src);
}

template <typename T>
inline void st_dev(T src, __gm__ T* dst, int16_t)
{
    *reinterpret_cast<T*>(dst) = src;
}

} // namespace AscendC

#include "asccomm/asccomm.h"

using namespace std;
using namespace asc;

static constexpr wqe_entry k_roce_no_cqe_cfg = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

class asccomm_roce_test_suite : public testing::Test {
protected:
    virtual void SetUp()
    {
        block_idx_bak = block_idx;
        addr = reinterpret_cast<uint64_t>(asccomm_tmp_buf);
        channel.sqNum = 1;
        channel.cqNum = 1;
        channel.protocol = COMM_PROTOCOL_ROCE;
        sq_ctx.contextInfo.roceSq.sqVa = (uint64_t)sq_va;
        sq_ctx.contextInfo.roceSq.dbHwVa = (uint64_t)db_va;
        sq_ctx.contextInfo.roceSq.dbSwVa = (uint64_t)db_va;
        sq_ctx.contextInfo.roceSq.wqeSize = 64;
        sq_ctx.contextInfo.roceSq.depth = 16;
        sq_ctx.contextInfo.roceSq.qpn = 1;
        sq_ctx.contextInfo.roceSq.headAddr = (uint64_t)(&head);
        sq_ctx.contextInfo.roceSq.tailAddr = (uint64_t)(&tail);
        sq_ctx.contextInfo.roceSq.sl = 1;
        sq_ctx.contextInfo.roceSq.dbVendorSpecified = 1;
        channel.sqContextAddr = &sq_ctx;
        cq_ctx.contextInfo.roceCq.cqVa = (uint64_t)cq_entries;
        cq_ctx.contextInfo.roceCq.dbHwVa = (uint64_t)db_va;
        cq_ctx.contextInfo.roceCq.dbSwVa = (uint64_t)db_va;
        cq_ctx.contextInfo.roceCq.cqeSize = sizeof(roce_cqe_entry);
        cq_ctx.contextInfo.roceCq.cqDepth = 16;
        cq_ctx.contextInfo.roceCq.cqn = 1;
        cq_ctx.contextInfo.roceCq.tailAddr = (uint64_t)(&tail);
        channel.cqContextAddr = &cq_ctx;
        channel.localBufferNum = 1;
        local_buff.type = RegedBufferType::REGED_BUFFER_RMA;
        local_buff.bufferInfo.rma.addr = 0x100;
        local_buff.bufferInfo.rma.size = 100;
        local_buff.bufferInfo.rma.protectionInfo.memInfo.roce.lkey = 123456;
        channel.localBufferAddr = &local_buff;
        channel.remoteBufferNum = 1;
        remote_buff.type = RegedBufferType::REGED_BUFFER_RMA;
        remote_buff.bufferInfo.rma.addr = 0x200;
        remote_buff.bufferInfo.rma.size = 100;
        remote_buff.bufferInfo.rma.protectionInfo.memInfo.roce.rkey = 123456;
        channel.remoteBufferAddr = &remote_buff;
    }
    virtual void TearDown() { block_idx = block_idx_bak; }

    void set_cqe(uint32_t cq_index, uint32_t wqebb_index)
    {
        uint32_t cqDepth = cq_ctx.contextInfo.roceCq.cqDepth;
        auto& cqe = cq_entries[cq_index & (cqDepth - 1U)];
        cqe = {};
        cqe.owner_id_qpn = sq_ctx.contextInfo.roceSq.qpn |
                           (static_cast<uint32_t>((cq_index & cqDepth) != 0U) << ROCE_1825_CQE_OWNER_SHIFT);
        cqe.op_sr_wqebb = static_cast<uint32_t>(HcommRoceOpType::WRITE) << ROCE_1825_CQE_OPCODE_SHIFT;
        cqe.op_sr_wqebb |= wqebb_index & ROCE_SQ_INDEX_MASK;
    }

    void set_error_cqe(uint32_t cq_index)
    {
        uint32_t cqDepth = cq_ctx.contextInfo.roceCq.cqDepth;
        auto& cqe = cq_entries[cq_index & (cqDepth - 1U)];
        cqe = {};
        cqe.owner_id_qpn = sq_ctx.contextInfo.roceSq.qpn |
                           (static_cast<uint32_t>((cq_index & cqDepth) != 0U) << ROCE_1825_CQE_OWNER_SHIFT);
        cqe.op_sr_wqebb = static_cast<uint32_t>(ROCE_1825_CQE_OPTYPE_ERROR) << ROCE_1825_CQE_OPCODE_SHIFT;
        cqe.syndrome = 0x12U;
    }

private:
    int64_t block_idx_bak;
    alignas(32) uint8_t asccomm_tmp_buf[512] = {};
    uint64_t addr;
    ChannelEntity channel{};
    SqContext sq_ctx{};
    CqContext cq_ctx{};
    uint8_t sq_va[1024] = {0};
    roce_cqe_entry cq_entries[16] = {};
    uint8_t db_va[8] = {0};
    uint32_t head = 0;
    uint32_t tail = 0;
    RegedBufferEntity local_buff{};
    RegedBufferEntity remote_buff{};
};

TEST_F(asccomm_roce_test_suite, INIT_READ_NBI_DRAIN)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    // Short buffers and misaligned addresses are debug-trap contract violations.
    // This UT target compiles out ASCENDC_DEBUG, so keep the valid-buffer path.
    EXPECT_EQ(asccomm.asccomm_init((__ubuf__ uint8_t*)(addr), ASCCOMM_UB_BUF_SIZE), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_init((__ubuf__ uint8_t*)(addr), 512), 0);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret = asccomm.asccomm_read_nbi(
        channel_handle, reinterpret_cast<GM_ADDR>(0x110), reinterpret_cast<GM_ADDR>(0x220), 10);
    EXPECT_EQ(ret, 0);
    set_cqe(0, 0);
    ret = asccomm.asccomm_drain(channel_handle);
    EXPECT_EQ(ret, 0);
    EXPECT_EQ(channel.sqTail, 1U);
}

TEST_F(asccomm_roce_test_suite, INIT_WRITE_NBI_DRAIN)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    EXPECT_EQ(
        asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), 0);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret = asccomm.asccomm_write_nbi<false>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = asccomm.asccomm_write_nbi<false>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, 0);
    ret = asccomm.asccomm_commit(channel_handle);
    EXPECT_EQ(ret, 0);
    set_cqe(0, 0);
    set_cqe(1, 1);
    ret = asccomm.asccomm_drain(channel_handle);
    EXPECT_EQ(ret, 0);
}

TEST_F(asccomm_roce_test_suite, INIT_WRITE_WITH_NOTIFY_NBI)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    EXPECT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), 0);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret = asccomm.asccomm_write_with_notify_nbi<false>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10,
        reinterpret_cast<GM_ADDR>(0x310), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
}

TEST_F(asccomm_roce_test_suite, DRAIN_WITHOUT_NEW_CQE_KEEPS_SQ_TAIL)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.cqHead = 1;
    channel.cqTail = 1;
    channel.sqTail = 7U;

    EXPECT_EQ(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqTail, 7U);
}

TEST_F(asccomm_roce_test_suite, DRAIN_CONSUMES_CQE_WHEN_TARGET_WRAPS_TO_ZERO)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 0;
    channel.sqTail = UINT32_MAX;
    channel.cqHead = 0;
    channel.cqTail = UINT32_MAX;
    set_cqe(UINT32_MAX, UINT32_MAX);

    EXPECT_EQ(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.cqTail, 0U);
    EXPECT_EQ(channel.sqTail, 0U);
}

TEST_F(asccomm_roce_test_suite, DRAIN_REJECTS_COUNTER_AT_OR_BEYOND_PRODUCER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 10U;
    channel.sqTail = 9U;
    channel.cqHead = 1;
    channel.cqTail = 0;

    for (uint32_t counter : {10U, 11U}) {
        SCOPED_TRACE(counter);
        set_cqe(0, counter - 1U);
        if (counter == 10U) {
            EXPECT_EQ(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), ASCCOMM_SUCCESS);
            EXPECT_EQ(channel.sqTail, counter);
            EXPECT_EQ(channel.cqTail, 1U);
            channel.cqTail = 0U;
        } else {
            EXPECT_DEATH(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), "");
        }
    }
}

TEST_F(asccomm_roce_test_suite, DRAIN_ACCEPTS_LAST_OUTSTANDING_WQE)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 10U;
    channel.sqTail = 7U;
    channel.cqHead = 1;
    channel.cqTail = 0;
    set_cqe(0, 9);

    EXPECT_EQ(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqTail, 10U);
    EXPECT_EQ(channel.cqTail, 1U);
}

TEST_F(asccomm_roce_test_suite, DRAIN_ERROR_CQE_DOES_NOT_ADVANCE_COUNTERS)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 2U;
    channel.sqTail = 0U;
    channel.cqHead = 1U;
    channel.cqTail = 0U;
    set_error_cqe(0);

    EXPECT_DEATH(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), "");
}

TEST_F(asccomm_roce_test_suite, DRAIN_INVALID_COMPLETION_DOES_NOT_ADVANCE_COUNTERS)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 2U;
    channel.sqTail = 0U;
    channel.cqHead = 1U;
    channel.cqTail = 0U;
    set_cqe(0, 2U);

    EXPECT_DEATH(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), "");
}

TEST_F(asccomm_roce_test_suite, DRAIN_HANDLES16_BIT_WQEBB_COUNTER_WRAP)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    channel.sqHead = 1U;
    channel.sqTail = UINT16_MAX - 1U;
    channel.cqHead = 1;
    channel.cqTail = 0;
    set_cqe(0, 0U);

    EXPECT_EQ(asccomm.asccomm_drain(reinterpret_cast<ChannelHandle>(&channel)), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqTail, channel.sqHead);
    EXPECT_EQ(channel.cqTail, 1U);
}

TEST_F(asccomm_roce_test_suite, WRITE_NBI_WITHOUT_CQE_DOES_NOT_ADVANCE_CQ_HEAD)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 0;
    channel.sqTail = 0;
    channel.cqHead = 0;
    channel.cqTail = 0;

    int32_t ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3, k_roce_no_cqe_cfg>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 1U);
    EXPECT_EQ(channel.cqHead, 0U);
}

TEST_F(asccomm_roce_test_suite, POST_SEND_KEEPS_ONE_SQ_SLOT_FOR_INVALID_MARKER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 14U;
    channel.sqTail = 0;
    channel.cqHead = 14;
    channel.cqTail = 14;

    int32_t ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3, k_roce_no_cqe_cfg>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 15U);
    ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3, k_roce_no_cqe_cfg>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 16U);
}

TEST_F(asccomm_roce_test_suite, POST_SEND_REJECTS_FULL_SQ_AFTER_POLL_CQ)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 15U;
    channel.sqTail = 0U;
    channel.cqHead = 15U;
    channel.cqTail = 15U;

    int32_t ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 16U);
}

TEST_F(asccomm_roce_test_suite, POST_SEND_REJECTS_OVERFLOW_SQ_AFTER_POLL_CQ)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 17U;
    channel.sqTail = 0U;
    channel.cqHead = 17U;
    channel.cqTail = 17U;

    int32_t ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10);
    EXPECT_EQ(ret, ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 18U);
}

TEST_F(asccomm_roce_test_suite, POST_SEND_HANDLES_SQ_COUNTER_WRAP)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 1U;
    channel.sqTail = UINT16_MAX - 1U;
    channel.cqHead = 1U;
    channel.cqTail = 1U;

    EXPECT_EQ(
        (asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3>(
            channel_handle, reinterpret_cast<GM_ADDR>(0x220), reinterpret_cast<GM_ADDR>(0x110), 10)),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 2U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_CHECKS_ACCUMULATED_SQ_CAPACITY)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 14U;
    channel.sqTail = 0;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 128, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi<k_roce_no_cqe_cfg>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 15U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi<k_roce_no_cqe_cfg>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 16U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_HANDLE_ACCEPTS32_BYTE_ALIGNED_BUFFER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 32, 128,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    EXPECT_NE(batch_handle.channel_handle, 0U);
    EXPECT_EQ(batch_handle.buffer.buffer_capacity, 2U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_POLLS_CQ_WHEN_SQ_IS_NEAR_FULL)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 12U;
    channel.sqTail = 0U;
    channel.cqHead = 12U;
    channel.cqTail = 12U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 64, 64, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi<k_roce_no_cqe_cfg>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 13U);
    EXPECT_EQ(channel.cqHead, 12U);
    EXPECT_EQ(channel.cqTail, 12U);
    EXPECT_EQ(channel.sqTail, 0U);
    EXPECT_EQ(batch_handle.cursor.cq_tail, 12U);
    EXPECT_EQ(batch_handle.cursor.sq_tail, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_CHECKS_SINGLE_SGE_SQ_CAPACITY)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 15U;
    channel.sqTail = 0U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 128, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 16U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_POLLS_PRIOR_CQE_WHEN_NEAR_FULL)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    // WQEBB 12 has already been submitted and completed, but its CQE is still
    // outstanding. The new batch starts at WQEBB 13.
    channel.sqHead = 13U;
    channel.sqTail = 0U;
    channel.cqHead = 12U;
    channel.cqTail = 11U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 64, 64, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    set_cqe(11, 11);
    ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 14U);
    EXPECT_EQ(channel.cqHead, 13U);
    EXPECT_EQ(channel.cqTail, 12U);
    EXPECT_EQ(channel.sqTail, 12U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_CHECKS_MULTI_SGE_SQ_CAPACITY)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 14U;
    channel.sqTail = 0U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 128, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    buf_desc descs[4] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
        {reinterpret_cast<GM_ADDR>(0x130), 12},
        {reinterpret_cast<GM_ADDR>(0x140), 13},
    };
    ASSERT_EQ(asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), descs, 4), ASCCOMM_SUCCESS);
    EXPECT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 16U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_HANDLE_KEEPS_DEBUG_TRAP_SEMANTICS_FOR_MISALIGNED_BUFFER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 1, 128, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    // This UT target compiles out ASCENDC_DEBUG, so ASCCOMM_DEBUG_TRAP_IF does not stop
    // execution and make_batch_handle keeps initializing the handle. Debug builds trap.
    EXPECT_NE(batch_handle.channel_handle, 0U);
    EXPECT_EQ(batch_handle.buffer.buffer_capacity, 2U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_POLLS_CQ_THRESHOLD_WITHOUT_INIT)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        reinterpret_cast<ChannelHandle>(&channel), reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 64, 64,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);
    const uint32_t poll_count = cq_ctx.contextInfo.roceCq.cqDepth - ASCCOMM_POLL_CQ_THRESHOLD;

    for (uint32_t i = 0; i <= poll_count; ++i) {
        SCOPED_TRACE(i);
        ASSERT_EQ(
            asccomm.asccomm_write_nbi(
                batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
            ASCCOMM_SUCCESS);
        roce_wqe_entry expected_wqe;
        std::memcpy(&expected_wqe, batch_handle.buffer.raw_buffer, sizeof(expected_wqe));
        set_cqe(i, i);
        ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
        EXPECT_EQ(std::memcmp(sq_va + i * ASCCOMM_ROCE_WQEBB_SIZE, &expected_wqe, sizeof(expected_wqe)), 0);
        EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 0U);
        if (i + 1U == poll_count) {
            // This is the first commit at which the CQ polling threshold is
            // reached; all currently published CQEs are consumed.
            EXPECT_EQ(channel.cqTail, i + 1U);
            EXPECT_EQ(channel.sqTail, i + 1U);
        }
    }

    EXPECT_EQ(asccomm.asccomm_drain(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.cqTail, poll_count + 1U);
    EXPECT_EQ(channel.sqTail, poll_count + 1U);
    EXPECT_EQ(batch_handle.cursor.sq_tail, channel.sqTail);
}

TEST_F(asccomm_roce_test_suite, BATCH_POST_SEND_USES_WQEBB_STRIDE)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 128, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x120), 10),
        ASCCOMM_SUCCESS);

    auto* first_wqe = reinterpret_cast<roce_wqe_entry*>(batch_handle.buffer.raw_buffer);
    auto* second_wqe = first_wqe + 1;
    EXPECT_EQ(second_wqe->task.data_len, first_wqe->task.data_len);
    EXPECT_NE(second_wqe->data.va_local, first_wqe->data.va_local);
}

TEST_F(asccomm_roce_test_suite, BATCH_POST_SEND_USES64_BIT_WQE_ENCODING)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 0;
    channel.sqTail = 0;
    channel.cqHead = 0;
    channel.cqTail = 0;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 256, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.asccomm_read_nbi<k_roce_no_cqe_cfg>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x120), reinterpret_cast<GM_ADDR>(0x220), 11),
        ASCCOMM_SUCCESS);

    roce_wqe_entry expected[2]{};
    asccomm.impl.fill_ctrl_seg<ROCE_DEFAULT_CFG>(&expected[0], 0, channel.sqContextAddr[0].contextInfo.roceSq.depth);
    asccomm.impl.fill_task_seg<ROCE_DEFAULT_CFG, HcommRoceOpType::WRITE>(
        &expected[0], reinterpret_cast<GM_ADDR>(0x200), 10, batch_handle.mr_key.r_key, batch_handle.mr_key.l_key);
    asccomm.impl.fill_data_seg(&expected[0], reinterpret_cast<GM_ADDR>(0x110), 10, batch_handle.mr_key.l_key);

    asccomm.impl.fill_ctrl_seg<k_roce_no_cqe_cfg>(&expected[1], 1, channel.sqContextAddr[0].contextInfo.roceSq.depth);
    asccomm.impl.fill_task_seg<k_roce_no_cqe_cfg, HcommRoceOpType::READ>(
        &expected[1], reinterpret_cast<GM_ADDR>(0x220), 11, batch_handle.mr_key.r_key, batch_handle.mr_key.l_key);
    asccomm.impl.fill_data_seg(&expected[1], reinterpret_cast<GM_ADDR>(0x120), 11, batch_handle.mr_key.l_key);

    auto* actual_words = reinterpret_cast<uint64_t*>(batch_handle.buffer.raw_buffer);
    auto* expected_words = reinterpret_cast<uint64_t*>(expected);
    for (uint32_t i = 0; i < 2 * ASCCOMM_ROCE_WQEBB_U64_NUM; ++i) {
        EXPECT_EQ(actual_words[i], expected_words[i]) << "word=" << i;
    }
}

TEST_F(asccomm_roce_test_suite, BATCH_POST_SEND_SHARES_PUBLIC_HEADER_ACROSS_SGES)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 512), ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 256, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    buf_desc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
    };
    ASSERT_EQ(asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), ASCCOMM_SUCCESS);

    auto* words = reinterpret_cast<uint64_t*>(batch_handle.buffer.raw_buffer);
    roce_wqe_entry expected{};
    asccomm.impl.fill_ctrl_seg<ROCE_DEFAULT_CFG>(&expected, 0, channel.sqContextAddr[0].contextInfo.roceSq.depth);
    expected.ctrl.wf_bdsl &=
        ~hto_ns(static_cast<uint16_t>(0xffU << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    expected.ctrl.wf_bdsl |=
        hto_ns(static_cast<uint16_t>(2U << (ROCE_1825_WQE_DATA_SEG_SHIFT - ROCE_1825_WQE_SECTION_ALIGN_SHIFT)));
    asccomm.impl.fill_task_seg<ROCE_DEFAULT_CFG, HcommRoceOpType::WRITE>(
        &expected, reinterpret_cast<GM_ADDR>(0x200), 21, batch_handle.mr_key.r_key, batch_handle.mr_key.l_key);
    auto* expected_words = reinterpret_cast<uint64_t*>(&expected);
    for (uint32_t i = 0; i < 6; ++i) {
        EXPECT_EQ(words[i], expected_words[i]) << "public word=" << i;
    }

    constexpr uint32_t sge_word_num = sizeof(roce_wqe_data_seg) / sizeof(uint64_t);
    for (uint32_t i = 0; i < 2; ++i) {
        const uint64_t* sge = words + 6 + i * sge_word_num;
        EXPECT_EQ(sge[0], hto_nll(reinterpret_cast<uint64_t>(descs[i].addr))) << "SGE address=" << i;
        EXPECT_EQ(sge[1] & 0xffffffffU, hto_nl(descs[i].len)) << "SGE length=" << i;
        const uint64_t last_sge_bit = i == 1U ? 0x80U : 0U;
        EXPECT_EQ((sge[1] >> 32U) & 0x80U, last_sge_bit) << "SGE last marker=" << i;
    }
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 2U);
    EXPECT_EQ(batch_handle.cursor.cq_head, 1U);
}

TEST_F(asccomm_roce_test_suite, BATCH_POST_SEND_MAINTAINS_WQEBB_CURSOR)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 14U;
    // Leave enough room for one single-SGE WQE (1 WQEBB) plus one 4-SGE WQE (2 WQEBBs)
    // while still preserving one SQ slot for the invalid marker.
    channel.sqTail = 2U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), 256, reinterpret_cast<GM_ADDR>(0x200),
        reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    buf_desc descs[4] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
        {reinterpret_cast<GM_ADDR>(0x130), 12},
        {reinterpret_cast<GM_ADDR>(0x140), 13},
    };
    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), descs, 4), ASCCOMM_SUCCESS);

    const auto* words = reinterpret_cast<const uint64_t*>(batch_handle.buffer.raw_buffer);
    constexpr uint32_t first_wqe_wqebb = 1;
    constexpr uint32_t multi_wqe_wqebb = 2;
    constexpr uint64_t owner_mask = 1ULL << ROCE_1825_WQE_OWNER_SHIFT;
    EXPECT_EQ(words[0] & owner_mask, 0U) << "owner before SQ wrap";
    EXPECT_EQ(words[(first_wqe_wqebb + multi_wqe_wqebb) * ASCCOMM_ROCE_WQEBB_U64_NUM] & owner_mask, 0U)
        << "owner after SQ wrap";
    EXPECT_EQ(batch_handle.cursor.sq_head, 14U);
    EXPECT_EQ(batch_handle.cursor.pre_sq_cnt, 3U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_PRESERVES_WQEBB_COUNTER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 128, 128,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    buf_desc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 11},
    };
    ASSERT_EQ(asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 2U);
    set_cqe(0, 1);
    ASSERT_EQ(asccomm.asccomm_drain(channel_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqTail, 2U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi(batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    const auto* words = reinterpret_cast<const uint64_t*>(batch_handle.buffer.raw_buffer);
    EXPECT_EQ(batch_handle.cursor.sq_head, 2U);
    ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqHead, 3U);
    set_cqe(1, 2);
    ASSERT_EQ(asccomm.asccomm_drain(channel_handle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel.sqTail, 3U);
}

TEST_F(asccomm_roce_test_suite, BATCH_COMMIT_WRITES_NEXT_WQEBB_OWNER_MARKER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 0U;
    channel.sqTail = 0U;
    auto batch_handle = asccomm.asccomm_make_batch_handle(
        channel_handle, reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf) + 128, 128,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110));
    ASSERT_NE(batch_handle.channel_handle, 0U);

    ASSERT_EQ(
        asccomm.asccomm_write_nbi<k_roce_no_cqe_cfg>(
            batch_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.asccomm_batch_commit(batch_handle), ASCCOMM_SUCCESS);

    EXPECT_EQ(sq_va[ASCCOMM_ROCE_WQEBB_SIZE], 0xffU);
    EXPECT_EQ(channel.sqHead, 1U);
}

TEST_F(asccomm_roce_test_suite, MAKE_WQE_WRITES_NEXT_WQEBB_OWNER_MARKER)
{
    asccomm<COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.asccomm_init(reinterpret_cast<__ubuf__ uint8_t*>(asccomm_tmp_buf), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    channel.sqHead = 0U;
    channel.sqTail = 0U;

    int32_t ret = asccomm.asccomm_write_nbi<false, PIPE_S, PIPE_MTE3>(
        channel_handle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x110), 10);
    ASSERT_EQ(ret, ASCCOMM_SUCCESS);

    EXPECT_EQ(sq_va[ASCCOMM_ROCE_WQEBB_SIZE], 0xffU);
    EXPECT_EQ(channel.sqHead, 1U);
}
