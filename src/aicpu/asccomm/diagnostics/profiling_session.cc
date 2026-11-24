/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "profiling_session.h"
#include "profiling_control.h"
#include "../resource/stream/sqe_v82.h"
#include "../resource/connection/udma_data_struct.h"
#include "hcomm_res_entity_defs.h" // existing public resource contract, no HCOMM functions
#include "aicpu_schedule/aicpu_context.h"
#include "log_types.h"
#include <dlfcn.h>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>
#include <unistd.h>
#include <sys/syscall.h>
#include <algorithm>
#if defined(__APPLE__)
#include <pthread.h>
#endif
namespace AscProf {
thread_local bool enabled = false;
void DrainSubmitted() noexcept;
} // namespace AscProf
namespace {
using Register = int32_t (*)(uint32_t, ProfCommandHandle);
using Report = int32_t (*)(uint32_t, const void*, uint32_t);
using Hash = uint64_t (*)(const char*, size_t);
Asc::ProfilingControl control;
std::atomic<uint64_t> epoch{0};
std::mutex callbackMutex;
struct Backend {
    Report report = nullptr;
    Report reportBatch = nullptr;
    Hash hash = nullptr;
    Asc::ProfilingControl::FeatureQuery query = nullptr;
    bool ms = false;
} backend;
std::once_flag init;
int32_t Callback(uint32_t, void* data, uint32_t len)
{
    if (!data || len < sizeof(MsprofCommandHandle)) {
        return -1;
    }
    const auto& c = *static_cast<MsprofCommandHandle*>(data);
    if (c.type == PROF_COMMANDHANDLE_TYPE_START || c.type == PROF_COMMANDHANDLE_TYPE_STOP) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        const uint32_t old = control.Mask();
        const uint32_t next = c.type == PROF_COMMANDHANDLE_TYPE_STOP ?
                                  0U :
                                  old | ((c.profSwitch & ADPROF_TASK_TIME_L0) ? Asc::ProfilingControl::L0 : 0U) |
                                      ((c.profSwitch & ADPROF_TASK_TIME_L1) ? Asc::ProfilingControl::L1 : 0U);
        // A late duplicate START is not a new profiling segment.
        if (next != old) {
            epoch.fetch_add(1, std::memory_order_acq_rel);
            control.Command(c);
            epoch.fetch_add(1, std::memory_order_release);
        }
    }
    return 0;
}
void Init()
{
    backend.ms = dlsym(RTLD_DEFAULT, "MsprofReportBatchAdditionalInfo") != nullptr;
    backend.report = reinterpret_cast<Report>(
        dlsym(RTLD_DEFAULT, backend.ms ? "MsprofReportAdditionalInfo" : "AdprofReportAdditionalInfo"));
    backend.reportBatch = reinterpret_cast<Report>(
        dlsym(RTLD_DEFAULT, backend.ms ? "MsprofReportBatchAdditionalInfo" : "AdprofReportBatchAdditionalInfo"));
    backend.hash = reinterpret_cast<Hash>(dlsym(RTLD_DEFAULT, backend.ms ? "MsprofStr2Id" : "AdprofGetHashId"));
    backend.query =
        reinterpret_cast<Asc::ProfilingControl::FeatureQuery>(dlsym(RTLD_DEFAULT, "AdprofCheckFeatureIsOn"));
    auto reg = reinterpret_cast<Register>(dlsym(RTLD_DEFAULT, "MsprofRegisterCallback"));
#if defined(RTLD_NODELETE)
    // profapi has no callback unregister. Pin our code before publishing its address.
    Dl_info owner{};
    if (backend.ms && (!dladdr(reinterpret_cast<void*>(&Callback), &owner) ||
                       !dlopen(owner.dli_fname, RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE))) {
        backend.report = nullptr;
        return;
    }
#endif
    if (backend.ms && (!reg || reg(AICPU, Callback) != 0)) {
        backend.report = nullptr;
    }
    // Some deployed runtimes replay an existing START asynchronously on late registration.
    // Bootstrap from the public Device feature query, not from HCOMM private state.
    // The lock prevents a concurrent callback from being overwritten by the bootstrap.
    if (backend.ms && backend.query) {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (epoch.load(std::memory_order_acquire) == 0) {
            control.Refresh(false, backend.query);
        }
    }
    // Callback lifetime is the Device kernel DSO lifetime; no unregister API is assumed.
}
uint64_t Clock()
{
    uint64_t t = 0;
#if defined(__aarch64__)
    asm volatile("mrs %0, cntvct_el0" : "=r"(t));
#endif
    return t;
}
// Only immutable, internal task-name literals use pointer keys. Public names are
// owned separately, so a reused caller buffer cannot return an obsolete ID.
struct NameCache {
    uint64_t generation = UINT64_MAX;
    std::unordered_map<const char*, uint64_t> tasks;
    uint64_t taskIds[static_cast<size_t>(AscProf::TaskName::Count)]{};
    uint32_t taskMask = 0;
    std::string comm, op;
    uint64_t group = 0, tag = 0;
    std::string algorithm;
    uint64_t algorithmId = 0, algorithmHash = 0;
};
thread_local NameCache names;
uint64_t HashName(const char* p)
{
    auto found = names.tasks.find(p);
    if (found != names.tasks.end()) {
        return found->second;
    }
    auto id = backend.hash(p, std::strlen(p));
    names.tasks.emplace(p, id);
    return id;
}
uint64_t TaskId(AscProf::TaskName name)
{
    static const char* const strings[] = {
        "",
        "Memcpy",
        "Reduce_Inline",
        "Notify_Wait",
        "Notify_Record",
        "CCore_Wait",
        "CCore_Record",
        "Ub_Inline_Write",
        "Ub_Reduce_Inline",
        "Ub_Write_Or_Read",
        "Reuce_With_Notify",
        "Write_With_Notify"};
    const auto i = static_cast<uint32_t>(name);
    const uint32_t bit = 1U << i;
    if (!(names.taskMask & bit)) {
        names.taskIds[i] = backend.hash(strings[i], std::strlen(strings[i]));
        names.taskMask |= bit;
    }
    return names.taskIds[i];
}
uint64_t Jetty(uint32_t id, uint32_t func, uint32_t die) { return id | (uint64_t(func) << 32) | (uint64_t(die) << 40); }
struct UbTask {
    MsprofAicpuHcclTaskInfo data{};
    AscProf::TaskName name = AscProf::TaskName::Unknown;
    uint16_t endPi = 0;
};
struct Channel {
    uint32_t peer;
    uint64_t jetty;
    std::vector<UbTask> pending;
    bool bound = false;
    std::vector<std::pair<uint64_t, uint32_t>> notifyDescription;
    std::unordered_map<uint64_t, uint64_t> notifyIds;
};
struct SubmittedBatch {
    uint32_t sq, offset, count;
};
struct Session {
    bool deferSubmitted = false;
    std::vector<uint8_t> submittedBytes;
    std::vector<SubmittedBatch> submittedBatches;
    bool active = false;
    uint32_t mask = 0;
    uint64_t generation = 0;
    uint64_t group = 0, tag = 0;
    AsccommProfOpV1 op{};
    int error = 0;
    std::unordered_map<uint64_t, Channel> channels;
    std::unordered_map<uint64_t, uint64_t> jettyChannel;
    std::unordered_map<uint64_t, bool> conditions;
    std::vector<MsprofAicpuHcclTaskInfo> records;
};
thread_local Session session;
bool Refresh()
{
    if (!session.active) {
        return false;
    }
    auto mask = control.Refresh(backend.ms, backend.query);
    if (epoch.load(std::memory_order_acquire) != session.generation || mask != session.mask) {
        // Do not invalidate an active Submitted() iterator. Cleanup occurs at operation end.
        AscProf::enabled = false;
        session.mask = 0; // mid-operation transition: discard segment, never relabel old tasks
        return false;
    }
    return (mask & Asc::ProfilingControl::L1) != 0;
}
uint32_t ReportThreadId()
{
    static thread_local const uint32_t id = []() {
#if defined(__APPLE__)
        uint64_t tid = 0;
        pthread_threadid_np(nullptr, &tid);
        return static_cast<uint32_t>(tid);
#else
        return static_cast<uint32_t>(syscall(SYS_gettid));
#endif
    }();
    return id;
}
bool FillPacket(MsprofAdditionalInfo& wire, uint32_t type, const void* p, size_t n)
{
    if (n > sizeof(wire.data)) {
        session.error = -3;
        return false;
    }
    wire.level = MSPROF_REPORT_AICPU_LEVEL;
    wire.type = type;
    wire.threadId = ReportThreadId();
    wire.dataLen = n;
    wire.timeStamp = Clock();
    std::memcpy(wire.data, p, n);
    // Reused packets may previously have held a longer payload.
    std::memset(wire.data + n, 0, sizeof(wire.data) - n);
    return true;
}
void Send(uint32_t type, const void* p, size_t n)
{
    MsprofAdditionalInfo wire{};
    if (FillPacket(wire, type, p, n) && backend.report(1, &wire, sizeof(wire)) != 0) {
        session.error = -4;
    }
}
void Flush()
{
    if (!AscProf::enabled || !Refresh()) {
        return;
    }
    constexpr size_t max = sizeof(MsprofAdditionalInfo{}.data) / sizeof(MsprofAicpuHcclTaskInfo);
    // Preserve the existing 64-record flush boundary and publication timing.
    // Reuse packet storage; only the transport call is batched. A failed batch
    // is not retried individually because it may already have been accepted.
    constexpr size_t PACKET_CAPACITY = (64 + max - 1) / max;
    static thread_local MsprofAdditionalInfo packets[PACKET_CAPACITY];
    if (backend.reportBatch && session.records.size() > max && session.records.size() <= PACKET_CAPACITY * max) {
        size_t packetCount = 0;
        for (size_t i = 0; i < session.records.size(); i += max) {
            if (!Refresh()) {
                break;
            }
            const auto n = std::min(max, session.records.size() - i);
            if (!FillPacket(
                    packets[packetCount], MSPROF_REPORT_AICPU_MC2_BATCH_HCCL_INFO, session.records.data() + i,
                    n * sizeof(MsprofAicpuHcclTaskInfo))) {
                break;
            }
            ++packetCount;
        }
        if (packetCount && Refresh() &&
            backend.reportBatch(1, packets, packetCount * sizeof(MsprofAdditionalInfo)) != 0) {
            session.error = -4;
        }
    } else {
        for (size_t i = 0; i < session.records.size(); i += max) {
            if (!Refresh()) {
                break;
            }
            const auto n = std::min(max, session.records.size() - i);
            Send(
                MSPROF_REPORT_AICPU_MC2_BATCH_HCCL_INFO, session.records.data() + i,
                n * sizeof(MsprofAicpuHcclTaskInfo));
        }
    }
    session.records.clear();
}
void Append(MsprofAicpuHcclTaskInfo& record, AscProf::TaskName name, uint32_t sq, uint32_t task)
{
    if (sq > UINT16_MAX) {
        session.error = -5;
        return;
    }
    record.itemId = TaskId(name);
    record.cclTag = session.tag;
    record.groupName = session.group;
    record.localRank = session.op.rank;
    record.rankSize = session.op.rankSize;
    record.taskId = task;
    record.streamId = sq;
    record.timeStamp = Clock();
    record.dataType = session.op.dataType;
    record.workFlowMode = 1;
    session.records.push_back(record);
    if (session.records.size() >= 64) {
        Flush();
    }
}
constexpr int PROFILING_INACTIVE = 1;
int BeginOperation(const AsccommProfOpV1* op)
{
    try {
        std::call_once(init, Init);
        auto& state = session;
        auto& nameCache = names;
        if (state.active) {
            return -2;
        }
        do {
            state.generation = epoch.load(std::memory_order_acquire);
            if (state.generation & 1U) {
                continue;
            }
            state.mask = control.Refresh(backend.ms, backend.query);
        } while ((state.generation & 1U) || state.generation != epoch.load(std::memory_order_acquire));
        state.error = 0;
        if (!state.mask) {
            if (nameCache.generation != UINT64_MAX) {
                nameCache = NameCache{};
                state.channels.clear();
                state.jettyChannel.clear();
            }
            return PROFILING_INACTIVE;
        }
        if (!backend.report || !backend.hash) {
            return -3;
        }
        state.op = *op;
        nameCache.algorithmId = 0;
        if (nameCache.generation != state.generation || nameCache.comm != op->comm) {
            nameCache = NameCache{};
            nameCache.generation = state.generation;
            state.channels.clear();
            state.jettyChannel.clear();
            nameCache.group = backend.hash(op->comm, std::strlen(op->comm));
            nameCache.comm = op->comm;
        }
        if (nameCache.op.empty() || nameCache.op != op->op) {
            nameCache.tag = backend.hash(op->op, std::strlen(op->op));
            nameCache.op = op->op;
        }
        state.group = nameCache.group;
        state.tag = nameCache.tag;
        state.op.comm = nullptr;
        state.op.op = nullptr;
        state.active = true;
        AscProf::enabled = (state.mask & Asc::ProfilingControl::L1) != 0;
        return 0;
    } catch (...) {
        return -6;
    }
}
} // namespace
extern "C" __attribute__((visibility("default"))) int asccomm_prof_begin_v1(const AsccommProfOpV1* op)
{
    if (!op || op->size != sizeof(*op) || op->version != 1 || !op->comm || !op->op) {
        return -1;
    }
    const int status = BeginOperation(op);
    return status == PROFILING_INACTIVE ? 0 : status;
}
extern "C" __attribute__((visibility("default"))) int asccomm_prof_begin_v2(const AsccommProfOpV2* op)
{
    if (!op || op->base.size != sizeof(*op) || op->base.version != 2 || !op->algorithm || !op->base.comm ||
        !op->base.op) {
        return -1;
    }
    const int status = BeginOperation(&op->base);
    if (status != 0) {
        return status == PROFILING_INACTIVE ? 0 : status;
    }
    if (!(session.mask & Asc::ProfilingControl::L0)) {
        return 0;
    }
    try {
        auto& nameCache = names;
        if (nameCache.algorithm != op->algorithm || nameCache.algorithmHash == 0) {
            nameCache.algorithm = op->algorithm;
            const std::string label = "AlgType::" + nameCache.algorithm;
            nameCache.algorithmHash = backend.hash(label.data(), label.size());
        }
        nameCache.algorithmId = nameCache.algorithmHash;
        return 0;
    } catch (...) {
        session.error = -6;
        session.active = false;
        AscProf::enabled = false;
        return -6;
    }
}
extern "C" __attribute__((visibility("default"))) int asccomm_prof_bind_channel_v1(uint64_t handle, uint32_t peer)
{
    if (!AscProf::Enabled()) {
        return 0;
    }
    try {
        const auto& c = *reinterpret_cast<const ChannelEntity*>(handle);
        if (!c.sqContextAddr || !c.sqNum) {
            return -1;
        }
        if (c.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_UB_JFS) {
            // L1 WQE decoding currently supports UB only. Keep L0 op reporting
            // and native communication; never interpret another protocol as UB.
            AscProf::enabled = false;
            return 0;
        }
        auto key = Jetty(c.sqContextAddr[0].contextInfo.ubJfs.jfsID, c.devInfo.funcId, c.devInfo.dieId);
        auto found = session.jettyChannel.find(key);
        if (found != session.jettyChannel.end() && found->second != handle &&
            session.channels.at(found->second).bound) {
            return -3;
        }
        auto entry = session.channels.find(handle);
        const bool fresh = entry == session.channels.end();
        if (fresh) {
            entry = session.channels.emplace(handle, Channel{}).first;
        }
        auto& channel = entry->second;
        if (!fresh && channel.jetty != key) {
            auto old = session.jettyChannel.find(channel.jetty);
            if (old != session.jettyChannel.end() && old->second == handle) {
                session.jettyChannel.erase(old);
            }
        }
        channel.peer = peer;
        channel.jetty = key;
        channel.bound = true;
        // Validate values, not just pointers: a shared resource can be rebuilt
        // at the same address. PI/CI and pending WQEs are never cached here.
        size_t index = 0;
        bool unchanged = !fresh;
        for (uint32_t i = 0; c.remoteNotifyAddr && i < c.remoteNotifyNum; ++i) {
            const auto& n = c.remoteNotifyAddr[i];
            if (n.type != HCOMM_REGED_NOTIFY_RMA_RT) {
                continue;
            }
            if (index >= channel.notifyDescription.size() ||
                channel.notifyDescription[index].first != n.notifyInfo.rmaRt.addr ||
                channel.notifyDescription[index].second != static_cast<uint32_t>(n.notifyInfo.rmaRt.notifyId)) {
                unchanged = false;
            }
            ++index;
        }
        if (!unchanged || index != channel.notifyDescription.size()) {
            channel.notifyDescription.clear();
            channel.notifyIds.clear();
            channel.notifyDescription.reserve(index);
            for (uint32_t i = 0; c.remoteNotifyAddr && i < c.remoteNotifyNum; ++i) {
                const auto& n = c.remoteNotifyAddr[i];
                if (n.type == HCOMM_REGED_NOTIFY_RMA_RT) {
                    const auto id = static_cast<uint32_t>(n.notifyInfo.rmaRt.notifyId);
                    channel.notifyDescription.emplace_back(n.notifyInfo.rmaRt.addr, id);
                    channel.notifyIds[n.notifyInfo.rmaRt.addr] = id;
                }
            }
        }
        if (found == session.jettyChannel.end() || found->second != handle) {
            session.jettyChannel[key] = handle;
        }
        return 0;
    } catch (...) {
        session.error = -6;
        return -6;
    }
}
extern "C" __attribute__((visibility("default"))) int asccomm_prof_end_v1(int)
{
    if (!session.active) {
        return 0;
    }
    try {
        AscProf::DrainSubmitted();
        Flush();
        if ((session.mask & Asc::ProfilingControl::L0) && epoch.load() == session.generation &&
            (control.Refresh(backend.ms, backend.query) & Asc::ProfilingControl::L0)) {
            uint64_t task = 0;
            uint32_t stream = 0;
            if (aicpu::GetTaskAndStreamId && aicpu::GetTaskAndStreamId(task, stream) == 0 && task <= UINT32_MAX) {
                MsprofAicpuHCCLOPInfo op{};
                op.taskId = task;
                op.streamId = stream;
                op.algType = names.algorithmId ? names.algorithmId : HashName("AlgType::UNKNOWN");
                op.count = session.op.count;
                op.dataType = session.op.dataType;
                op.groupName = session.group;
                op.ranksize = session.op.rankSize;
                Send(MSPROF_REPORT_AICPU_HCCL_OP_INFO, &op, sizeof(op));
            } else {
                session.error = -7;
            }
        }
    } catch (...) {
        session.error = -6;
    }
    int ret = session.error;
    AscProf::enabled = false;
    session.active = false;
    session.records.clear();
    for (auto it = session.channels.begin(); it != session.channels.end();) {
        auto& channel = it->second;
        if (!channel.bound || ret != 0) {
            auto key = session.jettyChannel.find(channel.jetty);
            if (key != session.jettyChannel.end() && key->second == it->first) {
                session.jettyChannel.erase(key);
            }
            it = session.channels.erase(it);
        } else {
            channel.pending.clear();
            channel.bound = false;
            ++it;
        }
    }
    session.conditions.clear();
    return ret;
}
namespace AscProf {
void Condition(uint32_t sq, uint32_t task, bool wait) noexcept
{
    if (Enabled()) {
        try {
            session.conditions[(uint64_t(sq) << 32) | task] = wait;
        } catch (...) {
            session.error = -6;
        }
    }
}
bool DescribeWqe(const void* bytes, uint32_t length, WqeDescription& d) noexcept
{
    d = {};
    if (!bytes || (length != 64 && length != 128)) {
        return false;
    }
    const auto& c = *static_cast<const Asc::UdmaSqeCommon*>(bytes);
    d.inlineData = c.inlineEn;
    d.read = c.opcode == Asc::UDMA_OPC_READ;
    d.notify = length == 128;
    d.localOffset = d.notify ? offsetof(Asc::UdmaSqeWriteWithNotify, localU) : offsetof(Asc::UdmaSqeWrite, u);
    if (c.udfFlag) {
        switch (c.inlinedata.udfData.reduceOp) {
            case 0xA:
                d.opType = 0;
                break;
            case 0x8:
                d.opType = 2;
                break;
            case 0x9:
                d.opType = 3;
                break;
            default:
                return false;
        }
    }
    d.name = d.inlineData ? TaskName::UbInlineWrite : c.udfFlag ? TaskName::UbReduceInline : TaskName::UbWriteOrRead;
    if (d.notify) {
        d.name = c.udfFlag ? TaskName::ReduceWithNotify : TaskName::WriteWithNotify;
    }
    return true;
}
void Wqe(
    uint64_t channel, const void* bytes, uint32_t length, uint16_t endPi, const WqeDescription* description) noexcept
{
    if (!Enabled()) {
        return;
    }
    try {
        if (!session.deferSubmitted && !session.submittedBatches.empty()) {
            DrainSubmitted();
        }
        auto it = session.channels.find(channel);
        if (it == session.channels.end() || !it->second.bound) {
            session.error = -8;
            return;
        }
        if (length != 64 && length != 128) {
            session.error = -9;
            return;
        }
        WqeDescription native;
        if (!description || description->name == TaskName::Unknown) {
            if (!DescribeWqe(bytes, length, native)) {
                session.error = -12;
                return;
            }
            description = &native;
        }
        const auto& d = *description;
        const auto& c = *static_cast<const Asc::UdmaSqeCommon*>(bytes);
        const auto& sge =
            *reinterpret_cast<const Asc::UdmaNormalSge*>(static_cast<const uint8_t*>(bytes) + d.localOffset);
        // Construct in retained channel storage, avoiding an intermediate record copy.
        auto& list = it->second.pending;
        list.emplace_back();
        auto& t = list.back();
        t.endPi = endPi;
        t.name = d.name;
        t.data.remoteRank = it->second.peer;
        t.data.linkType = 7;
        t.data.transportType = 3;
        t.data.notifyID = UINT64_MAX;
        t.data.opType = d.opType;
        t.data.dstAddr = uint64_t(c.rmtAddr.rmtAddrLow) | (uint64_t(c.rmtAddr.rmtAddrHigh) << 32);
        t.data.srcAddr = d.inlineData ? 0 : uint64_t(sge.dataAddrLow) | (uint64_t(sge.dataAddrHigh) << 32);
        t.data.dataSize = d.inlineData ? c.inlineMsgLen : sge.length;
        if (d.inlineData) {
            auto id = it->second.notifyIds.find(t.data.dstAddr);
            if (id != it->second.notifyIds.end()) {
                t.data.notifyID = id->second;
            }
        }
        if (d.read) {
            std::swap(t.data.srcAddr, t.data.dstAddr);
        }
        if (d.notify) {
            const auto& n = static_cast<const Asc::UdmaSqeWriteWithNotify*>(bytes)->notify;
            const auto addr = uint64_t(n.notifyAddrLow) | (uint64_t(n.notifyAddrHigh) << 32);
            auto id = it->second.notifyIds.find(addr);
            if (id != it->second.notifyIds.end()) {
                t.data.notifyID = id->second;
            }
        }
    } catch (...) {
        session.error = -6;
    }
}
thread_local std::vector<uint8_t> savedProfileBits;
void SqeProfileScope::Prepare(uint32_t count)
{
    try {
        savedProfileBits.resize(count);
    } catch (...) {
        session.error = -6;
        return;
    }
    count_ = count;
    for (uint32_t i = 0; i < count_; ++i) {
        auto& h = *reinterpret_cast<Asc::Rt91095StarsSqeHeader*>(static_cast<uint8_t*>(bytes_) + 64 * i);
        savedProfileBits[i] = h.sqeProf;
        h.sqeProf = 1; // ABI bit 15: sqeProf
    }
}
void SqeProfileScope::Restore()
{
    for (uint32_t i = 0; i < count_; ++i) {
        auto& header = *reinterpret_cast<Asc::Rt91095StarsSqeHeader*>(static_cast<uint8_t*>(bytes_) + 64 * i);
        header.sqeProf = savedProfileBits[i];
    }
}
static void SubmittedImmediate(uint32_t sq, const void* bytes, uint32_t count) noexcept
{
    if (!Enabled()) {
        return;
    }
    try {
        if (!Refresh()) {
            return;
        }
        for (uint32_t i = 0; i < count; ++i) {
            auto p = static_cast<const uint8_t*>(bytes) + 64 * i;
            const auto& h = *reinterpret_cast<const Asc::Rt91095StarsSqeHeader*>(p);
            uint32_t task = uint32_t(h.rtStreamId) | (uint32_t(h.taskId) << 16);
            MsprofAicpuHcclTaskInfo t{};
            t.remoteRank = session.op.rank;
            t.notifyID = UINT64_MAX;
            t.transportType = 2;
            AscProf::TaskName name = AscProf::TaskName::Unknown;
            using T = Asc::Rt91095StarsSqeType;
            if (h.type == static_cast<uint8_t>(T::RT_91095_SQE_TYPE_SDMA)) {
                const auto& s = *reinterpret_cast<const Asc::Rt91095StarsMemcpySqe*>(p);
                t.srcAddr = uint64_t(s.u.strideMode0.srcAddrLow) | (uint64_t(s.u.strideMode0.srcAddrHigh) << 32);
                t.dstAddr = uint64_t(s.u.strideMode0.dstAddrLow) | (uint64_t(s.u.strideMode0.dstAddrHigh) << 32);
                t.dataSize = s.u.strideMode0.lengthMove;
                t.transportType = 0;
                name = s.opcode ? TaskName::ReduceInline : TaskName::Memcpy;
            } else if (
                h.type == static_cast<uint8_t>(T::RT_91095_SQE_TYPE_NOTIFY_WAIT) ||
                h.type == static_cast<uint8_t>(T::RT_91095_SQE_TYPE_NOTIFY_RECORD)) {
                t.notifyID = reinterpret_cast<const Asc::Rt91095StarsNotifySqe*>(p)->notifyId;
                name = h.type == static_cast<uint8_t>(T::RT_91095_SQE_TYPE_NOTIFY_WAIT) ? TaskName::NotifyWait :
                                                                                          TaskName::NotifyRecord;
            } else if (h.type == static_cast<uint8_t>(T::RT_91095_SQE_TYPE_UBDMA)) {
                const auto& db = *reinterpret_cast<const Asc::Rt91095StarsUbdmaDBmodeSqe*>(p);
                auto found = session.jettyChannel.find(Jetty(db.jettyId1, db.funcId1, db.dieId1));
                if (found == session.jettyChannel.end()) {
                    session.error = -10;
                    continue;
                }
                auto& channel = session.channels.at(found->second);
                if (!channel.bound) {
                    session.error = -10;
                    continue;
                }
                auto& list = channel.pending;
                size_t consumed = 0;
                while (consumed < list.size() && static_cast<uint16_t>(db.piValue1 - list[consumed].endPi) < 0x8000U) {
                    auto& w = list[consumed++];
                    Append(w.data, w.name, sq, task);
                }
                list.erase(list.begin(), list.begin() + consumed);
                continue;
            } else {
                auto it = session.conditions.find((uint64_t(sq) << 32) | task);
                if (it != session.conditions.end()) {
                    name = it->second ? TaskName::CcoreWait : TaskName::CcoreRecord;
                    session.conditions.erase(it);
                }
            }
            if (name != TaskName::Unknown) {
                Append(t, name, sq, task);
            } else {
                session.error = -11;
            }
        }
    } catch (...) {
        session.error = -6;
    }
}
void DrainSubmitted() noexcept
{
    session.deferSubmitted = false;
    for (const auto& batch : session.submittedBatches) {
        SubmittedImmediate(batch.sq, session.submittedBytes.data() + batch.offset, batch.count);
    }
    session.submittedBatches.clear();
    session.submittedBytes.clear();
}
bool PublicationScope::Begin(uint32_t instructionCount) noexcept
{
    // Bound retained records and modular Jetty PI matching. Large/reentrant
    // executions keep the immediate path. Reserve before any hardware publication.
    if (session.deferSubmitted || instructionCount == 0 || instructionCount > 4096) {
        return false;
    }
    DrainSubmitted();
    try {
        session.submittedBytes.reserve(static_cast<size_t>(instructionCount) * 64);
        session.submittedBatches.reserve(instructionCount);
        session.deferSubmitted = true;
        return true;
    } catch (...) { /* Allocation failure keeps the existing immediate reporter. */
    }
    return false;
}
// Keep snapshots until the next successful submit (normally CCoreRecord),
// or operation end on failure. Replaying them here would delay the final signal.
void PublicationScope::End() noexcept { session.deferSubmitted = false; }
void Submitted(uint32_t sq, const void* bytes, uint32_t count) noexcept
{
    if (!Enabled()) {
        return;
    }
    if (session.deferSubmitted) {
        const size_t offset = session.submittedBytes.size();
        const size_t size = static_cast<size_t>(count) * 64;
        if (size <= session.submittedBytes.capacity() - offset &&
            session.submittedBatches.size() < session.submittedBatches.capacity()) {
            // Capacity was reserved before publishing. Capturing current bytes
            // preserves task IDs, DB PI, and addresses across later template reuse.
            session.submittedBytes.resize(offset + size);
            std::memcpy(session.submittedBytes.data() + offset, bytes, size);
            session.submittedBatches.push_back({sq, static_cast<uint32_t>(offset), count});
            return;
        }
        DrainSubmitted(); // Unexpected extra publication: preserve ordering.
    }
    if (!session.submittedBatches.empty()) {
        DrainSubmitted();
    }
    SubmittedImmediate(sq, bytes, count);
}

} // namespace AscProf
