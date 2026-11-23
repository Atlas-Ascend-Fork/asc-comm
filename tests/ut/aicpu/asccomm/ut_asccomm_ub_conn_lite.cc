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

#include <algorithm>
#include <limits>
#include <vector>

#include "../../src/aicpu/asccomm/common/exception/invalid_params_exception.h"
#include "ub_conn_lite.h"

namespace {
constexpr uint32_t SQ_DEPTH = 4U;
constexpr uint32_t MAX_WRITE_SIZE = 64U;
constexpr uint32_t REMOTE_JETTY_ID = 0x12345U;
constexpr uint32_t REMOTE_TOKEN_VALUE = 0x654321U;

Asc::UbConnLite MakeSendConnection(std::vector<uint8_t>& sqStorage, uint32_t maxWriteSize = MAX_WRITE_SIZE)
{
    sqStorage.assign(SQ_DEPTH * 64U, 0U);
    Asc::UbConnLiteParam param{};
    param.jettyId = 7U;
    param.sqVa = reinterpret_cast<uint64_t>(sqStorage.data());
    param.sqDepth = SQ_DEPTH;
    param.tpn = 9U;
    param.maxReadSize = MAX_WRITE_SIZE;
    param.maxWriteSize = maxWriteSize;
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

TEST(UbConnLiteSendSqeTest, SendBeyondMaxWriteSizeStaysSingleWqe)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x1000U, MAX_WRITE_SIZE + 8U, 0U, 0U);
    Asc::RmtRmaBufSliceLite target(
        0U, 0U, 0U, REMOTE_JETTY_ID, REMOTE_TOKEN_VALUE, std::numeric_limits<uint32_t>::max());
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.send(local, target, cfg, out);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    const auto* second = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data() + 64U);
    EXPECT_EQ(out.pi, 1U);
    EXPECT_EQ(sqe->u.sge.length, MAX_WRITE_SIZE + 8U);
    EXPECT_EQ(sqe->u.sge.dataAddrLow, 0x1000U);
    EXPECT_EQ(sqe->comm.rmtObjId, REMOTE_JETTY_ID & 0xfffffU);
    EXPECT_EQ(sqe->comm.tokenEn, 1U);
    EXPECT_EQ(sqe->comm.rmtTokenValue, REMOTE_TOKEN_VALUE);
    EXPECT_EQ(second->comm.rmtObjId, 0U);
    EXPECT_EQ(second->comm.rmtTokenValue, 0U);
}

TEST(UbConnLiteWriteWithNotifyTest, FourGiBSliceKeepsLastSliceAndNotifyWqe)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage, std::numeric_limits<uint32_t>::max());
    constexpr uint64_t fourGiB = 1ULL << 32U;
    Asc::RmaBufSliceLite local(0x1000U, fourGiB, 0U, 0x31U);
    Asc::RmtRmaBufSliceLite remote(0x2000U, fourGiB, 0U, 0x41U, 0x51U, std::numeric_limits<uint32_t>::max());
    Asc::RmtRmaBufSliceLite notify(0x3000U, 8U, 0U, 0x61U, 0x71U, 0x81U);
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.write_with_notify(local, remote, cfg, out, notify, 0x1234U);

    const auto* dataWqe = reinterpret_cast<const Asc::UdmaSqeWrite*>(sqStorage.data());
    const auto* notifyWqe = reinterpret_cast<const Asc::UdmaSqeWriteWithNotify*>(sqStorage.data() + 64U);
    EXPECT_EQ(out.pi, 3U);
    EXPECT_EQ(dataWqe->comm.opcode, static_cast<uint32_t>(Asc::UDMA_OPC_WRITE));
    EXPECT_EQ(dataWqe->u.sge.length, std::numeric_limits<uint32_t>::max());
    EXPECT_EQ(notifyWqe->comm.opcode, 0x5U);
    EXPECT_EQ(notifyWqe->localU.sge.length, 1U);
    EXPECT_EQ(notifyWqe->notify.notifyTokenId, 0x61U);
    EXPECT_EQ(notifyWqe->notify.notifyDataLow, 0x1234U);
}

TEST(UbConnLiteWriteReduceWithNotifyTest, MaxWriteSizeLessThanDataTypeRejectsInsteadOfDividingByZero)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage, 2U);
    Asc::RmaBufSliceLite local(0x1000U, 8U, 0U, 0x31U);
    Asc::RmtRmaBufSliceLite remote(0x2000U, 8U, 0U, 0x41U, 0x51U, std::numeric_limits<uint32_t>::max());
    Asc::RmtRmaBufSliceLite notify(0x3000U, 8U, 0U, 0x61U, 0x71U, 0x81U);
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    EXPECT_THROW(
        connection.write_reduce_with_notify(
            Asc::DataType::FP32, Asc::ReduceOp::SUM, local, remote, cfg, out, notify, 0x1234U),
        Asc::InvalidParamsException);
    EXPECT_EQ(out.pi, 0U);
}

TEST(UbConnLiteWriteWithNotifyTest, NotifyWqeKeepsTwoWqebbAreaZeroPadded)
{
    std::vector<uint8_t> sqStorage;
    auto connection = MakeSendConnection(sqStorage);
    Asc::RmaBufSliceLite local(0x1000U, 8U, 0U, 0x31U);
    Asc::RmtRmaBufSliceLite remote(0x2000U, 8U, 0U, 0x41U, 0x51U, std::numeric_limits<uint32_t>::max());
    Asc::RmtRmaBufSliceLite notify(0x3000U, 8U, 0U, 0x61U, 0x71U, 0x81U);
    Asc::SqeConfigLite cfg;
    Asc::ConnLiteOperationOut out{};

    connection.write_with_notify(local, remote, cfg, out, notify, 0x1234U);

    const auto* sqe = reinterpret_cast<const Asc::UdmaSqeWriteWithNotify*>(sqStorage.data());
    ASSERT_EQ(sizeof(*sqe), 96U);
    EXPECT_EQ(out.pi, 2U);
    EXPECT_EQ(sqe->comm.opcode, 0x5U);
    EXPECT_EQ(sqe->localU.sge.length, 8U);
    EXPECT_TRUE(std::all_of(
        sqStorage.begin() + sizeof(*sqe), sqStorage.begin() + 128U, [](uint8_t value) { return value == 0U; }));
}
