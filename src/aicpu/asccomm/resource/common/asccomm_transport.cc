/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#include "../../diagnostics/profiling_session.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <memory>
#include "../connection/ub_queue_budget.h"
#include "../../common/utils/exception_util.h"
#include "../../common/exception/internal_exception.h"
#include <vector>
#include "asccomm_transport.h"
#include "../../primitives/task_cache/binary_template.h"
#include <cstring>
#include <array>
#include "asccomm_runtime.h"
#include "../../primitives/launch/asccomm_launch.h"
#include "../connection/ub_conn_lite.h"
#include "../connection/ub_jetty_lite.h"
#include "../connection/rdma_vendor_1825_ops.h"
#include "securec.h"
#include "asccomm_log.h"

namespace {

struct UbFlow {
    Asc::SharedUbQueueBudget budget;
    HcommJfsContext* queue = nullptr;
    uint64_t base = 0;
    uint32_t jetty = 0;
    uint16_t doorbellPi = 0;
    bool doorbellSeen = false;
    ThreadEntity* thread = nullptr;
    ChannelEntity* channel = nullptr;
    struct Writer {
        ThreadEntity* thread;
        AsccommUbCompletion completion;
    };
    std::vector<std::unique_ptr<Writer>> writers;
    Writer* writer = nullptr;
};
struct UbDomain {
    std::unordered_map<HcommJfsContext*, std::shared_ptr<UbFlow>> queues;
    std::weak_ptr<void> lifetime;
    bool managed = false;
};
// Resource-domain identities are pinned by devkit, never inferred from a reused
// raw JFS address. A domain may not be concurrently owned by multiple workers.
thread_local uint64_t ubDomainId = 0;
thread_local UbFlow* currentUbFlow = nullptr;
thread_local std::unordered_map<uint64_t, UbDomain> ubDomains;
void FlowError(UbFlow& flow, const char* reason)
{
    flow.budget.failed = true;
    Asc::throw_exception<Asc::InternalException>(reason);
}
std::shared_ptr<UbFlow> GetUbFlow(ChannelEntity& channel)
{
    if (!ubDomainId) {
        return {};
    }
    auto* q = &channel.sqContextAddr[0].contextInfo.ubJfs;
    auto& domain = ubDomains.at(ubDomainId);
    auto it = domain.queues.find(q);
    if (it != domain.queues.end()) {
        auto& flow = *it->second;
        if (flow.base != q->sqVa || flow.jetty != q->jfsID || flow.budget.depth != q->sqDepth) {
            FlowError(flow, "UB queue changed inside a pinned resource domain");
        }
        return it->second;
    }
    if (domain.queues.size() >= 1024) {
        Asc::throw_exception<Asc::InternalException>("UB flow resource budget exceeded");
    }
    auto flow = std::make_shared<UbFlow>();
    flow->queue = q;
    flow->base = q->sqVa;
    flow->jetty = q->jfsID;
    flow->budget.depth = q->sqDepth;
    if (!flow->budget.Refresh(static_cast<uint16_t>(q->sqHead), static_cast<uint16_t>(q->sqTail))) {
        FlowError(*flow, "invalid shared UB queue capacity");
    }
    domain.queues.emplace(q, flow);
    return flow;
}
void ReserveUb(void* opaque, uint16_t pi, uint32_t count)
{
    auto& flow = *static_cast<UbFlow*>(opaque);
    auto& budget = flow.budget;
    if (!budget.Refresh(pi, static_cast<uint16_t>(flow.queue->sqTail))) {
        FlowError(flow, "invalid shared UB budget");
    }
    if (!budget.Matches(pi) || !count || count > budget.depth || budget.depth > UINT16_MAX) {
        FlowError(flow, "UB PI or queue capacity is inconsistent; refusing overwrite");
    }
    if (!budget.initialized) {
        FlowError(flow, "UB CI tracker was not initialized at resource binding");
    }
    if (!budget.HasSpace(count)) {
        // Native and binary paths both split long operations using progress DBs.
        // Checkpoint SQEs are intentionally outside captured templates: their
        // necessity depends on live capacity, not on the captured invocation.
        if (asccomm_launch_registered_threads() != ASCCOMM_SUCCESS) {
            FlowError(flow, "UB space wait failed to launch outstanding SQEs");
        }
        const auto& ch = *flow.channel;
        const Asc::UbJettyLiteId jetty(ch.devInfo.dieId, ch.devInfo.funcId, flow.jetty);
        for (const auto& writer : flow.writers) {
            if (asccomm_rtsq_launch(*writer->thread) != ASCCOMM_SUCCESS) {
                FlowError(flow, "UB writer launch failed");
            }
        }
        if (!flow.doorbellSeen || flow.doorbellPi != pi) {
            const auto tag = asccomm_ub_note_db(ch, pi);
            if (asccomm_rtsq_ub_checkpoint(*flow.thread, jetty, pi, tag) != ASCCOMM_SUCCESS) {
                FlowError(flow, "UB progress doorbell submission failed");
            }
            flow.doorbellSeen = true;
            flow.doorbellPi = pi;
        }
        // Require each writer's latest DB to retire. Do not wait for unrelated
        // later SQEs: those may depend on WQEs not generated yet.
        for (const auto& writer : flow.writers) {
            if (asccomm_rtsq_wait_ub(*writer->thread, writer->completion) != ASCCOMM_SUCCESS) {
                FlowError(flow, "UB WQEBB/CI completion wait failed");
            }
        }
        budget.Completed(pi);
        flow.queue->sqTail = pi;
    }
    if (!budget.Reserve(pi, count)) {
        FlowError(flow, "UB WQEBB reservation failed");
    }
}
void BindUbFlow(
    Asc::UbConnLite& conn, const std::shared_ptr<UbFlow>& flow, ThreadEntity& thread, ChannelEntity& channel)
{
    if (AscProf::Enabled()) {
        conn.set_profiling_channel(reinterpret_cast<uint64_t>(&channel));
    }
    currentUbFlow = flow.get();
    if (!flow) {
        conn.set_space_guard(nullptr, nullptr);
        return;
    }
    auto& f = *flow;
    f.writer = nullptr;
    for (const auto& writer : f.writers) {
        if (writer->thread == &thread) {
            f.writer = writer.get();
            break;
        }
    }
    if (!f.writer) {
        if (f.writers.size() >= 128) {
            FlowError(f, "UB writer budget exceeded");
        }
        auto writer = std::make_unique<UbFlow::Writer>();
        writer->thread = &thread;
        f.writer = writer.get();
        f.writers.push_back(std::move(writer));
    }
    f.thread = &thread;
    f.channel = &channel;
    conn.set_space_guard(ReserveUb, flow.get());
}

constexpr uint32_t UB_STRONG_ORDER = 2U;
constexpr uint32_t UB_RELAX_ORDER = 1U;
constexpr uint32_t UB_COMPLETION = 1U;
constexpr uint32_t UB_NO_COMPLETION = 0U;
constexpr uint32_t NOTIFY_VALUE = 1U;
constexpr uint32_t NOTIFY_SIZE = sizeof(uint32_t);
constexpr uint32_t DEFAULT_TIMEOUT = 1836U;
constexpr uint64_t RDMA_DMA_MAX_SIZE = 0x80000000ULL;

bool is_ub_protocol(CommProtocol protocol)
{
    return protocol == COMM_PROTOCOL_UBC_CTP || protocol == COMM_PROTOCOL_UBC_TP || protocol == COMM_PROTOCOL_UB_MEM ||
           protocol == static_cast<CommProtocol>(7) || protocol == static_cast<CommProtocol>(9);
}

bool is_reduce_op_supported(Asc::ReduceOp reduceOp)
{
    return reduceOp == Asc::ReduceOp::SUM || reduceOp == Asc::ReduceOp::MAX || reduceOp == Asc::ReduceOp::MIN;
}

bool is_reduce_data_type_supported(Asc::DataType dataType)
{
    return dataType == Asc::DataType::INT8 || dataType == Asc::DataType::INT16 || dataType == Asc::DataType::INT32 ||
           dataType == Asc::DataType::UINT8 || dataType == Asc::DataType::UINT16 || dataType == Asc::DataType::UINT32 ||
           dataType == Asc::DataType::FP16 || dataType == Asc::DataType::FP32 || dataType == Asc::DataType::BFP16;
}

bool is_range_contained(uint64_t base, uint64_t capacity, uint64_t addr, uint64_t size)
{
    return addr >= base && size <= capacity && (addr - base) <= (capacity - size);
}

const HcommRegedBufferEntity* find_buffer(
    const HcommRegedBufferEntity* buffers, uint32_t bufferNum, uint64_t addr, uint64_t size, bool useFirstAsFallback,
    bool warnOnFallback = true)
{
    if (buffers == nullptr || bufferNum == 0) {
        return nullptr;
    }
    for (uint32_t index = 0; index < bufferNum; ++index) {
        if (buffers[index].type == HCOMM_REGED_BUFFER_RMA &&
            is_range_contained(buffers[index].bufferInfo.rma.addr, buffers[index].bufferInfo.rma.size, addr, size)) {
            return &buffers[index];
        }
    }
    if (useFirstAsFallback && buffers[0].type == HCOMM_REGED_BUFFER_RMA) {
        if (warnOnFallback) {
            ASCCOMM_WARNING(
                "[%s] addr[0x%llx], size[0x%llx] is outside local buffers; use the first protection.", __func__, addr,
                size);
        }
        return &buffers[0];
    }
    return nullptr;
}

asccomm_result get_rtsq(ThreadEntity& thread, HcommThreadSqContext*& rtsq)
{
    CHK_PTR_NULL(thread.sqContextAddr);
    rtsq = thread.sqContextAddr;
    return ASCCOMM_SUCCESS;
}

asccomm_result get_ub_context(ChannelEntity& channel, HcommJfsContext*& jfs)
{
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol), ASCCOMM_ERROR("[%s] protocol[%d] is not UB.", __func__, channel.protocol),
        ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(channel.sqNum == 0, ASCCOMM_ERROR("[%s] UB SQ context is empty.", __func__), ASCCOMM_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PRT_RET(
        channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_UB_JFS,
        ASCCOMM_ERROR("[%s] SQ context is not UB JFS.", __func__), ASCCOMM_E_PARA);
    jfs = &channel.sqContextAddr[0].contextInfo.ubJfs;
    CHK_PRT_RET(
        jfs->sqDepth == 0 || jfs->sqVa == 0 || jfs->maxReadSize == 0 || jfs->maxWriteSize == 0,
        ASCCOMM_ERROR("[%s] invalid UB JFS context.", __func__), ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

asccomm_result build_ub_slices(
    const ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Asc::RmaBufSliceLite& local,
    Asc::RmtRmaBufSliceLite& remote)
{
    const HcommRegedBufferEntity* localBuffer =
        find_buffer(channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity* remoteBuffers =
        channel.remoteBufferAddr != nullptr && channel.remoteBufferNum > 1 ? channel.remoteBufferAddr + 1 : nullptr;
    const HcommRegedBufferEntity* remoteBuffer = find_buffer(
        remoteBuffers, channel.remoteBufferNum > 0 ? channel.remoteBufferNum - 1 : 0, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto& localResource = localBuffer->bufferInfo.rma;
    const auto& remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB buffer requires URMA protection.", __func__), ASCCOMM_E_PARA);
    local = Asc::RmaBufSliceLite(localAddr, len, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    remote = Asc::RmtRmaBufSliceLite(
        remoteAddr, len, 0, remoteResource.protectionInfo.memInfo.ub.tokenId,
        remoteResource.protectionInfo.memInfo.ub.tokenValue, std::numeric_limits<uint32_t>::max());
    return ASCCOMM_SUCCESS;
}

asccomm_result build_ub_local_slice(
    const ChannelEntity& channel, uintptr_t localAddr, uint64_t len, Asc::RmaBufSliceLite& local)
{
    const HcommRegedBufferEntity* localBuffer =
        find_buffer(channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    CHK_PTR_NULL(localBuffer);
    const auto& localResource = localBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB buffer requires URMA protection.", __func__), ASCCOMM_E_PARA);
    local = Asc::RmaBufSliceLite(localAddr, len, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    return ASCCOMM_SUCCESS;
}

asccomm_result build_ub_remote_notify(const ChannelEntity& channel, uint32_t index, Asc::RmtRmaBufSliceLite& notify)
{
    CHK_PRT_RET(
        index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        ASCCOMM_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index, channel.remoteNotifyNum),
        ASCCOMM_E_PARA);
    CHK_PRT_RET(
        channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        ASCCOMM_ERROR("[%s] remote notify is not RMA RT.", __func__), ASCCOMM_E_PARA);
    AsccommBinaryCache::NotifyIndex() = index;
    const auto& resource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(
        resource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB notify requires URMA protection.", __func__), ASCCOMM_E_PARA);
    notify = Asc::RmtRmaBufSliceLite(
        resource.addr, resource.size, 0, resource.protectionInfo.memInfo.ub.tokenId,
        resource.protectionInfo.memInfo.ub.tokenValue, resource.notifyId);
    return ASCCOMM_SUCCESS;
}

asccomm_result build_ub_send_rmt_obj(const ChannelEntity& channel, Asc::RmtRmaBufSliceLite& remote)
{
    remote = Asc::RmtRmaBufSliceLite(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (channel.remoteBufferAddr != nullptr) {
        for (uint32_t index = 1; index < channel.remoteBufferNum; ++index) {
            const HcommRegedBufferEntity& buffer = channel.remoteBufferAddr[index];
            if (buffer.type != HCOMM_REGED_BUFFER_RMA ||
                buffer.bufferInfo.rma.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA) {
                continue;
            }
            if (buffer.bufferInfo.rma.addr == 0 && buffer.bufferInfo.rma.size == 0) {
                const auto& memInfo = buffer.bufferInfo.rma.protectionInfo.memInfo.ub;
                remote = Asc::RmtRmaBufSliceLite(
                    0, 0, 0, memInfo.tokenId, memInfo.tokenValue, std::numeric_limits<uint32_t>::max());
                return ASCCOMM_SUCCESS;
            }
        }
    }
    CHK_PRT_RET(
        channel.remoteBufferNum <= 1, ASCCOMM_ERROR("[%s] remote send target buffer is missing.", __func__),
        ASCCOMM_E_PARA);
    return ASCCOMM_E_PARA;
}

void fill_ub_param(const HcommJfsContext& jfs, Asc::UbConnLiteParam& param)
{
    param.jettyId = jfs.jfsID;
    param.sqVa = jfs.sqVa;
    param.sqDepth = jfs.sqDepth;
    param.tpn = jfs.tpID;
    param.maxReadSize = jfs.maxReadSize;
    param.maxWriteSize = jfs.maxWriteSize;
    (void)memcpy_sp(param.rmtEid, sizeof(param.rmtEid), jfs.remoteEID, sizeof(jfs.remoteEID));
}

void import_ub_state(const HcommJfsContext& jfs, Asc::UbConnLite& connection)
{
    connection.import_queue_state(jfs.sqHead);
}

void export_ub_state(Asc::UbConnLite& connection, HcommJfsContext& jfs)
{
    uint32_t sqHead = 0;
    connection.export_queue_state(sqHead);
    const uint16_t cur = static_cast<uint16_t>(jfs.sqHead);
    const uint16_t nxt = static_cast<uint16_t>(sqHead);
    if (nxt == cur || static_cast<uint16_t>(nxt - cur) < 0x8000u) {
        jfs.sqHead = sqHead;
    } else {
        ASCCOMM_WARNING(
            "[UB_EXPORT_GUARD] refuse backward write, entityPi[%u] snapshotPi[%u] — second writer advanced, "
            "keeping entity value.",
            cur, nxt);
    }
}

asccomm_result ring_ub_doorbell(
    ThreadEntity& thread, const ChannelEntity& channel, const HcommJfsContext& jfs, uint16_t pi)
{
    HcommThreadSqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const Asc::UbJettyLiteId jettyId(channel.devInfo.dieId, channel.devInfo.funcId, jfs.jfsID);
    const uint64_t threadId = reinterpret_cast<uint64_t>(&thread);
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::UbEnd, reinterpret_cast<uint64_t>(&channel), &threadId, sizeof(threadId));
    AsccommBinaryCache::RtsqSuppression suppress;
    const auto tag = asccomm_ub_note_db(channel, pi);
    const auto ret = asccomm_rtsq_ub_db_send(thread, jettyId, pi, tag);
    if (ret != ASCCOMM_SUCCESS) {
        tag.Fail();
    }
    return ret;
}

void capture_fence(ChannelEntity& channel, uint64_t value);

Asc::SqeConfigLite get_ub_config(ChannelEntity& channel)
{
    Asc::SqeConfigLite cfg;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr && channel.sqContextAddr[0].fence != 0) {
        cfg.fence = 1;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        capture_fence(channel, 0);
        channel.sqContextAddr[0].fence = 0;
    }
    return cfg;
}

// Exact resource identity is checked before any replay side effect. The owner of
// the private cache ABI must keep the resources alive until destroy/clear.
struct BinaryBinding {
    std::array<uint64_t, 24> words{};
};
BinaryBinding channel_binding(const ChannelEntity& channel)
{
    BinaryBinding b;
    auto& w = b.words;
    w[2] = reinterpret_cast<uint64_t>(channel.sqContextAddr);
    w[3] = reinterpret_cast<uint64_t>(channel.cqContextAddr);
    w[4] = reinterpret_cast<uint64_t>(channel.localBufferAddr);
    w[5] = reinterpret_cast<uint64_t>(channel.remoteBufferAddr);
    w[6] = reinterpret_cast<uint64_t>(channel.localNotifyAddr);
    w[7] = reinterpret_cast<uint64_t>(channel.remoteNotifyAddr);
    w[8] = channel.localBufferNum;
    w[9] = channel.remoteBufferNum;
    w[10] = channel.remoteNotifyNum;
    w[11] = channel.protocol;
    if (is_ub_protocol(channel.protocol)) {
        const auto& q = channel.sqContextAddr[0].contextInfo.ubJfs;
        w[12] = q.sqVa;
        w[13] = q.sqDepth;
        w[14] = q.jfsID;
        w[15] = q.tpID;
        w[16] = q.maxReadSize;
        w[17] = q.maxWriteSize;
        (void)memcpy_sp(&w[18], 2 * sizeof(w[0]), q.remoteEID, 16);
        w[20] = channel.devInfo.dieId;
        w[21] = channel.devInfo.funcId;
    } else {
        const auto& q = channel.sqContextAddr[0].contextInfo.roceSq;
        const auto& c = channel.cqContextAddr[0].contextInfo.roceCq;
        w[12] = q.sqVa;
        w[13] = q.depth;
        w[14] = q.qpn;
        w[15] = q.wqeSize;
        w[16] = q.dbHwVa;
        w[17] = q.dbSwVa;
        w[18] = q.dbVendorSpecified;
        w[19] = c.cqVa;
        w[20] = c.cqDepth;
        w[21] = c.dbSwVa;
    }
    return b;
}
// Same immutable binding checks as channel_binding, without constructing a
// 192-byte temporary on every hit. Runtime PI/CI remain refreshed separately.
bool matches_ub_binding(const ChannelEntity& c, const uint64_t* w)
{
    const auto& q = c.sqContextAddr[0].contextInfo.ubJfs;
    return w[2] == reinterpret_cast<uint64_t>(c.sqContextAddr) && w[3] == reinterpret_cast<uint64_t>(c.cqContextAddr) &&
           w[4] == reinterpret_cast<uint64_t>(c.localBufferAddr) &&
           w[5] == reinterpret_cast<uint64_t>(c.remoteBufferAddr) &&
           w[6] == reinterpret_cast<uint64_t>(c.localNotifyAddr) &&
           w[7] == reinterpret_cast<uint64_t>(c.remoteNotifyAddr) && w[8] == c.localBufferNum &&
           w[9] == c.remoteBufferNum && w[10] == c.remoteNotifyNum && w[11] == c.protocol && w[12] == q.sqVa &&
           w[13] == q.sqDepth && w[14] == q.jfsID && w[15] == q.tpID && w[16] == q.maxReadSize &&
           w[17] == q.maxWriteSize && std::memcmp(w + 18, q.remoteEID, 16) == 0 && w[20] == c.devInfo.dieId &&
           w[21] == c.devInfo.funcId && !w[22] && !w[23];
}
BinaryBinding binary_binding(const ThreadEntity& thread, const ChannelEntity& channel)
{
    auto binding = channel_binding(channel);
    binding.words[0] = reinterpret_cast<uint64_t>(&thread);
    binding.words[1] = reinterpret_cast<uint64_t>(thread.sqContextAddr);
    return binding;
}
void capture_transport_begin(ThreadEntity& thread, ChannelEntity& channel, bool rdma)
{
    if (!AsccommBinaryCache::Capture()) {
        return;
    }
    const auto binding = binary_binding(thread, channel);
    AsccommBinaryCache::CaptureTask(
        rdma ? AsccommBinaryCache::Kind::RdmaBegin : AsccommBinaryCache::Kind::UbBegin,
        reinterpret_cast<uint64_t>(&channel), &binding, sizeof(binding));
}
void capture_fence(ChannelEntity& channel, uint64_t value)
{
    const uint64_t values[2] = {channel.sqContextAddr[0].fence, value};
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::Fence, reinterpret_cast<uint64_t>(&channel), values, sizeof(values));
}

template <typename Operation>
asccomm_result execute_ub_rma(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    Operation operation)
{
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));

    capture_transport_begin(thread, channel, false);
    AsccommBinaryCache::TransportScope captureScope(reinterpret_cast<uint64_t>(&channel));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    auto flow = GetUbFlow(channel);
    BindUbFlow(connection, flow, thread, channel);
    Asc::ConnLiteOperationOut out{};
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    operation(connection, local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

asccomm_result get_rdma_contexts(ChannelEntity& channel, HcommRdmaSqContext*& sq, HcommRdmaCqContext*& cq)
{
    CHK_PRT_RET(
        channel.protocol != COMM_PROTOCOL_ROCE,
        ASCCOMM_ERROR("[%s] protocol[%d] is not RoCE.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(
        channel.sqNum == 0 || channel.cqNum == 0, ASCCOMM_ERROR("[%s] RoCE SQ/CQ context is empty.", __func__),
        ASCCOMM_E_PARA);
    CHK_PTR_NULL(channel.sqContextAddr);
    CHK_PTR_NULL(channel.cqContextAddr);
    CHK_PRT_RET(
        channel.sqContextAddr[0].type != HCOMM_SQ_CONTEXT_TYPE_ROCE ||
            channel.cqContextAddr[0].type != HCOMM_CQ_CONTEXT_TYPE_ROCE,
        ASCCOMM_ERROR("[%s] SQ/CQ context is not RoCE.", __func__), ASCCOMM_E_PARA);
    sq = &channel.sqContextAddr[0].contextInfo.roceSq;
    cq = &channel.cqContextAddr[0].contextInfo.roceCq;
    CHK_PRT_RET(
        sq->depth == 0 || sq->sqVa == 0 || cq->cqDepth == 0 || cq->cqVa == 0,
        ASCCOMM_ERROR("[%s] invalid RoCE SQ/CQ context.", __func__), ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

void build_rdma_context(
    const HcommRdmaSqContext& resourceSq, const HcommRdmaCqContext& resourceCq, Asc::RdmaSqContextLite& sq,
    Asc::RdmaCqContextLite& cq)
{
    sq.qpn = resourceSq.qpn;
    sq.sqVa = resourceSq.sqVa;
    sq.wqeSize = resourceSq.wqeSize;
    sq.depth = resourceSq.depth;
    sq.dbHwVa = resourceSq.dbHwVa;
    sq.dbSwVa = resourceSq.dbSwVa;
    sq.dbVendorSpecified = resourceSq.dbVendorSpecified;
    cq.cqVa = resourceCq.cqVa;
    cq.cqDepth = resourceCq.cqDepth;
    cq.dbSwVa = resourceCq.dbSwVa;
}

asccomm_result build_rdma_slices(
    const ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len, Asc::RmaBufSliceLite& local,
    Asc::RmtRmaBufSliceLite& remote)
{
    const HcommRegedBufferEntity* localBuffer =
        find_buffer(channel.localBufferAddr, channel.localBufferNum, localAddr, len, true);
    const HcommRegedBufferEntity* remoteBuffers =
        channel.remoteBufferAddr != nullptr && channel.remoteBufferNum > 1 ? channel.remoteBufferAddr + 1 : nullptr;
    const HcommRegedBufferEntity* remoteBuffer = find_buffer(
        remoteBuffers, channel.remoteBufferNum > 0 ? channel.remoteBufferNum - 1 : 0, remoteAddr, len, false);
    CHK_PTR_NULL(localBuffer);
    CHK_PTR_NULL(remoteBuffer);
    const auto& localResource = localBuffer->bufferInfo.rma;
    const auto& remoteResource = remoteBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        ASCCOMM_ERROR("[%s] RoCE buffer requires RDMA protection.", __func__), ASCCOMM_E_PARA);
    local = Asc::RmaBufSliceLite(localAddr, len, localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::RmtRmaBufSliceLite(
        remoteAddr, len, remoteResource.protectionInfo.memInfo.roce.rkey, 0, 0, std::numeric_limits<uint32_t>::max());
    return ASCCOMM_SUCCESS;
}

asccomm_result build_rdma_notify_slices(
    const ChannelEntity& channel, uint32_t index, Asc::RmaBufSliceLite& local, Asc::RmtRmaBufSliceLite& remote)
{
    CHK_PRT_RET(
        index >= channel.remoteNotifyNum || channel.remoteNotifyAddr == nullptr,
        ASCCOMM_ERROR("[%s] remote notify index[%u] out of range[%u].", __func__, index, channel.remoteNotifyNum),
        ASCCOMM_E_PARA);
    const HcommRegedBufferEntity* notifyValueBuffer =
        channel.localBufferAddr != nullptr && channel.localBufferNum > 0 ? &channel.localBufferAddr[0] : nullptr;
    CHK_PTR_NULL(notifyValueBuffer);
    CHK_PRT_RET(
        notifyValueBuffer->type != HCOMM_REGED_BUFFER_RMA ||
            channel.remoteNotifyAddr[index].type != HCOMM_REGED_NOTIFY_RMA_RT,
        ASCCOMM_ERROR("[%s] RoCE notify resource type is invalid.", __func__), ASCCOMM_E_PARA);
    AsccommBinaryCache::NotifyIndex() = index;
    const auto& localResource = notifyValueBuffer->bufferInfo.rma;
    const auto& remoteResource = channel.remoteNotifyAddr[index].notifyInfo.rmaRt;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_RDMA,
        ASCCOMM_ERROR("[%s] RoCE notify requires RDMA protection.", __func__), ASCCOMM_E_PARA);
    local =
        Asc::RmaBufSliceLite(localResource.addr, localResource.size, localResource.protectionInfo.memInfo.roce.lkey, 0);
    remote = Asc::RmtRmaBufSliceLite(
        remoteResource.addr, remoteResource.size, remoteResource.protectionInfo.memInfo.roce.rkey, 0, 0,
        remoteResource.notifyId);
    return ASCCOMM_SUCCESS;
}

template <typename Operation>
asccomm_result for_each_rdma_slice(
    const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote, Operation operation)
{
    uint64_t offset = 0;
    while (offset < local.get_size()) {
        const uint64_t remaining = local.get_size() - offset;
        const uint32_t sliceSize = static_cast<uint32_t>(remaining > RDMA_DMA_MAX_SIZE ? RDMA_DMA_MAX_SIZE : remaining);
        Asc::RmaBufSliceLite localSlice(local.get_addr() + offset, sliceSize, local.get_lkey(), 0);
        Asc::RmtRmaBufSliceLite remoteSlice(
            remote.get_addr() + offset, sliceSize, remote.get_rkey(), 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(operation(localSlice, remoteSlice));
        offset += sliceSize;
    }
    return ASCCOMM_SUCCESS;
}

enum class RdmaOperation {
    READ,
    WRITE,
    WRITE_REDUCE,
    WRITE_WITH_NOTIFY,
    WRITE_REDUCE_WITH_NOTIFY,
    NOTIFY_RECORD,
};

asccomm_result execute_rdma(
    ThreadEntity& thread, ChannelEntity& channel, RdmaOperation operation, uintptr_t localAddr, uintptr_t remoteAddr,
    uint64_t len, const Asc::ReduceIn* reduceIn, uint32_t notifyIndex)
{
    HcommRdmaSqContext* resourceSq = nullptr;
    HcommRdmaCqContext* resourceCq = nullptr;
    CHK_RET(get_rdma_contexts(channel, resourceSq, resourceCq));
    capture_transport_begin(thread, channel, true);
    AsccommBinaryCache::TransportScope captureScope(reinterpret_cast<uint64_t>(&channel));
    Asc::RdmaSqContextLite sq{};
    Asc::RdmaCqContextLite cq{};
    build_rdma_context(*resourceSq, *resourceCq, sq, cq);
    Asc::Rdma1825Ops ops(&sq, &cq);
    ops.import_queue_state(
        resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, resourceCq->cqDbFlush != 0);

    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, local, remote));
    } else {
        CHK_RET(build_rdma_slices(channel, localAddr, remoteAddr, len, local, remote));
    }
    Asc::RmaBufSliceLite localNotify(0, 0, 0, 0);
    Asc::RmtRmaBufSliceLite remoteNotify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    if (operation == RdmaOperation::WRITE_WITH_NOTIFY || operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) {
        CHK_RET(build_rdma_notify_slices(channel, notifyIndex, localNotify, remoteNotify));
    }

    Asc::SqeConfigLite cfg;
    cfg.cqeEn = true;
    cfg.fence = channel.sqNum > 0 && channel.sqContextAddr != nullptr && channel.sqContextAddr[0].fence != 0 ? 1 : 0;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        capture_fence(channel, 0);
        channel.sqContextAddr[0].fence = 0;
    }
    if (operation == RdmaOperation::NOTIFY_RECORD) {
        AsccommBinaryCache::ResourceRole() = 3;
    }
    if (operation == RdmaOperation::READ) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.read(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE || operation == RdmaOperation::NOTIFY_RECORD) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write(loc, rmt, cfg);
            }));
    } else if (operation == RdmaOperation::WRITE_REDUCE) {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
    } else if (operation == RdmaOperation::WRITE_WITH_NOTIFY) {
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write(loc, rmt, cfg);
            }));
        AsccommBinaryCache::ResourceRole() = 3;
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    } else {
        CHK_PTR_NULL(reduceIn);
        CHK_RET(for_each_rdma_slice(
            local, remote, [&](const Asc::RmaBufSliceLite& loc, const Asc::RmtRmaBufSliceLite& rmt) {
                return ops.write_reduce(loc, rmt, cfg, reduceIn->dataType, reduceIn->reduceOp);
            }));
        AsccommBinaryCache::ResourceRole() = 3;
        CHK_RET(ops.write(localNotify, remoteNotify, cfg));
    }

    uint64_t dbAddr = 0;
    uint64_t dbValue = 0;
    CHK_RET(ops.build_doorbell(dbAddr, dbValue));
    bool cqDbFlush = false;
    ops.export_queue_state(resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, cqDbFlush);
    resourceCq->cqDbFlush = cqDbFlush ? 1 : 0;
    HcommThreadSqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const uint64_t finish[2] = {
        reinterpret_cast<uint64_t>(&thread),
        (operation == RdmaOperation::WRITE_WITH_NOTIFY || operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) ? 2U :
                                                                                                                  1U};
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::RdmaEnd, reinterpret_cast<uint64_t>(&channel), finish, sizeof(finish));
    {
        AsccommBinaryCache::RtsqSuppression suppress;
        CHK_RET(asccomm_rtsq_rdma_db_send(thread, dbAddr, dbValue));
    }

    const int32_t pollNum =
        (operation == RdmaOperation::WRITE_WITH_NOTIFY || operation == RdmaOperation::WRITE_REDUCE_WITH_NOTIFY) ? 2 : 1;
    std::vector<int32_t> errList;
    asccomm_result pollRet = ops.poll_cq(pollNum, 5, errList);
    (void)ops.build_cq_doorbell(dbAddr, dbValue);
    ops.export_queue_state(resourceSq->sqHead, resourceSq->sqTail, resourceCq->cqHead, resourceCq->cqTail, cqDbFlush);
    resourceCq->cqDbFlush = cqDbFlush ? 1 : 0;
    return pollRet;
}

