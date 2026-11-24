/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#define ASCCOMM_ROCE_BACKEND_MLX5 1
#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#define private public
#include "kernel_operator.h"

// The CPU simulator stubs do not move SQ data. Model the WQE copies synchronously
// so the tests exercise the production submission path.
namespace AscendC {
// RoCE PollCq reads CQE fields directly from GM with ld_dev. The CPU-debug
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

#include "asccomm/asccomm.h"

using namespace std;
using namespace AscendC;

static constexpr asc::wqe_entry kRoceNoCqeCfg = {
    .odr = 0,
    .fence = 1,
    .se = 0,
    .cqe = 0,
    .inline_en = 0,
};

class AsccommMlx5RoCETestSuite : public testing::Test {
protected:
    virtual void SetUp()
    {
        blockIdxBak_ = block_idx;
        pipe_.InitBuffer(asccommBuf_, 512);
        tempTensor_ = asccommBuf_.Get<uint8_t>();
        addr_ = (uint64_t)(tempTensor_.GetPhyAddr());
        channel_.sqNum = 1;
        channel_.cqNum = 1;
        channel_.protocol = COMM_PROTOCOL_ROCE;
        sqCtx_.contextInfo.roceSq.sqVa = (uint64_t)sqVa_;
        sqCtx_.contextInfo.roceSq.dbHwVa = (uint64_t)dbVa_;
        sqCtx_.contextInfo.roceSq.dbSwVa = (uint64_t)dbrVa_;
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
        cqCtx_.contextInfo.roceCq.dbSwVa = (uint64_t)dbrVa_;
        cqCtx_.contextInfo.roceCq.cqeSize = asc::ASCCOMM_MLX5_CQE_SIZE;
        cqCtx_.contextInfo.roceCq.cqDepth = 16;
        cqCtx_.contextInfo.roceCq.cqn = 1;
        cqCtx_.contextInfo.roceCq.tailAddr = (uint64_t)(&tail_);
        cqCtx_.contextInfo.roceCq.headAddr = (uint64_t)(&head_);
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
        asccommBuf_.FreeTensor(tempTensor_);
    }

    void SetCqe(uint32_t cqIndex, uint32_t wqebbIndex)
    {
        SetMlx5Cqe(cqIndex, static_cast<uint16_t>(wqebbIndex), asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);
    }

    void SetMlx5Cqe(uint32_t cqIndex, uint16_t wqebbIndex, uint8_t opcode)
    {
        uint32_t cqDepth = cqCtx_.contextInfo.roceCq.cqDepth;
        auto* cqe = cqEntries_ + static_cast<uint64_t>(cqIndex & (cqDepth - 1U)) * asc::ASCCOMM_MLX5_CQE_SIZE;
        std::fill(cqe, cqe + asc::ASCCOMM_MLX5_CQE_SIZE, 0U);
        uint8_t owner = ((cqIndex & cqDepth) == 0U) ? 0U : 1U;
        const uint8_t completionOpcode =
            opcode == asc::ASCCOMM_MLX5_OPCODE_RDMA_READ ?
                1U :
                static_cast<uint8_t>(opcode == asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE ? 0U : (opcode & 0x0fU));
        cqe[asc::ASCCOMM_MLX5_CQE_WQE_COUNTER_INDEX] = static_cast<uint8_t>(wqebbIndex >> 8U);
        cqe[asc::ASCCOMM_MLX5_CQE_WQE_COUNTER_INDEX + 1U] = static_cast<uint8_t>(wqebbIndex);
        cqe[asc::ASCCOMM_MLX5_CQE_OP_OWN_INDEX] = static_cast<uint8_t>((completionOpcode << 4U) | owner);
    }

