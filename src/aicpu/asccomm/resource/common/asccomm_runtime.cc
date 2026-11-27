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

#include "../../primitives/task_cache/replay_schedule.h"

#include <chrono>
#include <map>
#include <array>
#include <memory>
#include <cstring>
#include <cstddef>
#include <cstdlib>
#include "asccomm_runtime.h"
#include "asccomm_thread_notify.h"
#include "asccomm_shared_tail.h"
#include "asccomm_transport.h"
#include "asccomm_launch.h"
#include "asccomm_diag_internal.h"
#include "../stream/sqe_build_a5.h"
#include "../stream/sqe.h"
#include "securec.h"
#include "ascend_hal.h"
#include "asccomm_log.h"
#include "page_size_util.h"

// 局部打开宏以包含 internal 声明，与下方导出定义做编译期交叉校验；符号契约不变。
#ifndef ASCCOMM_ENABLE_INTERNAL_APIS
#define ASCCOMM_ENABLE_INTERNAL_APIS
#endif
#include "asccomm_primitives_internal.h"

namespace aicpu {
void __attribute__((weak)) __attribute__((visibility("default"))) GetSqeId(
    uint32_t batchNum, uint32_t& taskId, uint32_t& taskIdEnd);
}

namespace {
using HcommRtsqContext = decltype(((HcommThreadSqContext*)nullptr)->contextInfo.rtsqContext);

// [Head] 解码行位序锚定（小端），编译断言防布局漂移。
static_assert(offsetof(Asc::Rt91095StarsSqeHeader, rtStreamId) == 4U, "word1 low16 must be rtStreamId");
static_assert(offsetof(Asc::Rt91095StarsSqeHeader, taskId) == 6U, "word1 high16 must be taskId");

bool is_mapped_page(uint64_t address)
{
    const uintptr_t page = asccomm::GetPageBase(static_cast<uintptr_t>(address));
    unsigned char resident = 0;
    return syscall(SYS_mincore, reinterpret_cast<void*>(page), 1U, &resident) == 0;
}

asccomm_result validate_device_range(uint64_t addr, uint64_t len, const char* caller)
{
    CHK_PRT_RET(
        len > std::numeric_limits<uintptr_t>::max() - addr,
        ASCCOMM_ERROR(
            "[%s] device address range overflows address space, addr[0x%llx], len[%llu].", caller,
            static_cast<unsigned long long>(addr), static_cast<unsigned long long>(len)),
        ASCCOMM_E_PARA);
    const uint64_t end = len == 0 ? addr : addr + len - 1U;
    CHK_PRT_RET(
        !is_mapped_page(addr) || !is_mapped_page(end),
        ASCCOMM_ERROR(
            "[%s] device address is not mapped, addr[0x%llx], len[%llu].", caller,
            static_cast<unsigned long long>(addr), static_cast<unsigned long long>(len)),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

asccomm_result acquire_task_id(HcommThreadSqContext& contextOuter, uint32_t& taskId)
{
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    CHK_PRT_RET(
        contextOuter.type != HCOMM_THREAD_SQ_CONTEXT_TYPE_RTSQ || rtsq.localSqAddr == 0,
        ASCCOMM_ERROR("[%s] invalid local SQE buffer.", __func__), ASCCOMM_E_PTR);
    CHK_PRT_RET(
        rtsq.pendingSqeCnt >= HCOMM_RESOURCE_RTSQ_BATCH_SIZE,
        ASCCOMM_ERROR("[%s] pending SQE count[%u] exceeds buffer capacity.", __func__, rtsq.pendingSqeCnt),
        ASCCOMM_E_INTERNAL);
    if (rtsq.taskId >= rtsq.taskIdEnd) {
        CHK_PRT_RET(
            aicpu::GetSqeId == nullptr, ASCCOMM_ERROR("[%s] GetSqeId is unavailable.", __func__), ASCCOMM_E_INTERNAL);
        aicpu::GetSqeId(HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE, rtsq.taskId, rtsq.taskIdEnd);
        CHK_PRT_RET(
            rtsq.taskId >= rtsq.taskIdEnd || rtsq.taskIdEnd - rtsq.taskId > HCOMM_RESOURCE_RTSQ_TASK_ID_BATCH_SIZE,
            ASCCOMM_ERROR("[%s] invalid Task ID range[%u, %u).", __func__, rtsq.taskId, rtsq.taskIdEnd),
            ASCCOMM_E_INTERNAL);
    }
    taskId = rtsq.taskId;
    ++rtsq.taskId;
    return ASCCOMM_SUCCESS;
}

// A bounded, allocation-free cache for repeated submissions on this worker.
// Bind to the domain, physical device and SQ identity rather than a thread address alone.
// Failed resolutions are never published, and a changed binding must resolve again.
asccomm_result resolve_submission_device(const ThreadEntity& thread, uint32_t& device)
{
    struct Binding {
        const HcommThreadSqContext* context = nullptr;
        uint64_t domain = 0, base = 0;
        uint32_t hostDevice = 0, sqId = 0, localDevice = 0;
        bool valid = false;
    };
    static thread_local Binding cached;
    const auto domain = asccomm_ub_current_domain();
    const auto& q = thread.sqContextAddr->contextInfo.rtsqContext;
    if (cached.valid && cached.context == thread.sqContextAddr && cached.domain == domain &&
        cached.hostDevice == thread.devInfo.devPhyId && cached.base == q.sqBaseAddr && cached.sqId == q.sqId) {
        device = cached.localDevice;
        return ASCCOMM_SUCCESS;
    }
    uint32_t resolved = 0;
    if (drvGetLocalDevIDByHostDevID(thread.devInfo.devPhyId, &resolved) != 0) {
        return ASCCOMM_E_INTERNAL;
    }
    cached.context = thread.sqContextAddr;
    cached.domain = domain;
    cached.base = q.sqBaseAddr;
    cached.hostDevice = thread.devInfo.devPhyId;
    cached.sqId = q.sqId;
    cached.localDevice = resolved;
    cached.valid = true;
    device = resolved;
    return ASCCOMM_SUCCESS;
}

struct UbLedger {
    uint64_t base = 0;
    uint32_t sqId = 0, depth = 0, head = 0, device = 0;
    std::vector<AsccommUbCompletionTag> slots;
    std::array<AsccommUbCompletionTag, HCOMM_RESOURCE_RTSQ_BATCH_SIZE> pending{};
};
thread_local std::map<std::pair<const HcommThreadSqContext*, uint64_t>, std::unique_ptr<UbLedger>> ubLedgers;
thread_local uint64_t ubLedgerBytes = 0;
void observe_ub_head(uint32_t device, const HcommRtsqContext& q, uint32_t head)
{
    for (auto& item : ubLedgers) {
        if (item.first.second != asccomm_ub_current_domain()) {
            continue;
        }
        auto& l = *item.second;
        if (l.device != device || l.sqId != q.sqId || l.base != q.sqBaseAddr || head >= l.depth) {
            continue;
        }
        while (l.head != head) {
            l.slots[l.head].Complete();
            l.slots[l.head] = {};
            l.head = (l.head + 1) % l.depth;
        }
    }
}

asccomm_result query_sq_value(
    uint32_t localDevId, const HcommRtsqContext& context, drvSqCqPropType_t property, uint32_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = property;
    drvError_t ret = halSqCqQuery(localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], property[%u], ret[%d].", __func__, localDevId,
            context.sqId, property, ret),
        ASCCOMM_E_INTERNAL);
    value = queryInfo.value[0];
    if (property == DRV_SQCQ_PROP_SQ_HEAD) {
        observe_ub_head(localDevId, context, value);
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result get_ub_ledger(ThreadEntity& thread, UbLedger*& out)
{
    auto found = ubLedgers.find({thread.sqContextAddr, asccomm_ub_current_domain()});
    auto& q = thread.sqContextAddr->contextInfo.rtsqContext;
    if (found != ubLedgers.end()) {
        out = found->second.get();
        if (out->base != q.sqBaseAddr || out->depth != q.sqDepth || out->sqId != q.sqId) {
            return ASCCOMM_E_INTERNAL;
        }
        return ASCCOMM_SUCCESS;
    }
    if (ubLedgers.size() >= 4096 || !q.sqDepth || q.sqDepth > 65536) {
        return ASCCOMM_E_INTERNAL;
    }
    const uint64_t bytes = sizeof(UbLedger) + uint64_t(q.sqDepth) * sizeof(AsccommUbCompletionTag);
    constexpr uint64_t limit = 64ULL * 1024 * 1024;
    if (ubLedgerBytes > limit || bytes > limit - ubLedgerBytes) {
        return ASCCOMM_E_MEMORY;
    }
    auto l = std::make_unique<UbLedger>();
    l->base = q.sqBaseAddr;
    l->sqId = q.sqId;
    l->depth = q.sqDepth;
    if (drvGetLocalDevIDByHostDevID(thread.devInfo.devPhyId, &l->device) != 0) {
        return ASCCOMM_E_INTERNAL;
    }
    CHK_RET(query_sq_value(l->device, q, DRV_SQCQ_PROP_SQ_HEAD, l->head));
    if (l->head >= l->depth) {
        return ASCCOMM_E_INTERNAL;
    }
    l->slots.resize(l->depth);
    out = l.get();
    ubLedgers.emplace(std::make_pair(thread.sqContextAddr, asccomm_ub_current_domain()), std::move(l));
    ubLedgerBytes += bytes;
    return ASCCOMM_SUCCESS;
}
UbLedger* find_ub_ledger(ThreadEntity& thread)
{
    auto it = ubLedgers.find({thread.sqContextAddr, asccomm_ub_current_domain()});
    return it == ubLedgers.end() ? nullptr : it->second.get();
}
asccomm_result check_ub_slots(ThreadEntity& thread, uint32_t device, HcommRtsqContext& q, UbLedger* bound = nullptr)
{
    auto* ledger = bound ? bound : find_ub_ledger(thread);
    if (!ledger) {
        return ASCCOMM_SUCCESS;
    }
    for (uint32_t i = 0; i < q.pendingSqeCnt; ++i) {
        const auto& tag = ledger->slots[(q.sqTail + i) % q.sqDepth];
        if (!tag.Current() || tag.completion->done) {
            continue;
        }
        CHK_RET(query_sq_value(device, q, DRV_SQCQ_PROP_SQ_HEAD, q.sqHead));
        for (uint32_t j = i; j < q.pendingSqeCnt; ++j) {
            const auto& remaining = ledger->slots[(q.sqTail + j) % q.sqDepth];
            if (remaining.Current() && !remaining.completion->done) {
                return ASCCOMM_E_INTERNAL;
            }
        }
        break;
    }
    return ASCCOMM_SUCCESS;
}
template <class Queue>
void record_ub_slots(ThreadEntity& thread, const Queue& q, AsccommUbCompletionTag* tags, bool failed)
{
    auto* ledger = find_ub_ledger(thread);
    if (!ledger) {
        return;
    }
    for (uint32_t i = 0; i < q.pendingSqeCnt; ++i) {
        auto tag = tags[i];
        tags[i] = {};
        if (failed) {
            tag.Fail();
            continue;
        }
        if (tag.Current()) {
            tag.completion->submitted = true;
        }
        ledger->slots[(q.sqTail + i) % q.sqDepth] = tag;
    }
}

asccomm_result query_sq_base(uint32_t localDevId, const HcommRtsqContext& context, uint64_t& value)
{
    halSqCqQueryInfo queryInfo{};
    queryInfo.tsId = 0;
    queryInfo.sqId = context.sqId;
    queryInfo.cqId = 0;
    queryInfo.type = DRV_NORMAL_TYPE;
    queryInfo.prop = DRV_SQCQ_PROP_SQ_BASE;
    drvError_t ret = halSqCqQuery(localDevId, &queryInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqQuery failed, localDevId[%u], sqId[%u], ret[%d].", __func__, localDevId, context.sqId, ret),
        ASCCOMM_E_INTERNAL);
    value = (static_cast<uint64_t>(queryInfo.value[1]) << 32U) | queryInfo.value[0];
    return ASCCOMM_SUCCESS;
}

asccomm_result config_sq_tail(uint32_t localDevId, const HcommRtsqContext& context, uint32_t value)
{
    halSqCqConfigInfo configInfo{};
    configInfo.tsId = 0;
    configInfo.sqId = context.sqId;
    configInfo.cqId = 0;
    configInfo.type = DRV_NORMAL_TYPE;
    configInfo.prop = DRV_SQCQ_PROP_SQ_TAIL;
    configInfo.value[0] = value;
    drvError_t ret = halSqCqConfig(localDevId, &configInfo);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] halSqCqConfig failed, localDevId[%u], sqId[%u], tail[%u], ret[%d].", __func__, localDevId,
            context.sqId, value, ret),
        ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

uint32_t get_tail_to_head_distance(const HcommRtsqContext& context)
{
    if (context.sqHead == context.sqTail) {
        return context.sqDepth;
    }
    return context.sqTail < context.sqHead ? context.sqHead - context.sqTail :
                                             context.sqDepth - (context.sqTail - context.sqHead);
}

asccomm_result ensure_queue_space(uint32_t localDevId, ThreadEntity& thread, HcommRtsqContext& context)
{
    uint32_t availableSpace = get_tail_to_head_distance(context);
    if (availableSpace > context.pendingSqeCnt) {
        return ASCCOMM_SUCCESS;
    }
    auto startTime = std::chrono::steady_clock::now();
    const uint32_t sqFullTimeout = asccomm_get_sq_full_timeout();
    while (availableSpace <= context.pendingSqeCnt) {
        CHK_RET(query_sq_value(localDevId, context, DRV_SQCQ_PROP_SQ_HEAD, context.sqHead));
        availableSpace = get_tail_to_head_distance(context);
        if (availableSpace > context.pendingSqeCnt) {
            break;
        }
        const bool timeout =
            sqFullTimeout != 0 && std::chrono::steady_clock::now() - startTime >= std::chrono::seconds(sqFullTimeout);
        CHK_PRT_RET(
            timeout,
            ASCCOMM_ERROR(
                "[%s] RTSQ full timeout, sqId[%u], head[%u], tail[%u], pending[%u].", __func__, context.sqId,
                context.sqHead, context.sqTail, context.pendingSqeCnt),
            ASCCOMM_E_TIMEOUT);
        CHK_RET(asccomm_try_launch_registered_threads());
    }
    return ASCCOMM_SUCCESS;
}

// Native and binary submission use the functional shared entity tail ledger.
asccomm_result prepare_shared_rtsq(
    uint32_t device, ThreadEntity& thread, HcommRtsqContext& work, HcommRtsqContext& live, bool nonblocking,
    bool& ready)
{
    const int ret = AsccommSharedTail::Prepare(
        work, live, nonblocking, ready, ASCCOMM_E_INTERNAL,
        [&](HcommRtsqContext& q) -> int { return ensure_queue_space(device, thread, q); },
        [](const HcommRtsqContext& q) { return get_tail_to_head_distance(q); });
    return static_cast<asccomm_result>(ret);
}

asccomm_result copy_pending_sqes(HcommRtsqContext& context)
{
    CHK_PRT_RET(
        context.localSqAddr == 0, ASCCOMM_ERROR("[%s] local SQE buffer address is 0.", __func__), ASCCOMM_E_PTR);
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(context.localSqAddr));
    AscProf::SqeProfileScope profilingBits(localSqeBuffer, context.pendingSqeCnt);
    const uint32_t byteCount = context.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
    uint8_t* sqCurrent = reinterpret_cast<uint8_t*>(context.sqBaseAddr) + context.sqTail * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
    if (context.sqTail >= context.sqHead) {
        const uint32_t depthLeft = context.sqDepth - context.sqTail;
        const uint32_t firstCount = context.pendingSqeCnt < depthLeft ? context.pendingSqeCnt : depthLeft;
        const uint32_t firstBytes = firstCount * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
        CHK_PRT_RET(
            memcpy_sp(sqCurrent, firstBytes, localSqeBuffer, firstBytes) != EOK,
            ASCCOMM_ERROR("[%s] first RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
        if (firstCount != context.pendingSqeCnt) {
            const uint32_t remainingBytes = byteCount - firstBytes;
            CHK_PRT_RET(
                memcpy_sp(
                    reinterpret_cast<void*>(context.sqBaseAddr), remainingBytes, localSqeBuffer + firstBytes,
                    remainingBytes) != EOK,
                ASCCOMM_ERROR("[%s] wrapped RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
        }
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        memcpy_sp(sqCurrent, byteCount, localSqeBuffer, byteCount) != EOK,
        ASCCOMM_ERROR("[%s] RTSQ memcpy failed.", __func__), ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

using AsccommBinaryCache::BinaryRtsqTask;

thread_local AsccommBinaryCache::Entry* directEntry = nullptr;
AsccommBinaryCache::SqePlan* direct_queue(ThreadEntity& thread)
{
    if (!directEntry) {
        return nullptr;
    }
    for (auto& q : directEntry->queues) {
        if (q.thread == reinterpret_cast<uint64_t>(&thread)) {
            return &q;
        }
    }
    return nullptr;
}
asccomm_result launch_direct(ThreadEntity& thread, bool nonblocking, AsccommBinaryCache::SqePlan* bound = nullptr)
{
    auto* plan = bound ? bound : direct_queue(thread);
    if (!plan || plan->submitting || plan->cursor == plan->submitted) {
        return ASCCOMM_SUCCESS;
    }
    auto& live = thread.sqContextAddr->contextInfo.rtsqContext;
    if (live.pendingSqeCnt != 0) {
        return ASCCOMM_E_INTERNAL;
    }
    if (!plan->deviceResolved) {
        if (drvGetLocalDevIDByHostDevID(thread.devInfo.devPhyId, &plan->localDevice) != 0) {
            return ASCCOMM_E_INTERNAL;
        }
        plan->deviceResolved = true;
    }
    const uint32_t device = plan->localDevice;
    struct Guard {
        bool& value;
        explicit Guard(bool& v) : value(v) { value = true; }
        ~Guard() { value = false; }
    } guard(plan->submitting);
    while (plan->submitted < plan->cursor) {
        // Return without publishing this segment; the replay caller handles failure, not fallback.
        auto copy = live;
        // Entry-owned templates are not limited by the native 128-SQE local buffer.
        // Keep one empty ring slot and retain all explicit launch boundaries.
        if (copy.sqDepth < 2) {
            return ASCCOMM_E_INTERNAL;
        }
        copy.pendingSqeCnt = std::min<uint32_t>(copy.sqDepth - 1, plan->cursor - plan->submitted);
        copy.localSqAddr = reinterpret_cast<uint64_t>(plan->bytes.data() + plan->submitted * 64);
        bool ready = false;
        CHK_RET(prepare_shared_rtsq(device, thread, copy, live, nonblocking, ready));
        if (!ready) {
            return ASCCOMM_SUCCESS;
        }
        if (copy.sqTail >= copy.sqDepth || copy.sqHead >= copy.sqDepth) {
            return ASCCOMM_E_INTERNAL;
        }
        CHK_RET(check_ub_slots(thread, device, copy, static_cast<UbLedger*>(plan->completionLedger)));
        CHK_RET(copy_pending_sqes(copy));
        const uint32_t next = (copy.sqTail + copy.pendingSqeCnt) % copy.sqDepth;
        const auto tailRet =
            AsccommSharedTail::Publish(live, next, [&]() { return config_sq_tail(device, copy, next); });
        if (auto* ledger = static_cast<UbLedger*>(plan->completionLedger)) {
            while (plan->completionSubmitted < plan->completionOffsets.size() &&
                   plan->completionOffsets[plan->completionSubmitted] < plan->submitted + copy.pendingSqeCnt) {
                const auto i = plan->completionSubmitted++;
                auto tag = plan->completionTags[i];
                plan->completionTags[i] = {};
                if (tailRet != ASCCOMM_SUCCESS) {
                    tag.Fail();
                    continue;
                }
                if (tag.Current()) {
                    tag.completion->submitted = true;
                }
                ledger->slots[(copy.sqTail + plan->completionOffsets[i] - plan->submitted) % copy.sqDepth] = tag;
            }
        }
        CHK_RET(tailRet);
        live.sqHead = copy.sqHead;
        if (AscProf::Enabled()) {
            AscProf::Submitted(copy.sqId, reinterpret_cast<void*>(copy.localSqAddr), copy.pendingSqeCnt);
        }
        plan->submitted += copy.pendingSqeCnt;
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result refresh_rtsq(ThreadEntity& thread, bool* submitted = nullptr)
{
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    if (AsccommBinaryCache::Capture() && !AsccommBinaryCache::SuppressRtsq()) {
        BinaryRtsqTask task{};
        task.context = reinterpret_cast<uint64_t>(&contextOuter);
        task.local = rtsq.localSqAddr;
        task.base = rtsq.sqBaseAddr;
        task.depth = rtsq.sqDepth;
        task.sqId = rtsq.sqId;
        task.streamId = rtsq.streamId;
        (void)memcpy_sp(
            task.sqe, sizeof(task.sqe),
            reinterpret_cast<const void*>(rtsq.localSqAddr + rtsq.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE),
            sizeof(task.sqe));
        AsccommBinaryCache::CaptureTask(
            AsccommBinaryCache::Kind::Rtsq, reinterpret_cast<uint64_t>(&thread), &task, sizeof(task));
    }
    ++rtsq.pendingSqeCnt;
    const bool flush = asccomm_is_automatic_launch_mode() || rtsq.pendingSqeCnt == HCOMM_RESOURCE_RTSQ_BATCH_SIZE;
    if (submitted) {
        *submitted = flush;
    }
    if (flush) {
        return asccomm_rtsq_launch(thread);
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result launch_thread_sqes(ThreadEntity& thread, const char* caller, const uint32_t* resolvedDevice = nullptr)
{
    HcommRtsqContext& rtsq = thread.sqContextAddr->contextInfo.rtsqContext;
    uint32_t localDevId = resolvedDevice ? *resolvedDevice : 0;
    if (!resolvedDevice) {
        CHK_PRT_RET(
            resolve_submission_device(thread, localDevId) != ASCCOMM_SUCCESS,
            ASCCOMM_ERROR("[%s] resolve local device failed.", caller), ASCCOMM_E_INTERNAL);
    }
    bool ready = false;
    CHK_RET(prepare_shared_rtsq(localDevId, thread, rtsq, rtsq, false, ready));
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_RET(check_ub_slots(thread, localDevId, rtsq));
    CHK_RET(copy_pending_sqes(rtsq));
    const uint32_t profSubmittedCount = rtsq.pendingSqeCnt;
    const uint32_t newTail = (rtsq.sqTail + rtsq.pendingSqeCnt) % rtsq.sqDepth;
    // Preserve the submission origin/count for completion attribution after publishing live tail.
    const AsccommSharedTail::Submission submitted{rtsq.sqTail, rtsq.sqDepth, rtsq.pendingSqeCnt};
    const auto tailRet =
        AsccommSharedTail::Publish(rtsq, newTail, [&]() { return config_sq_tail(localDevId, rtsq, newTail); });
    if (auto* ledger = find_ub_ledger(thread)) {
        record_ub_slots(thread, submitted, ledger->pending.data(), tailRet != ASCCOMM_SUCCESS);
    }
    CHK_RET(tailRet);
    rtsq.pendingSqeCnt = 0;
    // Capture publication order, including full-buffer and recursive flushes.
    AsccommBinaryCache::CaptureTask(
        AsccommBinaryCache::Kind::Submit, reinterpret_cast<uint64_t>(&thread), &profSubmittedCount,
        sizeof(profSubmittedCount));
    auto* localSqeBuffer = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(rtsq.localSqAddr));
    if (AscProf::Enabled()) {
        AscProf::Submitted(rtsq.sqId, localSqeBuffer, profSubmittedCount);
    }
    constexpr size_t LOCAL_SQE_BUFFER_SIZE = HCOMM_RESOURCE_RTSQ_SQE_SIZE * HCOMM_RESOURCE_RTSQ_BATCH_SIZE;
    {
        CHK_PRT_RET(
            memset_s(localSqeBuffer, LOCAL_SQE_BUFFER_SIZE, 0, LOCAL_SQE_BUFFER_SIZE) != EOK,
            ASCCOMM_ERROR("[%s] clear SQE buffer failed.", caller), ASCCOMM_E_INTERNAL);
    }
    return ASCCOMM_SUCCESS;
}

uint8_t* get_current_sqe(HcommThreadSqContext& contextOuter)
{
    return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(contextOuter.contextInfo.rtsqContext.localSqAddr)) +
           contextOuter.contextInfo.rtsqContext.pendingSqeCnt * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
}

asccomm_result validate_thread_entity(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PRT_RET(
        thread->abiHeader.version != HCOMM_THREAD_ENTITY_VERSION ||
            thread->abiHeader.magicWord != HCOMM_THREAD_ENTITY_MAGIC_WORD ||
            thread->abiHeader.size < sizeof(ThreadEntity),
        ASCCOMM_ERROR("[%s] invalid resource thread entity.", __func__), ASCCOMM_E_PARA);
    CHK_PTR_NULL(thread->sqContextAddr);
    const HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0 || rtsq.sqHead >= rtsq.sqDepth || rtsq.sqTail >= rtsq.sqDepth,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ parameters, base[0x%llx], depth[%u], head[%u], tail[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth, rtsq.sqHead, rtsq.sqTail),
        ASCCOMM_E_PARA);
    return ASCCOMM_SUCCESS;
}

} // namespace

asccomm_result asccomm_validate_device_range(uint64_t addr, uint64_t len)
{
    return validate_device_range(addr, len, __func__);
}

void asccomm_release_retired_ub_ledgers(uint64_t domain)
{
    for (auto it = ubLedgers.begin(); it != ubLedgers.end();) {
        if (it->first.second == domain) {
            ubLedgerBytes -= sizeof(UbLedger) + uint64_t(it->second->depth) * sizeof(AsccommUbCompletionTag);
            it = ubLedgers.erase(it);
        } else {
            ++it;
        }
    }
}

asccomm_result asccomm_resolve_thread_rtsq(ThreadEntity* thread)
{
    CHK_PTR_NULL(thread);
    CHK_PTR_NULL(thread->sqContextAddr);
    HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;
    if (rtsq.sqBaseAddr != 0) {
        return ASCCOMM_SUCCESS;
    }

    uint32_t localDevId = 0;
    drvError_t ret = drvGetLocalDevIDByHostDevID(thread->devInfo.devPhyId, &localDevId);
    CHK_PRT_RET(
        ret != 0,
        ASCCOMM_ERROR(
            "[%s] drvGetLocalDevIDByHostDevID failed, devicePhyId[%u], ret[%d].", __func__, thread->devInfo.devPhyId,
            ret),
        ASCCOMM_E_INTERNAL);
    CHK_RET(query_sq_base(localDevId, rtsq, rtsq.sqBaseAddr));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_DEPTH, rtsq.sqDepth));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, rtsq.sqHead));
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_TAIL, rtsq.sqTail));
    CHK_PRT_RET(
        rtsq.sqBaseAddr == 0 || rtsq.sqDepth == 0,
        ASCCOMM_ERROR(
            "[%s] invalid RTSQ resource, base[0x%llx], depth[%u].", __func__,
            static_cast<unsigned long long>(rtsq.sqBaseAddr), rtsq.sqDepth),
        ASCCOMM_E_INTERNAL);
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_rtsq_launch(ThreadEntity& thread)
{
    if (direct_queue(thread)) {
        return launch_direct(thread, false);
    }
    CHK_PTR_NULL(thread.sqContextAddr);
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    CHK_PRT_RET(
        rtsq.sqDepth == 0 || rtsq.sqBaseAddr == 0,
        ASCCOMM_ERROR("[%s] invalid RTSQ context, depth[%u], base[0x%llx].", __func__, rtsq.sqDepth, rtsq.sqBaseAddr),
        ASCCOMM_E_PARA);
    return launch_thread_sqes(thread, __func__);
}

asccomm_result asccomm_rtsq_try_launch(ThreadEntity& thread)
{
    if (direct_queue(thread)) {
        return launch_direct(thread, true);
    }
    CHK_PTR_NULL(thread.sqContextAddr);
    HcommThreadSqContext& contextOuter = *thread.sqContextAddr;
    HcommRtsqContext& rtsq = contextOuter.contextInfo.rtsqContext;
    if (rtsq.pendingSqeCnt == 0) {
        return ASCCOMM_SUCCESS;
    }
    uint32_t localDevId = 0;
    {
        CHK_PRT_RET(
            resolve_submission_device(thread, localDevId) != ASCCOMM_SUCCESS,
            ASCCOMM_ERROR("[%s] resolve local device failed.", __func__), ASCCOMM_E_INTERNAL);
    }
    CHK_RET(query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, rtsq.sqHead));
    if (get_tail_to_head_distance(rtsq) <= rtsq.pendingSqeCnt) {
        return ASCCOMM_SUCCESS;
    }
    return launch_thread_sqes(thread, __func__, &localDevId);
}

asccomm_result asccomm_rtsq_wait_ub(ThreadEntity& thread, AsccommUbCompletion& completion)
{
    if (!completion.generation || completion.done) {
        return ASCCOMM_SUCCESS;
    }
    if (completion.failed || !completion.submitted) {
        return ASCCOMM_E_INTERNAL;
    }
    auto& q = thread.sqContextAddr->contextInfo.rtsqContext;
    UbLedger* ledger = nullptr;
    CHK_RET(get_ub_ledger(thread, ledger));
    const auto start = std::chrono::steady_clock::now();
    const auto timeout = asccomm_get_sq_full_timeout();
    while (!completion.done) {
        CHK_RET(query_sq_value(ledger->device, q, DRV_SQCQ_PROP_SQ_HEAD, q.sqHead));
        if (completion.done) {
            return ASCCOMM_SUCCESS;
        }
        if (completion.failed) {
            return ASCCOMM_E_INTERNAL;
        }
        if (timeout && std::chrono::steady_clock::now() - start >= std::chrono::seconds(timeout)) {
            ASCCOMM_ERROR("UB space wait timed out: sqId[%u] head[%u]", q.sqId, q.sqHead);
            return ASCCOMM_E_TIMEOUT;
        }
        CHK_RET(asccomm_try_launch_registered_threads());
    }
    return ASCCOMM_SUCCESS;
}
asccomm_result asccomm_rtsq_ub_checkpoint(
    ThreadEntity& thread, const Asc::UbJettyLiteId& jettyId, uint16_t pi, AsccommUbCompletionTag tag)
{
    // Complete already staged direct SQEs before using the native local buffer.
    CHK_RET(asccomm_rtsq_launch(thread));
    struct Restore {
        AsccommBinaryCache::Entry* saved = directEntry;
        Restore() { directEntry = nullptr; }
        ~Restore() { directEntry = saved; }
    } restore;
    AsccommBinaryCache::RtsqSuppression suppression;
    CHK_RET(asccomm_rtsq_ub_db_send(thread, jettyId, pi, tag));
    return asccomm_rtsq_launch(thread);
}

asccomm_result asccomm_rtsq_notify_wait(ThreadEntity& thread, uint32_t notifyId, uint32_t timeout)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_wait(
        context.contextInfo.rtsqContext.streamId, taskId, notifyId, timeout, get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_notify_record(ThreadEntity& thread, uint32_t notifyId)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_notify_record(
        context.contextInfo.rtsqContext.streamId, taskId, notifyId, get_current_sqe(context));
    return refresh_rtsq(thread);
}

// 地址语义编排原语（COND 型），devkit mc2_server 编排面消费。
// 两条地址统一按 49-bit 收口：load_imm 只编码 bit[48:0]，超限静默截断故入口 fail-loud。
constexpr uint64_t RTSQ_ADDR_NOTIFY_ADDR_LIMIT = 1ULL << 49U; // construct_load_imm 可编码的 bit[48:0] 上界

asccomm_result asccomm_rtsq_addr_notify_wait(ThreadEntity& thread, uint64_t waitAddr, uint64_t valueAddr, uint8_t last)
{
    CHK_PRT_RET(
        waitAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT || valueAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT,
        ASCCOMM_ERROR(
            "[%s] addr out of 49-bit range, waitAddr[0x%llx], valueAddr[0x%llx].", __func__,
            static_cast<unsigned long long>(waitAddr), static_cast<unsigned long long>(valueAddr)),
        ASCCOMM_E_PARA);
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    if (AscProf::Enabled()) {
        AscProf::Condition(context.contextInfo.rtsqContext.sqId, taskId, true);
    }
    Asc::build_a5_sqe_ccore_notify_wait(
        context.contextInfo.rtsqContext.streamId, taskId, waitAddr, valueAddr, last, get_current_sqe(context));
    bool submitted = false;
    CHK_RET(refresh_rtsq(thread, &submitted));
    AsccommBinaryCache::CaptureTask(AsccommBinaryCache::Kind::Launch, reinterpret_cast<uint64_t>(&thread), nullptr, 0);
    return submitted ? ASCCOMM_SUCCESS : asccomm_rtsq_launch(thread);
}

asccomm_result asccomm_rtsq_addr_notify_record(ThreadEntity& thread, uint64_t recordAddr, uint64_t valueAddr)
{
    CHK_PRT_RET(
        recordAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT || valueAddr >= RTSQ_ADDR_NOTIFY_ADDR_LIMIT,
        ASCCOMM_ERROR(
            "[%s] addr out of 49-bit range, recordAddr[0x%llx], valueAddr[0x%llx].", __func__,
            static_cast<unsigned long long>(recordAddr), static_cast<unsigned long long>(valueAddr)),
        ASCCOMM_E_PARA);
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    if (AscProf::Enabled()) {
        AscProf::Condition(context.contextInfo.rtsqContext.sqId, taskId, false);
    }
    Asc::build_a5_sqe_ccore_notify_record(
        context.contextInfo.rtsqContext.streamId, taskId, recordAddr, valueAddr, get_current_sqe(context));
    bool submitted = false;
    CHK_RET(refresh_rtsq(thread, &submitted));
    AsccommBinaryCache::CaptureTask(AsccommBinaryCache::Kind::Launch, reinterpret_cast<uint64_t>(&thread), nullptr, 0);
    return submitted ? ASCCOMM_SUCCESS : asccomm_rtsq_launch(thread);
}

asccomm_result asccomm_rtsq_sdma_copy(ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    constexpr uint32_t RTSQ_PART_ID = 0;
    constexpr uint32_t COPY_OPCODE = 0;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_sdma_copy(
        context.contextInfo.rtsqContext.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, COPY_OPCODE,
        get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_sdma_reduce(
    ThreadEntity& thread, uint64_t dstAddr, uint64_t srcAddr, uint32_t size, const Asc::ReduceIn& reduceIn)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t operation = 0;
    switch (reduceIn.reduceOp) {
        case Asc::ReduceOp::SUM:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_ADD);
            break;
        case Asc::ReduceOp::MAX:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MAX);
            break;
        case Asc::ReduceOp::MIN:
            operation = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncOperationKind::RT_STARS_MEMCPY_ASYNC_OP_KIND_MIN);
            break;
        default:
            ASCCOMM_ERROR("[%s] unsupported SDMA reduce operation[%u].", __func__, reduceIn.reduceOp);
            return ASCCOMM_E_NOT_SUPPORT;
    }

    uint32_t dataType = 0;
    switch (reduceIn.dataType) {
        case Asc::DataType::INT8:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT8);
            break;
        case Asc::DataType::INT16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT16);
            break;
        case Asc::DataType::INT32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_INT32);
            break;
        case Asc::DataType::FP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP16);
            break;
        case Asc::DataType::FP32:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_FP32);
            break;
        case Asc::DataType::BFP16:
            dataType = static_cast<uint32_t>(Asc::RtStarsMemcpyAsyncDataType::RT_STARS_MEMCPY_ASYNC_DATA_TYPE_BFP16);
            break;
        default:
            ASCCOMM_ERROR("[%s] unsupported SDMA data type[%u].", __func__, reduceIn.dataType);
            return ASCCOMM_E_NOT_SUPPORT;
    }

    constexpr uint32_t RTSQ_PART_ID = 0;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_sdma_copy(
        context.contextInfo.rtsqContext.streamId, taskId, dstAddr, srcAddr, size, RTSQ_PART_ID, operation | dataType,
        get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_ub_db_send(
    ThreadEntity& thread, const Asc::UbJettyLiteId& jettyId, uint16_t piValue, AsccommUbCompletionTag tag)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_ub_db_send(
        context.contextInfo.rtsqContext.streamId, taskId, jettyId, piValue, get_current_sqe(context));
    if (tag.completion) {
        UbLedger* ledger = nullptr;
        const auto ret = get_ub_ledger(thread, ledger);
        if (ret != ASCCOMM_SUCCESS) {
            tag.Fail();
            return ret;
        }
        ledger->pending[context.contextInfo.rtsqContext.pendingSqeCnt] = tag;
    }
    return refresh_rtsq(thread);
}

