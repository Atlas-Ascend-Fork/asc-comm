/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software: you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include "ub_conn_lite.h"

namespace {
constexpr uint32_t SQ_DEPTH = 4U;
constexpr uint32_t MAX_WRITE_SIZE = 64U;
constexpr uint32_t REMOTE_JETTY_ID = 0x12345U;
constexpr uint32_t REMOTE_TOKEN_VALUE = 0x654321U;

Asc::UbConnLite MakeSendConnection(std::vector<uint8_t>& sqStorage)
{
    sqStorage.assign(SQ_DEPTH * 64U, 0U);
    Asc::UbConnLiteParam param{};
    param.jettyId = 7U;
    param.sqVa = reinterpret_cast<uint64_t>(sqStorage.data());
    param.sqDepth = SQ_DEPTH;
    param.tpn = 9U;
    param.maxReadSize = MAX_WRITE_SIZE;
    param.maxWriteSize = MAX_WRITE_SIZE;
    param.dwqeCacheLocked = false;
    Asc::UbConnLite connection;
    connection.import_context(param);
    return connection;
}

} // namespace

TEST(UbConnLiteSendSqeTest, SendUsesExplicitRemoteTargetAndConditionalToken)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x1000U, 8U, 0U, 0x31U);
    Asc::RmtRmaBufSliceLite target(
        0U, 0U, 0U, REMOTE_JETTY_ID, REMOTE_TOKEN_VALUE, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.send(local, target, cfg, out);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    EXPECT_EQ(sqe->comm.opcode, static_cast<uint32_t>(Asc::UDMA_OPC_SEND));
    EXPECT_EQ(sqe->comm.rmtObjId, REMOTE_JETTY_ID & 0xfffffU);
    EXPECT_EQ(sqe->comm.tokenEn, 1U);
    EXPECT_EQ(sqe->comm.rmtTokenValue, REMOTE_TOKEN_VALUE);
    EXPECT_EQ(sqe->comm.rmtAddr.rmtAddrLow, 0U);
    EXPECT_EQ(sqe->comm.rmtAddr.rmtAddrHigh, 0U);
    EXPECT_EQ(sqe->u.sge.tokenId, 0x31U);
}

TEST(UbConnLiteSendSqeTest, SendWithImmCarriesImmediateAndCanDisableToken)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x2000U, 12U, 0U, 0x32U);
    Asc::RmtRmaBufSliceLite target(0U, 0U, 0U, REMOTE_JETTY_ID, 0U, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};
    constexpr uint64_t immData = 0x1122334455667788ULL;

    connection.send_with_imm(local, target, immData, cfg, out);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    EXPECT_EQ(sqe->comm.opcode, static_cast<uint32_t>(Asc::UDMA_OPC_SEND_WITH_IMM));
    EXPECT_EQ(sqe->comm.rmtObjId, REMOTE_JETTY_ID & 0xfffffU);
    EXPECT_EQ(sqe->comm.tokenEn, 0U);
    EXPECT_EQ(sqe->comm.rmtTokenValue, 0U);
    EXPECT_EQ(sqe->comm.immData.immDataLow, 0x55667788U);
    EXPECT_EQ(sqe->comm.immData.immDataHigh, 0x11223344U);
}

TEST(UbConnLiteSendSqeTest, SendWithImmZeroLengthStillEmitsWqeAndAdvancesPi)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x3000U, 0U, 0U, 0x33U);
    Asc::RmtRmaBufSliceLite target(
        0U, 0U, 0U, REMOTE_JETTY_ID, REMOTE_TOKEN_VALUE, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};
    constexpr uint64_t immData = 0x1122334455667788ULL;

    connection.send_with_imm(local, target, immData, cfg, out);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    EXPECT_EQ(out.pi, 1U);
    EXPECT_EQ(sqe->comm.opcode, static_cast<uint32_t>(Asc::UDMA_OPC_SEND_WITH_IMM));
    EXPECT_EQ(sqe->comm.sgeNum, 0U);
    EXPECT_EQ(sqe->u.sge.length, 0U);
    EXPECT_EQ(sqe->comm.rmtObjId, REMOTE_JETTY_ID & 0xfffffU);
    EXPECT_EQ(sqe->comm.rmtTokenValue, REMOTE_TOKEN_VALUE);
    EXPECT_EQ(sqe->comm.immData.immDataLow, 0x55667788U);
    EXPECT_EQ(sqe->comm.immData.immDataHigh, 0x11223344U);
}

TEST(UbConnLiteSendSqeTest, SendZeroLengthStillEmitsWqeAndAdvancesPi)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x3000U, 0U, 0U, 0U);
    Asc::RmtRmaBufSliceLite target(0U, 0U, 0U, REMOTE_JETTY_ID, 0U, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.send(local, target, cfg, out);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    EXPECT_EQ(out.pi, 1U);
    EXPECT_EQ(sqe->comm.opcode, static_cast<uint32_t>(Asc::UDMA_OPC_SEND));
    EXPECT_EQ(sqe->comm.sgeNum, 0U);
    EXPECT_EQ(sqe->u.sge.length, 0U);
}

TEST(UbConnLiteSendSqeTest, SlicesKeepRemoteTargetWhileAdvancingLocalAddress)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x1000U, MAX_WRITE_SIZE + 8U, 0U, 0U);
    Asc::RmtRmaBufSliceLite target(
        0U, 0U, 0U, REMOTE_JETTY_ID, REMOTE_TOKEN_VALUE, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.send(local, target, cfg, out);

    const auto* first = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    const auto* last = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data() + 64U);
    EXPECT_EQ(first->u.sge.length, MAX_WRITE_SIZE);
    EXPECT_EQ(last->u.sge.length, 8U);
    EXPECT_EQ(last->u.sge.dataAddrLow, 0x1040U);
    EXPECT_EQ(last->comm.rmtObjId, REMOTE_JETTY_ID & 0xfffffU);
    EXPECT_EQ(last->comm.tokenEn, 1U);
    EXPECT_EQ(last->comm.rmtTokenValue, REMOTE_TOKEN_VALUE);
}