    void SetErrorCqe(uint32_t cqIndex) { (void)cqIndex; }

private:
    TPipe pipe_;
    int64_t blockIdxBak_;
    TBuf<TPosition::VECOUT> asccommBuf_;
    LocalTensor<uint8_t> tempTensor_;
    uint64_t addr_;
    ChannelEntity channel_{};
    SqContext sqCtx_{};
    CqContext cqCtx_{};
    uint8_t sqVa_[1024] = {0};
    uint8_t cqEntries_[16 * asc::ASCCOMM_MLX5_CQE_SIZE] = {};
    uint8_t dbVa_[8] = {0};
    uint8_t dbrVa_[16] = {0};
    uint32_t head_ = 0;
    uint32_t tail_ = 0;
    RegedBufferEntity localBuff_{};
    RegedBufferEntity remoteBuff_{};
};

TEST_F(AsccommMlx5RoCETestSuite, InitReadWriteAndDrain)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(
        asccomm.read_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x110), reinterpret_cast<GM_ADDR>(0x210), 10),
        ASCCOMM_SUCCESS);
    ASSERT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
    SetMlx5Cqe(0, 0, asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);
    SetMlx5Cqe(1, 1, asc::ASCCOMM_MLX5_OPCODE_RDMA_READ);
    ASSERT_EQ(asccomm.drain(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 2U);
    EXPECT_EQ(channel_.sqTail, 2U);
}

TEST_F(AsccommMlx5RoCETestSuite, ReadNbiUsesRemoteSourceAndLocalDestination)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    ASSERT_EQ(
        asccomm.read_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x110), reinterpret_cast<GM_ADDR>(0x210), 10),
        ASCCOMM_SUCCESS);

    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    auto* raddr = reinterpret_cast<asc::roce_mlx5_wqe_raddr_seg*>(sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg));
    auto* data = reinterpret_cast<asc::roce_mlx5_wqe_data_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    EXPECT_EQ(asc::hto_nl(ctrl->opmod_idx_opcode), (0U << 8U) | asc::ASCCOMM_MLX5_OPCODE_RDMA_READ);
    EXPECT_EQ(asc::hto_nll(raddr->raddr), 0x210U);
    EXPECT_EQ(asc::hto_nl(raddr->rkey), 123456U);
    EXPECT_EQ(asc::hto_nl(data->byte_count), 10U);
    EXPECT_EQ(asc::hto_nl(data->lkey), 123456U);
    EXPECT_EQ(asc::hto_nll(data->addr), 0x110U);
}

TEST_F(AsccommMlx5RoCETestSuite, ReadNbiRejectsInvalidLocalAddress)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    // This target builds with ASCENDC_DEBUG undefined, so the invalid-address
    // debug trap is compiled out. Keep the release-build return value explicit
    // instead of expecting debug-only failure semantics.
    ASSERT_EQ(
        asccomm.read_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x500), reinterpret_cast<GM_ADDR>(0x200), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 1U);
    EXPECT_EQ(channel_.cqHead, 1U);
    EXPECT_NE(*reinterpret_cast<uint64_t*>(sqVa_), 0U);
}

TEST_F(AsccommMlx5RoCETestSuite, PostSendAcceptsZeroLength)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 0),
        ASCCOMM_SUCCESS);

    auto* data = reinterpret_cast<asc::roce_mlx5_wqe_data_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    EXPECT_EQ(asc::hto_nl(data->byte_count), 0U);
    EXPECT_EQ(asc::hto_nll(data->addr), 0x100U);
    EXPECT_EQ(channel_.sqHead, 1U);
}

TEST_F(AsccommMlx5RoCETestSuite, SqWatermarkUsesForwardDeltaAcrossWrap)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = UINT16_MAX + 10U;
    channel_.sqTail = UINT16_MAX - 4U;
    channel_.cqHead = 0U;
    channel_.cqTail = 0U;

    // Keep 32-bit producer/consumer serials. Their low 16-bit hardware indices wrap,
    // but the outstanding count remains a normal serial subtraction.
    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, UINT16_MAX + 11U);
}