asccomm_result asccomm_rtsq_rdma_db_send(ThreadEntity& thread, uint64_t dbAddr, uint64_t dbValue)
{
    HcommThreadSqContext& context = *thread.sqContextAddr;
    uint32_t taskId = 0;
    CHK_RET(acquire_task_id(context, taskId));
    Asc::build_a5_sqe_rdma_db_send(
        context.contextInfo.rtsqContext.streamId, taskId, dbAddr, dbValue, get_current_sqe(context));
    return refresh_rtsq(thread);
}

asccomm_result asccomm_task_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    if (threadNum == 0) {
        return ASCCOMM_SUCCESS;
    }
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_launch(*thread));
    }
    return ASCCOMM_SUCCESS;
}

asccomm_result asccomm_try_launch(ThreadHandle* threads, uint32_t threadNum)
{
    CHK_PTR_NULL(threads);
    if (threadNum == 0) {
        return ASCCOMM_SUCCESS;
    }
    for (uint32_t index = 0; index < threadNum; ++index) {
        CHK_PRT_RET(threads[index] == 0, ASCCOMM_ERROR("[%s] thread handle is 0.", __func__), ASCCOMM_E_PTR);
        ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threads[index]));
        CHK_RET(asccomm_resolve_thread_rtsq(thread));
        CHK_RET(validate_thread_entity(thread));
        CHK_RET(asccomm_rtsq_try_launch(*thread));
    }
    return ASCCOMM_SUCCESS;
}

