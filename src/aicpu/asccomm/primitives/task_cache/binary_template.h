/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */
#ifndef ASCCOMM_BINARY_TEMPLATE_H
#define ASCCOMM_BINARY_TEMPLATE_H
#include "../../resource/connection/ub_completion.h"
#include <cstdint>
#include <cstddef>
#include <vector>
#include "securec.h"
#include "asccomm_command_replay.h"
#include <limits>
#include <algorithm>
#include <memory>
#include <array>
#include <initializer_list>

namespace AsccommBinaryCache {
constexpr uint32_t VERSION = 1;
enum class Kind : uint32_t {
    Rtsq,
    UbWqe,
    RdmaWqe,
    Fence,
    Launch,
    BatchEnd,
    UbBegin,
    UbEnd,
    RdmaBegin,
    RdmaEnd,
    ThreadNotifyGuard,
    ChannelNotifyGuard,
    Submit,                    // successful native publication; payload is the SQE count
    BatchFlush,                // switch to eager before native batch publications
    ThreadNotifyGuardSegmented // compiled-only guard for linked notify segments
};
enum class State { Capturing, Ready, Invalid };
enum class Result { Complete, Fallback, Failed };
inline uint32_t& RejectReason()
{
    static thread_local uint32_t reason = 0;
    return reason;
}
inline bool Reject(AsccommRejectReason reason)
{
    if (RejectReason() == 0) {
        RejectReason() = static_cast<uint32_t>(reason);
    }
    return false;
}
struct Event {
    Kind kind;
    uint64_t resource;
    uint32_t layout;
    uint32_t offset;
    uint32_t length;
    uint32_t patchBegin;
    uint32_t patchCount;
};
// Source indices are interpreted by a type-specific adapter, never cached token values.
struct Patch {
    uint32_t field;
    uint32_t source;
    uint64_t offset;
    uint64_t span;
};
// Compiled at capture end. Indices remain stable until Reset; no queue state
// or live token is stored here. Workspace is serialized by the owning entry.
struct ResourcePlan {
    uint64_t identity;
    uint32_t wqebb = 0;
    uint32_t firstEvent = UINT32_MAX;
};
struct EventPlan {
    uint32_t resourceIndex = UINT32_MAX;
    uint32_t previousFence = UINT32_MAX;
    uint32_t begin = UINT32_MAX;
    uint32_t end = UINT32_MAX;
    uint32_t sqIndex = UINT32_MAX;
    uint32_t sqOffset = 0;
    uint32_t completionIndex = UINT32_MAX;
};
struct ReplaySegment {
    Kind kind;
    uint32_t first, count;
    uint32_t fence = UINT32_MAX;
};
// Immutable executable record. Capture events/bytes are never interpreted by direct hit.
struct ReplayInstruction {
    Kind kind;
    uint64_t resource = 0;
    uint32_t offset = 0, length = 0;
    EventPlan plan;
    std::array<uint64_t, 24> args{};
};
struct ReplayPlan {
    uint64_t domain = 0;
    std::vector<ReplayInstruction> instructions;
    bool ready = false;
};

struct SqePlan {
    uint64_t thread = 0;
    uint64_t context = 0, local = 0, base = 0;
    uint32_t depth = 0, sqId = 0, streamId = 0;
    bool bindingValid = false, bindingChecked = false;
    std::vector<uint8_t> bytes;
    std::vector<AsccommUbCompletionTag> completionTags;
    std::vector<uint32_t> completionOffsets;
    uint32_t completionSubmitted = 0;
    uint32_t cursor = 0, submitted = 0;
    bool submitting = false;
    uint32_t localDevice = 0, hostDevice = 0;
    void* completionLedger = nullptr;
    bool deviceResolved = false;
};
using Ranges = std::array<uint64_t, 4>; // input base/span, output base/span
inline const Ranges*& InvocationRanges()
{
    static thread_local const Ranges* p = nullptr;
    return p;
}
inline bool ValidRanges(const Ranges& r)
{
    for (size_t i : {size_t(0), size_t(2)}) {
        if (!r[i] || !r[i + 1] || r[i + 1] > UINT64_MAX - r[i]) {
            return false;
        }
    }
    return r[0] + r[1] <= r[2] || r[2] + r[3] <= r[0];
}
// Compiled address classification: 0=input, 2=output, 4=resource-owned.
struct AddressPlan {
    uint64_t address = 0, length = 0, offset = 0;
    uint32_t role = 4;
    bool Compile(const Ranges& ranges, bool relocatable, uint64_t value, uint64_t size)
    {
        address = value;
        length = size;
        role = 4;
        offset = 0;
        if (size > UINT64_MAX - value) {
            return false;
        }
        if (!relocatable) {
            return true;
        }
        for (uint32_t i : {0U, 2U}) {
            if (value >= ranges[i] && value - ranges[i] < ranges[i + 1]) {
                offset = value - ranges[i];
                role = i;
                return size <= ranges[i + 1] - offset;
            }
            if (value < ranges[i] && size > ranges[i] - value) {
                return false;
            }
        }
        return true;
    }
    bool Resolve(const Ranges& captured, const Ranges& current, bool relocatable, uint64_t& value) const
    {
        value = address;
        if (!relocatable) {
            return true;
        }
        if (role != 4) {
            if (offset > current[role + 1] || length > current[role + 1] - offset ||
                offset > UINT64_MAX - current[role]) {
                return false;
            }
            value = current[role] + offset;
            return length <= UINT64_MAX - value;
        }
        for (uint32_t i : {0U, 2U}) {
            if (current[i] != captured[i] && address < current[i] + current[i + 1] && current[i] < address + length) {
                return false;
            }
        }
        return true;
    }
};
struct SdmaPatch {
    uint32_t queue, offset;
    AddressPlan source, target;
};
struct TransportWorkspace;
struct Entry {
    Ranges captureRanges{}, replayRanges{};
    bool relocatable = false;
    bool MapAddress(uint64_t address, uint64_t length, uint64_t& mapped) const
    {
        mapped = address;
        if (!relocatable) {
            return true;
        }
        if (length > UINT64_MAX - address) {
            return false;
        }
        for (size_t i : {size_t(0), size_t(2)}) {
            const uint64_t base = captureRanges[i], span = captureRanges[i + 1];
            if (address >= base && address - base < span) {
                const uint64_t offset = address - base;
                if (length > span - offset || offset > replayRanges[i + 1] || length > replayRanges[i + 1] - offset ||
                    offset > UINT64_MAX - replayRanges[i]) {
                    return false;
                }
                mapped = replayRanges[i] + offset;
                return length <= UINT64_MAX - mapped;
            }
            if (address < base && length > base - address) {
                return false;
            }
        }
        // A relocated user buffer must not alias a previously resource-owned operand.
        for (size_t i : {size_t(0), size_t(2)}) {
            if (replayRanges[i] != captureRanges[i] && address < replayRanges[i] + replayRanges[i + 1] &&
                replayRanges[i] < address + length) {
                return false;
            }
        }
        return true; // resource-owned address, unchanged within the pinned domain
    }
    uint32_t version = VERSION;
    State state = State::Capturing;
    std::vector<uint8_t> bytes;
    std::vector<Event> events;
    std::vector<Patch> patches;
    uint64_t generation = 0;
    std::vector<ResourcePlan> resources;
    std::vector<EventPlan> plan;
    std::vector<uint8_t> workspace;
    bool compiled = false;
    bool direct = false;
    std::vector<uint32_t> checks;
    std::vector<SdmaPatch> sdmaPatches;
    std::vector<ReplaySegment> segments;
    ReplayPlan replay;
    std::vector<SqePlan> queues;
    uint64_t queueStorageBytes = 0;
    std::shared_ptr<TransportWorkspace> transportWorkspace;
    uint64_t transportStorageBytes = 0;
    bool Append(
        Kind kind, uint64_t resource, uint32_t layout, const void* data, uint32_t length,
        std::initializer_list<Patch> fields, uint64_t budget) noexcept
    {
        if (state != State::Capturing) {
            return false;
        }
        const uint64_t required = uint64_t(bytes.size()) + length + (uint64_t(events.size()) + 1) * sizeof(Event) +
                                  (uint64_t(patches.size()) + fields.size()) * sizeof(Patch);
        if ((length && !data) || required > budget || required > UINT32_MAX) {
            state = State::Invalid;
            return false;
        }
        try {
            Event event{
                kind,
                resource,
                layout,
                static_cast<uint32_t>(bytes.size()),
                length,
                static_cast<uint32_t>(patches.size()),
                static_cast<uint32_t>(fields.size())};
            if (length) {
                const auto* source = static_cast<const uint8_t*>(data);
                bytes.insert(bytes.end(), source, source + length);
            }
            patches.insert(patches.end(), fields.begin(), fields.end());
            events.push_back(event);
            if (StorageBytes() > budget) {
                state = State::Invalid;
                return false;
            }
            return true;
        } catch (...) {
            state = State::Invalid;
            return false;
        }
    }
    uint64_t StorageBytes() const noexcept
    {
        return bytes.capacity() + events.capacity() * sizeof(Event) + patches.capacity() * sizeof(Patch) +
               replay.instructions.capacity() * sizeof(ReplayInstruction) + sdmaPatches.capacity() * sizeof(SdmaPatch) +
               checks.capacity() * sizeof(uint32_t) + segments.capacity() * sizeof(ReplaySegment) +
               resources.capacity() * sizeof(ResourcePlan) + plan.capacity() * sizeof(EventPlan) +
               workspace.capacity() + transportStorageBytes + queueStorageBytes;
    }
    void Reset() noexcept
    {
        relocatable = InvocationRanges() && ValidRanges(*InvocationRanges());
        captureRanges = replayRanges = relocatable ? *InvocationRanges() : Ranges{};
        version = VERSION;
        state = State::Capturing;
        bytes.clear();
        events.clear();
        patches.clear();
        resources.clear();
        plan.clear();
        workspace.clear();
        compiled = false;
        direct = false;
        checks.clear();
        segments.clear();
        sdmaPatches.clear();
        replay.instructions.clear();
        replay.ready = false;
        ++generation;
    }
    bool Seal(bool communicationSucceeded) noexcept
    {
        if (!communicationSucceeded || state != State::Capturing || events.empty()) {
            state = State::Invalid;
            return false;
        }
        try {
            resources.clear();
            plan.assign(events.size(), {});
            uint32_t begin = UINT32_MAX;
            for (uint32_t i = 0; i < events.size(); ++i) {
                const auto& e = events[i];
                if (e.layout != VERSION || e.offset > bytes.size() || e.length > bytes.size() - e.offset ||
                    e.patchBegin > patches.size() || e.patchCount > patches.size() - e.patchBegin) {
                    state = State::Invalid;
                    return false;
                }
                auto found = std::find_if(resources.begin(), resources.end(), [&](const ResourcePlan& r) {
                    return r.identity == e.resource;
                });
                if (found == resources.end()) {
                    resources.push_back({e.resource, 0, i});
                    plan[i].resourceIndex = resources.size() - 1;
                } else {
                    plan[i].resourceIndex = found - resources.begin();
                }
                if (e.kind == Kind::UbWqe) {
                    if ((e.length != 64 && e.length != 128) || e.patchCount != 1) {
                        state = State::Invalid;
                        return false;
                    }
                    resources[plan[i].resourceIndex].wqebb += e.length / 64;
                }
                if (e.kind == Kind::Fence) {
                    if (e.length != 16) {
                        state = State::Invalid;
                        return false;
                    }
                    uint64_t next[2];
                    (void)memcpy_sp(next, sizeof(next), bytes.data() + e.offset, sizeof(next));
                    if (next[1] > 1) {
                        state = State::Invalid;
                        return false;
                    }
                    for (uint32_t j = i; j-- > 0;) {
                        if (events[j].kind == Kind::Fence && events[j].resource == e.resource) {
                            uint64_t previous[2];
                            (void)memcpy_sp(
                                previous, sizeof(previous), bytes.data() + events[j].offset, sizeof(previous));
                            if (previous[1] != next[0]) {
                                state = State::Invalid;
                                return false;
                            }
                            plan[i].previousFence = j;
                            break;
                        }
                    }
                }
                if (e.kind == Kind::UbBegin || e.kind == Kind::RdmaBegin) {
                    if (begin != UINT32_MAX) {
                        state = State::Invalid;
                        return false;
                    }
                    begin = i;
                }
                plan[i].begin = begin;
                if (e.kind == Kind::UbEnd || e.kind == Kind::RdmaEnd) {
                    if (begin == UINT32_MAX || events[begin].resource != e.resource ||
                        ((e.kind == Kind::UbEnd) != (events[begin].kind == Kind::UbBegin))) {
                        state = State::Invalid;
                        return false;
                    }
                    plan[begin].end = i;
                    begin = UINT32_MAX;
                }
            }
            if (begin != UINT32_MAX || events.back().kind != Kind::BatchEnd) {
                state = State::Invalid;
                return false;
            }
            workspace.assign(bytes.begin(), bytes.end());
            if (StorageBytes() > ASCCOMM_BINARY_ENTRY_BUDGET) {
                state = State::Invalid;
                return false;
            }
            compiled = true;
            state = State::Ready;
            return true;
        } catch (...) {
            state = State::Invalid;
            return false;
        }
    }
};
// Capture cursor is private to the worker and cleared before replay or destruction.
inline Entry*& Capture()
{
    static thread_local Entry* entry = nullptr;
    return entry;
}
inline void CaptureTask(Kind kind, uint64_t resource, const void* bytes, uint32_t size) noexcept
{
    if (Capture()) {
        (void)Capture()->Append(kind, resource, VERSION, bytes, size, {}, ASCCOMM_BINARY_ENTRY_BUDGET);
    }
}
inline void Unsupported() noexcept
{
    if (Capture()) {
        Capture()->state = State::Invalid;
    }
}
// Transport context never survives the native call; templates store the caller-owned
// channel identity, not a stack UbConnLite/RdmaOps pointer.
inline uint64_t& Channel()
{
    static thread_local uint64_t value = 0;
    return value;
}
inline uint32_t& ResourceRole()
{
    static thread_local uint32_t value = 0;
    return value;
}
inline uint32_t& NotifyIndex()
{
    static thread_local uint32_t value = UINT32_MAX;
    return value;
}
inline bool& SuppressRtsq()
{
    static thread_local bool value = false;
    return value;
}
struct TransportScope {
    uint64_t previous;
    uint32_t previousRole;
    explicit TransportScope(uint64_t channel) : previous(Channel()), previousRole(ResourceRole())
    {
        Channel() = channel;
        ResourceRole() = 0;
    }
    ~TransportScope()
    {
        Channel() = previous;
        ResourceRole() = previousRole;
    }
};
struct RtsqSuppression {
    bool previous;
    RtsqSuppression() : previous(SuppressRtsq()) { SuppressRtsq() = true; }
    ~RtsqSuppression() { SuppressRtsq() = previous; }
};
inline void CaptureWqe(Kind kind, const void* bytes, uint32_t length) noexcept
{
    if (!Capture()) {
        return;
    }
    if (!Channel()) {
        Unsupported();
        return;
    }
    (void)Capture()->Append(
        kind, Channel(), VERSION, bytes, length, {{2, ResourceRole(), 0, NotifyIndex()}}, ASCCOMM_BINARY_ENTRY_BUDGET);
}
bool CompileTransport(Entry&);
bool CompileRtsq(Entry&);
void BeginTransportReplay(Entry&);
void EndTransportReplay();
void BeginTransportPublication();
void CompleteTransportPublication();
bool PrepareTransport(const Event&, const Entry&, std::vector<uint8_t>&);
bool ApplyTransport(const Event&, const uint8_t*, uint32_t resourceIndex);
bool PrepareCompiledTransport(const ReplayInstruction&);
bool ApplyCompiledTransport(const ReplayInstruction&, uint32_t);
bool PrepareCompiledTokens();
bool PublishCompiledWqes(Entry&, uint32_t, uint32_t);
uint16_t CompiledTransportPi(Entry&, uint32_t);
// Preserve the first native status after execution starts. Exceptions use the
// caller's existing internal-error mapping; this is not a retry signal.
inline int32_t& LastError()
{
    static thread_local int32_t error = 0;
    return error;
}
inline bool Succeeded(int32_t status)
{
    if (status != 0 && LastError() == 0) {
        LastError() = status;
    }
    return status == 0;
}
// A runtime adapter returns fallback only before touching task IDs or queues.
Result ReplayRtsq(Entry& entry);
// Adapter owns resource validation and typed patches. Prepare must have no queue,
// taskId, fence or publication side effects. Apply may have side effects even on error.
// All Prepare calls finish before the first Apply. No retry after Apply starts.
template <class Adapter>
Result Replay(Entry& entry, uint64_t generation, Adapter& adapter)
{
    RejectReason() = 0;
    if (entry.state != State::Ready || entry.version != VERSION || entry.generation != generation) {
        Reject(AsccommRejectReason::EntryNotReady);
        return Result::Fallback;
    }
    try {
        for (const auto& event : entry.events) {
            if (event.offset > entry.bytes.size() || event.length > entry.bytes.size() - event.offset ||
                event.patchBegin > entry.patches.size() || event.patchCount > entry.patches.size() - event.patchBegin) {
                Reject(AsccommRejectReason::EntryBounds);
                return Result::Fallback;
            }
            if (!adapter.Prepare(event, entry)) {
                Reject(AsccommRejectReason::AdapterRejected); // Preserve the adapter-specific first reason.
                return Result::Fallback;
            }
        }
    } catch (...) {
        Reject(AsccommRejectReason::PrepareException);
        return Result::Fallback;
    }
    // From here on, returning Fallback is forbidden, including adapter exceptions.
    try {
        for (const auto& event : entry.events) {
            if (!adapter.Apply(event, entry)) {
                entry.state = State::Invalid;
                return Result::Failed;
            }
        }
    } catch (...) {
        entry.state = State::Invalid;
        return Result::Failed;
    }
    return Result::Complete;
}
} // namespace AsccommBinaryCache
#endif