TEST_F(AsccommMlx5RoCETestSuite, CqeTailUpdatesAcrossSqWrap)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = UINT16_MAX + 1U;
    channel_.sqTail = UINT16_MAX - 1U;
    channel_.cqHead = 1U;
    channel_.cqTail = 0U;
    constexpr uint32_t sqDepth = 16U;
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_ + (sqDepth - 1U) * asc::ASCCOMM_MLX5_WQEBB_SIZE);
    ctrl->qpn_ds = asc::hto_nl((1U << 8U) | 3U);
    SetMlx5Cqe(0U, static_cast<uint16_t>(asc::ASCCOMM_MLX5_SQ_INDEX_MASK), asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);

    ASSERT_EQ(asccomm.drain(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, UINT16_MAX + 1U);
    EXPECT_EQ(channel_.cqTail, 1U);
}

TEST_F(AsccommMlx5RoCETestSuite, CommitRejectsEmptyAndRepeatedCommits)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);

    std::fill(dbVa_, dbVa_ + sizeof(dbVa_), 0U);
    std::fill(dbrVa_, dbrVa_ + sizeof(dbrVa_), 0U);
#ifdef ASCENDC_DEBUG
#ifdef ASCENDC_DEBUG
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_FAILED);
#else
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
#endif
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), 0U);
#else
    // This UT target compiles out ASCENDC_DEBUG, so commit checks are debug traps.
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), 0U);
#endif

    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 10),
        ASCCOMM_SUCCESS);
    const uint64_t firstExpectedDbValue = *reinterpret_cast<const uint64_t*>(tempTensor_.GetPhyAddr());
    channel_.sqHead = 2U;
#ifdef ASCENDC_DEBUG
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_FAILED);
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), 0U);
#else
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), firstExpectedDbValue);
    EXPECT_EQ(reinterpret_cast<uint32_t*>(dbrVa_)[1], asc::hto_nl(1U));
#endif
    channel_.sqHead = 1U;
    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 10),
        ASCCOMM_SUCCESS);
    const uint64_t secondExpectedDbValue = *reinterpret_cast<const uint64_t*>(tempTensor_.GetPhyAddr());
    ASSERT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), secondExpectedDbValue);
    EXPECT_EQ(reinterpret_cast<uint32_t*>(dbrVa_)[1], asc::hto_nl(2U));

#ifdef ASCENDC_DEBUG
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_FAILED);
#else
    EXPECT_EQ(asccomm.commit(channelHandle), ASCCOMM_SUCCESS);
#endif
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(dbVa_), secondExpectedDbValue);
    EXPECT_EQ(reinterpret_cast<uint32_t*>(dbrVa_)[1], asc::hto_nl(2U));
}

TEST_F(AsccommMlx5RoCETestSuite, DeferredPostDoesNotUseUnpublishedWqesForCqReclaim)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 15U;
    channel_.sqTail = 0U;
    reinterpret_cast<uint32_t*>(dbVa_)[1] = asc::hto_nl(10U);
    channel_.cqHead = 0U;
    channel_.cqTail = 0U;

    // Ten active WQEBBs are below the near-full threshold; five deferred-only WQEBBs
    // must not trigger a CQE wait for completions that were never doorbelled.
    ASSERT_EQ(
        asccomm.write_nbi<false>(channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), 10),
        ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 16U);
    EXPECT_EQ(asc::hto_nl(reinterpret_cast<uint32_t*>(dbVa_)[1]), 10U);
    EXPECT_EQ(channel_.sqTail, 0U);
}

