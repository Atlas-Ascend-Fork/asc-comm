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
#include <vector>
#define private public
#include "kernel_operator.h"
#include "asccomm/asccomm.h"

using namespace std;
using namespace asc;
class asccomm_common_test_suite : public testing::Test {
protected:
    virtual void SetUp()
    {
        block_idx_bak = block_idx;
        channel.sqNum = 1;
        sq_ctx.contextInfo.roceSq.sqVa = (uint64_t)sq_va;
        sq_ctx.contextInfo.roceSq.dbHwVa = (uint64_t)db_va;
        sq_ctx.contextInfo.roceSq.wqeSize = 48;
        sq_ctx.contextInfo.roceSq.depth = 10;
        sq_ctx.contextInfo.roceSq.qpn = 1;
        sq_ctx.contextInfo.roceSq.headAddr = (uint64_t)(&head);
        sq_ctx.contextInfo.roceSq.tailAddr = (uint64_t)(&tail);
        sq_ctx.contextInfo.roceSq.sl = 1;
        channel.sqContextAddr = &sq_ctx;
        channel.localBufferNum = 1;
        local_buff.type = RegedBufferType::REGED_BUFFER_RMA;
        local_buff.bufferInfo.rma.protectionInfo.memInfo.roce.lkey = 123456;
        channel.localBufferAddr = &local_buff;
        channel.remoteBufferNum = 1;
        remote_buff.type = RegedBufferType::REGED_BUFFER_RMA;
        remote_buff.bufferInfo.rma.protectionInfo.memInfo.roce.rkey = 123456;
        channel.remoteBufferAddr = &remote_buff;
    }
    virtual void TearDown() { block_idx = block_idx_bak; }

private:
    int64_t block_idx_bak;
    ChannelEntity channel;
    SqContext sq_ctx;
    uint8_t sq_va[100] = {0};
    uint8_t db_va[8] = {0};
    uint32_t head = 0;
    uint32_t tail = 0;
    RegedBufferEntity local_buff;
    RegedBufferEntity remote_buff;
};

TEST_F(asccomm_common_test_suite, AIV_READ)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret =
        asccomm.asccomm_read_nbi(channel_handle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1);
    EXPECT_EQ(ret, 0);
}

TEST_F(asccomm_common_test_suite, AIV_WRITE)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret =
        asccomm.asccomm_write_nbi(channel_handle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1);
    EXPECT_EQ(ret, 0);
}

TEST_F(asccomm_common_test_suite, AIV_WRITE_WITH_NOTIFY_NBI)
{
    asccomm<asc::COMM_PROTOCOL_ROCE> asccomm;
    ChannelHandle channel_handle = reinterpret_cast<ChannelHandle>(&channel);
    int32_t ret = asccomm.asccomm_write_with_notify_nbi(
        channel_handle, reinterpret_cast<GM_ADDR>(0x11), reinterpret_cast<GM_ADDR>(0x22), 1,
        reinterpret_cast<GM_ADDR>(0x33), 2);
    EXPECT_EQ(ret, -1);
}