asccomm_result convert_batch_reduce(
    asccomm_data_type dataType, asccomm_reduce_op reduceOp, Asc::ReduceIn& reduceIn, uint64_t count, uint64_t& len)
{
    Asc::DataType innerType = Asc::DataType::INVALID;
    uint32_t typeSize = 0;
    switch (dataType) {
        case ASCCOMM_DATA_TYPE_INT8:
            innerType = Asc::DataType::INT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_INT16:
            innerType = Asc::DataType::INT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT32:
            innerType = Asc::DataType::INT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP16:
            innerType = Asc::DataType::FP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_FP32:
            innerType = Asc::DataType::FP32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_INT64:
            innerType = Asc::DataType::INT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT64:
            innerType = Asc::DataType::UINT64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_UINT8:
            innerType = Asc::DataType::UINT8;
            typeSize = 1;
            break;
        case ASCCOMM_DATA_TYPE_UINT16:
            innerType = Asc::DataType::UINT16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_UINT32:
            innerType = Asc::DataType::UINT32;
            typeSize = 4;
            break;
        case ASCCOMM_DATA_TYPE_FP64:
            innerType = Asc::DataType::FP64;
            typeSize = 8;
            break;
        case ASCCOMM_DATA_TYPE_BFP16:
            innerType = Asc::DataType::BFP16;
            typeSize = 2;
            break;
        case ASCCOMM_DATA_TYPE_INT128:
            innerType = Asc::DataType::INT128;
            typeSize = 16;
            break;
        default:
            return ASCCOMM_E_PARA;
    }
    Asc::ReduceOp innerOp = Asc::ReduceOp::INVALID;
    switch (reduceOp) {
        case ASCCOMM_REDUCE_SUM:
            innerOp = Asc::ReduceOp::SUM;
            break;
        case ASCCOMM_REDUCE_PROD:
            innerOp = Asc::ReduceOp::PROD;
            break;
        case ASCCOMM_REDUCE_MAX:
            innerOp = Asc::ReduceOp::MAX;
            break;
        case ASCCOMM_REDUCE_MIN:
            innerOp = Asc::ReduceOp::MIN;
            break;
        default:
            return ASCCOMM_E_PARA;
    }
    CHK_PRT_RET(
        count > std::numeric_limits<uint64_t>::max() / typeSize,
        ASCCOMM_ERROR("[%s] reduce length overflow.", __func__), ASCCOMM_E_PARA);
    len = count * typeSize;
    reduceIn = Asc::ReduceIn(innerType, innerOp);
    return ASCCOMM_SUCCESS;
}

} // namespace