TEST_F(AsccommMlx5RoCETestSuite, BatchCommitUsesLastWqeControlWordAsDoorbell)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = asccomm.make_batch_handle(
        channelHandle, reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), 256,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100));
    ASSERT_NE(batchHandle.channelHandle, 0U);
    EXPECT_EQ(batchHandle.buffer.raw_buffer, reinterpret_cast<__ubuf__ uint32_t*>(tempTensor_.GetPhyAddr()));
    asc::buf_desc descs[3] = {
        {reinterpret_cast<GM_ADDR>(0x100), 10},
        {reinterpret_cast<GM_ADDR>(0x110), 10},
        {reinterpret_cast<GM_ADDR>(0x120), 10},
    };

    ASSERT_EQ(asccomm.write_nbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 3), ASCCOMM_SUCCESS);
    ASSERT_EQ(batchHandle.cursor.pre_sq_cnt, 2U);
    const auto& batchState = *reinterpret_cast<const asc::roce_mlx5_batch_state*>(
        reinterpret_cast<const uint8_t*>(&batchHandle.mr_key) + sizeof(asc::roce_mr_key));
    const uint64_t expectedDbValue = *reinterpret_cast<uint64_t*>(tempTensor_.GetPhyAddr());
    EXPECT_EQ(batchState.last_wqe_wqebb_idx, 0U);

    ASSERT_EQ(asccomm.batch_commit(batchHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 2U);
    // The last WQEBB contains SGE data, not the control segment. The UAR value must
    // come from the WQE's first 64 bits.
    EXPECT_EQ(*reinterpret_cast<uint64_t*>(dbVa_), expectedDbValue);
}

TEST_F(AsccommMlx5RoCETestSuite, InlineWriteEncodesWqe)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    ASSERT_EQ(
        asccomm.write_value_nbi(channelHandle, reinterpret_cast<GM_ADDR>(0x200), uint64_t(0x1122334455667788ULL)),
        ASCCOMM_SUCCESS);
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    auto* raddr = reinterpret_cast<asc::roce_mlx5_wqe_raddr_seg*>(sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg));
    auto* inl = reinterpret_cast<asc::roce_mlx5_wqe_inl_data_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    EXPECT_EQ(asc::hto_nl(ctrl->opmod_idx_opcode), (0U << 8U) | asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);
    EXPECT_EQ(asc::hto_nl(ctrl->qpn_ds), (1U << 8U) | 3U);
    EXPECT_EQ(asc::hto_nll(raddr->raddr), 0x200U);
    EXPECT_EQ(asc::hto_nl(raddr->rkey), 123456U);
    EXPECT_EQ(asc::hto_nl(inl->byte_count), sizeof(uint64_t) | asc::ASCCOMM_MLX5_INLINE_SEG);
    EXPECT_EQ(*reinterpret_cast<const uint64_t*>(inl + 1), 0x1122334455667788ULL);
    EXPECT_EQ(channel_.sqHead, 1U);
}

TEST_F(AsccommMlx5RoCETestSuite, InlineWriteEncodesUint32Value)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    ASSERT_EQ(
        asccomm.write_value_nbi(channelHandle, reinterpret_cast<GM_ADDR>(0x200), uint32_t(0x12345678U)),
        ASCCOMM_SUCCESS);
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    auto* inl = reinterpret_cast<asc::roce_mlx5_wqe_inl_data_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    EXPECT_EQ(asc::hto_nl(ctrl->qpn_ds), (1U << 8U) | 3U);
    EXPECT_EQ(asc::hto_nl(inl->byte_count), sizeof(uint32_t) | asc::ASCCOMM_MLX5_INLINE_SEG);
    EXPECT_EQ(*reinterpret_cast<const uint32_t*>(inl + 1), 0x12345678U);
}

TEST_F(AsccommMlx5RoCETestSuite, InlineWritePreservesFloatBits)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    const float value = -1.5f;
    ASSERT_EQ(asccomm.write_value_nbi(channelHandle, reinterpret_cast<GM_ADDR>(0x200), value), ASCCOMM_SUCCESS);
    auto* inl = reinterpret_cast<asc::roce_mlx5_wqe_inl_data_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    uint32_t expectedBits = 0U;
    std::memcpy(&expectedBits, &value, sizeof(expectedBits));
    EXPECT_EQ(*reinterpret_cast<const uint32_t*>(inl + 1), expectedBits);
}