namespace AsccommBinaryCache {
bool CompileRtsq(Entry& entry)
{
    try {
        entry.direct = false;
        entry.sdmaPatches.clear();
        // Keep the existing native RDMA completion/CQ path as the control.
        for (const auto& e : entry.events) {
            if (e.kind == Kind::RdmaBegin) {
                return true;
            }
        }
        // Keep per-thread vector capacity across recycled capture entries.
        for (auto& q : entry.queues) {
            q.thread = 0;
            q.bindingValid = q.bindingChecked = false;
            q.bytes.clear();
            q.completionTags.clear();
            q.completionOffsets.clear();
            q.completionSubmitted = 0;
        }
        size_t usedQueues = 0;
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            if (e.kind != Kind::Rtsq && e.kind != Kind::UbEnd && e.kind != Kind::Launch && e.kind != Kind::Submit &&
                e.kind != Kind::ThreadNotifyGuard) {
                continue;
            }
            uint64_t identity = e.resource;
            if (e.kind == Kind::UbEnd) {
                if (e.length != sizeof(identity)) {
                    return false;
                }
                (void)memcpy_sp(&identity, sizeof(identity), entry.bytes.data() + e.offset, sizeof(identity));
            } else if (e.kind == Kind::Rtsq && e.length != sizeof(BinaryRtsqTask)) {
                return false;
            }
            auto* thread = reinterpret_cast<ThreadEntity*>(identity);
            if (!thread || !thread->sqContextAddr) {
                return false;
            }
            uint32_t index = 0;
            while (index < usedQueues && entry.queues[index].thread != identity) {
                ++index;
            }
            if (index == usedQueues) {
                if (index == entry.queues.size()) {
                    entry.queues.emplace_back();
                }
                auto& newQueue = entry.queues[index];
                newQueue.thread = identity;
                const auto& live = thread->sqContextAddr->contextInfo.rtsqContext;
                // Device resolution is stable only within the same domain and SQ binding.
                newQueue.deviceResolved = newQueue.deviceResolved &&
                                          entry.replay.domain == asccomm_ub_current_domain() &&
                                          newQueue.context == reinterpret_cast<uint64_t>(thread->sqContextAddr) &&
                                          newQueue.base == live.sqBaseAddr && newQueue.sqId == live.sqId &&
                                          newQueue.hostDevice == thread->devInfo.devPhyId;
                newQueue.context = reinterpret_cast<uint64_t>(thread->sqContextAddr);
                newQueue.local = live.localSqAddr;
                newQueue.base = live.sqBaseAddr;
                newQueue.depth = live.sqDepth;
                newQueue.sqId = live.sqId;
                newQueue.streamId = live.streamId;
                newQueue.bindingValid = true;
                ++usedQueues;
            }
            auto& q = entry.queues[index];
            entry.plan[i].sqIndex = index;
            if (e.kind == Kind::Launch || e.kind == Kind::Submit || e.kind == Kind::ThreadNotifyGuard) {
                continue;
            }
            entry.plan[i].sqOffset = q.bytes.size();
            const size_t offset = q.bytes.size();
            if (e.kind == Kind::Rtsq) {
                BinaryRtsqTask task;
                (void)memcpy_sp(&task, sizeof(task), entry.bytes.data() + e.offset, sizeof(task));
                if (q.bindingValid && (q.context != task.context || q.local != task.local || q.base != task.base ||
                                       q.depth != task.depth || q.sqId != task.sqId || q.streamId != task.streamId)) {
                    return false;
                }
                q.context = task.context;
                q.local = task.local;
                q.base = task.base;
                q.depth = task.depth;
                q.sqId = task.sqId;
                q.streamId = task.streamId;
                q.bindingValid = true;
                // Append initialized SQE bytes without first zeroing the destination.
                q.bytes.insert(q.bytes.end(), task.sqe, task.sqe + 64);
                const auto* sdma = reinterpret_cast<const Asc::Rt91095StarsMemcpySqe*>(task.sqe);
                if (entry.relocatable &&
                    sdma->header.type == static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_SDMA)) {
                    if (sdma->stride != 0) {
                        return false;
                    }
                    const auto& f = sdma->u.strideMode0;
                    SdmaPatch patch;
                    patch.queue = index;
                    patch.offset = offset;
                    if (!patch.source.Compile(
                            entry.captureRanges, true, uint64_t(f.srcAddrLow) | (uint64_t(f.srcAddrHigh) << 32),
                            f.lengthMove) ||
                        !patch.target.Compile(
                            entry.captureRanges, true, uint64_t(f.dstAddrLow) | (uint64_t(f.dstAddrHigh) << 32),
                            f.lengthMove)) {
                        return false;
                    }
                    entry.sdmaPatches.push_back(patch);
                }
            } else {
                q.bytes.resize(offset + 64, 0); // DB builder requires initialized reserved bits.
                entry.plan[i].completionIndex = q.completionOffsets.size();
                q.completionOffsets.push_back(offset / 64);
                const auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
                const auto& jfs = channel.sqContextAddr[0].contextInfo.ubJfs;
                const Asc::UbJettyLiteId jetty(channel.devInfo.dieId, channel.devInfo.funcId, jfs.jfsID);
                Asc::build_a5_sqe_ub_db_send(
                    thread->sqContextAddr->contextInfo.rtsqContext.streamId, 0, jetty, 0, q.bytes.data() + offset);
            }
        }
        // Validate exact captured ranges once. Hit uses only bound queue indices.
        std::vector<uint32_t> prepared(usedQueues, 0), published(usedQueues, 0);
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            if (e.kind == Kind::Rtsq || e.kind == Kind::UbEnd) {
                ++prepared[entry.plan[i].sqIndex];
            } else if (e.kind == Kind::Submit) {
                uint32_t count = 0;
                if (e.length != sizeof(count)) {
                    return false;
                }
                (void)memcpy_sp(&count, sizeof(count), entry.bytes.data() + e.offset, sizeof(count));
                const auto q = entry.plan[i].sqIndex;
                if (!count || count != prepared[q] - published[q]) {
                    return false;
                }
                entry.plan[i].sqOffset = published[q]; // SQE index for Submit, bytes for Rtsq
                entry.plan[i].completionIndex = count;
                published[q] += count;
            }
        }
        if (prepared != published) {
            return false;
        }
        entry.queues.resize(usedQueues);
        for (auto& q : entry.queues) {
            q.completionTags.resize(q.completionOffsets.size());
            auto& t = *reinterpret_cast<ThreadEntity*>(q.thread);
            q.hostDevice = t.devInfo.devPhyId;
            if (!q.deviceResolved && resolve_submission_device(t, q.localDevice) != ASCCOMM_SUCCESS) {
                return false;
            }
            q.deviceResolved = true;
            q.completionLedger = find_ub_ledger(t);
            if (!q.completionOffsets.empty() && asccomm_ub_current_domain() && !q.completionLedger) {
                return false;
            }
        }
        for (size_t i = 0; i < entry.events.size(); ++i) {
            if (entry.events[i].kind != Kind::UbBegin) {
                continue;
            }
            uint64_t binding[2];
            if (entry.events[i].length < sizeof(binding)) {
                return false;
            }
            (void)memcpy_sp(binding, sizeof(binding), entry.bytes.data() + entry.events[i].offset, sizeof(binding));
            uint32_t q = 0;
            while (q < entry.queues.size() && entry.queues[q].thread != binding[0]) {
                ++q;
            }
            if (q == entry.queues.size() || entry.queues[q].context != binding[1]) {
                return false;
            }
            entry.plan[i].sqIndex = q;
        }
        entry.replay.domain = asccomm_ub_current_domain();
        entry.queueStorageBytes = entry.queues.capacity() * sizeof(SqePlan);
        for (const auto& q : entry.queues) {
            entry.queueStorageBytes += q.bytes.capacity() +
                                       q.completionTags.capacity() * sizeof(AsccommUbCompletionTag) +
                                       q.completionOffsets.capacity() * sizeof(uint32_t);
        }
        if (entry.StorageBytes() > ASCCOMM_BINARY_ENTRY_BUDGET) {
            return false;
        }
        if (!CompileReplaySchedule(entry)) {
            return false;
        }
        entry.replay.ready = false;
        entry.replay.instructions.clear();
        for (size_t i = 0; i < entry.events.size(); ++i) {
            const auto& e = entry.events[i];
            ReplayInstruction op{};
            op.kind = e.kind;
            op.resource = e.resource;
            op.offset = e.offset;
            op.length = e.length;
            op.plan = entry.plan[i];
            if (e.kind == Kind::UbBegin || e.kind == Kind::UbEnd || e.kind == Kind::Fence ||
                e.kind == Kind::ThreadNotifyGuard || e.kind == Kind::ChannelNotifyGuard) {
                if (e.length > sizeof(op.args)) {
                    return false;
                }
                (void)memcpy_sp(op.args.data(), sizeof(op.args), entry.bytes.data() + e.offset, e.length);
            }
            if (op.kind == Kind::ThreadNotifyGuard && op.args[0] >= Asc::THREAD_NOTIFY_SEGMENT_SIZE) {
                op.kind = Kind::ThreadNotifyGuardSegmented;
            }
            entry.replay.instructions.push_back(op);
        }
        entry.replay.ready = true;
        if (entry.StorageBytes() > ASCCOMM_BINARY_ENTRY_BUDGET) {
            return false;
        }
        entry.direct = true;
        return true;
    } catch (...) {
        return false;
    }
}

