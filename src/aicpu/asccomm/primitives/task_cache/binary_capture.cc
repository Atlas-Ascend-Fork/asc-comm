/**
 * Copyright (c) 2026 Huawei Technologies Co., Ltd.
 * This program is free software, you can redistribute it and/or modify it under the terms and conditions of
 * CANN Open Software License Agreement Version 2.0 (the "License").
 * Please refer to the License for details. You may not use this file except in compliance with the License.
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
 * See LICENSE in the root of the software repository for the full text of the License.
 */

#include "binary_capture.h"
#include "binary_template.h"
#include "asccomm_command_replay.h"
#include "asccomm_result.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <cstring>
#include <new>
#include <thread>
#include <vector>

namespace AsccommBinaryCapture {
namespace {
struct Entry {
    AsccommBinaryCache::Entry binary;
    uint64_t captureStart = 0;
    size_t used = 0;
    uint64_t workCommands = 0;
    uint64_t taskCounts[ASCCOMM_COUNT_SIZE]{};
    bool ready = false;
    bool failed = false;
    bool capturing = false;
    std::thread::id owner;
};
thread_local Entry* active = nullptr;
thread_local bool replaying = false;

// Retain storage only, never a published replay handle. One spare per worker,
// bounded both per worker and process; active entries retain their existing budget.
constexpr uint64_t MAX_SPARE_BYTES = 1U * 1024U * 1024U;
constexpr uint64_t MAX_ALL_SPARES_BYTES = 16U * 1024U * 1024U;
std::atomic<uint64_t> spareBytes{0};
struct SpareSlot {
    std::unique_ptr<Entry> entry;
    uint64_t bytes = 0;
    ~SpareSlot() { spareBytes.fetch_sub(bytes, std::memory_order_relaxed); }
};
thread_local SpareSlot spare;

void Recycle(Entry* entry)
{
    const uint64_t bytes = sizeof(Entry) + entry->binary.StorageBytes();
    if (!spare.entry && bytes <= MAX_SPARE_BYTES) {
        uint64_t previous = spareBytes.load(std::memory_order_relaxed);
        while (previous <= MAX_ALL_SPARES_BYTES && bytes <= MAX_ALL_SPARES_BYTES - previous) {
            if (spareBytes.compare_exchange_weak(previous, previous + bytes, std::memory_order_relaxed)) {
                spare.bytes = bytes;
                spare.entry.reset(entry);
                return;
            }
        }
    }
    delete entry;
}

} // namespace
void Invalidate() noexcept
{
    if (active != nullptr && !replaying) {
        active->failed = true;
    }
}
// Native paths own SQE/WQE capture. This hook retains only control boundaries
// and workload counts; it never copies primitive arguments or descriptors.
void Record(Op op) noexcept
{
    if (active == nullptr || replaying || active->failed) {
        return;
    }
    switch (op) {
        case Op::asccomm_batch_mode_start:
        case Op::asccomm_set_notify_wait_timeout:
        case Op::asccomm_thread_res_acquire_timeout:
        case Op::asccomm_channel_notify_wait_on_thread_with_default_timeout:
        case Op::asccomm_thread_notify_wait_on_thread_with_default_timeout:
            AsccommBinaryCache::Unsupported();
            break;
        case Op::asccomm_batch_mode_end:
            AsccommBinaryCache::CaptureTask(AsccommBinaryCache::Kind::BatchFlush, 0, nullptr, 0);
            break;
        default:
            ++active->workCommands;
            break;
    }
    ++active->used;
}

int32_t Dispatch(uint32_t action, void** handle, AsccommReplayInfoV1* info)
{
    if (info != nullptr) {
        *info = {};
    }
    if (action == ASCCOMM_REPLAY_QUERY) {
        if (info == nullptr) {
            return ASCCOMM_E_PTR;
        }
        info->ready = 1;
        return ASCCOMM_SUCCESS;
    }
    if (handle == nullptr) {
        return ASCCOMM_E_PTR;
    }
    if (action == ASCCOMM_REPLAY_CREATE) {
        if (*handle != nullptr) {
            return ASCCOMM_E_PARA;
        }
        if (spare.entry) {
            *handle = spare.entry.release();
            spareBytes.fetch_sub(spare.bytes, std::memory_order_relaxed);
            spare.bytes = 0;
        } else {
            *handle = new (std::nothrow) Entry;
        }
        return *handle != nullptr ? ASCCOMM_SUCCESS : ASCCOMM_E_MEMORY;
    }
    auto* entry = static_cast<Entry*>(*handle);
    if (entry == nullptr) {
        return ASCCOMM_E_PTR;
    }
    switch (action) {
        case ASCCOMM_REPLAY_BEGIN:
            if (active != nullptr || replaying || entry->capturing) {
                return ASCCOMM_E_IN_STATUS;
            }
            entry->used = 0;
            entry->workCommands = 0;
            entry->ready = false;
            entry->failed = false;
            entry->capturing = true;
            entry->owner = std::this_thread::get_id();
            entry->binary.Reset();
            AsccommBinaryCache::Capture() = &entry->binary;
            active = entry;
            return ASCCOMM_SUCCESS;
        case ASCCOMM_REPLAY_END:
            if (active != entry || entry->owner != std::this_thread::get_id() || info == nullptr) {
                return ASCCOMM_E_IN_STATUS;
            }
            active = nullptr;
            AsccommBinaryCache::Capture() = nullptr;
            {
                bool sealed;
                {
                    sealed = entry->binary.Seal(!entry->failed);
                }
                if (sealed) {
                    bool transport;
                    {
                        transport = AsccommBinaryCache::CompileTransport(entry->binary);
                    }
                    bool rtsq = false;
                    if (transport) {
                        rtsq = AsccommBinaryCache::CompileRtsq(entry->binary);
                    }
                    if (!transport || !rtsq) {
                        entry->binary.state = AsccommBinaryCache::State::Invalid;
                    }
                }
            }
            std::fill(std::begin(entry->taskCounts), std::end(entry->taskCounts), 0);
            entry->taskCounts[ASCCOMM_COUNT_COMMAND] = entry->used;
            for (const auto& event : entry->binary.events) {
                if (event.kind == AsccommBinaryCache::Kind::Rtsq) {
                    ++entry->taskCounts[ASCCOMM_COUNT_SQE];
                }
                if (event.kind == AsccommBinaryCache::Kind::UbWqe || event.kind == AsccommBinaryCache::Kind::RdmaWqe) {
                    ++entry->taskCounts[ASCCOMM_COUNT_WQE];
                }
                if (event.kind == AsccommBinaryCache::Kind::UbEnd || event.kind == AsccommBinaryCache::Kind::RdmaEnd) {
                    ++entry->taskCounts[ASCCOMM_COUNT_SQE];
                    ++entry->taskCounts[ASCCOMM_COUNT_DB];
                }
            }
            if (info) {
                info->reserved = entry->binary.state == AsccommBinaryCache::State::Ready ? ASCCOMM_BINARY_READY :
                                                                                           ASCCOMM_BINARY_UNAVAILABLE;
            }
            entry->capturing = false;
            entry->ready =
                !entry->failed && entry->workCommands != 0 && entry->binary.state == AsccommBinaryCache::State::Ready;
            info->ready = entry->ready ? 1 : 0;
            info->commandCount = entry->used;
            // Binary storage only; no primitive-command shadow capture.
            info->storageBytes = sizeof(Entry) + entry->binary.StorageBytes();

            return ASCCOMM_SUCCESS;
        case ASCCOMM_REPLAY_EXECUTE: {
            if (!entry->ready || active != nullptr || replaying || entry->capturing) {
                return ASCCOMM_E_IN_STATUS;
            }
            replaying = true;
            {
                const auto* ranges = AsccommBinaryCache::InvocationRanges();
                if (ranges && (!AsccommBinaryCache::ValidRanges(*ranges) || !entry->binary.relocatable ||
                               !entry->binary.direct || (*ranges)[1] != entry->binary.captureRanges[1] ||
                               (*ranges)[3] != entry->binary.captureRanges[3])) {
                    replaying = false;
                    if (info) {
                        info->reserved =
                            ASCCOMM_BINARY_FALLBACK; // no side effects: caller must expand current parameters
                    }
                    return ASCCOMM_SUCCESS;
                }
                entry->binary.replayRanges = ranges ? *ranges : entry->binary.captureRanges;
                const auto result = AsccommBinaryCache::ReplayRtsq(entry->binary);
                if (info) {
                    info->reserved =
                        result == AsccommBinaryCache::Result::Complete ? ASCCOMM_BINARY_READY : ASCCOMM_BINARY_FALLBACK;
                }
                if (result != AsccommBinaryCache::Result::Fallback) {
                    replaying = false;
                    if (result == AsccommBinaryCache::Result::Failed) {
                        entry->ready = false;
                        if (info) {
                            info->reserved = ASCCOMM_BINARY_FAILED;
                        }
                        return AsccommBinaryCache::LastError() != 0 ? AsccommBinaryCache::LastError() :
                                                                      ASCCOMM_E_INTERNAL;
                    }
                    return ASCCOMM_SUCCESS;
                }
                replaying = false;
                // ReplayRtsq guarantees no publication on Fallback. The caller
                // must re-expand the algorithm using the current invocation.
                return ASCCOMM_SUCCESS;
            }
        }

        case ASCCOMM_REPLAY_ABORT:
        case ASCCOMM_REPLAY_DESTROY:
            if (entry->capturing && (active != entry || entry->owner != std::this_thread::get_id())) {
                return ASCCOMM_E_IN_STATUS;
            }
            if (active == entry) {
                active = nullptr;
                AsccommBinaryCache::Capture() = nullptr;
            }
            entry->capturing = false;
            entry->ready = false;
            entry->used = 0;
            if (action == ASCCOMM_REPLAY_DESTROY) {
                Recycle(entry);
                *handle = nullptr;
            }
            return ASCCOMM_SUCCESS;
        default:
            return ASCCOMM_E_PARA;
    }
}
} // namespace AsccommBinaryCapture

