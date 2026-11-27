/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "gtest/gtest.h"

// 仅放开 UB 连接内部状态，便于断言 pi；其余标准头保持原样。
#define private public
#include "ub_conn_lite.h"
#undef private

#include "asccomm_transport.cc"
#include "ascend_hal.h"

bool HcclCheckLogLevel(int, int) { return false; }

bool IsErrorToWarn() { return false; }

void DlogRecord(int32_t, int32_t, const char*, ...) {}

// The full cache backend is linked; only the driver boundary is stubbed.
extern "C" drvError_t halSqCqQuery(uint32_t, struct halSqCqQueryInfo* info)
{
    info->value[0] = 0;
    return DRV_ERROR_NONE;
}
extern "C" drvError_t halSqCqConfig(uint32_t, struct halSqCqConfigInfo*) { return DRV_ERROR_NONE; }
extern "C" drvError_t drvGetLocalDevIDByHostDevID(uint32_t host, uint32_t* local)
{
    *local = host;
    return DRV_ERROR_NONE;
}

namespace {
class AsccommTransportTest : public testing::Test {};
} // namespace

TEST_F(AsccommTransportTest, ImportQueueStateIgnoresEntitySqTail)
{
    Asc::UbConnLite connection;
    connection.import_context(Asc::UbConnLiteParam{});
    HcommJfsContext jfs{};
    jfs.sqHead = 100U;
    jfs.sqTail = 37U;

    import_ub_state(jfs, connection);

    EXPECT_EQ(connection.pi, 100U);
}

TEST_F(AsccommTransportTest, ExportUbStateKeepsEntitySqTail)
{
    Asc::UbConnLite connection;
    connection.import_context(Asc::UbConnLiteParam{});
    connection.import_queue_state(100U);

    HcommJfsContext jfs{};
    jfs.sqHead = 100U;
    jfs.sqTail = 37U;

    export_ub_state(connection, jfs);

    EXPECT_EQ(jfs.sqHead, 100U);
    EXPECT_EQ(jfs.sqTail, 37U); // 实体 ci 未被覆写
}

TEST_F(AsccommTransportTest, ExportUbStateRefusesBackwardWrite)
{
    Asc::UbConnLite connection;
    connection.import_context(Asc::UbConnLiteParam{});
    connection.import_queue_state(90U); // 快照 pi=90

    HcommJfsContext jfs{};
    jfs.sqHead = 100U; // 实体已被推进到 100
    jfs.sqTail = 37U;

    export_ub_state(connection, jfs);

    EXPECT_EQ(jfs.sqHead, 100U); // 保持实体现值，不制造回退
}

TEST_F(AsccommTransportTest, ExportUbStateWritesForwardSnapshot)
{
    Asc::UbConnLite connection;
    connection.import_context(Asc::UbConnLiteParam{});
    connection.import_queue_state(105U);

    HcommJfsContext jfs{};
    jfs.sqHead = 100U;
    jfs.sqTail = 37U;

    export_ub_state(connection, jfs);

    EXPECT_EQ(jfs.sqHead, 105U);
}

TEST_F(AsccommTransportTest, FillCommSqeOwnerUsesRingSlotAcrossWrap)
{
    constexpr uint32_t kSqeSize = 64;
    constexpr uint32_t kSqDepth = 8;
    alignas(64) uint8_t sqBuf[kSqDepth * kSqeSize]{};

    Asc::UbConnLite connection;
    Asc::UbConnLiteParam param{};
    param.sqVa = reinterpret_cast<uint64_t>(sqBuf);
    param.sqDepth = kSqDepth;
    param.maxWriteSize = kSqeSize;
    connection.import_context(param);

    Asc::RmaBufSliceLite local(0x1000U, kSqeSize, 0U, 0U);
    Asc::RmtRmaBufSliceLite remote(0x2000U, kSqeSize, 0U, 0U, 0U, UINT32_MAX);
    Asc::SqeConfigLite cfg{};
    Asc::ConnLiteOperationOut out{};

    // pi=15 已在第二圈，环末槽 7 的 owner 必须仍为 1（裸 pi 比较会漏掉）
    connection.import_queue_state(15U);
    connection.write(local, remote, cfg, out);
    EXPECT_EQ(reinterpret_cast<const Asc::UdmaSqeCommon*>(sqBuf + 7U * kSqeSize)->owner, 1U);
    EXPECT_EQ(out.pi, 16U);

    // 紧随其后写入槽 0，owner 应为 0
    connection.write(local, remote, cfg, out);
    EXPECT_EQ(reinterpret_cast<const Asc::UdmaSqeCommon*>(sqBuf + 0U * kSqeSize)->owner, 0U);
    EXPECT_EQ(out.pi, 17U);
}