namespace {
// A compiled segment contains adjacent SQEs for one RTSQ. Bind its queue once,
// patch only task IDs, then submit the contiguous template at the native boundary.
bool ApplyReplaySqes(Entry& entry, uint32_t first, uint32_t count)
{
    const auto& initial = entry.replay.instructions[first].plan;
    auto& q = entry.queues[initial.sqIndex];
    auto& thread = *reinterpret_cast<ThreadEntity*>(q.thread);
    if (initial.sqOffset != q.cursor * 64) {
        return false;
    }
    const bool registerThread = q.cursor == 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t taskId = 0;
        if (!Succeeded(acquire_task_id(*thread.sqContextAddr, taskId))) {
            return false;
        }
        Asc::set_sqe_header_task_fields(q.bytes.data() + (q.cursor + i) * 64, taskId);
    }
    q.cursor += count;
    if (registerThread) {
        asccomm_register_thread(static_cast<ThreadHandle>(q.thread));
    }
    return true; // Captured Submit instructions own the publication boundaries.
}
bool ApplyReplayInstruction(Entry& entry, const ReplayInstruction& e, uint32_t opIndex)
{
    if ((e.kind == Kind::Rtsq || e.kind == Kind::UbEnd)) {
        const auto& compiled = e.plan;
        auto& q = entry.queues[compiled.sqIndex];
        auto& thread = *reinterpret_cast<ThreadEntity*>(q.thread);
        if (compiled.sqOffset != q.cursor * 64) {
            return false;
        }
        uint32_t taskId = 0;
        if (!Succeeded(acquire_task_id(*thread.sqContextAddr, taskId))) {
            return false;
        }
        auto* sqe = q.bytes.data() + compiled.sqOffset;
        Asc::set_sqe_header_task_fields(sqe, taskId);
        if (e.kind == Kind::UbEnd) {
            const auto pi = CompiledTransportPi(entry, opIndex);
            const auto& channel = *reinterpret_cast<ChannelEntity*>(e.resource);
            reinterpret_cast<Asc::Rt91095StarsUbdmaDBmodeSqe*>(sqe)->piValue1 = pi;
            auto tag = asccomm_ub_note_db(channel, pi);
            if (tag.completion) {
                if (!q.completionLedger) {
                    tag.Fail();
                    return false;
                }
            }
            q.completionTags[compiled.completionIndex] = tag;
        }
        ++q.cursor;
        if (q.cursor == 1) {
            asccomm_register_thread(static_cast<ThreadHandle>(q.thread));
        }
        return true; // Publish only at the captured boundary.
    }
    if (e.kind == Kind::BatchFlush) {
        asccomm_set_batch_launch_mode(false);
        return true;
    }
    if (e.kind == Kind::ThreadNotifyGuard || e.kind == Kind::ThreadNotifyGuardSegmented) {
        return true;
    }
    if (e.kind == Kind::BatchEnd) {
        asccomm_set_batch_launch_mode(false);
        asccomm_clear_active_threads();
        CompleteTransportPublication();
        return true;
    }
    if (e.kind == Kind::Submit) {
        auto& q = entry.queues[e.plan.sqIndex];
        // An earlier capacity checkpoint may already have drained a prefix.
        const uint32_t end = e.plan.sqOffset + e.plan.completionIndex;
        if (q.cursor != end || q.submitted > end) {
            return false;
        }
        return Succeeded(launch_direct(*reinterpret_cast<ThreadEntity*>(q.thread), false, &q));
    }
    if (e.kind == Kind::Launch) {
        return true; // actual publication is captured separately
    }
    if (e.kind != Kind::Rtsq) {
        return ApplyCompiledTransport(e, opIndex);
    }
    return false;
}
} // namespace
Result ReplayRtsq(Entry& entry)
{
    struct Adapter {
        std::vector<uint8_t>& patched;
        uint64_t transport = 0;
        Kind transportKind = Kind::UbBegin;
        Entry& entry;
        explicit Adapter(Entry& source) : patched(source.workspace), entry(source)
        {
            BeginTransportReplay(source);
            for (auto& q : source.queues) {
                q.cursor = q.submitted = q.completionSubmitted = 0;
                q.submitting = false;
                q.bindingChecked = false;
            }
        }
        ~Adapter()
        {
            directEntry = nullptr;
            EndTransportReplay();
        }
        bool Prepare(const Event& e, const Entry& source, bool scheduled = false)
        {
            if (e.layout != VERSION) {
                return Reject(AsccommRejectReason::Layout);
            }
            if (e.kind == Kind::BatchFlush) {
                return e.length == 0 && transport == 0;
            }
            if (e.kind == Kind::Submit) {
                return e.length == sizeof(uint32_t) && e.resource != 0;
            }
            if (e.kind == Kind::BatchEnd) {
                return (e.length == 0 && transport == 0 && &e == &source.events.back()) ||
                       Reject(AsccommRejectReason::BatchEnd);
            }
            if (e.kind == Kind::ThreadNotifyGuard) {
                if (!e.resource || e.length != 2 * sizeof(uint64_t) || transport) {
                    return Reject(AsccommRejectReason::NotifyEncoding);
                }
                auto* thread = reinterpret_cast<ThreadEntity*>(e.resource);
                uint64_t binding[2];
                (void)memcpy_sp(binding, sizeof(binding), source.bytes.data() + e.offset, sizeof(binding));
                if (thread->abiHeader.version != HCOMM_THREAD_ENTITY_VERSION ||
                    thread->abiHeader.magicWord != HCOMM_THREAD_ENTITY_MAGIC_WORD || !thread->localNotifyAddr ||
                    binding[0] >= thread->localNotifyNum) {
                    return Reject(AsccommRejectReason::NotifyBinding);
                }
                const auto* notify = Asc::ResolveThreadNotify(*thread, binding[0]);
                return (notify && notify->type == HCOMM_REGED_NOTIFY_IPC_RT && notify->notifyInfo.ipcRt.notifyId >= 0 &&
                        static_cast<uint64_t>(notify->notifyInfo.ipcRt.notifyId) == binding[1]) ||
                       Reject(AsccommRejectReason::NotifyChanged);
            }
            if (e.kind == Kind::Rtsq || e.kind == Kind::Launch) {
                if (!e.resource || (!scheduled && transport)) {
                    return Reject(AsccommRejectReason::RtsqOrder);
                }
                if (e.length != (e.kind == Kind::Rtsq ? sizeof(BinaryRtsqTask) : 0)) {
                    return Reject(AsccommRejectReason::RtsqEncoding);
                }
                auto* thread = reinterpret_cast<ThreadEntity*>(e.resource);
                if (validate_thread_entity(thread) != ASCCOMM_SUCCESS || !thread->sqContextAddr) {
                    return Reject(AsccommRejectReason::ThreadBinding);
                }
                const auto& current = thread->sqContextAddr->contextInfo.rtsqContext;
                if (current.pendingSqeCnt != 0) {
                    return Reject(AsccommRejectReason::PendingSqe);
                }
                if (e.kind == Kind::Rtsq) {
                    BinaryRtsqTask captured;
                    (void)memcpy_sp(&captured, sizeof(captured), source.bytes.data() + e.offset, sizeof(captured));
                    if (captured.context != reinterpret_cast<uint64_t>(thread->sqContextAddr) ||
                        captured.local != current.localSqAddr || captured.base != current.sqBaseAddr ||
                        captured.depth != current.sqDepth || captured.sqId != current.sqId ||
                        captured.streamId != current.streamId) {
                        return Reject(AsccommRejectReason::QueueChanged);
                    }
                }
                return true;
            }
            if (!scheduled) {
                if (e.kind == Kind::UbBegin || e.kind == Kind::RdmaBegin) {
                    if (transport) {
                        return Reject(AsccommRejectReason::NestedTransport);
                    }
                    transport = e.resource;
                    transportKind = e.kind;
                } else if (e.kind != Kind::Fence && e.kind != Kind::ChannelNotifyGuard) {
                    if (transport != e.resource) {
                        return Reject(AsccommRejectReason::TransportResource);
                    }
                    if ((e.kind == Kind::UbWqe || e.kind == Kind::UbEnd) && transportKind != Kind::UbBegin) {
                        return Reject(AsccommRejectReason::UbOrder);
                    }
                    if ((e.kind == Kind::RdmaWqe || e.kind == Kind::RdmaEnd) && transportKind != Kind::RdmaBegin) {
                        return Reject(AsccommRejectReason::RdmaOrder);
                    }
                }
            }
            if (!PrepareTransport(e, source, patched)) {
                return false;
            }
            if (e.kind == Kind::UbEnd || e.kind == Kind::RdmaEnd) {
                transport = 0;
            }
            return true;
        }
        bool started = false;
        bool Apply(const Event& e, const Entry&)
        {
            if (!started) {
                started = true;
                BeginTransportPublication();
            }
            if (e.kind == Kind::Submit || e.kind == Kind::BatchFlush) {
                return true; // native fallback retains native launches
            }
            if (e.kind == Kind::ThreadNotifyGuard) {
                return true;
            }
            if (e.kind == Kind::BatchEnd) {
                asccomm_set_batch_launch_mode(false);
                const auto ret = asccomm_launch_registered_threads();
                if (ret == ASCCOMM_SUCCESS) {
                    asccomm_clear_active_threads();
                    CompleteTransportPublication();
                }
                return Succeeded(ret);
            }
            if (e.kind != Kind::Rtsq && e.kind != Kind::Launch) {
                return ApplyTransport(e, patched.data() + e.offset, entry.plan[&e - entry.events.data()].resourceIndex);
            }
            auto* thread = reinterpret_cast<ThreadEntity*>(e.resource);
            if (e.kind == Kind::Launch) {
                return Succeeded(asccomm_rtsq_launch(*thread));
            }
            uint32_t taskId = 0;
            if (!Succeeded(acquire_task_id(*thread->sqContextAddr, taskId))) {
                return false;
            }
            auto* dst = get_current_sqe(*thread->sqContextAddr);
            BinaryRtsqTask captured;
            (void)memcpy_sp(&captured, sizeof(captured), patched.data() + e.offset, sizeof(captured));
            (void)memcpy_sp(dst, HCOMM_RESOURCE_RTSQ_SQE_SIZE, captured.sqe, sizeof(captured.sqe));
            Asc::set_sqe_header_task_fields(dst, taskId);
            asccomm_register_thread(static_cast<ThreadHandle>(e.resource));
            return Succeeded(refresh_rtsq(*thread));
        }
    };
    RejectReason() = 0;
    if (entry.state != State::Ready || entry.version != VERSION || !entry.compiled ||
        entry.workspace.size() != entry.bytes.size() ||
        (!entry.direct && (entry.events.empty() || entry.events.back().kind != Kind::BatchEnd))) {
        Reject(AsccommRejectReason::ScheduleCompile);
        return Result::Fallback;
    }
    try {
        LastError() = 0;
        Adapter adapter(entry);
        if (!entry.direct) {
            return Replay(entry, entry.generation, adapter);
        }
        if (!entry.replay.ready || entry.replay.domain != asccomm_ub_current_domain()) {
            Reject(AsccommRejectReason::EntryNotReady);
            return Result::Fallback;
        }
        // No queue state is changed until every dynamic resource check/patch succeeds.
        try {
            for (const auto& q : entry.queues) {
                auto* thread = reinterpret_cast<ThreadEntity*>(q.thread);
                if (validate_thread_entity(thread) != ASCCOMM_SUCCESS || !thread->sqContextAddr) {
                    Reject(AsccommRejectReason::ThreadBinding);
                    return Result::Fallback;
                }
                const auto& live = thread->sqContextAddr->contextInfo.rtsqContext;
                if (live.pendingSqeCnt) {
                    Reject(AsccommRejectReason::PendingSqe);
                    return Result::Fallback;
                }
                if (q.hostDevice != thread->devInfo.devPhyId ||
                    q.context != reinterpret_cast<uint64_t>(thread->sqContextAddr) || q.local != live.localSqAddr ||
                    q.base != live.sqBaseAddr || q.depth != live.sqDepth || q.sqId != live.sqId ||
                    q.streamId != live.streamId) {
                    Reject(AsccommRejectReason::QueueChanged);
                    return Result::Fallback;
                }
            }
            for (const auto& p : entry.sdmaPatches) {
                uint64_t src, dst;
                if (!p.source.Resolve(entry.captureRanges, entry.replayRanges, entry.relocatable, src) ||
                    !p.target.Resolve(entry.captureRanges, entry.replayRanges, entry.relocatable, dst)) {
                    Reject(AsccommRejectReason::RtsqEncoding);
                    return Result::Fallback;
                }
                auto& fields =
                    reinterpret_cast<Asc::Rt91095StarsMemcpySqe*>(entry.queues[p.queue].bytes.data() + p.offset)
                        ->u.strideMode0;
                fields.srcAddrLow = src;
                fields.srcAddrHigh = src >> 32;
                fields.dstAddrLow = dst;
                fields.dstAddrHigh = dst >> 32;
            }
            for (const auto index : entry.checks) {
                const auto& op = entry.replay.instructions[index];
                if (op.kind == Kind::UbWqe) {
                    continue;
                }
                if (op.kind == Kind::ThreadNotifyGuard) {
                    auto* t = reinterpret_cast<ThreadEntity*>(op.resource);
                    if (!t->localNotifyAddr || op.args[0] >= t->localNotifyNum) {
                        Reject(AsccommRejectReason::NotifyBinding);
                        return Result::Fallback;
                    }
                    // Capture compilation selected this guard only for the first segment.
                    const auto& n = t->localNotifyAddr[op.args[0]];
                    if (n.type != HCOMM_REGED_NOTIFY_IPC_RT || n.notifyInfo.ipcRt.notifyId < 0 ||
                        uint64_t(n.notifyInfo.ipcRt.notifyId) != op.args[1]) {
                        Reject(AsccommRejectReason::NotifyChanged);
                        return Result::Fallback;
                    }
                } else if (op.kind == Kind::ThreadNotifyGuardSegmented) {
                    const auto* t = reinterpret_cast<const ThreadEntity*>(op.resource);
                    const auto* n = Asc::ResolveThreadNotify(*t, op.args[0]);
                    if (!n || n->type != HCOMM_REGED_NOTIFY_IPC_RT || n->notifyInfo.ipcRt.notifyId < 0 ||
                        uint64_t(n->notifyInfo.ipcRt.notifyId) != op.args[1]) {
                        Reject(AsccommRejectReason::NotifyChanged);
                        return Result::Fallback;
                    }
                } else if (!PrepareCompiledTransport(op)) {
                    return Result::Fallback;
                }
            }
            if (!PrepareCompiledTokens()) {
                return Result::Fallback;
            }
        } catch (...) {
            Reject(AsccommRejectReason::PrepareException);
            return Result::Fallback;
        }
        try {
            AscProf::PublicationScope deferredProfiling(static_cast<uint32_t>(entry.replay.instructions.size()));
            adapter.started = true;
            directEntry = &entry;
            BeginTransportPublication();
            for (const auto& segment : entry.segments) {
                if (segment.kind == Kind::UbBegin && segment.count > 1) {
                    const auto last = segment.first + segment.count - 1;
                    const auto firstWqe = segment.first + (segment.fence == UINT32_MAX ? 1U : 2U);
                    if (!ApplyCompiledTransport(entry.replay.instructions[segment.first], segment.first) ||
                        (segment.fence != UINT32_MAX &&
                         !ApplyCompiledTransport(entry.replay.instructions[segment.fence], segment.fence)) ||
                        !PublishCompiledWqes(entry, firstWqe, last - firstWqe) ||
                        !ApplyReplayInstruction(entry, entry.replay.instructions[last], last)) {
                        entry.state = State::Invalid;
                        return Result::Failed;
                    }
                } else if (segment.kind == Kind::Rtsq) {
                    if (!ApplyReplaySqes(entry, segment.first, segment.count)) {
                        entry.state = State::Invalid;
                        return Result::Failed;
                    }
                } else if (segment.kind == Kind::UbWqe) {
                    if (!PublishCompiledWqes(entry, segment.first, segment.count)) {
                        entry.state = State::Invalid;
                        return Result::Failed;
                    }
                } else {
                    for (uint32_t i = segment.first; i < segment.first + segment.count; ++i) {
                        if (!ApplyReplayInstruction(entry, entry.replay.instructions[i], i)) {
                            entry.state = State::Invalid;
                            return Result::Failed;
                        }
                    }
                }
            }
        } catch (...) {
            entry.state = State::Invalid;
            return Result::Failed;
        }
        return Result::Complete;
    } catch (...) {
        Reject(AsccommRejectReason::ApplyException);
        return Result::Fallback;
    } // allocation before any Apply
}
} // namespace AsccommBinaryCache
int32_t asccomm_diag_dump_sq(uint64_t threadHandle)
{
    // 探针恒开：只在 server 退避路径被调（正常路径零调用零成本），输出量天然有界无需开关。
    // 例外：[NotifyFlag] 段直读有挂死风险（真机实证），默认禁用，仅 ASCCOMM_DIAG_NOTIFY_FLAG=1 时开。
    ThreadEntity* thread = reinterpret_cast<ThreadEntity*>(static_cast<uintptr_t>(threadHandle));
    if (thread == nullptr || thread->sqContextAddr == nullptr) {
        ASCCOMM_ERROR("[SQ_DUMP] invalid thread handle[%llu].", static_cast<unsigned long long>(threadHandle));
        return static_cast<int32_t>(ASCCOMM_E_PTR);
    }
    // 入口实体 ABI 校验：错句柄直接读 sqContextAddr 会让探针自身崩溃丢诊断现场。
    if (validate_thread_entity(thread) != ASCCOMM_SUCCESS) {
        ASCCOMM_ERROR(
            "[SQ_DUMP] thread entity ABI mismatch, handle[%llu].", static_cast<unsigned long long>(threadHandle));
        return static_cast<int32_t>(ASCCOMM_E_PARA);
    }
    const HcommRtsqContext& rtsq = thread->sqContextAddr->contextInfo.rtsqContext;

    uint32_t localDevId = 0;
    if (drvGetLocalDevIDByHostDevID(thread->devInfo.devPhyId, &localDevId) != 0) {
        ASCCOMM_ERROR("[SQ_DUMP] drvGetLocalDevIDByHostDevID failed, devPhyId[%u].", thread->devInfo.devPhyId);
        return static_cast<int32_t>(ASCCOMM_E_INTERNAL);
    }

    // query HEAD/TAIL/DEPTH；失败回落镜像值，不中断 dump
    uint32_t hwHead = rtsq.sqHead;
    uint32_t hwTail = rtsq.sqTail;
    uint32_t depth = rtsq.sqDepth;
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_HEAD, hwHead) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query HEAD failed, fallback to mirror[%u].", hwHead);
    }
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_TAIL, hwTail) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query TAIL failed, fallback to mirror[%u].", hwTail);
    }
    if (query_sq_value(localDevId, rtsq, DRV_SQCQ_PROP_SQ_DEPTH, depth) != ASCCOMM_SUCCESS) {
        ASCCOMM_INFO("[SQ_DUMP] query DEPTH failed, fallback to mirror[%u].", depth);
    }
    if (depth == 0 || rtsq.sqBaseAddr == 0) {
        ASCCOMM_ERROR(
            "[SQ_DUMP] invalid RTSQ, sqId[%u], depth[%u], base[0x%llx].", rtsq.sqId, depth,
            static_cast<unsigned long long>(rtsq.sqBaseAddr));
        return static_cast<int32_t>(ASCCOMM_E_PARA);
    }

    // 打印 [hwHead-10, hwHead+2) 共 12 条 SQE × 16 words：健康跑 head 越过整批，向后扩 10
    // 才能拍到已消费的 transport SQE；hwHead 处前 8 word 暂存 headSqe 供 [Head] 解码行复用。
    uint32_t headSqe[8] = {0};
    uint32_t pos = static_cast<uint32_t>((static_cast<uint64_t>(hwHead) + depth - 10U) % depth);
    for (uint32_t i = 0; i < 12U; ++i) {
        const char* marker = "";
        if (pos == hwHead && pos == hwTail) {
            marker = "<-HEAD/TAIL";
        } else if (pos == hwHead) {
            marker = "<-HEAD";
        } else if (pos == hwTail) {
            marker = "<-TAIL";
        }
        const uint64_t sqeBase = rtsq.sqBaseAddr + static_cast<uint64_t>(pos) * HCOMM_RESOURCE_RTSQ_SQE_SIZE;
        for (uint32_t w = 0; w < 16U; ++w) {
            const uint64_t wordAddr = sqeBase + static_cast<uint64_t>(w) * sizeof(uint32_t);
            if (wordAddr == 0) {
                ASCCOMM_INFO("[SQ_DUMP] read fail pos=%u w=%u", pos, w);
                continue;
            }
            const uint32_t val = *reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(wordAddr));
            ASCCOMM_INFO("[SQ_DUMP] sqId[%u] pos[%u] %s w%02u=0x%08x", rtsq.sqId, pos, marker, w, val);
            if (pos == hwHead && w < 8U) {
                headSqe[w] = val;
            }
        }
        pos = (pos + 1U) % depth;
    }

    // [Head] 解码行：word0[5:0]=type、word1 低16/高16=rtStreamId/taskId、word2[16:0]=notifyId、
    // word3[23:16]=credit。type=7 打 notifyId；type=20（COND）word4 起为指令流不解码；其余只打 type。
    const uint8_t headType = static_cast<uint8_t>(headSqe[0] & 0x3FU);
    if (headType == static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_NOTIFY_WAIT)) {
        const uint32_t taskId = (headSqe[1] >> 16U) & 0xFFFFU;
        const uint32_t nid = headSqe[2] & 0x1FFFFU;
        const uint32_t credit = (headSqe[3] >> 16U) & 0xFFU;
        ASCCOMM_INFO(
            "[SQ_DUMP][Head] type=%u taskId=%u notifyId=%u credit=0x%02x", static_cast<uint32_t>(headType), taskId, nid,
            credit);

        // [NotifyFlag] 直读 TS notify 表：基址 0x06a0100000，16 组×512 项×4B，组距 0x10000；
        // bit0=flag、bit1=pendingClr。AICPU 核直读 TS 私有寄存器空间有挂死风险（load 指令本身
        // 不返回，无法兜底），故默认禁用，仅 ASCCOMM_DIAG_NOTIFY_FLAG=1 时开。
        constexpr uint64_t NOTIFY_TBL_BASE = 0x06a0100000ULL; // 0x06a0000000 + 0x100000
        constexpr uint32_t NOTIFY_PER_GROUP = 512U;
        constexpr uint32_t NOTIFY_TOTAL = 16U * NOTIFY_PER_GROUP;
        constexpr uint64_t NOTIFY_GROUP_STRIDE = 0x10000ULL;
        // magic static 落 .data（AICPU 加载域 .bss 零初始化不可信）；AICPU 侧 getenv 不继承
        // host env，常态读不到=禁用=安全默认。
        static const bool notifyFlagReadEnabled = []() {
            const char* env = getenv("ASCCOMM_DIAG_NOTIFY_FLAG");
            return env != nullptr && env[0] == '1';
        }();
        if (notifyFlagReadEnabled) {
            // 只读 nid±1 三个 4B 字看邻域污染；边界 nid 夹到合法区间防回绕野地址。
            for (int32_t delta = -1; delta <= 1; ++delta) {
                int64_t curNid = static_cast<int64_t>(nid) + delta;
                if (curNid < 0) {
                    curNid = 0;
                }
                if (curNid >= NOTIFY_TOTAL) {
                    curNid = NOTIFY_TOTAL - 1;
                }
                const uint32_t group = static_cast<uint32_t>(curNid) / NOTIFY_PER_GROUP;
                const uint32_t idx = static_cast<uint32_t>(curNid) % NOTIFY_PER_GROUP;
                const uint64_t flagAddr = NOTIFY_TBL_BASE + static_cast<uint64_t>(group) * NOTIFY_GROUP_STRIDE +
                                          static_cast<uint64_t>(idx) * sizeof(uint32_t);
                const uint32_t raw = *reinterpret_cast<volatile const uint32_t*>(static_cast<uintptr_t>(flagAddr));
                ASCCOMM_INFO(
                    "[SQ_DUMP][NotifyFlag] notifyId=%u group=%u flagAddr=0x%llx flag=0x%08x pendingClr=0x%08x "
                    "raw=0x%08x",
                    static_cast<uint32_t>(curNid), group, static_cast<unsigned long long>(flagAddr), raw & 0x1U,
                    (raw >> 1U) & 0x1U, raw);
            }
        } else {
            ASCCOMM_INFO("[SQ_DUMP][NotifyFlag] disabled (107 batch: TS register read hangs AICPU core).");
        }
    } else if (headType == static_cast<uint8_t>(Asc::Rt91095StarsSqeType::RT_91095_SQE_TYPE_COND)) {
        ASCCOMM_INFO("[SQ_DUMP][Head] type=%u(COND) word4=0x%08x", static_cast<uint32_t>(headType), headSqe[4]);
    } else {
        ASCCOMM_INFO("[SQ_DUMP][Head] type=%u", static_cast<uint32_t>(headType));
    }

    // 汇总行（便于 grep）+ 镜像对账行 + 分隔行
    ASCCOMM_INFO(
        "[SQ_DUMP][Summary] sqId=%u streamId=%u depth=%u hwHead=%u hwTail=%u base=0x%llx", rtsq.sqId, rtsq.streamId,
        depth, hwHead, hwTail, static_cast<unsigned long long>(rtsq.sqBaseAddr));
    // 镜像取自 rtsq 软件账本（query 失败回落时是唯一真相）；Mirror 与 Summary 背离 = 软硬件分叉的直接证据。
    ASCCOMM_INFO(
        "[SQ_DUMP][Mirror] sqId=%u mirrorHead=%u mirrorTail=%u mirrorDepth=%u pendCnt=%u taskIdRange=[%u,%u)",
        rtsq.sqId, rtsq.sqHead, rtsq.sqTail, rtsq.sqDepth, rtsq.pendingSqeCnt, rtsq.taskId, rtsq.taskIdEnd);
    ASCCOMM_INFO("[SQ_DUMP][End]");
    return static_cast<int32_t>(ASCCOMM_SUCCESS);
}