asccomm_result asccomm_write(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [](Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
               const Asc::SqeConfigLite& cfg,
               Asc::ConnLiteOperationOut& out) { connection.write(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE, localAddr, remoteAddr, len, nullptr, 0);
}

asccomm_result asccomm_write_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        CHK_PRT_RET(
            !is_reduce_data_type_supported(reduceIn.dataType),
            ASCCOMM_ERROR(
                "[%s] UB reduce data type[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.dataType)),
            ASCCOMM_E_NOT_SUPPORT);
        CHK_PRT_RET(
            !is_reduce_op_supported(reduceIn.reduceOp),
            ASCCOMM_ERROR(
                "[%s] UB reduce operation[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.reduceOp)),
            ASCCOMM_E_NOT_SUPPORT);
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_reduce(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
            });
    }
    return execute_rdma(thread, channel, RdmaOperation::WRITE_REDUCE, localAddr, remoteAddr, len, &reduceIn, 0);
}

asccomm_result asccomm_write_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&notify](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_with_notify(local, remote, cfg, out, notify, NOTIFY_VALUE);
            });
    }
    return execute_rdma(
        thread, channel, RdmaOperation::WRITE_WITH_NOTIFY, localAddr, remoteAddr, len, nullptr, remoteNotifyIdx);
}

asccomm_result asccomm_write_reduce_with_notify(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn, uint32_t remoteNotifyIdx)
{
    if (is_ub_protocol(channel.protocol)) {
        CHK_PRT_RET(
            !is_reduce_data_type_supported(reduceIn.dataType),
            ASCCOMM_ERROR(
                "[%s] UB reduce data type[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.dataType)),
            ASCCOMM_E_NOT_SUPPORT);
        CHK_PRT_RET(
            !is_reduce_op_supported(reduceIn.reduceOp),
            ASCCOMM_ERROR(
                "[%s] UB reduce operation[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.reduceOp)),
            ASCCOMM_E_NOT_SUPPORT);
        Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn, &notify](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg, Asc::ConnLiteOperationOut& out) {
                connection.write_reduce_with_notify(
                    reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out, notify, NOTIFY_VALUE);
            });
    }
    return execute_rdma(
        thread, channel, RdmaOperation::WRITE_REDUCE_WITH_NOTIFY, localAddr, remoteAddr, len, &reduceIn,
        remoteNotifyIdx);
}

asccomm_result asccomm_read(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len)
{
    if (is_ub_protocol(channel.protocol)) {
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [](Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
               const Asc::SqeConfigLite& cfg,
               Asc::ConnLiteOperationOut& out) { connection.read(local, remote, cfg, out); });
    }
    return execute_rdma(thread, channel, RdmaOperation::READ, localAddr, remoteAddr, len, nullptr, 0);
}