// Binary-only lifecycle. reserved: 1=complete, 2=unavailable capture,
// 4=publication failure (never retry), 5=pre-publication algorithm fallback.
extern "C" __attribute__((visibility("default"))) int32_t
asccomm_binary_replay_v1(uint32_t action, void** handle, AsccommReplayInfoV1* info, uint64_t* counts)
{
    try {
        const auto ret = AsccommBinaryCapture::Dispatch(action, handle, info);
        if (counts && handle && *handle && action != ASCCOMM_REPLAY_DESTROY) {
            const auto* entry = static_cast<AsccommBinaryCapture::Entry*>(*handle);
            std::copy(std::begin(entry->taskCounts), std::end(entry->taskCounts), counts);
            // Only preflight fallback repurposes counts[0]; successful task counts are unchanged.
            if (action == ASCCOMM_REPLAY_EXECUTE && info && info->reserved == ASCCOMM_BINARY_FALLBACK) {
                counts[0] = ASCCOMM_REJECT_MAGIC | AsccommBinaryCache::RejectReason();
            }
        }
        return ret;
    } catch (...) {
        return ASCCOMM_E_INTERNAL;
    }
}

// Range-aware binary replay with the same lifecycle as v1.
extern "C" __attribute__((visibility("default"))) int32_t asccomm_binary_replay_v2(
    uint32_t action, void** handle, AsccommReplayInfoV1* info, uint64_t* counts, const uint64_t* ranges)
{
    AsccommBinaryCache::Ranges values{};
    if (ranges) {
        std::copy(ranges, ranges + 4, values.begin());
    }
    struct Scope {
        const AsccommBinaryCache::Ranges* previous;
        explicit Scope(const AsccommBinaryCache::Ranges* p) : previous(AsccommBinaryCache::InvocationRanges())
        {
            AsccommBinaryCache::InvocationRanges() = p;
        }
        ~Scope() { AsccommBinaryCache::InvocationRanges() = previous; }
    } scope(ranges ? &values : nullptr);
    return asccomm_binary_replay_v1(action, handle, info, counts);
}
