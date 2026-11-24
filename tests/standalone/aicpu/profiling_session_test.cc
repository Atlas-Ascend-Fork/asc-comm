/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
// Host protocol test: use the actual reporter and SQE/WQE wire layouts, no NPU or HCOMM calls.
#include <cassert>
#include <string>
#include <unordered_map>
#include "../../../src/aicpu/asccomm/diagnostics/profiling_session.cc"
namespace aicpu {
status_t GetTaskAndStreamId(uint64_t& task, uint32_t& stream)
{
    task = 1;
    stream = 2;
    return static_cast<status_t>(0);
}
} // namespace aicpu
namespace {
std::vector<MsprofAicpuHcclTaskInfo> reported;
std::vector<MsprofAicpuHCCLOPInfo> reportedOps;
std::unordered_map<std::string, uint64_t> hashes;
size_t hashCalls = 0;
uint64_t MockHash(const char* p, size_t n)
{
    ++hashCalls;
    auto key = std::string(p, n);
    auto i = hashes.emplace(key, hashes.size() + 1);
    return i.first->second;
}
int32_t MockReport(uint32_t, const void* data, uint32_t)
{
    const auto& w = *static_cast<const MsprofAdditionalInfo*>(data);
    if (w.type == MSPROF_REPORT_AICPU_MC2_BATCH_HCCL_INFO) {
        auto p = reinterpret_cast<const MsprofAicpuHcclTaskInfo*>(w.data);
        reported.insert(reported.end(), p, p + w.dataLen / sizeof(*p));
    }
    if (w.type == MSPROF_REPORT_AICPU_HCCL_OP_INFO) {
        reportedOps.push_back(*reinterpret_cast<const MsprofAicpuHCCLOPInfo*>(w.data));
    }
    return 0;
}
size_t batchCalls = 0;
size_t singleCalls = 0;
bool failBatch = false;
int32_t MockBatch(uint32_t flag, const void* data, uint32_t length)
{
    ++batchCalls;
    assert(length % sizeof(MsprofAdditionalInfo) == 0);
    if (failBatch) {
        return -1;
    }
    const auto* packets = static_cast<const MsprofAdditionalInfo*>(data);
    for (size_t i = 0; i < length / sizeof(*packets); ++i) {
        MockReport(flag, &packets[i], sizeof(*packets));
    }
    return 0;
}
int32_t CountSingle(uint32_t flag, const void* data, uint32_t length)
{
    ++singleCalls;
    return MockReport(flag, data, length);
}
void Start()
{
    MsprofCommandHandle cmd{};
    cmd.type = PROF_COMMANDHANDLE_TYPE_START;
    cmd.profSwitch = ADPROF_TASK_TIME_L1;
    assert(Callback(0, &cmd, sizeof(cmd)) == 0);
}
void Stop()
{
    MsprofCommandHandle cmd{};
    cmd.type = PROF_COMMANDHANDLE_TYPE_STOP;
    assert(Callback(0, &cmd, sizeof(cmd)) == 0);
}
} // namespace
int main()
{
    std::call_once(init, [] {
        backend.ms = true;
        backend.report = MockReport;
        backend.hash = MockHash;
    });
    AsccommProfOp op{sizeof(AsccommProfOp), 0, 2, 1, 0, 4096, "test-group", "OpType::ALLGATHER", nullptr};
    assert(asccomm_prof_begin(&op) == 0);
    assert(!AscProf::Enabled());
    assert(asccomm_prof_end(0) == 0);
    Start();
    const auto firstEpoch = epoch.load();
    Start();
    assert(epoch.load() == firstEpoch);
    assert(asccomm_prof_begin(&op) == 0);
    Asc::Rt91095StarsMemcpySqe sqe{};
    sqe.header.type = static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_SDMA);
    sqe.header.rtStreamId = 0x1234;
    sqe.header.taskId = 2;
    sqe.u.strideMode0.lengthMove = 4096;
    {
        AscProf::SqeProfileScope copy(&sqe, 1);
        assert(sqe.header.sqeProf == 1);
    }
    assert(sqe.header.sqeProf == 0);
    // The submit hook is called only after tail publication; preparing alone reports nothing.
    assert(reported.empty());
    AscProf::Submitted(13, &sqe, 1);
    assert(asccomm_prof_end(0) == 0);
    assert(reported.size() == 1 && reported[0].streamId == 13 && reported[0].taskId == 0x21234);
    assert(reported[0].workFlowMode == 1 && reported[0].dataSize == 4096);
    // Deferred publication owns bytes, not a pointer into a mutable cache template.
    reported.clear();
    assert(asccomm_prof_begin(&op) == 0);
    {
        AscProf::PublicationScope publication(80);
        for (uint32_t i = 0; i < 70; ++i) {
            sqe.header.rtStreamId = i;
            sqe.header.taskId = 0;
            sqe.u.strideMode0.lengthMove = 4096 + i;
            AscProf::Submitted(13, &sqe, 1);
        }
        assert(reported.empty());
        assert(session.records.empty());
        sqe.header.rtStreamId = 999;
        sqe.u.strideMode0.lengthMove = 0;
    }
    assert(reported.empty());
    assert(session.records.empty());
    assert(asccomm_prof_end(0) == 0);
    assert(reported.size() == 70);
    for (uint32_t i = 0; i < 70; ++i) {
        assert(reported[i].taskId == i && reported[i].dataSize == 4096 + i);
    }
    // Exceptional/early-return paths drain records for already-published tasks.
    reported.clear();
    assert(asccomm_prof_begin(&op) == 0);
    auto earlyReturn = [&]() {
        AscProf::PublicationScope publication(1);
        AscProf::Submitted(13, &sqe, 1);
        return;
    };
    earlyReturn();
    assert(asccomm_prof_end(0) == 0);
    assert(reported.size() == 1);
    // Binary/native observers both use current patched bytes, and a wrapped PI belongs to the same DB.
    reported.clear();
    assert(asccomm_prof_begin(&op) == 0);
    SqContext sq{};
    sq.type = HCOMM_SQ_CONTEXT_TYPE_UB_JFS;
    sq.contextInfo.ubJfs.jfsID = 5;
    ChannelEntity ch{};
    ch.sqNum = 1;
    ch.sqContextAddr = &sq;
    uint64_t channel = reinterpret_cast<uint64_t>(&ch);
    assert(asccomm_prof_bind_channel(channel, 1) == 0);
    Asc::UdmaSqeWrite wqe{};
    wqe.u.sge.length = 128;
    AscProf::Wqe(channel, &wqe, 64, 65535);
    AscProf::Wqe(channel, &wqe, 64, 0);
    Asc::Rt91095StarsUbdmaDBmodeSqe db{};
    db.header.type = static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_UBDMA);
    db.jettyId1 = 5;
    db.piValue1 = 0;
    db.header.rtStreamId = 77;
    {
        AscProf::PublicationScope publication(8);
        AscProf::Submitted(14, &db, 1);
        // A future WQE must not be attributed to this DB, even after PI wrap.
        AscProf::Wqe(channel, &wqe, 64, 1);
        db.piValue1 = 1;
        db.header.rtStreamId = 78;
        AscProf::Submitted(14, &db, 1);
        db.header.rtStreamId = 999;
    }
    assert(session.records.empty());
    // A final non-window publication happens before the deferred metadata work.
    AscProf::Submitted(14, &sqe, 1);
    assert(session.submittedBatches.empty());
    assert(session.records.size() == 4);
    assert(asccomm_prof_end(0) == 0);
    assert(reported.back().taskId == 999);
    reported.pop_back();
    assert(reported.size() == 3 && reported[2].taskId == 78);
    reported.pop_back();
    assert(reported.size() == 2 && reported[0].taskId == 77 && reported[1].streamId == 14);
    assert(reported[0].remoteRank == 1 && reported[0].transportType == 3 && reported[0].linkType == 7);
    // Steady-state reporting retains channel nodes/capacity but no old WQEs.
    auto* retained = &session.channels.at(channel);
    const auto capacity = retained->pending.capacity();
    const auto beforeHashes = hashCalls;
    assert(asccomm_prof_begin(&op) == 0);
    assert(asccomm_prof_bind_channel(channel, 1) == 0);
    assert(&session.channels.at(channel) == retained);
    assert(retained->pending.empty() && retained->pending.capacity() == capacity);
    AscProf::Wqe(channel, &wqe, 64, 2);
    db.piValue1 = 2;
    AscProf::Submitted(14, &db, 1);
    assert(asccomm_prof_end(0) == 0);
    assert(hashCalls == beforeHashes);
    // Notify descriptors can change in place without changing the channel handle.
    RegedNotifyEntity notify{};
    notify.type = HCOMM_REGED_NOTIFY_RMA_RT;
    notify.notifyInfo.rmaRt.addr = 0x123400;
    notify.notifyInfo.rmaRt.notifyId = 17;
    ch.remoteNotifyAddr = &notify;
    ch.remoteNotifyNum = 1;
    assert(asccomm_prof_begin(&op) == 0);
    assert(asccomm_prof_bind_channel(channel, 1) == 0);
    assert(session.channels.at(channel).notifyIds.at(0x123400) == 17);
    assert(asccomm_prof_end(0) == 0);
    notify.notifyInfo.rmaRt.notifyId = 23;
    assert(asccomm_prof_begin(&op) == 0);
    assert(asccomm_prof_bind_channel(channel, 1) == 0);
    assert(session.channels.at(channel).notifyIds.at(0x123400) == 23);
    assert(asccomm_prof_end(0) == 0);
    ch.remoteNotifyNum = 0;
    assert(asccomm_prof_begin(&op) == 0);
    assert(asccomm_prof_bind_channel(channel, 1) == 0);
    assert(session.channels.at(channel).notifyIds.empty());
    assert(asccomm_prof_end(0) == 0);
    // Reusing the same entity address with a different Jetty/peer rebinds it.
    sq.contextInfo.ubJfs.jfsID = 6;
    assert(asccomm_prof_begin(&op) == 0);
    assert(asccomm_prof_bind_channel(channel, 9) == 0);
    assert(session.jettyChannel.count(Jetty(5, 0, 0)) == 0);
    assert(session.channels.at(channel).peer == 9);
    assert(asccomm_prof_end(0) == 0);
    // A channel not bound in the current invocation cannot consume stale WQEs.
    assert(asccomm_prof_begin(&op) == 0);
    AscProf::Wqe(channel, &wqe, 64, 3);
    assert(asccomm_prof_end(0) == -8);
    assert(session.channels.empty());
    // Public caller strings are compared by value, not a reused pointer.
    char group[] = "group-A";
    op.comm = group;
    assert(asccomm_prof_begin(&op) == 0);
    const auto groupA = session.group;
    assert(asccomm_prof_end(0) == 0);
    group[6] = 'B';
    assert(asccomm_prof_begin(&op) == 0);
    assert(session.group != groupA);
    assert(asccomm_prof_end(0) == 0);
    // STOP / START in an operation discards the old segment, never attaches it to the new session.
    reported.clear();
    assert(asccomm_prof_begin(&op) == 0);
    {
        AscProf::PublicationScope publication(1);
        AscProf::Submitted(13, &sqe, 1);
        Stop();
        Start();
    }
    assert(asccomm_prof_end(0) == 0);
    assert(reported.empty());
    // A new profiling generation must register names again.
    const auto previousGenerationHashes = hashCalls;
    // Task IDs can coincide on separate physical queues.
    assert(asccomm_prof_begin(&op) == 0);
    assert(hashCalls > previousGenerationHashes);
    AscProf::Condition(21, 7, true);
    AscProf::Condition(22, 7, false);
    Asc::Rt91095StarsSqeHeader cond{};
    cond.type = static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_COND);
    cond.rtStreamId = 7;
    AscProf::Submitted(21, &cond, 1);
    AscProf::Submitted(22, &cond, 1);
    assert(asccomm_prof_end(0) == 0);
    assert(reported.size() == 2 && reported[0].itemId != reported[1].itemId);
    // Same flush boundary, task order and payload for batch and fallback paths.
    for (bool batching : {false, true}) {
        reported.clear();
        batchCalls = singleCalls = 0;
        backend.report = CountSingle;
        backend.reportBatch = batching ? MockBatch : nullptr;
        assert(asccomm_prof_begin(&op) == 0);
        for (uint32_t i = 0; i < 70; ++i) {
            sqe.header.rtStreamId = i;
            sqe.header.taskId = 0;
            sqe.u.strideMode0.lengthMove = 4096 + i;
            AscProf::Submitted(13, &sqe, 1);
        }
        assert(reported.size() == 64);
        assert(asccomm_prof_end(0) == 0);
        assert(reported.size() == 70);
        assert(batchCalls == (batching ? 2 : 0));
        assert(singleCalls == (batching ? 0 : 35));
        for (uint32_t i = 0; i < 70; ++i) {
            assert(reported[i].taskId == i);
            assert(reported[i].dataSize == 4096 + i);
        }
    }
    reported.clear();
    batchCalls = 0;
    assert(asccomm_prof_begin(&op) == 0);
    session.records.resize(129);
    Flush();
    assert(batchCalls == 0 && reported.size() == 129);
    assert(asccomm_prof_end(0) == 0);
    // Do not duplicate partially accepted batches on transport failure.
    failBatch = true;
    singleCalls = 0;
    assert(asccomm_prof_begin(&op) == 0);
    for (int i = 0; i < 3; ++i) {
        AscProf::Submitted(13, &sqe, 1);
    }
    assert(asccomm_prof_end(0) == -4);
    assert(singleCalls == 0);
    failBatch = false;
    // STOP/START invalidates a pending batch as it does the single path.
    reported.clear();
    assert(asccomm_prof_begin(&op) == 0);
    for (int i = 0; i < 3; ++i) {
        AscProf::Submitted(13, &sqe, 1);
    }
    Stop();
    Start();
    assert(asccomm_prof_end(0) == 0);
    assert(reported.empty());
    // Static descriptions are identical to native decode after address, length,
    // notify binding and profiling-generation changes. They own no live IDs.
    for (unsigned kind = 0; kind < 6; ++kind) {
        Asc::UdmaSqeWriteWithNotify wire{};
        const uint32_t length = kind >= 4 ? 128 : 64;
        auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(&wire);
        common.inlineEn = kind == 1;
        common.opcode = kind == 2 ? static_cast<uint32_t>(Asc::UDMA_OPC_READ) : 0U;
        common.udfFlag = kind == 3 || kind == 5;
        if (common.udfFlag) {
            common.inlinedata.udfData.reduceOp = 0x8;
        }
        AscProf::WqeDescription description;
        assert(AscProf::DescribeWqe(&wire, length, description));
        common.rmtAddr.rmtAddrLow = 0x654300;
        common.rmtAddr.rmtAddrHigh = 1;
        common.inlineMsgLen = kind == 1 ? 4 : common.inlineMsgLen;
        auto& sge = *reinterpret_cast<Asc::UdmaNormalSge*>(reinterpret_cast<uint8_t*>(&wire) + description.localOffset);
        if (!description.inlineData) {
            sge.dataAddrLow = 0x987600;
            sge.dataAddrHigh = 2;
            sge.length = 8192;
        }
        wire.notify.notifyAddrLow = 0x123400;
        ch.remoteNotifyAddr = &notify;
        ch.remoteNotifyNum = 1;
        notify.notifyInfo.rmaRt.notifyId = 41 + kind;
        MsprofAicpuHcclTaskInfo native{};
        for (bool compiled : {false, true}) {
            Stop();
            Start();
            reported.clear();
            assert(asccomm_prof_begin(&op) == 0);
            assert(asccomm_prof_bind_channel(channel, 3) == 0);
            AscProf::Wqe(channel, &wire, length, 9, compiled ? &description : nullptr);
            db.jettyId1 = 6;
            db.piValue1 = 9;
            db.header.rtStreamId = 321;
            AscProf::Submitted(14, &db, 1);
            assert(asccomm_prof_end(0) == 0);
            assert(reported.size() == 1);
            reported[0].timeStamp = 0; // each invocation intentionally samples a fresh timestamp
            if (!compiled) {
                native = reported[0];
            } else {
                assert(std::memcmp(&native, &reported[0], sizeof(native)) == 0);
            }
            assert(reported[0].taskId == 321 && reported[0].remoteRank == 3);
            if (description.notify) {
                assert(reported[0].notifyID == 41 + kind);
            }
        }
    }
    MsprofAdditionalInfo packet{};
    std::memset(packet.data, 0xab, sizeof(packet.data));
    const uint32_t payload = 0x12345678;
    assert(FillPacket(packet, MSPROF_REPORT_AICPU_HCCL_OP_INFO, &payload, sizeof(payload)));
    assert(packet.magicNumber == MSPROF_REPORT_DATA_MAGIC_NUM);
    assert(packet.threadId == ReportThreadId());
    for (size_t i = sizeof(payload); i < sizeof(packet.data); ++i) {
        assert(packet.data[i] == 0);
    }
    Stop();
    assert(asccomm_prof_begin(&op) == 0);
    assert(!AscProf::Enabled());
    assert(asccomm_prof_end(0) == 0);

    Stop();
    MsprofCommandHandle both{};
    both.type = PROF_COMMANDHANDLE_TYPE_START;
    both.profSwitch = ADPROF_TASK_TIME_L0 | ADPROF_TASK_TIME_L1;
    assert(Callback(0, &both, sizeof(both)) == 0);
    op.algorithm = "AllGatherMesh";
    assert(asccomm_prof_begin(&op) == 0);
    const auto algorithmHash = names.algorithmId;
    assert(algorithmHash == hashes.at("AlgType::AllGatherMesh"));
    SqContext unsupported{};
    unsupported.type = HCOMM_SQ_CONTEXT_TYPE_ROCE;
    ChannelEntity other{};
    other.sqContextAddr = &unsupported;
    other.sqNum = 1;
    assert(asccomm_prof_bind_channel(reinterpret_cast<uint64_t>(&other), 1) == 0);
    assert(!AscProf::Enabled());
    assert(session.mask & Asc::ProfilingControl::L0);
    const auto opReports = reportedOps.size();
    assert(asccomm_prof_end(0) == 0);
    assert(reportedOps.size() == opReports + 1);
    assert(reportedOps.back().algType == algorithmHash);
    assert(asccomm_prof_begin(&op) == 0);
    assert(names.algorithmId == algorithmHash);
    assert(asccomm_prof_end(0) == 0);
    op.algorithm = "ReduceScatterMesh";
    assert(asccomm_prof_begin(&op) == 0);
    assert(names.algorithmId != algorithmHash);
    assert(asccomm_prof_end(0) == 0);
}