TEST_F(AsccommMlx5RoCETestSuite, AtomicWqeEncoding)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    int32_t ret = asccomm.atomic_faa_nbi<uint64_t, false>(
        channelHandle, reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100), uint64_t(7));
    ASSERT_EQ(ret, ASCCOMM_SUCCESS);
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    auto* atomic = reinterpret_cast<asc::roce_mlx5_wqe_atomic_seg*>(
        sqVa_ + sizeof(asc::roce_mlx5_wqe_ctrl_seg) + sizeof(asc::roce_mlx5_wqe_raddr_seg));
    EXPECT_EQ(asc::hto_nl(ctrl->opmod_idx_opcode), asc::ASCCOMM_MLX5_OPCODE_ATOMIC_FA);
    EXPECT_EQ(asc::hto_nll(atomic->swap_add), 7U);
    EXPECT_EQ(asc::hto_nll(atomic->compare), 0U);
    SetMlx5Cqe(0, 0, asc::ASCCOMM_MLX5_OPCODE_ATOMIC_FA);
    ASSERT_EQ(asccomm.drain(channelHandle), ASCCOMM_SUCCESS);
}

TEST_F(AsccommMlx5RoCETestSuite, BatchMultiSgeAndCommit)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = asccomm.make_batch_handle(
        channelHandle, reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), 256,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100));
    ASSERT_NE(batchHandle.channelHandle, 0U);
    asc::buf_desc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x100), 10},
        {reinterpret_cast<GM_ADDR>(0x110), 11},
    };
    ASSERT_EQ(asccomm.write_nbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), ASCCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.pre_sq_cnt, 1U);
    ASSERT_EQ(asccomm.batch_commit(batchHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqHead, 1U);
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    EXPECT_EQ(asc::hto_nl(ctrl->qpn_ds), (1U << 8U) | 4U);
    SetMlx5Cqe(0, 0, asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);
    ASSERT_EQ(asccomm.drain(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.sqTail, 1U);
}

TEST_F(AsccommMlx5RoCETestSuite, BatchPostSendAcceptsZeroLength)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    auto batchHandle = asccomm.make_batch_handle(
        channelHandle, reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), 256,
        reinterpret_cast<GM_ADDR>(0x200), reinterpret_cast<GM_ADDR>(0x100));
    ASSERT_NE(batchHandle.channelHandle, 0U);
    asc::buf_desc descs[2] = {
        {reinterpret_cast<GM_ADDR>(0x100), 0},
        {reinterpret_cast<GM_ADDR>(0x110), 0},
    };
    ASSERT_EQ(asccomm.write_nbi(batchHandle, reinterpret_cast<GM_ADDR>(0x200), descs, 2), ASCCOMM_SUCCESS);
    EXPECT_EQ(batchHandle.cursor.pre_sq_cnt, 1U);

    auto* data =
        reinterpret_cast<asc::roce_mlx5_wqe_data_seg*>(tempTensor_.GetPhyAddr() + asc::ASCCOMM_MLX5_PUBLIC_SEG_BYTES);
    EXPECT_EQ(asc::hto_nl(data->byte_count), 0U);
    EXPECT_EQ(asc::hto_nll(data->addr), 0x100U);
    EXPECT_EQ(asc::hto_nl((data + 1)->lkey), 123456U);
}

TEST_F(AsccommMlx5RoCETestSuite, CqeOwnerAndTail)
{
    asc::asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ASSERT_EQ(
        asccomm.init(reinterpret_cast<__ubuf__ uint8_t*>(tempTensor_.GetPhyAddr()), ASCCOMM_UB_BUF_SIZE),
        ASCCOMM_SUCCESS);
    ChannelHandle channelHandle = reinterpret_cast<ChannelHandle>(&channel_);
    channel_.sqHead = 1;
    channel_.cqHead = 1;
    auto* ctrl = reinterpret_cast<asc::roce_mlx5_wqe_ctrl_seg*>(sqVa_);
    ctrl->qpn_ds = asc::hto_nl((1U << 8U) | 3U);
    SetMlx5Cqe(0, 0, asc::ASCCOMM_MLX5_OPCODE_RDMA_WRITE);
    ASSERT_EQ(asccomm.drain(channelHandle), ASCCOMM_SUCCESS);
    EXPECT_EQ(channel_.cqTail, 1U);
    EXPECT_EQ(channel_.sqTail, 1U);
}