asccomm_result asccomm_read_reduce(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uintptr_t remoteAddr, uint64_t len,
    const Asc::ReduceIn& reduceIn)
{
    if (is_ub_protocol(channel.protocol)) {
        CHK_PRT_RET(
            !is_reduce_data_type_supported(reduceIn.dataType),
            ASCCOMM_ERROR(
                "[%s] UB reduce data type[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.dataType)),
            ASCCOMM_E_NOT_SUPPORT);
        CHK_PRT_RET(
            !is_reduce_op_supported(reduceIn.reduceOp),
            ASCCOMM_ERROR(
                "[%s] UB reduce operation[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.reduceOp)),
            ASCCOMM_E_NOT_SUPPORT);
        return execute_ub_rma(
            thread, channel, localAddr, remoteAddr, len,
            [&reduceIn](
                Asc::UbConnLite& connection, const Asc::RmaBufSliceLite& local, const Asc::RmtRmaBufSliceLite& remote,
                const Asc::SqeConfigLite& cfg,
                Asc::ConnLiteOperationOut& out) { connection.read_reduce(reduceIn, local, remote, cfg, out); });
    }
    CHK_PRT_RET(
        channel.protocol != COMM_PROTOCOL_ROCE,
        ASCCOMM_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(
        !is_reduce_data_type_supported(reduceIn.dataType),
        ASCCOMM_ERROR(
            "[%s] RoCE reduce data type[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.dataType)),
        ASCCOMM_E_NOT_SUPPORT);
    CHK_PRT_RET(
        !is_reduce_op_supported(reduceIn.reduceOp),
        ASCCOMM_ERROR(
            "[%s] RoCE reduce operation[%u] is not supported.", __func__, static_cast<uint32_t>(reduceIn.reduceOp)),
        ASCCOMM_E_NOT_SUPPORT);
    // The original RoCE transport does not override ReadReduce and therefore performs no operation.
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_send(ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uint64_t len)
{
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol),
        ASCCOMM_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    CHK_RET(build_ub_local_slice(channel, localAddr, len, local));
    Asc::RmtRmaBufSliceLite remoteObj(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_send_rmt_obj(channel, remoteObj));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::ConnLiteOperationOut out{};
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    connection.send(local, remoteObj, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

asccomm_result asccomm_send_with_imm(
    ThreadEntity& thread, ChannelEntity& channel, uintptr_t localAddr, uint64_t len, uint64_t immData)
{
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol),
        ASCCOMM_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmaBufSliceLite local(0, 0, 0, 0);
    CHK_RET(build_ub_local_slice(channel, localAddr, len, local));
    Asc::RmtRmaBufSliceLite remoteObj(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_send_rmt_obj(channel, remoteObj));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    Asc::ConnLiteOperationOut out{};
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    connection.send_with_imm(local, remoteObj, immData, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

asccomm_result asccomm_notify_record(ThreadEntity& thread, ChannelEntity& channel, uint32_t remoteNotifyIdx)
{
    if (!is_ub_protocol(channel.protocol)) {
        return execute_rdma(thread, channel, RdmaOperation::NOTIFY_RECORD, 0, 0, 0, nullptr, remoteNotifyIdx);
    }
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
    CHK_RET(build_ub_remote_notify(channel, remoteNotifyIdx, notify));
    capture_transport_begin(thread, channel, false);
    AsccommBinaryCache::TransportScope captureScope(reinterpret_cast<uint64_t>(&channel));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    auto flow = GetUbFlow(channel);
    BindUbFlow(connection, flow, thread, channel);
    Asc::SqeConfigLite cfg;
    if (remoteNotifyIdx == 1) {
        cfg.cqeEn = true;
        cfg.placeOdr = UB_STRONG_ORDER;
        cfg.compOrder = UB_COMPLETION;
    }
    Asc::ConnLiteOperationOut out{};
    const uint32_t value = NOTIFY_VALUE;
    connection.inline_write(reinterpret_cast<const uint8_t*>(&value), NOTIFY_SIZE, notify, cfg, out);
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

asccomm_result asccomm_notify_wait(
    ThreadEntity& thread, const ChannelEntity& channel, uint32_t localNotifyIdx, uint32_t timeout)
{
    CHK_PRT_RET(
        channel.localNotifyAddr == nullptr || channel.localNotifyNum <= 1U ||
            localNotifyIdx >= channel.localNotifyNum - 1U,
        ASCCOMM_ERROR(
            "[%s] local notify index[%u] out of range[%u].", __func__, localNotifyIdx,
            channel.localNotifyNum > 0 ? channel.localNotifyNum - 1U : 0U),
        ASCCOMM_E_PARA);
    HcommThreadSqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    const HcommRegedNotifyEntity& notify = channel.localNotifyAddr[localNotifyIdx + 1U];
    CHK_PRT_RET(
        notify.type != HCOMM_REGED_NOTIFY_RMA_RT, ASCCOMM_ERROR("[%s] local notify is not RMA RT.", __func__),
        ASCCOMM_E_PARA);
    const int32_t notifyId = notify.notifyInfo.rmaRt.notifyId;
    CHK_PRT_RET(notifyId < 0, ASCCOMM_ERROR("[%s] invalid notify id[%d].", __func__, notifyId), ASCCOMM_E_PARA);
    const uint64_t binding[2] = {localNotifyIdx + 1U, static_cast<uint32_t>(notifyId)};
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::ChannelNotifyGuard, reinterpret_cast<uint64_t>(&channel), binding, sizeof(binding));
    return asccomm_rtsq_notify_wait(thread, static_cast<uint32_t>(notifyId), timeout);
}

asccomm_result asccomm_fence(ChannelEntity& channel)
{
    CHK_PRT_RET(
        channel.sqNum == 0 || channel.sqContextAddr == nullptr, ASCCOMM_ERROR("[%s] SQ context is empty.", __func__),
        ASCCOMM_E_PARA);
    capture_fence(channel, 1);
    channel.sqContextAddr[0].fence = 1;
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_drain(ThreadEntity& thread, ChannelEntity& channel)
{
    if (channel.protocol == COMM_PROTOCOL_ROCE) {
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        !is_ub_protocol(channel.protocol),
        ASCCOMM_ERROR("[%s] protocol[%d] is not supported.", __func__, channel.protocol), ASCCOMM_E_NOT_SUPPORT);
    if (channel.localNotifyNum == 0 || channel.remoteBufferNum == 0 || channel.localNotifyAddr == nullptr ||
        channel.remoteBufferAddr == nullptr) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    const HcommRegedNotifyEntity* localDrainNotify = &channel.localNotifyAddr[0];
    const HcommRegedBufferEntity* remoteDrainBuffer = &channel.remoteBufferAddr[0];
    if (localDrainNotify->type != HCOMM_REGED_NOTIFY_RMA_RT || remoteDrainBuffer->type != HCOMM_REGED_BUFFER_RMA) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    if (localDrainNotify->notifyInfo.rmaRt.size == 0 || remoteDrainBuffer->bufferInfo.rma.size == 0) {
        ASCCOMM_WARNING("[%s] drain resource is empty; skip.", __func__);
        return ASCCOMM_SUCCESS;
    }
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    const auto& localResource = localDrainNotify->notifyInfo.rmaRt;
    const auto& remoteResource = remoteDrainBuffer->bufferInfo.rma;
    CHK_PRT_RET(
        localResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            remoteResource.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA,
        ASCCOMM_ERROR("[%s] UB drain requires URMA protection.", __func__), ASCCOMM_E_PARA);
    Asc::RmaBufSliceLite local(
        localResource.addr, localResource.size, 0, localResource.protectionInfo.memInfo.ub.tokenId);
    Asc::RmtRmaBufSliceLite remote(
        remoteResource.addr, remoteResource.size, 0, remoteResource.protectionInfo.memInfo.ub.tokenId,
        remoteResource.protectionInfo.memInfo.ub.tokenValue, std::numeric_limits<uint32_t>::max());
    capture_transport_begin(thread, channel, false);
    AsccommBinaryCache::TransportScope captureScope(reinterpret_cast<uint64_t>(&channel));
    AsccommBinaryCache::ResourceRole() = 2;
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    auto flow = GetUbFlow(channel);
    BindUbFlow(connection, flow, thread, channel);
    Asc::SqeConfigLite cfg;
    cfg.fence = 1;
    cfg.placeOdr = UB_STRONG_ORDER;
    cfg.compOrder = UB_COMPLETION;
    if (channel.sqNum > 0 && channel.sqContextAddr != nullptr) {
        capture_fence(channel, 0);
        channel.sqContextAddr[0].fence = 0;
    }
    Asc::ConnLiteOperationOut out{};
    connection.read(local, remote, cfg, out);
    export_ub_state(connection, *jfs);
    CHK_RET(ring_ub_doorbell(thread, channel, *jfs, out.pi));
    HcommThreadSqContext* rtsq = nullptr;
    CHK_RET(get_rtsq(thread, rtsq));
    CHK_PRT_RET(
        localResource.notifyId < 0,
        ASCCOMM_ERROR("[%s] invalid drain notify id[%d].", __func__, localResource.notifyId), ASCCOMM_E_PARA);
    const uint64_t binding[2] = {0, static_cast<uint32_t>(localResource.notifyId)};
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::ChannelNotifyGuard, reinterpret_cast<uint64_t>(&channel), binding, sizeof(binding));
    return asccomm_rtsq_notify_wait(thread, static_cast<uint32_t>(localResource.notifyId), DEFAULT_TIMEOUT);
}

asccomm_result asccomm_batch_transfer(
    ThreadEntity& thread, ChannelEntity& channel, const asccomm_batch_transfer_desc* transferDescs,
    uint32_t transferDescNum)
{
    CHK_PTR_NULL(transferDescs);
    CHK_PRT_RET(transferDescNum == 0, ASCCOMM_ERROR("[%s] transferDescNum is 0.", __func__), ASCCOMM_E_PARA);
    HcommJfsContext* jfs = nullptr;
    CHK_RET(get_ub_context(channel, jfs));
    capture_transport_begin(thread, channel, false);
    AsccommBinaryCache::TransportScope captureScope(reinterpret_cast<uint64_t>(&channel));
    Asc::UbConnLiteParam param{};
    fill_ub_param(*jfs, param);
    Asc::UbConnLite connection;
    connection.import_context(param);
    import_ub_state(*jfs, connection);
    auto flow = GetUbFlow(channel);
    BindUbFlow(connection, flow, thread, channel);
    Asc::SqeConfigLite cfg = get_ub_config(channel);
    Asc::ConnLiteOperationOut out{};

    for (uint32_t index = 0; index < transferDescNum; ++index) {
        const asccomm_batch_transfer_desc& desc = transferDescs[index];
        cfg.cqeEn = index == transferDescNum - 1;
        cfg.placeOdr = cfg.cqeEn ? UB_STRONG_ORDER : UB_RELAX_ORDER;
        cfg.compOrder = cfg.cqeEn ? UB_COMPLETION : UB_NO_COMPLETION;
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_NOTIFY_RECORD) {
            Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            const uint32_t notifyIdx = desc.transferInfo.notifyRecord.notifyIdx;
            CHK_RET(build_ub_remote_notify(channel, notifyIdx, notify));
            if (notifyIdx == 1) {
                cfg.cqeEn = true;
                cfg.placeOdr = UB_STRONG_ORDER;
                cfg.compOrder = UB_COMPLETION;
            }
            const uint32_t value = NOTIFY_VALUE;
            connection.inline_write(reinterpret_cast<const uint8_t*>(&value), NOTIFY_SIZE, notify, cfg, out);
            continue;
        }

        uintptr_t localAddr = 0;
        uintptr_t remoteAddr = 0;
        uint64_t len = 0;
        uint32_t notifyIdx = 0;
        Asc::ReduceIn reduceIn{Asc::DataType::INVALID, Asc::ReduceOp::INVALID};
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.write.dst);
            len = desc.transferInfo.write.len;
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.dst);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.read.src);
            len = desc.transferInfo.read.len;
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeWithNotify.dst);
            len = desc.transferInfo.writeWithNotify.len;
            notifyIdx = desc.transferInfo.writeWithNotify.notifyIdx;
        } else if (
            desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ||
            desc.transType == ASCCOMM_TRANSFER_TYPE_READ_REDUCE) {
            localAddr = desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                            reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src) :
                            reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst);
            remoteAddr = desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ?
                             reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.dst) :
                             reinterpret_cast<uintptr_t>(desc.transferInfo.reduce.src);
            CHK_RET(convert_batch_reduce(
                desc.transferInfo.reduce.dataType, desc.transferInfo.reduce.reduceOp, reduceIn,
                desc.transferInfo.reduce.count, len));
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY) {
            localAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.src);
            remoteAddr = reinterpret_cast<uintptr_t>(desc.transferInfo.writeReduceWithNotify.dst);
            notifyIdx = desc.transferInfo.writeReduceWithNotify.notifyIdx;
            CHK_RET(convert_batch_reduce(
                desc.transferInfo.writeReduceWithNotify.dataType, desc.transferInfo.writeReduceWithNotify.reduceOp,
                reduceIn, desc.transferInfo.writeReduceWithNotify.count, len));
        } else {
            ASCCOMM_ERROR("[%s] unsupported transfer type[%d].", __func__, desc.transType);
            return ASCCOMM_E_PARA;
        }
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE ||
            desc.transType == ASCCOMM_TRANSFER_TYPE_READ_REDUCE ||
            desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE_WITH_NOTIFY) {
            CHK_PRT_RET(
                !is_reduce_data_type_supported(reduceIn.dataType),
                ASCCOMM_ERROR(
                    "[%s] UB reduce data type[%u] is not supported at batch index[%u].", __func__,
                    static_cast<uint32_t>(reduceIn.dataType), index),
                ASCCOMM_E_NOT_SUPPORT);
            CHK_PRT_RET(
                !is_reduce_op_supported(reduceIn.reduceOp),
                ASCCOMM_ERROR(
                    "[%s] UB reduce operation[%u] is not supported at batch index[%u].", __func__,
                    static_cast<uint32_t>(reduceIn.reduceOp), index),
                ASCCOMM_E_NOT_SUPPORT);
        }
        CHK_PRT_RET(
            localAddr == 0 || remoteAddr == 0, ASCCOMM_ERROR("[%s] null address at batch index[%u].", __func__, index),
            ASCCOMM_E_PTR);
        Asc::RmaBufSliceLite local(0, 0, 0, 0);
        Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
        CHK_RET(build_ub_slices(channel, localAddr, remoteAddr, len, local, remote));
        if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE) {
            connection.write(local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ) {
            connection.read(local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_REDUCE) {
            connection.write_reduce(reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out);
        } else if (desc.transType == ASCCOMM_TRANSFER_TYPE_READ_REDUCE) {
            connection.read_reduce(reduceIn, local, remote, cfg, out);
        } else {
            Asc::RmtRmaBufSliceLite notify(0, 0, 0, 0, 0, std::numeric_limits<uint32_t>::max());
            CHK_RET(build_ub_remote_notify(channel, notifyIdx, notify));
            if (desc.transType == ASCCOMM_TRANSFER_TYPE_WRITE_WITH_NOTIFY) {
                connection.write_with_notify(local, remote, cfg, out, notify, NOTIFY_VALUE);
            } else {
                connection.write_reduce_with_notify(
                    reduceIn.dataType, reduceIn.reduceOp, local, remote, cfg, out, notify, NOTIFY_VALUE);
            }
        }
    }
    export_ub_state(connection, *jfs);
    return ring_ub_doorbell(thread, channel, *jfs, out.pi);
}