TEST_F(AsccommTransportTest, FillCommSqeWithNotifyOwnerUsesStartSlotNotPhase)
{
    constexpr uint32_t kSqeSize = 64;
    constexpr uint32_t kSqDepth = 8;
    alignas(64) uint8_t sqBuf[kSqDepth * kSqeSize]{};

    Asc::UbConnLite connection;
    Asc::UbConnLiteParam param{};
    param.sqVa = reinterpret_cast<uint64_t>(sqBuf);
    param.sqDepth = kSqDepth;
    param.maxWriteSize = kSqeSize;
    connection.import_context(param);

    Asc::RmaBufSliceLite local(0x1000U, kSqeSize, 0U, 0U);
    Asc::RmtRmaBufSliceLite remote(0x2000U, kSqeSize, 0U, 0U, 0U, UINT32_MAX);
    Asc::RmtRmaBufSliceLite notify(0x3000U, 4U, 0U, 0U, 0U, 0U);
    Asc::SqeConfigLite cfg{};
    Asc::ConnLiteOperationOut out{};

    // 起始槽 5：旧实现按 pi+2(=7=depth-1) 会误置 owner=1，按起始槽应为 0
    connection.import_queue_state(5U);
    connection.write_with_notify(local, remote, cfg, out, notify, 1U);
    EXPECT_EQ(reinterpret_cast<const Asc::UdmaSqeCommon*>(sqBuf + 5U * kSqeSize)->owner, 0U);
    EXPECT_EQ(out.pi, 7U);

    // 起始槽 7（环末，2 槽 WQE 跨环）：owner 必须为 1
    connection.write_with_notify(local, remote, cfg, out, notify, 1U);
    EXPECT_EQ(reinterpret_cast<const Asc::UdmaSqeCommon*>(sqBuf + 7U * kSqeSize)->owner, 1U);
    EXPECT_EQ(out.pi, 9U);
}

TEST_F(AsccommTransportTest, FillCommSqeOwnerBoundaryDepth)
{
    Asc::RmtRmaBufSliceLite remote(0x2000U, 64U, 0U, 0U, 0U, UINT32_MAX);
    Asc::SqeConfigLite cfg{};

    Asc::UbConnLite zeroDepth;
    zeroDepth.import_context(Asc::UbConnLiteParam{});
    Asc::UdmaSqeWrite sqeZero{};
    zeroDepth.fill_comm_sqe(&sqeZero.comm, remote, cfg, Asc::UdmaSqOpcode::UDMA_OPC_WRITE, 0U);
    EXPECT_EQ(sqeZero.comm.owner, 0U);

    Asc::UbConnLite oneDepth;
    Asc::UbConnLiteParam param{};
    param.sqDepth = 1U;
    oneDepth.import_context(param);
    Asc::UdmaSqeWrite sqeOne{};
    oneDepth.fill_comm_sqe(&sqeOne.comm, remote, cfg, Asc::UdmaSqOpcode::UDMA_OPC_WRITE, 0U);
    EXPECT_EQ(sqeOne.comm.owner, 1U);
}

TEST_F(AsccommTransportTest, BinaryTemplateOwnerUsesStartingSlotAcrossLaps)
{
    constexpr uint32_t depth = 8;
    alignas(8) uint8_t queue[depth * 64] = {};
    Asc::UbConnLiteParam param{};
    param.sqVa = reinterpret_cast<uint64_t>(queue);
    param.sqDepth = depth;
    Asc::UbConnLite connection;
    connection.import_context(param);
    for (const uint32_t length : {64U, 128U}) {
        for (const uint32_t pi : {5U, 7U, 13U, 15U, 23U, 65535U}) {
            alignas(8) uint8_t bytes[128] = {};
            connection.import_queue_state(pi);
            connection.publish_template(bytes, length);
            const auto* wqe = reinterpret_cast<const Asc::UdmaSqeCommon*>(queue + (pi % depth) * 64);
            EXPECT_EQ(wqe->owner, pi % depth == depth - 1 ? 1U : 0U);
            uint32_t nextPi = 0;
            connection.export_queue_state(nextPi);
            EXPECT_EQ(nextPi, static_cast<uint16_t>(pi + length / 64));
        }
    }
}
