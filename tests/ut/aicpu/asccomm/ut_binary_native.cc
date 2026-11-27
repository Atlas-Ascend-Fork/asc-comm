/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "securec.h"
#include "gtest/gtest.h"
#include "ub_conn_lite.h"
#include "rdma_vendor_1825_ops.h"
#include <array>
#include <cstring>
#include <functional>

bool HcclCheckLogLevel(int, int) { return false; }
bool IsErrorToWarn() { return false; }
namespace {
using Queue = std::array<uint8_t, 8 * 64>;
using UbOperation = std::function<void(Asc::UbConnLite&, Asc::ConnLiteOperationOut&)>;
void UbEquivalent(const UbOperation& operation)
{
    for (uint32_t head : {0U, 6U, 7U, 65535U}) {
        Queue captureQueue{}, nativeQueue{}, replayQueue{};
        Asc::UbConnLiteParam param{};
        param.sqVa = reinterpret_cast<uint64_t>(captureQueue.data());
        param.sqDepth = 8;
        param.maxReadSize = 128;
        param.maxWriteSize = 128;
        Asc::UbConnLite capture;
        capture.import_context(param);
        capture.import_queue_state(0);
        AsccommBinaryCache::Entry entry;
        AsccommBinaryCache::Capture() = &entry;
        AsccommBinaryCache::Channel() = 1;
        Asc::ConnLiteOperationOut out{};
        operation(capture, out);
        AsccommBinaryCache::Capture() = nullptr;
        AsccommBinaryCache::Channel() = 0;
        ASSERT_FALSE(entry.events.empty());
        param.sqVa = reinterpret_cast<uint64_t>(nativeQueue.data());
        Asc::UbConnLite native;
        native.import_context(param);
        native.import_queue_state(head);
        operation(native, out);
        param.sqVa = reinterpret_cast<uint64_t>(replayQueue.data());
        Asc::UbConnLite replay;
        replay.import_context(param);
        replay.import_queue_state(head);
        for (const auto& event : entry.events) {
            alignas(8) std::array<uint8_t, 128> bytes{};
            ASSERT_LE(event.length, bytes.size());
            ASSERT_EQ(memcpy_sp(bytes.data(), bytes.size(), entry.bytes.data() + event.offset, event.length), EOK);
            replay.publish_template(bytes.data(), event.length);
        }
        uint32_t nh, rh;
        native.export_queue_state(nh);
        replay.export_queue_state(rh);
        EXPECT_EQ(nativeQueue, replayQueue) << "initial PI=" << head;
        EXPECT_EQ(nh, rh);
    }
}
TEST(BinaryNative, Ub64ReadWriteReduceAndInline)
{
    const Asc::RmaBufSliceLite local(0x10000, 256, 0, 17);
    const Asc::RmtRmaBufSliceLite remote(0x20000, 256, 0, 23, 29, 0);
    Asc::SqeConfigLite cfg;
    cfg.fence = 1;
    cfg.cqeEn = true;
    UbEquivalent([&](auto& c, auto& out) { c.read(local, remote, cfg, out); });
    UbEquivalent([&](auto& c, auto& out) { c.write(local, remote, cfg, out); });
    UbEquivalent(
        [&](auto& c, auto& out) { c.read_reduce({Asc::DataType::FP16, Asc::ReduceOp::SUM}, local, remote, cfg, out); });
    UbEquivalent(
        [&](auto& c, auto& out) { c.write_reduce(Asc::DataType::FP16, Asc::ReduceOp::SUM, local, remote, cfg, out); });
    const uint32_t value = 1;
    UbEquivalent(
        [&](auto& c, auto& out) { c.inline_write(reinterpret_cast<const uint8_t*>(&value), 4, remote, cfg, out); });
}
TEST(BinaryNative, Ub128NotifyWrapAndPadding)
{
    const Asc::RmaBufSliceLite local(0x10000, 256, 0, 17);
    const Asc::RmtRmaBufSliceLite remote(0x20000, 256, 0, 23, 29, 0), notify(0x30000, 4, 0, 31, 37, 0);
    Asc::SqeConfigLite cfg;
    cfg.fence = 1;
    UbEquivalent([&](auto& c, auto& out) { c.write_with_notify(local, remote, cfg, out, notify, 1); });
    UbEquivalent([&](auto& c, auto& out) {
        c.write_reduce_with_notify(Asc::DataType::FP16, Asc::ReduceOp::SUM, local, remote, cfg, out, notify, 1);
    });
}
TEST(BinaryNative, RdmaReadWriteReduceOwnerSsnInvalidAndSoftPi)
{
    for (int kind = 0; kind < 3; ++kind) {
        for (uint32_t head : {0U, 7U, 8U, 15U, 65535U}) {
            Queue captureQueue{}, nativeQueue{}, replayQueue{};
            uint32_t capturePi = 0, nativePi = 0, replayPi = 0;
            Asc::RdmaSqContextLite sq{};
            Asc::RdmaCqContextLite cq{};
            sq.sqVa = reinterpret_cast<uint64_t>(captureQueue.data());
            sq.depth = 8;
            sq.wqeSize = 64;
            sq.dbSwVa = reinterpret_cast<uint64_t>(&capturePi);
            const Asc::RmaBufSliceLite local(0x10000, 128, 0x1234, 0);
            const Asc::RmtRmaBufSliceLite remote(0x20000, 128, 0x5678, 0, 0, 0);
            Asc::SqeConfigLite cfg;
            cfg.fence = 1;
            cfg.cqeEn = true;
            auto execute = [&](Asc::Rdma1825Ops& ops) {
                if (kind == 0) {
                    return ops.read(local, remote, cfg);
                }
                if (kind == 1) {
                    return ops.write(local, remote, cfg);
                }
                return ops.write_reduce(local, remote, cfg, Asc::DataType::FP16, Asc::ReduceOp::SUM);
            };
            Asc::Rdma1825Ops capture(&sq, &cq);
            AsccommBinaryCache::Entry entry;
            AsccommBinaryCache::Capture() = &entry;
            AsccommBinaryCache::Channel() = 1;
            ASSERT_EQ(execute(capture), ASCCOMM_SUCCESS);
            AsccommBinaryCache::Capture() = nullptr;
            AsccommBinaryCache::Channel() = 0;
            ASSERT_EQ(entry.events.size(), 1U);
            sq.sqVa = reinterpret_cast<uint64_t>(nativeQueue.data());
            sq.dbSwVa = reinterpret_cast<uint64_t>(&nativePi);
            Asc::Rdma1825Ops native(&sq, &cq);
            native.import_queue_state(head, head, 0, 0, false);
            ASSERT_EQ(execute(native), ASCCOMM_SUCCESS);
            sq.sqVa = reinterpret_cast<uint64_t>(replayQueue.data());
            sq.dbSwVa = reinterpret_cast<uint64_t>(&replayPi);
            Asc::Rdma1825Ops replay(&sq, &cq);
            replay.import_queue_state(head, head, 0, 0, false);
            Asc::Roce3WqeEntry bytes;
            (void)memcpy_sp(&bytes, sizeof(bytes), entry.bytes.data(), sizeof(bytes));
            ASSERT_EQ(replay.publish_template(bytes), ASCCOMM_SUCCESS);
            EXPECT_EQ(nativeQueue, replayQueue) << "kind=" << kind << " PI=" << head;
            EXPECT_EQ(nativePi, replayPi);
            uint32_t nh, nt, nc, nd, rh, rt, rc, rd;
            bool nf, rf;
            native.export_queue_state(nh, nt, nc, nd, nf);
            replay.export_queue_state(rh, rt, rc, rd, rf);
            EXPECT_EQ(nh, rh);
            EXPECT_EQ(nt, rt);
            EXPECT_EQ(nc, rc);
            EXPECT_EQ(nd, rd);
            EXPECT_EQ(nf, rf);
        }
    }
}
} // namespace