namespace AsccommBinaryCache {
struct TransportWorkspace {
    struct UbState {
        uint64_t identity = 0;
        std::shared_ptr<Asc::UbConnLite> connection;
        std::shared_ptr<UbFlow> flow;
        HcommJfsContext* queue = nullptr;
        bool prepared = false;
        uint32_t leaseIndex = UINT32_MAX;
        uint32_t ownerIndex = UINT32_MAX;
    };
    struct Protection {
        const ChannelEntity* channel;
        uint64_t address, length;
        bool local;
        uint32_t token, value;
        uint64_t generation = 0;
    };
    enum class ProtectionKind { Local, Remote, DrainLocal, DrainRemote, Notify };
    struct ProtectionRef {
        ProtectionKind kind;
        uint64_t address, length;
        uint32_t index;
        uint32_t registration = UINT32_MAX;
        AddressPlan addressPlan;
        uint32_t staticSlot = UINT32_MAX;
    };
    struct StaticProtection {
        const ChannelEntity* channel;
        ProtectionRef ref;
        uint32_t token = 0, value = 0;
        uint64_t address = 0;
    };
    std::vector<StaticProtection> staticProtection;
    struct Registration {
        const ChannelEntity* channel;
        const HcommRegedBufferEntity* captured;
        bool local;
        uint32_t role;
        uint64_t begin, end, generation = 0;
        uint32_t token = 0, value = 0;
        bool uniform = false, valid = false;
        AddressPlan addressPlan;
    };
    struct Binding {
        ThreadEntity* thread = nullptr;
        UbFlow::Writer* writer = nullptr;
    };
    struct Lease {
        std::shared_ptr<UbFlow> flow;
        uint32_t count = 0;
        bool reserved = false;
    };
    std::vector<Binding> bindings;
    std::vector<Lease> leases;
    bool publishing = false;
    std::vector<Registration> registrations;
    struct ResolvedProtection {
        uint32_t token = 0, value = 0;
        uint64_t address = 0;
    };
    struct BoundProtection {
        const uint32_t* token = nullptr;
        const uint32_t* value = nullptr;
        const uint64_t* address = nullptr;
        uint64_t fixedAddress = 0, offset = 0;
        uint32_t role = UINT32_MAX;
    };
    // Immutable publication plan, separate from capture/validation descriptors.
    // Bound pointers target mutable per-entry registration slots, never PI/CI snapshots.
    struct UbPublishPlan {
        AscProf::WqeDescription profiling;
        BoundProtection boundRemote{}, boundLocal{}, boundNotify{};
        uint32_t localOffset = 0;
        bool hasLocal = false, hasNotify = false;
    };
    std::vector<UbPublishPlan> publishPlans;
    struct UbPatch {
        ProtectionRef remote{}, local{}, notify{};
        uint32_t localOffset = 0;
        bool hasLocal = false, hasNotify = false, valid = false;
    };
    std::vector<UbPatch> patches;
    std::vector<UbState> ub;
    std::vector<Protection> protection;
    uint64_t generation = 0;
};
namespace {
thread_local TransportWorkspace* liveWorkspace = nullptr;
thread_local Entry* liveEntry = nullptr;
void BindRegistration(Entry&, const ChannelEntity&, TransportWorkspace::ProtectionRef&, TransportWorkspace&);
bool CompileUbPatch(const ChannelEntity&, const Event&, const Patch&, const uint8_t*, TransportWorkspace::UbPatch&);
bool RefreshUbPatch(const ChannelEntity&, const TransportWorkspace::UbPatch&, uint8_t*);
TransportWorkspace::UbState* LiveUb(uint32_t index, uint64_t identity)
{
    if (!liveWorkspace || index >= liveWorkspace->ub.size()) {
        return nullptr;
    }
    auto& state = liveWorkspace->ub[index];
    return state.identity == identity ? &state : nullptr;
}
} // namespace
bool CompileTransport(Entry& entry)
{
    try {
        if (!entry.transportWorkspace) {
            entry.transportWorkspace = std::make_shared<TransportWorkspace>();
        }
        auto& work = *entry.transportWorkspace;
        work.ub.resize(entry.resources.size());
        // Keep owner allocations, but discard aliases before recycling slots. Every
        // retained connection is reset/imported below; no queue state survives capture.
        for (size_t i = 0; i < work.ub.size(); ++i) {
            auto& slot = work.ub[i];
            if (slot.ownerIndex != i) {
                slot.connection.reset();
            }
            slot.identity = 0;
        }
        work.patches.resize(entry.events.size());
        for (auto& patch : work.patches) {
            patch.valid = false;
        }
        work.registrations.clear();
        work.staticProtection.clear();
        // Only UbBegin events consume bindings; each is overwritten before publication.
        work.bindings.resize(entry.events.size());
        work.leases.clear();
        // Registration groups own the normal path. Keep only a bounded optional
        // exact-query fallback cache; saturation still resolves native protection.
        work.protection.resize(32);
        for (auto& slot : work.protection) {
            slot.generation = 0;
        }
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            // Type/layout/order is immutable after capture compilation.
            if ((e.kind == Kind::UbEnd && e.length != sizeof(uint64_t)) ||
                ((e.kind == Kind::ChannelNotifyGuard || e.kind == Kind::ThreadNotifyGuard) && e.length != 16)) {
                return false;
            }
            if (e.kind != Kind::UbBegin) {
                continue;
            }
            auto& slot = work.ub[entry.plan[i].resourceIndex];
            if (slot.identity == e.resource) {
                continue;
            }
            auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
            HcommJfsContext* q = nullptr;
            if (get_ub_context(channel, q) != ASCCOMM_SUCCESS) {
                return false;
            }
            TransportWorkspace::UbState state;
            state.identity = e.resource;
            state.queue = q;
            state.ownerIndex = entry.plan[i].resourceIndex;
            state.flow = GetUbFlow(channel);
            for (const auto& bound : work.ub) {
                if (bound.identity && bound.queue == q && bound.connection) {
                    state.connection = bound.connection;
                    state.ownerIndex = bound.ownerIndex;
                }
            }
            if (!state.connection) {
                // Reuse storage only, not the previous connection identity/state.
                state.connection = std::move(slot.connection);
                if (!state.connection || !state.connection.unique()) {
                    state.connection = std::make_shared<Asc::UbConnLite>();
                } else {
                    *state.connection = Asc::UbConnLite{};
                }
            }
            Asc::UbConnLiteParam param{};
            fill_ub_param(*q, param);
            state.connection->import_context(param);
            slot = std::move(state);
        }
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            if (e.kind == Kind::UbBegin) {
                auto& state = work.ub[entry.plan[i].resourceIndex];
                BinaryBinding binding;
                if (e.length != sizeof(binding)) {
                    return false;
                }
                (void)memcpy_sp(&binding, sizeof(binding), entry.bytes.data() + e.offset, sizeof(binding));
                auto* thread = reinterpret_cast<ThreadEntity*>(binding.words[0]);
                if (!thread || !thread->sqContextAddr) {
                    return false;
                }
                auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
                BindUbFlow(*state.connection, state.flow, *thread, channel);
                work.bindings[i] = {thread, state.flow ? state.flow->writer : nullptr};
            }
            if (e.kind == Kind::UbWqe) {
                auto flow = work.ub[entry.plan[i].resourceIndex].flow;
                if (flow) {
                    auto it = std::find_if(
                        work.leases.begin(), work.leases.end(),
                        [&](const TransportWorkspace::Lease& l) { return l.flow == flow; });
                    if (it == work.leases.end()) {
                        work.leases.push_back({flow, 0, false});
                        it = work.leases.end() - 1;
                    }
                    it->count += e.length / 64;
                }
            }
            if (e.kind == Kind::UbWqe &&
                (!e.resource || e.patchCount != 1 ||
                 !CompileUbPatch(
                     *reinterpret_cast<ChannelEntity*>(e.resource), e, entry.patches[e.patchBegin],
                     entry.bytes.data() + e.offset, work.patches[i]))) {
                return false;
            }
            if (e.kind == Kind::UbWqe) {
                auto& patch = work.patches[i];
                auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
                BindRegistration(entry, channel, patch.remote, work);
                if (patch.hasLocal) {
                    BindRegistration(entry, channel, patch.local, work);
                }
                for (auto* ref : {&patch.remote, &patch.local, &patch.notify}) {
                    if ((ref == &patch.local && !patch.hasLocal) || (ref == &patch.notify && !patch.hasNotify)) {
                        continue;
                    }
                    if (!ref->addressPlan.Compile(entry.captureRanges, entry.relocatable, ref->address, ref->length)) {
                        return false;
                    }
                    if (ref->registration == UINT32_MAX) {
                        uint32_t slot = 0;
                        for (; slot < work.staticProtection.size(); ++slot) {
                            const auto& item = work.staticProtection[slot];
                            if (item.channel == &channel && item.ref.kind == ref->kind &&
                                item.ref.index == ref->index && item.ref.address == ref->address &&
                                item.ref.length == ref->length) {
                                break;
                            }
                        }
                        if (slot == work.staticProtection.size()) {
                            work.staticProtection.push_back({&channel, *ref});
                        }
                        ref->staticSlot = slot;
                    }
                }
            }
        }
        for (auto& state : work.ub) {
            state.leaseIndex = UINT32_MAX;
            for (uint32_t i = 0; i < work.leases.size(); ++i) {
                if (work.leases[i].flow == state.flow) {
                    state.leaseIndex = i;
                    break;
                }
            }
        }
        for (auto& group : work.registrations) {
            if (!group.addressPlan.Compile(
                    entry.captureRanges, entry.relocatable, group.begin, group.end - group.begin)) {
                return false;
            }
        }
        // No vector is resized during replay. Recompilation rebuilds every
        // binding after the registration/static slot vectors finish growing.
        auto bind = [&](const TransportWorkspace::ProtectionRef& ref) {
            TransportWorkspace::BoundProtection b;
            if (ref.staticSlot != UINT32_MAX) {
                const auto& slot = work.staticProtection[ref.staticSlot];
                b.token = &slot.token;
                b.value = &slot.value;
                b.address = &slot.address;
            } else {
                const auto& group = work.registrations[ref.registration];
                b.token = &group.token;
                b.value = &group.value;
                b.role = ref.addressPlan.role;
                b.offset = ref.addressPlan.offset;
                b.fixedAddress = ref.address;
            }
            return b;
        };
        work.publishPlans.resize(work.patches.size());
        for (size_t i = 0; i < work.patches.size(); ++i) {
            const auto& patch = work.patches[i];
            if (!patch.valid) {
                continue;
            }
            auto& plan = work.publishPlans[i];
            plan = {}; // Only WQE events have publication plans.
            plan.boundRemote = bind(patch.remote);
            if (patch.hasLocal) {
                plan.boundLocal = bind(patch.local);
            }
            if (patch.hasNotify) {
                plan.boundNotify = bind(patch.notify);
            }
            plan.localOffset = patch.localOffset;
            plan.hasLocal = patch.hasLocal;
            plan.hasNotify = patch.hasNotify;
            const auto& event = entry.events[i];
            // No addresses or session IDs are retained. StorageBytes includes this field.
            (void)AscProf::DescribeWqe(entry.bytes.data() + event.offset, event.length, plan.profiling);
        }
        entry.transportStorageBytes =
            work.bindings.capacity() * sizeof(TransportWorkspace::Binding) +
            work.leases.capacity() * sizeof(TransportWorkspace::Lease) + sizeof(TransportWorkspace) +
            work.staticProtection.capacity() * sizeof(TransportWorkspace::StaticProtection) +
            work.registrations.capacity() * sizeof(TransportWorkspace::Registration) +
            work.patches.capacity() * sizeof(TransportWorkspace::UbPatch) +
            work.publishPlans.capacity() * sizeof(TransportWorkspace::UbPublishPlan) +
            work.ub.capacity() * sizeof(TransportWorkspace::UbState) + work.ub.size() * sizeof(Asc::UbConnLite) +
            work.protection.capacity() * sizeof(TransportWorkspace::Protection);
        return entry.StorageBytes() <= ASCCOMM_BINARY_ENTRY_BUDGET;
    } catch (...) {
        return false;
    }
}
void BeginTransportReplay(Entry& entry)
{
    liveEntry = &entry;
    liveWorkspace = entry.transportWorkspace.get();
    if (liveWorkspace) {
        for (auto& state : liveWorkspace->ub) {
            state.prepared = false;
        }
        if (++liveWorkspace->generation == 0) {
            for (auto& slot : liveWorkspace->protection) {
                slot.generation = 0;
            }
            for (auto& group : liveWorkspace->registrations) {
                group.generation = 0;
            }
            ++liveWorkspace->generation;
        } // generation tags invalidate all tokens on every hit without clearing the table
    }
}
// Called only after all preflight checks. Reserving a whole connection is optional:
// a long/near-full queue retains the native per-WQE checkpoint path.
void BeginTransportPublication()
{
    if (!liveWorkspace) {
        return;
    }
    liveWorkspace->publishing = true;
    for (auto& lease : liveWorkspace->leases) {
        auto& f = *lease.flow;
        lease.reserved = false;
        const auto pi = static_cast<uint16_t>(f.queue->sqHead);
        if (!f.budget.Refresh(pi, static_cast<uint16_t>(f.queue->sqTail))) {
            FlowError(f, "invalid shared replay PI/CI");
        }
        if (f.budget.HasSpace(lease.count)) {
            if (!f.budget.Reserve(pi, lease.count)) {
                FlowError(f, "replay connection reservation failed");
            }
            lease.reserved = true;
        }
    }

    if (liveEntry && liveEntry->direct) {
        for (auto& state : liveWorkspace->ub) {
            if (!state.identity || !state.connection || &state != &liveWorkspace->ub[state.ownerIndex]) {
                continue;
            }
            const bool reserved = state.leaseIndex != UINT32_MAX && liveWorkspace->leases[state.leaseIndex].reserved;
            state.connection->set_space_guard((reserved || !state.flow) ? nullptr : ReserveUb, state.flow.get());
        }
    }
}
void CompleteTransportPublication()
{
    if (!liveWorkspace) {
        return;
    }
    if (liveEntry && liveEntry->direct) {
        for (auto& state : liveWorkspace->ub) {
            if (state.identity && state.prepared && &state == &liveWorkspace->ub[state.ownerIndex]) {
                export_ub_state(*state.connection, *state.queue);
            }
        }
    }
    for (const auto& lease : liveWorkspace->leases) {
        if (lease.reserved && !lease.flow->budget.Matches(static_cast<uint16_t>(lease.flow->queue->sqHead))) {
            FlowError(*lease.flow, "replay WQEBB count differs from compiled plan");
        }
    }
    liveWorkspace->publishing = false;
}
void EndTransportReplay()
{
    if (liveWorkspace) {
        if (liveWorkspace->publishing) {
            for (auto& state : liveWorkspace->ub) {
                if (state.prepared && state.connection && state.queue) {
                    export_ub_state(*state.connection, *state.queue);
                }
            }
            // Partial execution cannot retry or release unused reserved credits.
            for (auto& lease : liveWorkspace->leases) {
                lease.flow->budget.failed = true;
            }
        }
        for (auto& state : liveWorkspace->ub) {
            if (state.identity && state.connection && state.flow && &state == &liveWorkspace->ub[state.ownerIndex]) {
                state.connection->set_space_guard(ReserveUb, state.flow.get());
            }
        }
    }
    liveWorkspace = nullptr;
    liveEntry = nullptr;
}
namespace {
bool ub_protection(const ChannelEntity& c, uint64_t addr, uint64_t len, bool local, uint32_t& token, uint32_t& value)
{
    TransportWorkspace::Protection* cacheSlot = nullptr;
    if (liveWorkspace && !liveWorkspace->protection.empty()) {
        const size_t mask = liveWorkspace->protection.size() - 1;
        size_t index = ((reinterpret_cast<uintptr_t>(&c) >> 4) ^ (addr >> 4) ^ (len * 17) ^ local) & mask;
        for (size_t attempts = 0; attempts <= mask; ++attempts, index = (index + 1) & mask) {
            auto& cached = liveWorkspace->protection[index];
            if (cached.generation != liveWorkspace->generation) {
                cacheSlot = &cached;
                break;
            }
            if (cached.channel == &c && cached.address == addr && cached.length == len && cached.local == local) {
                token = cached.token;
                value = cached.value;
                return true;
            }
        }
    }
    const auto* buffers = local ? c.localBufferAddr : c.remoteBufferAddr;
    const auto count = local ? c.localBufferNum : c.remoteBufferNum;
    // Match build_ub_slices: local addresses may use the first registered protection.
    // Remote ranges remain strict and skip the reserved slot. Resolve live tokens on
    // every replay; no cached token and no per-hit fallback warning.
    const auto* b = find_buffer(
        !local && buffers && count ? buffers + 1 : buffers, !local && count ? count - 1 : count, addr, len, local,
        false);
    if (b && b->bufferInfo.rma.protectionInfo.type == HCOMM_PROTECTION_TYPE_URMA) {
        token = b->bufferInfo.rma.protectionInfo.memInfo.ub.tokenId;
        value = b->bufferInfo.rma.protectionInfo.memInfo.ub.tokenValue;
        if (cacheSlot) {
            *cacheSlot = {&c, addr, len, local, token, value, liveWorkspace->generation};
        }
        return true;
    }
    return false;
}
bool rdma_protection(const ChannelEntity& c, uint64_t addr, uint64_t len, bool local, uint32_t& key)
{
    const auto* remote = c.remoteBufferAddr && c.remoteBufferNum ? c.remoteBufferAddr + 1 : nullptr;
    const auto* b = find_buffer(
        local ? c.localBufferAddr : remote, local ? c.localBufferNum : (c.remoteBufferNum ? c.remoteBufferNum - 1 : 0),
        addr, len, false);
    if (b && b->bufferInfo.rma.protectionInfo.type == HCOMM_PROTECTION_TYPE_RDMA) {
        const auto& p = b->bufferInfo.rma.protectionInfo.memInfo.roce;
        key = local ? p.lkey : p.rkey;
        return true;
    }
    return false;
}
uint64_t address(uint32_t low, uint32_t high) { return low | (uint64_t(high) << 32); }
bool patch_ub(const ChannelEntity& channel, const Event& event, const Patch& field, uint8_t* data)
{
    static_assert(sizeof(Asc::UdmaSqeWrite) == 64, "UB WQE ABI");
    static_assert(sizeof(Asc::UdmaSqeWriteWithNotify) == 96, "UB notify WQE ABI");
    if ((event.length != 64 && event.length != 128) || field.field != 2 || field.offset != 0 ||
        (field.source != 0 && field.source != 2)) {
        return Reject(AsccommRejectReason::UbPatchEncoding);
    }
    alignas(8) uint8_t scratch[128]{};
    (void)memcpy_sp(scratch, sizeof(scratch), data, event.length);
    auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(scratch);
    const bool notify = event.length == 128;
    if ((notify && common.opcode != 5) ||
        (!notify && common.opcode != Asc::UDMA_OPC_READ && common.opcode != Asc::UDMA_OPC_WRITE)) {
        return Reject(AsccommRejectReason::UbOpcode);
    }
    auto& normal = *reinterpret_cast<Asc::UdmaSqeWrite*>(scratch);
    auto& withNotify = *reinterpret_cast<Asc::UdmaSqeWriteWithNotify*>(scratch);
    auto& sge = notify ? withNotify.localU.sge : normal.u.sge;
    const uint64_t len = common.inlineEn ? common.inlineMsgLen : sge.length;
    uint32_t token = 0, value = 0;
    const auto remoteAddr = address(common.rmtAddr.rmtAddrLow, common.rmtAddr.rmtAddrHigh);
    auto notifyProtection = [&](uint64_t addr, uint64_t size) {
        if (field.span > UINT32_MAX) {
            return Reject(AsccommRejectReason::NotifyIndex);
        }
        Asc::RmtRmaBufSliceLite n(0, 0, 0, 0, 0, 0);
        if (build_ub_remote_notify(channel, static_cast<uint32_t>(field.span), n) != ASCCOMM_SUCCESS ||
            !is_range_contained(n.get_addr(), n.get_size(), addr, size)) {
            return Reject(AsccommRejectReason::RemoteNotifyRange);
        }
        token = n.get_token_id();
        value = n.get_token_value();
        return true;
    };
    if (field.source == 2) {
        if (!channel.remoteBufferAddr || !channel.remoteBufferNum) {
            return Reject(AsccommRejectReason::RemoteBufferMissing);
        }
        const auto& b = channel.remoteBufferAddr[0];
        const auto& r = b.bufferInfo.rma;
        if (b.type != HCOMM_REGED_BUFFER_RMA || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
            !is_range_contained(r.addr, r.size, remoteAddr, len)) {
            return Reject(AsccommRejectReason::RemoteBufferRange);
        }
        token = r.protectionInfo.memInfo.ub.tokenId;
        value = r.protectionInfo.memInfo.ub.tokenValue;
    } else if (common.inlineEn) {
        if (!notifyProtection(remoteAddr, len)) {
            return Reject(AsccommRejectReason::InlineProtection);
        }
    } else if (!ub_protection(channel, remoteAddr, len, false, token, value)) {
        return Reject(AsccommRejectReason::RemoteProtection);
    }
    common.rmtObjId = token;
    common.rmtTokenValue = value;
    if (common.inlineEn) {
        // All supported native inline calls are the four-byte NOTIFY_VALUE.
        // Arbitrary inline payloads require an explicit dynamic source and are rejected.
        uint32_t payload = 0;
        (void)memcpy_sp(&payload, sizeof(payload), normal.u.inlineData.data, sizeof(payload));
        if (notify || len != sizeof(payload) || payload != NOTIFY_VALUE) {
            return Reject(AsccommRejectReason::InlinePayload);
        }
    } else {
        const auto localAddr = address(sge.dataAddrLow, sge.dataAddrHigh);
        if (field.source == 2) {
            if (!channel.localNotifyAddr || !channel.localNotifyNum) {
                return Reject(AsccommRejectReason::LocalNotifyMissing);
            }
            const auto& b = channel.localNotifyAddr[0];
            const auto& r = b.notifyInfo.rmaRt;
            if (b.type != HCOMM_REGED_NOTIFY_RMA_RT || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                !is_range_contained(r.addr, r.size, localAddr, len)) {
                return Reject(AsccommRejectReason::LocalNotifyRange);
            }
            token = r.protectionInfo.memInfo.ub.tokenId;
        } else if (!ub_protection(channel, localAddr, len, true, token, value)) {
            return Reject(AsccommRejectReason::LocalProtection);
        }
        sge.tokenId = token;
    }
    if (notify) {
        auto& n = withNotify.notify;
        if (!notifyProtection(address(n.notifyAddrLow, n.notifyAddrHigh), sizeof(uint32_t))) {
            return Reject(AsccommRejectReason::WriteNotifyProtection);
        }
        n.notifyTokenId = token;
        n.notifyTokenValue = value;
    }
    (void)memcpy_sp(data, event.length, scratch, event.length);
    return true;
}
bool CompileUbPatch(
    const ChannelEntity& channel, const Event& e, const Patch& field, const uint8_t* data,
    TransportWorkspace::UbPatch& plan)
{
    plan = {}; // Reset active patches, including optional fields from older captures.
    alignas(8) uint8_t scratch[128]{};
    if ((e.length != 64 && e.length != 128) || e.offset % alignof(uint32_t)) {
        return false;
    }
    (void)memcpy_sp(scratch, sizeof(scratch), data, e.length);
    if (!patch_ub(channel, e, field, scratch)) {
        return false;
    }
    const auto& common = *reinterpret_cast<const Asc::UdmaSqeCommon*>(scratch);
    const bool notify = e.length == 128;
    const auto& normal = *reinterpret_cast<const Asc::UdmaSqeWrite*>(scratch);
    const auto& extended = *reinterpret_cast<const Asc::UdmaSqeWriteWithNotify*>(scratch);
    const auto& sge = notify ? extended.localU.sge : normal.u.sge;
    const uint64_t len = common.inlineEn ? common.inlineMsgLen : sge.length;
    using K = TransportWorkspace::ProtectionKind;
    plan.remote = {
        field.source == 2 ? K::DrainRemote : (common.inlineEn ? K::Notify : K::Remote),
        address(common.rmtAddr.rmtAddrLow, common.rmtAddr.rmtAddrHigh),
        len,
        static_cast<uint32_t>(field.span),
        UINT32_MAX,
        {}};
    plan.hasLocal = !common.inlineEn;
    if (plan.hasLocal) {
        plan.local = {field.source == 2 ? K::DrainLocal : K::Local,
                      address(sge.dataAddrLow, sge.dataAddrHigh),
                      len,
                      0,
                      UINT32_MAX,
                      {}};
        plan.localOffset = notify ? offsetof(Asc::UdmaSqeWriteWithNotify, localU) : offsetof(Asc::UdmaSqeWrite, u);
    }
    plan.hasNotify = notify;
    if (notify) {
        plan.notify = {K::Notify,        address(extended.notify.notifyAddrLow, extended.notify.notifyAddrHigh),
                       sizeof(uint32_t), static_cast<uint32_t>(field.span),
                       UINT32_MAX,       {}};
    }
    plan.valid = true;
    return true;
}
void BindRegistration(
    Entry& entry, const ChannelEntity& channel, TransportWorkspace::ProtectionRef& ref, TransportWorkspace& work)
{
    using K = TransportWorkspace::ProtectionKind;
    if (ref.kind != K::Local && ref.kind != K::Remote) {
        return;
    }
    const bool local = ref.kind == K::Local;
    const auto* buffers = local ? channel.localBufferAddr : channel.remoteBufferAddr;
    auto count = local ? channel.localBufferNum : channel.remoteBufferNum;
    if (!local && buffers && count) {
        ++buffers;
        --count;
    }
    const auto* selected = find_buffer(buffers, count, ref.address, ref.length, local, false);
    if (!selected || ref.length > UINT64_MAX - ref.address) {
        return;
    }
    uint32_t role = 2;
    if (entry.relocatable) {
        for (uint32_t i = 0; i < 2; ++i) {
            if (is_range_contained(
                    entry.captureRanges[i * 2], entry.captureRanges[i * 2 + 1], ref.address, ref.length)) {
                role = i;
            }
        }
    }
    uint32_t index = 0;
    for (; index < work.registrations.size(); ++index) {
        const auto& g = work.registrations[index];
        if (g.channel == &channel && g.captured == selected && g.local == local && g.role == role) {
            break;
        }
    }
    if (index == work.registrations.size()) {
        work.registrations.push_back(
            {&channel, selected, local, role, ref.address, ref.address + ref.length, 0, 0, 0, false, false, {}});
    }
    auto& group = work.registrations[index];
    group.begin = std::min(group.begin, ref.address);
    group.end = std::max(group.end, ref.address + ref.length);
    ref.registration = index;
}
bool RegisteredProtection(
    const ChannelEntity& channel, const TransportWorkspace::ProtectionRef& ref, uint64_t address, uint32_t& token,
    uint32_t& value)
{
    const bool local = ref.kind == TransportWorkspace::ProtectionKind::Local;
    if (!liveWorkspace || ref.registration >= liveWorkspace->registrations.size()) {
        return ub_protection(channel, address, ref.length, local, token, value);
    }
    auto& group = liveWorkspace->registrations[ref.registration];
    if (group.generation != liveWorkspace->generation) {
        group.generation = liveWorkspace->generation;
        group.valid = group.uniform = false;
        uint64_t begin = group.begin;
        const uint64_t length = group.end - group.begin;
        if (liveEntry && !group.addressPlan.Resolve(
                             liveEntry->captureRanges, liveEntry->replayRanges, liveEntry->relocatable, begin)) {
            return false;
        }
        const auto* buffers = local ? channel.localBufferAddr : channel.remoteBufferAddr;
        auto count = local ? channel.localBufferNum : channel.remoteBufferNum;
        if (!local && buffers && count) {
            ++buffers;
            --count;
        }
        const auto* selected = find_buffer(buffers, count, begin, length, local, false);
        if (selected) {
            const auto& region = selected->bufferInfo.rma;
            group.uniform = true;
            const bool contained = is_range_contained(region.addr, region.size, begin, length);
            // Preserve first-match and local-fallback rules for overlapping registrations.
            // Ambiguous groups must re-expand the original primitive: native
            // chooses protection for the whole operation, not individual WQEs.
            for (uint32_t i = 0; i < count; ++i) {
                if (contained && &buffers[i] == selected) {
                    break;
                }
                const auto& candidate = buffers[i];
                const auto& r = candidate.bufferInfo.rma;
                if (candidate.type == HCOMM_REGED_BUFFER_RMA && r.size && r.size <= UINT64_MAX - r.addr &&
                    begin < r.addr + r.size && r.addr < begin + length) {
                    group.uniform = false;
                }
            }
            group.valid = region.protectionInfo.type == HCOMM_PROTECTION_TYPE_URMA;
            if (group.valid) {
                group.token = region.protectionInfo.memInfo.ub.tokenId;
                group.value = region.protectionInfo.memInfo.ub.tokenValue;
            }
        }
    }
    if (!group.uniform) {
        return false; // preflight fallback before any publication
    }
    if (!group.valid) {
        return false;
    }
    token = group.token;
    value = group.value;
    return true;
}
bool ResolveCompiledProtection(
    const ChannelEntity& channel, const TransportWorkspace::ProtectionRef& ref, uint32_t& token, uint32_t& value,
    uint64_t& mapped)
{
    using K = TransportWorkspace::ProtectionKind;
    mapped = ref.address;
    if (liveEntry && liveEntry->direct && ref.staticSlot != UINT32_MAX) {
        const auto& slot = liveWorkspace->staticProtection[ref.staticSlot];
        token = slot.token;
        value = slot.value;
        mapped = slot.address;
        return true;
    }
    if (liveEntry && liveEntry->direct && ref.registration != UINT32_MAX) {
        const auto& group = liveWorkspace->registrations[ref.registration];
        // The registration's union range was validated once before this loop.
        mapped = ref.addressPlan.role < 4 ? liveEntry->replayRanges[ref.addressPlan.role] + ref.addressPlan.offset :
                                            ref.address;
        token = group.token;
        value = group.value;
        return true;
    }
    if (liveEntry &&
        !ref.addressPlan.Resolve(liveEntry->captureRanges, liveEntry->replayRanges, liveEntry->relocatable, mapped)) {
        return Reject(AsccommRejectReason::RemoteProtection);
    }
    // Notify/drain storage belongs to the resource, never to a relocated user buffer.
    if (ref.kind != K::Local && ref.kind != K::Remote && mapped != ref.address) {
        return Reject(AsccommRejectReason::RemoteProtection);
    }
    if (liveEntry && liveEntry->direct && ref.registration != UINT32_MAX) {
        const auto& group = liveWorkspace->registrations[ref.registration];
        token = group.token;
        value = group.value;
        return group.valid && group.uniform;
    }
    auto current = ref;
    current.address = mapped;
    const auto& resolved = current;
    switch (ref.kind) {
        case K::Local:
            return RegisteredProtection(channel, ref, resolved.address, token, value) ||
                   Reject(AsccommRejectReason::LocalProtection);
        case K::Remote:
            return RegisteredProtection(channel, ref, resolved.address, token, value) ||
                   Reject(AsccommRejectReason::RemoteProtection);
        case K::Notify: {
            Asc::RmtRmaBufSliceLite n(0, 0, 0, 0, 0, 0);
            if (build_ub_remote_notify(channel, ref.index, n) != ASCCOMM_SUCCESS ||
                !is_range_contained(n.get_addr(), n.get_size(), resolved.address, resolved.length)) {
                return Reject(AsccommRejectReason::RemoteNotifyRange);
            }
            token = n.get_token_id();
            value = n.get_token_value();
            return true;
        }
        case K::DrainLocal: {
            if (!channel.localNotifyAddr || !channel.localNotifyNum) {
                return Reject(AsccommRejectReason::LocalNotifyMissing);
            }
            const auto& b = channel.localNotifyAddr[0];
            const auto& r = b.notifyInfo.rmaRt;
            if (b.type != HCOMM_REGED_NOTIFY_RMA_RT || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                !is_range_contained(r.addr, r.size, resolved.address, resolved.length)) {
                return Reject(AsccommRejectReason::LocalNotifyRange);
            }
            token = r.protectionInfo.memInfo.ub.tokenId;
            value = r.protectionInfo.memInfo.ub.tokenValue;
            return true;
        }
        case K::DrainRemote: {
            if (!channel.remoteBufferAddr || !channel.remoteBufferNum) {
                return Reject(AsccommRejectReason::RemoteBufferMissing);
            }
            const auto& b = channel.remoteBufferAddr[0];
            const auto& r = b.bufferInfo.rma;
            if (b.type != HCOMM_REGED_BUFFER_RMA || r.protectionInfo.type != HCOMM_PROTECTION_TYPE_URMA ||
                !is_range_contained(r.addr, r.size, resolved.address, resolved.length)) {
                return Reject(AsccommRejectReason::RemoteBufferRange);
            }
            token = r.protectionInfo.memInfo.ub.tokenId;
            value = r.protectionInfo.memInfo.ub.tokenValue;
            return true;
        }
    }
    return false;
}
bool RefreshUbPatch(const ChannelEntity& channel, const TransportWorkspace::UbPatch& plan, uint8_t* data)
{
    if (!plan.valid) {
        return Reject(AsccommRejectReason::UbPatchEncoding);
    }
    uint32_t token = 0, value = 0;
    uint64_t mapped = 0;
    if (!ResolveCompiledProtection(channel, plan.remote, token, value, mapped)) {
        return false;
    }
    auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(data);
    common.rmtObjId = token;
    common.rmtTokenValue = value;
    if (liveEntry) {
        common.rmtAddr.rmtAddrLow = mapped;
        common.rmtAddr.rmtAddrHigh = mapped >> 32;
    }
    if (plan.hasLocal) {
        if (!ResolveCompiledProtection(channel, plan.local, token, value, mapped)) {
            return false;
        }
        auto& local = *reinterpret_cast<Asc::UdmaNormalSge*>(data + plan.localOffset);
        local.tokenId = token;
        if (liveEntry) {
            local.dataAddrLow = mapped;
            local.dataAddrHigh = mapped >> 32;
        }
    }
    if (plan.hasNotify) {
        if (!ResolveCompiledProtection(channel, plan.notify, token, value, mapped)) {
            return false;
        }
        auto& notify = reinterpret_cast<Asc::UdmaSqeWriteWithNotify*>(data)->notify;
        notify.notifyTokenId = token;
        notify.notifyTokenValue = value;
    }
    return true;
}

} // namespace

bool PrepareTransport(const Event& e, const Entry& entry, std::vector<uint8_t>& patched)
{
    if (e.layout != VERSION || !e.resource) {
        return Reject(AsccommRejectReason::TransportEncoding);
    }
    auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
    if (!channel.sqContextAddr || !channel.sqNum) {
        return Reject(AsccommRejectReason::ChannelQueue);
    }
    const auto* source = entry.bytes.data() + e.offset;
    if (e.kind == Kind::UbBegin || e.kind == Kind::RdmaBegin) {
        if (e.length != sizeof(BinaryBinding)) {
            return Reject(AsccommRejectReason::TransportBinding);
        }
        BinaryBinding expected;
        (void)memcpy_sp(&expected, sizeof(expected), source, sizeof(expected));
        auto* thread = reinterpret_cast<ThreadEntity*>(expected.words[0]);
        if (!thread || !thread->sqContextAddr) {
            return Reject(AsccommRejectReason::ThreadQueue);
        }
        if (thread->sqContextAddr->contextInfo.rtsqContext.pendingSqeCnt != 0) {
            return Reject(AsccommRejectReason::PendingTransport);
        }
        if (e.kind == Kind::UbBegin) {
            HcommJfsContext* q = nullptr;
            if (get_ub_context(channel, q) != ASCCOMM_SUCCESS || q->sqDepth < 2) {
                return Reject(AsccommRejectReason::UbQueue);
            }
        } else {
            HcommRdmaSqContext* q = nullptr;
            HcommRdmaCqContext* c = nullptr;
            if (get_rdma_contexts(channel, q, c) != ASCCOMM_SUCCESS || (q->depth & (q->depth - 1)) ||
                q->wqeSize != sizeof(Asc::Roce3WqeEntry) || !q->dbSwVa) {
                return Reject(AsccommRejectReason::RdmaQueue);
            }
        }
        if (expected.words != binary_binding(*thread, channel).words) {
            return Reject(AsccommRejectReason::ResourceChanged);
        }
        if (e.kind == Kind::UbBegin) {
            auto* live = LiveUb(entry.plan[&e - entry.events.data()].resourceIndex, e.resource);
            if (!live || live->queue != &channel.sqContextAddr[0].contextInfo.ubJfs) {
                return Reject(AsccommRejectReason::UbQueue);
            }
            if (!live->prepared) {
                import_ub_state(*live->queue, *live->connection);
                live->prepared = true;
            }
        }
        return true;
    }
    if (e.kind == Kind::ChannelNotifyGuard) {
        if (e.length != 2 * sizeof(uint64_t)) {
            return Reject(AsccommRejectReason::ChannelNotifyEncoding);
        }
        uint64_t binding[2];
        (void)memcpy_sp(binding, sizeof(binding), source, sizeof(binding));
        if (!channel.localNotifyAddr || binding[0] >= channel.localNotifyNum) {
            return Reject(AsccommRejectReason::ChannelNotifyIndex);
        }
        const auto& notify = channel.localNotifyAddr[binding[0]];
        return (notify.type == HCOMM_REGED_NOTIFY_RMA_RT && notify.notifyInfo.rmaRt.notifyId >= 0 &&
                static_cast<uint64_t>(notify.notifyInfo.rmaRt.notifyId) == binding[1]) ||
               Reject(AsccommRejectReason::ChannelNotifyChanged);
    }
    if (e.kind == Kind::Fence) {
        if (e.length != 2 * sizeof(uint64_t)) {
            return Reject(AsccommRejectReason::FenceEncoding);
        }
        uint64_t values[2];
        (void)memcpy_sp(values, sizeof(values), source, sizeof(values));
        uint64_t predicted = channel.sqContextAddr[0].fence;
        // Capture compilation checked every internal transition. Only the first
        // fence for each channel depends on this invocation's live state.
        const auto index = static_cast<size_t>(&e - entry.events.data());
        if (entry.compiled && index < entry.plan.size()) {
            if (entry.plan[index].previousFence != UINT32_MAX) {
                return true;
            }
        } else {
            return Reject(AsccommRejectReason::FencePlan);
        }
        return (values[0] == predicted && values[1] <= 1) || Reject(AsccommRejectReason::FenceChanged);
    }
    if (e.kind == Kind::UbWqe) {
        if (!is_ub_protocol(channel.protocol) || e.patchCount != 1) {
            return Reject(AsccommRejectReason::UbProtocol);
        }
        if (!liveWorkspace) {
            return Reject(AsccommRejectReason::UbPatchEncoding);
        }
        return RefreshUbPatch(channel, liveWorkspace->patches[&e - entry.events.data()], patched.data() + e.offset);
    }
    if (e.kind == Kind::RdmaWqe) {
        if (channel.protocol != COMM_PROTOCOL_ROCE || e.length != sizeof(Asc::Roce3WqeEntry) || e.patchCount != 1) {
            return Reject(AsccommRejectReason::RdmaWqeEncoding);
        }
        const auto& field = entry.patches[e.patchBegin];
        if (field.field != 1 || (field.source != 0 && field.source != 3) || field.offset > 255) {
            return Reject(AsccommRejectReason::RdmaPatchEncoding);
        }
        Asc::Roce3WqeEntry wqe;
        (void)memcpy_sp(&wqe, sizeof(wqe), source, sizeof(wqe));
        uint32_t lkey = 0, rkey = 0;
        const auto len = Asc::htonl32(wqe.data.rLen);
        if (field.source == 3) {
            if (field.span > UINT32_MAX) {
                return Reject(AsccommRejectReason::RdmaNotifyIndex);
            }
            Asc::RmaBufSliceLite local(0, 0, 0, 0);
            Asc::RmtRmaBufSliceLite remote(0, 0, 0, 0, 0, 0);
            if (build_rdma_notify_slices(channel, static_cast<uint32_t>(field.span), local, remote) !=
                    ASCCOMM_SUCCESS ||
                !is_range_contained(local.get_addr(), local.get_size(), Asc::htonll64(wqe.data.bufAddr), len) ||
                !is_range_contained(remote.get_addr(), remote.get_size(), Asc::htonll64(wqe.task.va), len)) {
                return Reject(AsccommRejectReason::RdmaNotifyRange);
            }
            lkey = local.get_lkey();
            rkey = remote.get_rkey();
        } else if (
            !rdma_protection(channel, Asc::htonll64(wqe.data.bufAddr), len, true, lkey) ||
            !rdma_protection(channel, Asc::htonll64(wqe.task.va), len, false, rkey)) {
            return Reject(AsccommRejectReason::RdmaProtection);
        }
        wqe.data.leKey = Asc::htonl32((lkey & ~ROCE_WQE_NEXT_SGE_INVALID) | ROCE_WQE_NEXT_SGE_INVALID);
        wqe.task.rkey = Asc::htonl32(rkey);
        wqe.task.ulp = Asc::htonl32((lkey & 0xffff) | static_cast<uint32_t>(field.offset));
        (void)memcpy_sp(patched.data() + e.offset, e.length, &wqe, sizeof(wqe));
        return true;
    }
    if (e.kind == Kind::UbEnd || e.kind == Kind::RdmaEnd) {
        const uint32_t expected = e.kind == Kind::UbEnd ? sizeof(uint64_t) : 2 * sizeof(uint64_t);
        if (e.length != expected) {
            return Reject(AsccommRejectReason::TransportEndEncoding);
        }
        uint64_t finish[2]{};
        (void)memcpy_sp(finish, sizeof(finish), source, expected);
        auto* t = reinterpret_cast<ThreadEntity*>(finish[0]);
        return (t && t->sqContextAddr && (e.kind == Kind::UbEnd || (finish[1] >= 1 && finish[1] <= 2))) ||
               Reject(AsccommRejectReason::TransportEndBinding);
    }
    return Reject(AsccommRejectReason::UnsupportedEvent);
}

bool PrepareCompiledTransport(const ReplayInstruction& op)
{
    auto& c = *reinterpret_cast<ChannelEntity*>(op.resource);
    if (!c.sqContextAddr || !c.sqNum) {
        return Reject(AsccommRejectReason::ChannelQueue);
    }
    if (op.kind == Kind::UbBegin) {
        HcommJfsContext* q = nullptr;
        if (get_ub_context(c, q) != ASCCOMM_SUCCESS || q->sqDepth < 2) {
            return Reject(AsccommRejectReason::UbQueue);
        }
        if (!matches_ub_binding(c, op.args.data())) {
            return Reject(AsccommRejectReason::ResourceChanged);
        }
        auto* live = LiveUb(op.plan.resourceIndex, op.resource);
        if (!live || live->queue != q) {
            return Reject(AsccommRejectReason::UbQueue);
        }
        if (!live->prepared) {
            auto& owner = liveWorkspace->ub[live->ownerIndex];
            if (!owner.prepared) {
                import_ub_state(*q, *live->connection);
                owner.prepared = true;
            }
            live->prepared = true;
        }
        return true;
    }
    if (op.kind == Kind::ChannelNotifyGuard) {
        if (!c.localNotifyAddr || op.args[0] >= c.localNotifyNum) {
            return Reject(AsccommRejectReason::ChannelNotifyIndex);
        }
        const auto& n = c.localNotifyAddr[op.args[0]];
        return (n.type == HCOMM_REGED_NOTIFY_RMA_RT && n.notifyInfo.rmaRt.notifyId >= 0 &&
                uint64_t(n.notifyInfo.rmaRt.notifyId) == op.args[1]) ||
               Reject(AsccommRejectReason::ChannelNotifyChanged);
    }
    if (op.kind == Kind::Fence) {
        return (c.sqContextAddr[0].fence == op.args[0] && op.args[1] <= 1) || Reject(AsccommRejectReason::FenceChanged);
    }
    return Reject(AsccommRejectReason::UnsupportedEvent);
}
bool ApplyCompiledTransport(const ReplayInstruction& op, uint32_t index)
{
    auto& c = *reinterpret_cast<ChannelEntity*>(op.resource);
    if (op.kind == Kind::Fence) {
        c.sqContextAddr[0].fence = op.args[1];
        return true;
    }
    if (op.kind != Kind::UbBegin) {
        return false;
    }
    auto* live = LiveUb(op.plan.resourceIndex, op.resource);
    if (!live || !live->prepared) {
        return false;
    }
    const auto& binding = liveWorkspace->bindings[index];
    currentUbFlow = live->flow.get();
    if (live->flow) {
        auto& f = *live->flow;
        if (f.writer != binding.writer) {
            f.writer = binding.writer;
        }
        if (f.thread != binding.thread) {
            f.thread = binding.thread;
        }
        if (f.channel != &c) {
            f.channel = &c;
        }
        if (!f.writer) {
            return false;
        }
    }
    return true;
}
bool PrepareCompiledTokens()
{
    if (!liveWorkspace) {
        return false;
    }
    if (liveEntry->relocatable &&
        (!ValidRanges(liveEntry->replayRanges) || liveEntry->captureRanges[1] != liveEntry->replayRanges[1] ||
         liveEntry->captureRanges[3] != liveEntry->replayRanges[3])) {
        return Reject(AsccommRejectReason::RemoteProtection);
    }
    for (auto& slot : liveWorkspace->staticProtection) {
        if (!ResolveCompiledProtection(*slot.channel, slot.ref, slot.token, slot.value, slot.address)) {
            return false;
        }
    }
    // Refresh each registration once, after resource checks and before WQE patch.
    for (uint32_t i = 0; i < liveWorkspace->registrations.size(); ++i) {
        const auto& group = liveWorkspace->registrations[i];
        TransportWorkspace::ProtectionRef ref{};
        ref.kind = group.local ? TransportWorkspace::ProtectionKind::Local : TransportWorkspace::ProtectionKind::Remote;
        ref.registration = i;
        ref.address = group.begin;
        ref.length = group.end - group.begin;
        uint32_t token, value;
        if (!RegisteredProtection(*group.channel, ref, group.begin, token, value)) {
            return Reject(group.local ? AsccommRejectReason::LocalProtection : AsccommRejectReason::RemoteProtection);
        }
    }
    return true;
}
// All bounds and protection checks run once per resource before publication.
// Compiled references are immutable; this path only reads slots and adds offsets.
static TransportWorkspace::ResolvedProtection ReadCompiledProtection(const TransportWorkspace::BoundProtection& ref)
{
    const auto address = ref.address  ? *ref.address :
                         ref.role < 4 ? liveEntry->replayRanges[ref.role] + ref.offset :
                                        ref.fixedAddress;
    return {*ref.token, *ref.value, address};
}

bool PublishCompiledWqes(Entry& entry, uint32_t first, uint32_t count)
{
    auto* live = LiveUb(entry.plan[first].resourceIndex, entry.replay.instructions[first].resource);
    if (!live || !live->prepared) {
        return false;
    }
    for (uint32_t i = first; i < first + count; ++i) {
        const auto& e = entry.replay.instructions[i];
        // All failure-capable checks ran in preflight. Patch and publish in
        // one traversal, keeping the freshly written template hot.
        const auto& p = liveWorkspace->publishPlans[i];
        auto* data = entry.workspace.data() + e.offset;
        auto& common = *reinterpret_cast<Asc::UdmaSqeCommon*>(data);
        const auto remote = ReadCompiledProtection(p.boundRemote);
        common.rmtObjId = remote.token;
        common.rmtTokenValue = remote.value;
        common.rmtAddr.rmtAddrLow = remote.address;
        common.rmtAddr.rmtAddrHigh = remote.address >> 32;
        if (p.hasLocal) {
            const auto resolved = ReadCompiledProtection(p.boundLocal);
            auto& local = *reinterpret_cast<Asc::UdmaNormalSge*>(data + p.localOffset);
            local.tokenId = resolved.token;
            local.dataAddrLow = resolved.address;
            local.dataAddrHigh = resolved.address >> 32;
        }
        if (p.hasNotify) {
            const auto resolved = ReadCompiledProtection(p.boundNotify);
            auto& notify = reinterpret_cast<Asc::UdmaSqeWriteWithNotify*>(data)->notify;
            notify.notifyTokenId = resolved.token;
            notify.notifyTokenValue = resolved.value;
        }
        if (AscProf::Enabled()) {
            live->connection->set_profiling_channel(e.resource);
        }
        live->connection->publish_template(data, e.length, &p.profiling);
    }
    return true;
}
uint16_t CompiledTransportPi(Entry& entry, uint32_t end)
{
    auto* live = LiveUb(entry.plan[end].resourceIndex, entry.replay.instructions[end].resource);
    return live->connection->current_pi();
}

bool ApplyTransport(const Event& e, const uint8_t* data, uint32_t resourceIndex)
{
    auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
    if (e.kind == Kind::UbBegin) {
        auto* live = LiveUb(resourceIndex, e.resource);
        if (!live || !live->prepared) {
            return false;
        }
        const auto index = static_cast<size_t>(&e - liveEntry->events.data());
        const auto& pinned = liveWorkspace->bindings[index];
        auto& thread = *pinned.thread; // validated by the compiled resource-check table
        currentUbFlow = live->flow.get();
        bool reserved = false;
        if (live->flow) {
            auto& flow = *live->flow;
            flow.writer = pinned.writer;
            flow.thread = &thread;
            flow.channel = &channel;
            if (!flow.writer) {
                return false;
            }
            reserved = live->leaseIndex != UINT32_MAX && liveWorkspace->leases[live->leaseIndex].reserved;
        }
        live->connection->set_space_guard((reserved || !live->flow) ? nullptr : ReserveUb, live->flow.get());
        return true;
    }
    if (e.kind == Kind::RdmaBegin || e.kind == Kind::ChannelNotifyGuard) {
        return true;
    }
    if (e.kind == Kind::Fence) {
        uint64_t values[2];
        (void)memcpy_sp(values, sizeof(values), data, sizeof(values));
        channel.sqContextAddr[0].fence = values[1];
        return true;
    }
    if (e.kind == Kind::UbWqe) {
        auto* live = LiveUb(resourceIndex, e.resource);
        if (!live || !live->prepared) {
            return false;
        }
        alignas(8) uint8_t scratch[128];
        (void)memcpy_sp(scratch, sizeof(scratch), data, e.length);
        if (AscProf::Enabled()) {
            live->connection->set_profiling_channel(e.resource);
        }
        live->connection->publish_template(scratch, e.length);
        // Publish state after each WQE, including before a later failure. Static
        // connection fields/EID are imported only at capture compilation.
        export_ub_state(*live->connection, *live->queue);
        return true;
    }
    if (e.kind == Kind::UbEnd) {
        uint64_t threadId;
        (void)memcpy_sp(&threadId, sizeof(threadId), data, sizeof(threadId));
        auto& q = channel.sqContextAddr[0].contextInfo.ubJfs;
        auto& thread = *reinterpret_cast<ThreadEntity*>(threadId);
        asccomm_register_thread(static_cast<ThreadHandle>(threadId));
        return Succeeded(ring_ub_doorbell(thread, channel, q, static_cast<uint16_t>(q.sqHead)));
    }
    HcommRdmaSqContext* q = nullptr;
    HcommRdmaCqContext* c = nullptr;
    if (!Succeeded(get_rdma_contexts(channel, q, c))) {
        return false;
    }
    Asc::RdmaSqContextLite sq{};
    Asc::RdmaCqContextLite cq{};
    build_rdma_context(*q, *c, sq, cq);
    Asc::Rdma1825Ops ops(&sq, &cq);
    ops.import_queue_state(q->sqHead, q->sqTail, c->cqHead, c->cqTail, c->cqDbFlush != 0);
    auto save = [&]() {
        bool flush = false;
        ops.export_queue_state(q->sqHead, q->sqTail, c->cqHead, c->cqTail, flush);
        c->cqDbFlush = flush ? 1 : 0;
    };
    if (e.kind == Kind::RdmaWqe) {
        Asc::Roce3WqeEntry wqe;
        (void)memcpy_sp(&wqe, sizeof(wqe), data, sizeof(wqe));
        const auto ret = ops.publish_template(wqe);
        save();
        return Succeeded(ret);
    }
    if (e.kind == Kind::RdmaEnd) {
        uint64_t finish[2];
        (void)memcpy_sp(finish, sizeof(finish), data, sizeof(finish));
        auto& thread = *reinterpret_cast<ThreadEntity*>(finish[0]);
        uint64_t addr = 0, value = 0;
        if (!Succeeded(ops.build_doorbell(addr, value))) {
            return false;
        }
        asccomm_register_thread(static_cast<ThreadHandle>(finish[0]));
        if (!Succeeded(asccomm_rtsq_rdma_db_send(thread, addr, value))) {
            return false;
        }
        std::vector<int32_t> errors;
        const auto ret = ops.poll_cq(static_cast<int32_t>(finish[1]), 5, errors);
        const auto dbRet = ops.build_cq_doorbell(addr, value);
        save();
        return Succeeded(ret) && Succeeded(dbRet);
    }
    return false;
}
} // namespace AsccommBinaryCache

// Called only after all resource-holder and cache-scope owners have retired.
void asccomm_release_retired_ub_ledgers(uint64_t domain);
extern "C" __attribute__((visibility("default"))) uint64_t
asccomm_ub_domain_exchange_v2(uint64_t domain, const void* lifetime)
{
    const auto previous = ubDomainId;
    if (domain) {
        auto& domains = ubDomains;
        if (domains.find(domain) != domains.end()) {
            ubDomainId = domain;
            currentUbFlow = nullptr;
            return previous;
        }
        if (!lifetime) {
            Asc::throw_exception<Asc::InternalException>("missing UB domain lifetime");
        }
        // No scanning, weak lock, or reference-count change on repeated hits.
        for (auto it = domains.begin(); it != domains.end();) {
            if (it->second.managed && it->second.lifetime.expired()) {
                asccomm_release_retired_ub_ledgers(it->first);
                it = domains.erase(it);
            } else {
                ++it;
            }
        }
        if (domains.size() >= 256) {
            Asc::throw_exception<Asc::InternalException>("UB domain budget exceeded");
        }
        UbDomain entry;
        entry.lifetime = *static_cast<const std::weak_ptr<void>*>(lifetime);
        entry.managed = true;
        domains.emplace(domain, std::move(entry));
    }
    ubDomainId = domain;
    currentUbFlow = nullptr;
    return previous;
}

// Private capability, paired with devkit's pinned Mc2BinaryCacheDomain. No public ABI.
extern "C" __attribute__((visibility("default"))) uint64_t asccomm_ub_domain_exchange_v1(uint64_t domain)
{
    const auto previous = ubDomainId;
    if (domain && ubDomains.find(domain) == ubDomains.end()) {
        if (ubDomains.size() >= 256) {
            Asc::throw_exception<Asc::InternalException>("UB domain budget exceeded");
        }
        ubDomains.emplace(domain, UbDomain{});
    }
    ubDomainId = domain;
    currentUbFlow = nullptr;
    return previous;
}

AsccommUbCompletionTag asccomm_ub_note_db(const ChannelEntity& channel, uint16_t pi)
{
    if (ubDomainId && currentUbFlow && currentUbFlow->queue == &channel.sqContextAddr[0].contextInfo.ubJfs) {
        currentUbFlow->doorbellPi = pi;
        currentUbFlow->doorbellSeen = true;
        auto& c = currentUbFlow->writer->completion;
        if (c.generation == UINT64_MAX) {
            FlowError(*currentUbFlow, "UB completion generation exhausted");
        }
        ++c.generation;
        c.done = c.failed = c.submitted = false;
        return {&c, c.generation};
    }
    return {};
}

uint64_t asccomm_ub_current_domain() { return ubDomainId; }
